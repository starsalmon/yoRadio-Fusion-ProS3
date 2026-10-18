#include "bt_companion.h"

#include "options.h"
#include "display.h"
#include "config.h"

#if defined(BT_COMPANION_ENABLE) && (BT_COMPANION_ENABLE != 0)

// Defaults (override in myoptions.h)
#ifndef BT_COMPANION_UART_PORT
#define BT_COMPANION_UART_PORT 1
#endif
#ifndef BT_COMPANION_UART_BAUD
#define BT_COMPANION_UART_BAUD 115200
#endif
#ifndef BT_COMPANION_UART_TX
#define BT_COMPANION_UART_TX 43
#endif
#ifndef BT_COMPANION_UART_RX
#define BT_COMPANION_UART_RX 44
#endif
#ifndef BT_COMPANION_WAKE_PIN
#define BT_COMPANION_WAKE_PIN 38
#endif
#ifndef BT_RX_VOL_GAIN_PCT
// Trim BT receive level vs local sources (100 = match the yoRadio volume curve).
#define BT_RX_VOL_GAIN_PCT 100
#endif
#ifndef BT_COMPANION_WAKE_PULSE_MS
#define BT_COMPANION_WAKE_PULSE_MS 80
#endif
#ifndef BT_COMPANION_SINK_NAME
#define BT_COMPANION_SINK_NAME "HiFi-IIS"
#endif
#ifndef BT_COMPANION_RX_LOCAL_NAME
#define BT_COMPANION_RX_LOCAL_NAME "yoRadio Fusion"
#endif

static HardwareSerial s_btUart(BT_COMPANION_UART_PORT);
static bool s_enabled = false;
static String s_line;
static String s_uartLastComplete;
// Handover replies latched per line in pumpRx(): s_uartLastComplete only holds the LAST line, so a
// STATUS/META reply arriving in the same pump overwrote BUS_HIGROW_MASTER -> false timeout,
// volume not re-sent, amp boost flapping via the late-confirm path.
static volatile bool s_gotHigrowArmed = false;
static volatile bool s_gotBusMaster = false;
static volatile bool s_gotBusReleased = false;
// HiGrow-restart detection in BT RX: consecutive STATUS replies saying mode=tx (or rx with bt=0)
// while we believe the HiGrow is our RX master. Acted on in btcompanion_loop(), not in pumpRx().
static uint8_t s_hgRestartStrikes = 0;
static bool s_hgRestartPending = false;
static volatile uint32_t s_lastPongMs = 0;
static volatile uint32_t s_lastStatusMs = 0;
static uint32_t s_lastStatusReqMs = 0;
static uint32_t s_enabledSinceMs = 0;
static uint32_t s_nextKickMs = 0;
static uint32_t s_kickBackoffMs = 0;
static uint32_t s_lastBlinkReqMs = 0;
static uint32_t s_pcmHz = 0;
static int s_conn = -1;
static int s_audio = -1;
static BtCompanionLinkState s_link = BtCompanionLinkState::OFF;
static char s_sinkName[64] = BT_COMPANION_SINK_NAME;
static bool s_rxActive = false;
// Knob-press software mute in BT RX: VOL 0 to the HiGrow, restore the mapped volume on unmute.
static bool s_rxMuted = false;
// millis() when the HiGrow was confirmed I2S master for this RX session (0 = not yet).
static uint32_t s_rxMasterAtMs = 0;
static char s_rxLocalName[64] = BT_COMPANION_RX_LOCAL_NAME;
static char s_rxTitle[96] = "";
static char s_rxArtist[96] = "";
static int s_rxConn = -1;
static int s_rxAudio = -1;
static BtCompanionLinkState s_rxLink = BtCompanionLinkState::OFF;
static uint32_t s_lastRxUiMs = 0;

enum class RxHandoverStep : uint8_t {
  None = 0,
  ResetSleepWait,
  EnterWake,
  EnterPing,
  EnterStatusCheck,
  EnterHandover,
  EnterModeRx,
  ExitModeTx,
};
static RxHandoverStep s_rxStep = RxHandoverStep::None;
static uint32_t s_rxStepAtMs = 0;
static uint32_t s_rxPingBaselineMs = 0;
static uint8_t s_pingTries = 0;
// HiGrow b3b2fec releases Classic-BT memory in rxSinkStop() (a2dp_sink.end(true)), so a second
// sink start in the same awake session silently fails (stays "waiting", unconnectable). Only a
// freshly booted HiGrow may be given MODE RX; otherwise reset it first (SLEEP + wake).
static bool s_rxHgFresh = false;
static uint32_t s_rxDropAtMs = 0;       // phone dropped (was connected, now not); 0 = none
static uint32_t s_rxLastResetMs = 0;
static void rxResetHiGrow(const char *why);
// Last HiGrow STATUS: mode=rx and bt=1 (A2DP sink running => HiGrow is I2S master).
static bool s_hgModeRx = false;
static int s_hgRxSink = -1;
static uint32_t s_rxStatusBaselineMs = 0;
static uint32_t s_lastStatusMsFwd = 0;
static bool s_rxExitCompleteFlag = false;

