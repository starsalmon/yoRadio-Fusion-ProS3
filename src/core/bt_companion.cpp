#include "bt_companion.h"

#include "options.h"

#ifdef BT_COMPANION_ENABLE

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

static void pumpRx() {
  while (s_btUart.available()) {
    const char c = (char)s_btUart.read();
    if (c == '\r') continue;
    if (c == '\n') {
      String out = s_line;
      s_line = "";
      out.trim();
      if (out.length() == 0) continue;
      Serial.print("[BT2] ");
      Serial.println(out);
      if (out == "PONG") s_lastPongMs = millis();
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
  Serial.print("[BT2->] ");
  Serial.println(s);
  s_btUart.print(s);
  s_btUart.print('\n');
}

static void sendConnectDefault() {
  char buf[96];
  snprintf(buf, sizeof(buf), "CONNECT %s", BT_COMPANION_SINK_NAME);
  sendLine(buf);
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
  sendLine("SLEEP");
  s_enabled = false;
}

void btcompanion_setEnabled(bool enable) {
  if (enable == s_enabled) return;
  s_enabled = enable;
  Serial.printf("[BT2] enabled=%d\n", s_enabled ? 1 : 0);

  if (s_enabled) {
    wakePulse();
    // Give the companion time to boot UART, then handshake.
    delay(250);
    sendLine("PING");
    (void)waitForPong(800);
    // CONNECT is the most deterministic: it sets the target and forces a scan.
    sendConnectDefault();
    delay(200);
    sendConnectDefault();
    sendLine("STATUS");
  } else {
    // Best-effort clean disconnect then sleep.
    sendLine("DISCONNECT");
    delay(150);
    sendLine("SLEEP");
  }
}

bool btcompanion_enabled() { return s_enabled; }

void btcompanion_toggle() { btcompanion_setEnabled(!s_enabled); }

static void handleLine(const String &line) {
  // Keep logs very lightweight; the companion already chatters on its own UART.
  if (line.length() == 0) return;
  Serial.print("[BT2] ");
  Serial.println(line);
}

void btcompanion_loop() {
  pumpRx();
}

#else  // BT_COMPANION_ENABLE

void btcompanion_init() {}
void btcompanion_loop() {}
void btcompanion_setEnabled(bool) {}
bool btcompanion_enabled() { return false; }
void btcompanion_toggle() {}

#endif

