#include "bt_companion.h"

#include "options.h"
#include "display.h"

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
#ifndef BT_COMPANION_WAKE_PULSE_MS
#define BT_COMPANION_WAKE_PULSE_MS 80
#endif
#ifndef BT_COMPANION_SINK_NAME
#define BT_COMPANION_SINK_NAME "HiFi-IIS"
#endif

static HardwareSerial s_btUart(BT_COMPANION_UART_PORT);
static bool s_enabled = false;
static String s_line;
static volatile uint32_t s_lastPongMs = 0;
static volatile uint32_t s_lastStatusMs = 0;
static uint32_t s_lastStatusReqMs = 0;
static uint32_t s_enabledSinceMs = 0;
static uint32_t s_nextKickMs = 0;
static uint32_t s_kickBackoffMs = 0;
static uint32_t s_lastBlinkReqMs = 0;
static int s_conn = -1;
static int s_audio = -1;
static BtCompanionLinkState s_link = BtCompanionLinkState::OFF;

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
    Serial.printf("[BT2] state=%d (conn=%d audio=%d)\n", (int)s_link, s_conn, s_audio);
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
      // Avoid spamming Serial with periodic STATUS responses.
      if (!out.startsWith("STATUS ")) {
        Serial.print("[BT2] ");
        Serial.println(out);
      }
      if (out == "PONG") s_lastPongMs = millis();
      if (out.startsWith("STATUS ")) {
        // Example:
        // STATUS conn=2 audio=2 bt=1 ring=... i2sB=... underrunB=... overrunB=... peak=... dc=...
        auto readInt = [&](const char *key, int &dst) -> void {
          const int idx = out.indexOf(key);
          if (idx < 0) return;
          int start = idx + (int)strlen(key);
          int end = start;
          while (end < (int)out.length() && isdigit((unsigned char)out[end])) end++;
          dst = out.substring(start, end).toInt();
        };
        readInt("conn=", s_conn);
        readInt("audio=", s_audio);
        s_lastStatusMs = millis();
        maybeUpdateLinkState();
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
  // Avoid spamming Serial with periodic STATUS polling.
  if (strcmp(s, "STATUS") != 0) {
    Serial.print("[BT2->] ");
    Serial.println(s);
  }
  s_btUart.print(s);
  s_btUart.print('\n');
}

static void sendConnectDefault() {
  char buf[96];
  snprintf(buf, sizeof(buf), "CONNECT %s", BT_COMPANION_SINK_NAME);
  sendLine(buf);
}

void btcompanion_forceSleep() {
  // Don't change s_enabled here; caller may be about to enable immediately after boot.
  sendLine("DISCONNECT");
  delay(80);
  sendLine("SLEEP");
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
  Serial.printf("[BT2] init uart=%d baud=%d rx=%d tx=%d wake=%d sink='%s'\n",
                (int)BT_COMPANION_UART_PORT,
                (int)BT_COMPANION_UART_BAUD,
                (int)BT_COMPANION_UART_RX,
                (int)BT_COMPANION_UART_TX,
                (int)BT_COMPANION_WAKE_PIN,
                BT_COMPANION_SINK_NAME);
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

void btcompanion_setEnabled(bool enable) {
  if (enable == s_enabled) return;
  s_enabled = enable;
  Serial.printf("[BT2] enabled=%d\n", s_enabled ? 1 : 0);

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
      Serial.printf("[BT2] already connected (audio=%d), skipping CONNECT\n", s_audio);
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

void btcompanion_loop() {
  pumpRx();

  // Poll STATUS while enabled so UI can show searching/connected.
  if (s_enabled) {
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
        Serial.println("[BT2] kick");
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

#else  // BT_COMPANION_ENABLE

void btcompanion_init() {}
void btcompanion_loop() {}
void btcompanion_setEnabled(bool) {}
bool btcompanion_enabled() { return false; }
void btcompanion_toggle() {}
void btcompanion_forceSleep() {}
BtCompanionLinkState btcompanion_linkState() { return BtCompanionLinkState::OFF; }

#endif