static void sendLine(const char *s);
static void wakePulse();

static void rxHandoverAdvance() {
  const uint32_t now = millis();
  switch (s_rxStep) {
    case RxHandoverStep::None:
      break;
    case RxHandoverStep::ResetSleepWait:
      if ((uint32_t)(now - s_rxStepAtMs) >= 1800u) {
        s_rxHgFresh = true;     // it is booting from deep sleep now
        s_pingTries = 0;
        wakePulse();
        s_rxStep = RxHandoverStep::EnterWake;
        s_rxStepAtMs = millis();
      }
      break;
    case RxHandoverStep::EnterWake:
      if ((uint32_t)(now - s_rxStepAtMs) >= 250u) {
        s_rxPingBaselineMs = s_lastPongMs;
        sendLine("PING");
        s_rxStep = RxHandoverStep::EnterPing;
        s_rxStepAtMs = now;
      }
      break;
    case RxHandoverStep::EnterPing:
      if (s_lastPongMs == s_rxPingBaselineMs) {
        // HiGrow needs ~1 s to boot out of deep sleep: retry PING every 500 ms, up to ~5 s.
        if ((uint32_t)(now - s_rxStepAtMs) < 500u) break;
        if (++s_pingTries < 10) {
          // A SLEEP sent just before entry can race the first wake pulse (HiGrow still
          // shutting down). Re-pulse wake every other retry; harmless while it's awake.
          if ((s_pingTries & 1u) == 0u) wakePulse();
          sendLine("PING");
          s_rxStepAtMs = now;
          break;
        }
        s_pingTries = 0;
        Serial.println("[BT2] RX handover: no PONG after retries, continuing");
      } else {
        if (s_pingTries > 0) s_rxHgFresh = true;   // needed retries => HiGrow just booted
        s_pingTries = 0;
        Serial.printf("[BT2] RX handover: PONG ok (fresh=%d)\n", s_rxHgFresh ? 1 : 0);
      }
      // Keep-connected: if the HiGrow is already in RX with the sink up (e.g. ProS3 rebooted),
      // don't restart it (that drops the phone). Ask for STATUS first.
      s_rxStatusBaselineMs = s_lastStatusMsFwd;
      sendLine("STATUS");
      s_rxStep = RxHandoverStep::EnterStatusCheck;
      s_rxStepAtMs = now;
      break;
    case RxHandoverStep::EnterStatusCheck: {
      const bool fresh = (s_lastStatusMsFwd != s_rxStatusBaselineMs);
      if (fresh && s_hgModeRx && s_hgRxSink == 1) {
        // ProS3 I2S pins were already released before enterRxMode(); HiGrow stays master.
        Serial.println("[BT2] RX handover: HiGrow already RX master, keeping phone connected");
        s_rxMasterAtMs = now ? now : 1;
        btcompanion_setRxVolume(config.store.volume);  // resync volume
        sendLine("STATUS");  // also makes the HiGrow replay cached META
        display.putRequest(DRAWVOL);
        display.putRequest(NEWSTATION);
        display.putRequest(NEWTITLE);
        s_rxStep = RxHandoverStep::None;
        break;
      }
      if (!fresh && (uint32_t)(now - s_rxStepAtMs) >= 800u && (uint32_t)(now - s_rxStepAtMs) < 900u) {
        sendLine("STATUS");   // one retry before deciding
        s_rxStepAtMs -= 100u; // ensure only one retry
        break;
      }
      if (fresh || (uint32_t)(now - s_rxStepAtMs) >= 1700u) {
        if (!s_rxHgFresh) {
          rxResetHiGrow(fresh ? "HiGrow awake but not RX master" : "no STATUS reply");
          break;
        }
        s_gotHigrowArmed = false;
        s_gotBusMaster = false;
        sendLine("HANDOVER PROS3_RELEASED");
        s_rxStep = RxHandoverStep::EnterHandover;
        s_rxStepAtMs = now;
      }
      break;
    }
    case RxHandoverStep::EnterHandover:
      if (s_gotHigrowArmed) {
        s_gotHigrowArmed = false;
        s_gotBusMaster = false;
        char buf[96];
        snprintf(buf, sizeof(buf), "MODE RX %s", s_rxLocalName);
        sendLine(buf);
        s_rxStep = RxHandoverStep::EnterModeRx;
        s_rxStepAtMs = now;
      } else if ((uint32_t)(now - s_rxStepAtMs) >= 2000u) {
        Serial.println("[BT2] RX handover: HIGROW_ARMED timeout");
        s_gotBusMaster = false;
        char buf[96];
        snprintf(buf, sizeof(buf), "MODE RX %s", s_rxLocalName);
        sendLine(buf);
        s_rxStep = RxHandoverStep::EnterModeRx;
        s_rxStepAtMs = now;
      }
      break;
    case RxHandoverStep::EnterModeRx:
      if (s_gotBusMaster) {
        s_gotBusMaster = false;
        s_rxMasterAtMs = now ? now : 1;
        btcompanion_setRxVolume(config.store.volume);  // VOL sent before wake may have been lost
        sendLine("STATUS");
        display.putRequest(DRAWVOL);
        s_rxStep = RxHandoverStep::None;
      } else if ((uint32_t)(now - s_rxStepAtMs) >= 5000u) {
        Serial.println("[BT2] RX handover: BUS_HIGROW_MASTER timeout");
        sendLine("STATUS");
        s_rxStep = RxHandoverStep::None;
      }
      break;
    case RxHandoverStep::ExitModeTx:
      if (s_gotBusReleased) {
        s_gotBusReleased = false;
        sendLine("SLEEP");
        s_rxActive = false;
        s_rxMuted = false;
        s_rxMasterAtMs = 0;
        s_rxConn = -1;
        s_rxAudio = -1;
        s_rxLink = BtCompanionLinkState::OFF;
        s_rxTitle[0] = s_rxArtist[0] = '\0';
        s_rxStep = RxHandoverStep::None;
        s_rxExitCompleteFlag = true;
        display.putRequest(DRAWVOL);
      } else if ((uint32_t)(now - s_rxStepAtMs) >= 4000u) {
        Serial.println("[BT2] RX exit: BUS_HIGROW_RELEASED timeout");
        sendLine("SLEEP");
        s_rxActive = false;
        s_rxMuted = false;
        s_rxMasterAtMs = 0;
        s_rxConn = -1;
        s_rxAudio = -1;
        s_rxLink = BtCompanionLinkState::OFF;
        s_rxTitle[0] = s_rxArtist[0] = '\0';
        s_rxStep = RxHandoverStep::None;
        s_rxExitCompleteFlag = true;
        display.putRequest(DRAWVOL);
      }
      break;
  }
}

static void maybeUpdateRxLinkState() {
  BtCompanionLinkState next = BtCompanionLinkState::OFF;
  if (!s_rxActive) next = BtCompanionLinkState::OFF;
  else if (s_rxConn == 2 && (s_rxAudio == 2 || s_rxAudio == 1)) next = BtCompanionLinkState::AUDIO;
  else if (s_rxConn == 2) next = BtCompanionLinkState::CONNECTED;
  else next = BtCompanionLinkState::SEARCHING;
  if (next != s_rxLink) {
    const bool wasConn = (s_rxLink == BtCompanionLinkState::CONNECTED || s_rxLink == BtCompanionLinkState::AUDIO);
    const bool isConn  = (next == BtCompanionLinkState::CONNECTED || next == BtCompanionLinkState::AUDIO);
    if (wasConn && !isConn) {
      Serial.printf("[BT2] phone dropped (conn=%d): back to waiting, clearing metadata\n", s_rxConn);
      s_rxTitle[0] = s_rxArtist[0] = '\0';
      s_rxDropAtMs = millis() | 1u;
    }
    if (isConn) s_rxDropAtMs = 0;
    s_rxLink = next;
    display.putRequest(DRAWVOL);
    display.putRequest(NEWSTATION);
    if (!s_rxTitle[0] && !s_rxArtist[0]) {
      display.putRequest(NEWTITLE);
    }
  }
}

static void maybeUpdateLinkState() {
  BtCompanionLinkState next = BtCompanionLinkState::OFF;
  if (!s_enabled) {
    next = BtCompanionLinkState::OFF;
  } else {
    // Heuristic: conn=2 is CONNECTED.
    // Audio state values can vary slightly by library/version; treat any non-zero
    // "started" code as AUDIO.
    if (s_conn == 2 && (s_audio == 2 || s_audio == 1)) next = BtCompanionLinkState::AUDIO;
    else if (s_conn == 2)            next = BtCompanionLinkState::CONNECTED;
    else                             next = BtCompanionLinkState::SEARCHING;
  }
  if (next != s_link) {
    s_link = next;
  #if defined(BT2_DIAG_LOG) && BT2_DIAG_LOG
    Serial.printf("[BT2] state=%d (conn=%d audio=%d)\n", (int)s_link, s_conn, s_audio);
  #endif
    // Reuse DRAWVOL to refresh the footer output icon.
    display.putRequest(DRAWVOL);
  }
}

static void pumpRx() {
  while (s_btUart.available()) {
    const char c = (char)s_btUart.read();
    if (c == '\r') continue;
    if (c == '\n') {
      String out = s_line;
      s_line = "";
      out.trim();
      // Some setups can prepend NUL bytes; strip leading control chars so we still
      // recognize PONG/STATUS lines.
      while (out.length() > 0 && (uint8_t)out[0] < 0x20) out.remove(0, 1);
      if (out.length() == 0) continue;
      s_uartLastComplete = out;
      if (out.indexOf("OK HIGROW_ARMED") >= 0) s_gotHigrowArmed = true;
      if (out.indexOf("BUS_HIGROW_MASTER") >= 0) s_gotBusMaster = true;
      if (out.indexOf("BUS_HIGROW_RELEASED") >= 0) s_gotBusReleased = true;
      // Avoid spamming Serial with periodic STATUS responses.
      if (!out.startsWith("STATUS ")) {
        Serial.print("[BTC] ");
        Serial.println(out);
      }
      if (out == "PONG") s_lastPongMs = millis();
      if (out.startsWith("STATUS ")) {
        auto readInt = [&](const char *key, int &dst) -> void {
          const int idx = out.indexOf(key);
          if (idx < 0) return;
          int start = idx + (int)strlen(key);
          int end = start;
          while (end < (int)out.length() && (isdigit((unsigned char)out[end]) || out[end] == '-')) end++;
          dst = out.substring(start, end).toInt();
        };
        auto readStr = [&](const char *key, char *dst, size_t dstLen) -> void {
          if (!dst || dstLen == 0) return;
          const int idx = out.indexOf(key);
          if (idx < 0) return;
          int start = idx + (int)strlen(key);
          while (start < (int)out.length() && out[start] == ' ') start++;
          int end = start;
          while (end < (int)out.length() && out[end] != ' ') end++;
          String v = out.substring(start, end);
          v.trim();
          strlcpy(dst, v.c_str(), dstLen);
        };
        {
          // Log the HiGrow link state only when it changes (not every poll).
          static String s_lastStatusKey;
          String key;
          const char *keys[] = {"mode=", "conn=", "audio=", "bt="};
          for (const char *k : keys) {
            const int i = out.indexOf(k);
            if (i < 0) continue;
            int e = out.indexOf(' ', i);
            key += out.substring(i, e < 0 ? out.length() : e);
            key += ' ';
          }
          key += "title=";
          key += s_rxTitle;
          if (key != s_lastStatusKey) {
            s_lastStatusKey = key;
            Serial.printf("[BTC] STATUS change: %s (t=%lus)\n", key.c_str(), (unsigned long)(millis() / 1000));
          }
        }
        s_hgModeRx = (out.indexOf("mode=rx") >= 0);
        if (s_hgModeRx) { int b = -1; readInt("bt=", b); s_hgRxSink = b; } else { s_hgRxSink = -1; }
        // HiGrow restarted? Only between handovers and only once we've had it as RX master.
        if (s_rxActive && s_rxStep == RxHandoverStep::None && s_rxMasterAtMs != 0) {
          const bool lost = !s_hgModeRx || s_hgRxSink == 0;
          if (lost) {
            if (s_hgRestartStrikes < 255) s_hgRestartStrikes++;
            if (s_hgRestartStrikes >= 2) s_hgRestartPending = true;  // debounce: 2 in a row
          } else {
            s_hgRestartStrikes = 0;
          }
        } else {
          s_hgRestartStrikes = 0;
        }
        s_lastStatusMsFwd = millis();
        if (out.indexOf("mode=rx") >= 0) {
          readInt("conn=", s_rxConn);
          readInt("audio=", s_rxAudio);
          s_lastStatusMs = millis();
          maybeUpdateRxLinkState();
          s_lastRxUiMs = millis();
          display.putRequest(NEWTITLE);
        } else {
          readInt("conn=", s_conn);
          readInt("audio=", s_audio);
          s_lastStatusMs = millis();
          maybeUpdateLinkState();
        }
      }
      if (out.startsWith("META ")) {
        auto readMeta = [&](const char *key, char *dst, size_t dstLen) {
          const int idx = out.indexOf(key);
          if (idx < 0) return;
          int start = idx + (int)strlen(key);
          String v = out.substring(start);
          v.trim();
          strlcpy(dst, v.c_str(), dstLen);
        };
        if (out.indexOf("title=") >= 0) readMeta("title=", s_rxTitle, sizeof(s_rxTitle));
        if (out.indexOf("artist=") >= 0) readMeta("artist=", s_rxArtist, sizeof(s_rxArtist));
        s_lastRxUiMs = millis();
        Serial.printf("[BTC] META stored title='%s' artist='%s'\n", s_rxTitle, s_rxArtist);
        display.putRequest(NEWTITLE);
      }
      continue;
    }
    if (s_line.length() < 200) s_line += c;
  }
}

static void wakePulse() {
  if (BT_COMPANION_WAKE_PIN == 255) return;
  pinMode(BT_COMPANION_WAKE_PIN, OUTPUT);
  digitalWrite(BT_COMPANION_WAKE_PIN, LOW);
  delay(2);
  digitalWrite(BT_COMPANION_WAKE_PIN, HIGH);
  // EXT0 wake is level-based; keep it high long enough to be reliable.
  delay(BT_COMPANION_WAKE_PULSE_MS);
  digitalWrite(BT_COMPANION_WAKE_PIN, LOW);
}

static void sendLine(const char *s) {
  if (!s) return;
  if (strcmp(s, "STATUS") != 0) {
    Serial.printf("[BTC->] %s\n", s);
  }
  s_btUart.print(s);
  s_btUart.print('\n');
}

static void sendConnectDefault() {
  char buf[96];
  snprintf(buf, sizeof(buf), "CONNECT %s", s_sinkName);
  sendLine(buf);
}

void btcompanion_forceSleep() {
  // Don't change s_enabled here; caller may be about to enable immediately after boot.
  sendLine("DISCONNECT");
  delay(80);
  sendLine("SLEEP");
}

static bool waitForUartContains(const char *needle, uint32_t timeoutMs) {
  if (!needle || !needle[0]) return false;
  const uint32_t start = millis();
  while ((uint32_t)(millis() - start) < timeoutMs) {
    pumpRx();
    if (s_uartLastComplete.indexOf(needle) >= 0) {
      s_uartLastComplete = "";
      return true;
    }
    delay(10);
  }
  return false;
}

static bool waitForPong(uint32_t timeoutMs) {
  const uint32_t start = millis();
  const uint32_t prev = s_lastPongMs;
  while ((uint32_t)(millis() - start) < timeoutMs) {
    pumpRx();
    if (s_lastPongMs != prev) return true;
    delay(10);
  }
  return false;
}

void btcompanion_init() {
  // Wake pin held low by default.
  if (BT_COMPANION_WAKE_PIN != 255) {
    pinMode(BT_COMPANION_WAKE_PIN, OUTPUT);
    digitalWrite(BT_COMPANION_WAKE_PIN, LOW);
  }

  s_btUart.begin(BT_COMPANION_UART_BAUD, SERIAL_8N1, BT_COMPANION_UART_RX, BT_COMPANION_UART_TX);
  delay(30);
  s_lastPongMs = millis();

  // Default behavior: keep BT output disabled until user toggles it.
  // If the companion ESP32 is currently awake, ask it to sleep.
  #if defined(BT2_DIAG_LOG) && BT2_DIAG_LOG
    Serial.printf("[BT2] init uart=%d baud=%d rx=%d tx=%d wake=%d sink='%s'\n",
                  (int)BT_COMPANION_UART_PORT,
                  (int)BT_COMPANION_UART_BAUD,
                  (int)BT_COMPANION_UART_RX,
                  (int)BT_COMPANION_UART_TX,
                  (int)BT_COMPANION_WAKE_PIN,
                  s_sinkName);
  #endif
  // Don't immediately force SLEEP here: on reboot while BT is active, we want
  // to bring the companion back up and reconnect deterministically.
  s_enabled = false;
  s_link = BtCompanionLinkState::OFF;
  s_conn = -1;
  s_audio = -1;
  s_lastStatusMs = 0;
  s_lastStatusReqMs = 0;
  s_enabledSinceMs = 0;
  s_nextKickMs = 0;
  s_kickBackoffMs = 0;
  s_lastBlinkReqMs = 0;
}

void btcompanion_setSinkName(const char* name) {
  if (!name) return;
  while (*name == ' ' || *name == '\t' || *name == '\r' || *name == '\n') name++;
  if (*name == '\0') return;

  // Keep it ASCII-printable to avoid UART/control issues.
  char tmp[sizeof(s_sinkName)];
  size_t j = 0;
  for (size_t i = 0; name[i] && j < sizeof(tmp) - 1; i++) {
    const unsigned char c = (unsigned char)name[i];
    if (c < 0x20 || c >= 0x7F) continue;
    tmp[j++] = (char)c;
  }
  tmp[j] = '\0';
  while (j > 0 && tmp[j - 1] == ' ') tmp[--j] = '\0';
  if (j == 0) return;

  if (strncmp(tmp, s_sinkName, sizeof(s_sinkName)) == 0) return;
  strlcpy(s_sinkName, tmp, sizeof(s_sinkName));
  #if defined(BT2_DIAG_LOG) && BT2_DIAG_LOG
    Serial.printf("[BT2] sink='%s'\n", s_sinkName);
  #endif
}

const char* btcompanion_sinkName() { return s_sinkName; }

void btcompanion_requestConnect() {
  if (!s_enabled) return;
  if (s_link == BtCompanionLinkState::AUDIO) return; // don't disrupt active audio
  s_conn = -1;
  s_audio = -1;
  s_link = BtCompanionLinkState::SEARCHING;
  display.putRequest(DRAWVOL);
  sendConnectDefault();
  sendLine("STATUS");
}

void btcompanion_setPcmSampleRate(uint32_t hz) {
  if (hz == 0) return;
  if (hz == s_pcmHz) return;
  s_pcmHz = hz;
  if (!s_enabled) return;
  char buf[32];
  snprintf(buf, sizeof(buf), "SR %lu", (unsigned long)hz);
  sendLine(buf);
}

void btcompanion_setEnabled(bool enable) {
  if (enable == s_enabled) return;
  // Backstop: TX bridge control (CONNECT/SR/DISCONNECT/SLEEP) must never run while the
  // HiGrow is in RX (phone sink) mode; RX enter/exit owns the HiGrow then.
  if (s_rxActive) {
    Serial.printf("[BT2] setEnabled(%d) ignored: BT RX active\n", enable ? 1 : 0);
    return;
  }
  s_enabled = enable;
  #if defined(BT2_DIAG_LOG) && BT2_DIAG_LOG
    Serial.printf("[BT2] enabled=%d\n", s_enabled ? 1 : 0);
  #endif

  if (s_enabled) {
    s_conn = -1;
    s_audio = -1;
    s_lastStatusMs = 0;
    s_lastStatusReqMs = 0;
    s_enabledSinceMs = millis();
    // Kick schedule: wait 30s for the initial connect/scan to settle, then kick every 60s
    // indefinitely while still not in AUDIO.
    s_kickBackoffMs = 45000u;
    s_nextKickMs = s_enabledSinceMs + s_kickBackoffMs;
    s_lastBlinkReqMs = 0;
    s_link = BtCompanionLinkState::SEARCHING;
    display.putRequest(DRAWVOL);
    wakePulse();
    // Give the companion time to boot UART, then handshake.
    delay(250);
    sendLine("PING");
    (void)waitForPong(800);
    // Ask the companion for its current state first. If it's already connected, don't
    // tear it down: some speakers are sensitive to disconnect/reconnect churn.
    sendLine("STATUS");
    const uint32_t wantPrev = s_lastStatusMs;
    const uint32_t t0 = millis();
    while ((uint32_t)(millis() - t0) < 500u && s_lastStatusMs == wantPrev) {
      pumpRx();
      delay(10);
    }

    // If we learned we're already connected, keep it as-is and just continue polling.
    if (s_conn == 2) {
      #if defined(BT2_DIAG_LOG) && BT2_DIAG_LOG
        Serial.printf("[BT2] already connected (audio=%d), skipping CONNECT\n", s_audio);
      #endif
      maybeUpdateLinkState();
      display.putRequest(DRAWVOL);
      return;
    }

    // Not connected (or no STATUS response yet): do a clean connect attempt.
    if (s_conn > 0) {
      sendLine("DISCONNECT");
      delay(120);
    }
    sendConnectDefault();
    if (s_pcmHz) {
      char buf[32];
      snprintf(buf, sizeof(buf), "SR %lu", (unsigned long)s_pcmHz);
      sendLine(buf);
    }
    sendLine("STATUS");
  } else {
    s_link = BtCompanionLinkState::OFF;
    display.putRequest(DRAWVOL);
    // Best-effort clean disconnect then sleep.
    sendLine("DISCONNECT");
    delay(150);
    sendLine("SLEEP");
  }
}

bool btcompanion_enabled() { return s_enabled; }

void btcompanion_toggle() { btcompanion_setEnabled(!s_enabled); }

void btcompanion_pump() {
#if defined(BT_COMPANION_ENABLE) && (BT_COMPANION_ENABLE != 0)
  pumpRx();
  rxHandoverAdvance();
#endif
}

static void rxResetHiGrow(const char *why) {
  Serial.printf("[BT2] HiGrow reset (SLEEP + wake): %s\n", why ? why : "");
  sendLine("SLEEP");
  s_rxMasterAtMs = 0;              // amp off while the bus floats
  s_rxConn = -1;
  s_rxAudio = -1;
  s_hgModeRx = false;
  s_hgRxSink = -1;
  s_rxTitle[0] = s_rxArtist[0] = '\0';
  s_rxDropAtMs = 0;
  s_rxLastResetMs = millis() | 1u;
  s_rxLink = BtCompanionLinkState::SEARCHING;
  s_rxStep = RxHandoverStep::ResetSleepWait;
  s_rxStepAtMs = millis();
  display.putRequest(DRAWVOL);
  display.putRequest(NEWSTATION);
  display.putRequest(NEWTITLE);
}

void btcompanion_loop() {
  btcompanion_pump();

  // BT RX watchdogs (only between handovers).
  if (s_rxActive && s_rxStep == RxHandoverStep::None) {
    const uint32_t now = millis();
    const bool recentReset = s_rxLastResetMs && (uint32_t)(now - s_rxLastResetMs) < 30000u;
    // HiGrow came back in TX/default mode (crash, brownout, watchdog): our RX view is stale.
    if (s_hgRestartPending) {
      s_hgRestartPending = false;
      s_hgRestartStrikes = 0;
      const bool tooSoon = s_rxLastResetMs && (uint32_t)(now - s_rxLastResetMs) < 10000u;
      if (!tooSoon) {
        Serial.println("[BTC] HiGrow restarted (mode=tx) - re-handover");
        s_rxConn = -1;
        s_rxAudio = -1;
        s_rxTitle[0] = s_rxArtist[0] = '\0';
        rxResetHiGrow("HiGrow restarted (mode=tx / bt=0)");
        return;
      }
    }
    // Phone dropped and nothing reconnected for 15 s: the HiGrow may be stuck unconnectable.
    // Resetting it is harmless while nothing is connected.
    if (s_rxDropAtMs && (uint32_t)(now - s_rxDropAtMs) >= 60000u && s_rxConn != 2 && !recentReset) {
      rxResetHiGrow("phone dropped, not reconnected within 60 s");
    }
    // HiGrow stopped answering STATUS (asleep/hung): UI must not stay on stale "connected".
    else if (s_lastStatusMs && (uint32_t)(now - s_lastStatusMs) >= 12000u && !recentReset) {
      rxResetHiGrow("no STATUS for 12 s");
    }
  }

  // Poll STATUS while enabled so UI can show searching/connected.
  if (s_rxActive) {
    const uint32_t now = millis();
    const uint32_t period = (s_rxLink == BtCompanionLinkState::AUDIO) ? 5000u : 2000u;
    if (s_lastStatusReqMs == 0 || (uint32_t)(now - s_lastStatusReqMs) >= period) {
      s_lastStatusReqMs = now;
      sendLine("STATUS");
    }
  } else if (s_enabled) {
    const uint32_t now = millis();
    // While not connected, poll faster. Once connected, slow down.
    const uint32_t period = (s_link == BtCompanionLinkState::AUDIO) ? 3000u : 800u;
    if (s_lastStatusReqMs == 0 || (uint32_t)(now - s_lastStatusReqMs) >= period) {
      s_lastStatusReqMs = now;
      sendLine("STATUS");
    }

    // While SEARCHING, request a redraw periodically so the UI can blink the icon.
    if (s_link == BtCompanionLinkState::SEARCHING) {
      if (s_lastBlinkReqMs == 0 || (uint32_t)(now - s_lastBlinkReqMs) >= 500u) {
        s_lastBlinkReqMs = now;
        display.putRequest(DRAWVOL);
      }
    } else {
      s_lastBlinkReqMs = 0;
    }

    // If we're still not connected, occasionally "kick" the companion to re-scan.
    // Do NOT spam CONNECT: on the companion, CONNECT restarts the BT scan and can prevent
    // it from ever settling into CONNECTED/AUDIO.
    if (s_link == BtCompanionLinkState::SEARCHING) {
      if (s_nextKickMs != 0 && (int32_t)(now - s_nextKickMs) >= 0) {
        #if defined(BT2_DIAG_LOG) && BT2_DIAG_LOG
          Serial.println("[BT2] kick");
        #endif
        // Best-effort clean disconnect, then CONNECT (restart scan).
        sendLine("DISCONNECT");
        delay(80);
        sendConnectDefault();
        // After the initial 30s kick window, kick every 60s indefinitely.
        s_kickBackoffMs = 60000u;
        s_nextKickMs = now + 60000u;
      }
    } else {
      // AUDIO: no kicks.
    }
  }
}

BtCompanionLinkState btcompanion_linkState() { return s_link; }

bool btcompanion_rxActive() { return s_rxActive; }

void btcompanion_setRxLocalName(const char* name) {
  if (!name || !name[0]) return;
  strlcpy(s_rxLocalName, name, sizeof(s_rxLocalName));
}

const char* btcompanion_rxLocalName() { return s_rxLocalName; }

void btcompanion_getRxMetadata(char* title, size_t titleLen, char* artist, size_t artistLen) {
  if (title && titleLen) strlcpy(title, s_rxTitle, titleLen);
  if (artist && artistLen) strlcpy(artist, s_rxArtist, artistLen);
}

BtCompanionLinkState btcompanion_rxLinkState() { return s_rxLink; }

void btcompanion_setRxVolume(uint8_t volume) {
  if (!s_rxActive) return;
  if (s_rxMuted) {               // turning the knob (or any volume resync) unmutes
    s_rxMuted = false;
    display.putRequest(DRAWVOL);
  }
  // yoRadio volume is 0..100 (setVolumeSteps(100)). Previously this clamped to 0..21, so any
  // volume above 21% sent full scale (VOL 127) and BT was far louder than local sources.
  // Mirror Audio::calculateVolumeLimits() so the same knob position gives a similar level.
  uint8_t v = volume;
  if (v > 100) v = 100;
  const float t = (float)v / 100.0f;
  float g = (t < 0.5f) ? (t * t * 1.3f) : (0.3f + ((t - 0.5f) / 0.5f) * 0.7f);
  g *= 0.9f * ((float)BT_RX_VOL_GAIN_PCT / 100.0f);
  if (g > 1.0f) g = 1.0f;
  if (g < 0.0f) g = 0.0f;
  // HiGrow b3b2fec applies TWO gains for VOL v (0..127): the ESP32-A2DP default exponential
  // volume control E(v) = (1.4^(12v/127-12) - 1.4^-12)/(1 - 1.4^-12), then a linear v/127
  // in its stream reader. Pick the largest v whose total gain E(v)*v/127 <= g.
  auto higrowGain = [](int vv) -> float {
    const float z = powf(1.4f, -12.0f);
    const float e = (powf(1.4f, (float)vv * 12.0f / 127.0f - 12.0f) - z) / (1.0f - z);
    return e * (float)vv / 127.0f;
  };
  uint8_t avrcp = 0;
  for (int vv = 127; vv >= 0; --vv) {
    if (higrowGain(vv) <= g + 1e-6f) { avrcp = (uint8_t)vv; break; }
  }
  if (v > 0 && avrcp == 0) avrcp = 1;
  char buf[24];
  snprintf(buf, sizeof(buf), "VOL %u", (unsigned)avrcp);
  Serial.printf("[BTC] RX volume %u%% -> VOL %u (gain %.3f)\n", (unsigned)v, (unsigned)avrcp, (double)g);
  sendLine(buf);
}

bool btcompanion_rxMuted() { return s_rxActive && s_rxMuted; }

#ifndef BT_RX_BOOST_SETTLE_MS
#define BT_RX_BOOST_SETTLE_MS 250u
#endif
bool btcompanion_rxBusReady() {
  if (!s_rxActive || s_rxStep != RxHandoverStep::None) return false;
  // Late confirmation (handover timed out but a later STATUS shows RX + sink up).
  if (s_rxMasterAtMs == 0 && s_hgModeRx && s_hgRxSink == 1) s_rxMasterAtMs = millis() | 1u;
  return s_rxMasterAtMs != 0 && (uint32_t)(millis() - s_rxMasterAtMs) >= BT_RX_BOOST_SETTLE_MS;
}

void btcompanion_rxToggleMute() {
  if (!s_rxActive) return;
  if (s_rxMuted) {
    Serial.println("[BTC] knob: UNMUTE");
    btcompanion_setRxVolume(config.store.volume);   // clears s_rxMuted, sends mapped VOL
  } else {
    Serial.println("[BTC] knob: MUTE (VOL 0)");
    sendLine("VOL 0");
    s_rxMuted = true;
    display.putRequest(DRAWVOL);
  }
}

void btcompanion_rxAvrcpToggle() {
  if (!s_rxActive) return;
  Serial.println("[BTC] AVRCP TOGGLE");
  sendLine("AVRCP TOGGLE");
}

void btcompanion_enterRxMode(const char* localName) {
  if (localName && localName[0]) btcompanion_setRxLocalName(localName);

  if (s_enabled) btcompanion_setEnabled(false);

  if (s_rxActive && s_rxStep != RxHandoverStep::None) {
    return;
  }

  if (!s_rxActive) {
    s_rxConn = -1;
    s_rxAudio = -1;
    s_rxTitle[0] = s_rxArtist[0] = '\0';
    s_rxLink = BtCompanionLinkState::SEARCHING;
    s_rxActive = true;
    s_rxMuted = false;
    s_rxMasterAtMs = 0;
    s_rxExitCompleteFlag = false;
    s_rxHgFresh = false;
    s_rxDropAtMs = 0;
    s_lastStatusMs = 0;
  }

  wakePulse();
  s_rxStep = RxHandoverStep::EnterWake;
  s_rxStepAtMs = millis();
  display.putRequest(DRAWVOL);
}

void btcompanion_exitRxMode() {
  if (!s_rxActive && s_rxStep == RxHandoverStep::None) return;
  if (s_rxStep == RxHandoverStep::ExitModeTx) return;
  s_gotBusReleased = false;
  sendLine("MODE TX");
  s_rxStep = RxHandoverStep::ExitModeTx;
  s_rxStepAtMs = millis();
}

bool btcompanion_rxExitComplete() {
  if (!s_rxExitCompleteFlag) return false;
  s_rxExitCompleteFlag = false;
  return true;
}

#else  // BT_COMPANION_ENABLE

void btcompanion_init() {}
void btcompanion_loop() {}
void btcompanion_pump() {}
void btcompanion_setEnabled(bool) {}
bool btcompanion_enabled() { return false; }
void btcompanion_toggle() {}
void btcompanion_forceSleep() {}
void btcompanion_setSinkName(const char*) {}
const char* btcompanion_sinkName() { return ""; }
void btcompanion_requestConnect() {}
void btcompanion_setPcmSampleRate(uint32_t) {}
BtCompanionLinkState btcompanion_linkState() { return BtCompanionLinkState::OFF; }
bool btcompanion_rxActive() { return false; }
void btcompanion_enterRxMode(const char*) {}
void btcompanion_exitRxMode() {}
BtCompanionLinkState btcompanion_rxLinkState() { return BtCompanionLinkState::OFF; }
void btcompanion_getRxMetadata(char*, size_t, char*, size_t) {}
void btcompanion_setRxLocalName(const char*) {}
const char* btcompanion_rxLocalName() { return ""; }
void btcompanion_setRxVolume(uint8_t) {}
void btcompanion_rxAvrcpToggle() {}
bool btcompanion_rxMuted() { return false; }
bool btcompanion_rxBusReady() { return false; }
void btcompanion_rxToggleMute() {}
bool btcompanion_rxExitComplete() { return false; }

#endif

