// USB-CDC <-> UART bridge so esptool can flash the BT companion through the S3.
// Runs only after an explicit request (RTC flag + reboot), before WiFi/audio/display start, so it
// cannot affect normal operation. The S3's native USB-Serial/JTAG gives firmware no DTR/RTS or baud
// (and esptool's DTR/RTS reset would hit the S3 itself), so: the S3 puts the Node in download mode
// itself, esptool runs with --before no_reset, and esptool's CHANGE_BAUDRATE (SLIP cmd 0x0F) is
// sniffed to retune UART1. Exit: 30 s idle after traffic (5 min if none) -> Node reset to its app,
// S3 restart.
#include "node_flash.h"
#include "options.h"
#include <Arduino.h>
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

#if defined(BT_COMPANION_NODE_EN_PIN) && defined(BT_COMPANION_NODE_BOOT_PIN) && \
    (BT_COMPANION_NODE_EN_PIN != 255) && (BT_COMPANION_NODE_BOOT_PIN != 255)

static RTC_NOINIT_ATTR uint32_t s_nodeflashMagic;
static constexpr uint32_t kReq = 0x4E464C31;     // "NFL1": enter bridge on next boot
static constexpr uint32_t kActive = 0x4E464C41;  // bridge was running (S3 reset mid-session)

// EN and IO0 are open-drain: driven LOW or released (Node pull-ups keep them high).
static void nodeLine(int pin, bool low) {
  if (low) { pinMode(pin, OUTPUT); digitalWrite(pin, LOW); }
  else pinMode(pin, INPUT);
}
static void nodeReset(bool download) {
  nodeLine(BT_COMPANION_NODE_BOOT_PIN, download);
  nodeLine(BT_COMPANION_NODE_EN_PIN, true);
  delay(100);
  nodeLine(BT_COMPANION_NODE_EN_PIN, false);
  delay(download ? 300 : 50);                  // IO0 sampled a few ms after EN rises
  nodeLine(BT_COMPANION_NODE_BOOT_PIN, false);
}

bool nodeflash_supported() { return true; }

void nodeflash_request() {
  s_nodeflashMagic = kReq;
  Serial.println("[NODEFLASH] rebooting into Node flash bridge");
  Serial.flush();
  delay(200);
  ESP.restart();
}

// SLIP sniffer for one direction: decodes frames, keeps the first 12 decoded bytes.
struct Slip {
  bool in = false, esc = false;
  uint8_t buf[12];
  uint8_t n = 0;
  // returns true when a frame ends (buf/n hold its head)
  bool feed(uint8_t c) {
    if (c == 0xC0) {
      bool ended = in && n > 0;
      in = true; esc = false;
      if (ended) return true;
      n = 0;
      return false;
    }
    if (!in) return false;
    if (esc) { c = (c == 0xDC) ? 0xC0 : (c == 0xDD) ? 0xDB : c; esc = false; }
    else if (c == 0xDB) { esc = true; return false; }
    if (n < sizeof(buf)) buf[n++] = c;
    return false;
  }
  void reset() { n = 0; }
};

static void runBridge() {
  esp_log_level_set("*", ESP_LOG_NONE);
  Serial.setDebugOutput(false);
  esp_task_wdt_deinit();                         // nothing else runs; the loop below yields anyway
  Serial.setRxBufferSize(32768);                 // >= one esptool block (16 KB + SLIP overhead)
  HardwareSerial &u = Serial1;
  uint32_t baud = 115200;
  u.setRxBufferSize(8192);
  u.setTxBufferSize(32768);
  u.begin(baud, SERIAL_8N1, BT_COMPANION_UART_RX, BT_COMPANION_UART_TX);
  nodeReset(true);
  while (u.available()) u.read();                // drop the ROM boot banner

  Slip fromHost, fromNode;
  uint32_t pendingBaud = 0;
  bool seen = false;
  uint32_t last = millis();
  static uint8_t b[1024];
  for (;;) {
    int n = Serial.available();
    if (n > 0) {
      n = Serial.read(b, n > (int)sizeof(b) ? sizeof(b) : n);
      for (int i = 0; i < n; i++) {
        if (fromHost.feed(b[i])) {
          if (fromHost.n >= 12 && fromHost.buf[0] == 0x00 && fromHost.buf[1] == 0x0F)
            pendingBaud = fromHost.buf[8] | (fromHost.buf[9] << 8) | (fromHost.buf[10] << 16) | ((uint32_t)fromHost.buf[11] << 24);
          fromHost.reset();
        }
      }
      u.write(b, n);
      seen = true; last = millis();
    }
    n = u.available();
    if (n > 0) {
      n = u.read(b, n > (int)sizeof(b) ? sizeof(b) : n);
      for (int i = 0; i < n; i++) {
        if (fromNode.feed(b[i])) {
          bool baudAck = fromNode.n >= 2 && fromNode.buf[0] == 0x01 && fromNode.buf[1] == 0x0F;
          fromNode.reset();
          if (baudAck && pendingBaud >= 9600 && pendingBaud <= 2000000) {
            Serial.write(b, i + 1);              // ack goes out at the old rate, then retune
            Serial.flush();
            memmove(b, b + i + 1, n - i - 1); n -= i + 1; i = -1;
            u.flush();
            baud = pendingBaud; pendingBaud = 0;
            u.updateBaudRate(baud);
          }
        }
      }
      if (n > 0) Serial.write(b, n);
      seen = true; last = millis();
    }
    if (millis() - last > (seen ? 30000UL : 300000UL)) break;
    if (Serial.available() == 0 && u.available() == 0) delay(1);
  }
  u.end();
  nodeReset(false);                              // Node back to its app
  s_nodeflashMagic = 0;
  ESP.restart();
}

void nodeflash_boot() {
  nodeLine(BT_COMPANION_NODE_EN_PIN, false);
  nodeLine(BT_COMPANION_NODE_BOOT_PIN, false);
  const uint32_t m = s_nodeflashMagic;
  if (m == kReq) {
    s_nodeflashMagic = kActive;
    runBridge();
  } else if (m == kActive) {
    // S3 was reset mid-bridge (e.g. esptool --after hard_reset): Node is still in its ROM loader.
    s_nodeflashMagic = 0;
    nodeReset(false);
  } else {
    s_nodeflashMagic = 0;
  }
}

#else
void nodeflash_boot() {}
void nodeflash_request() { Serial.println("[NODEFLASH] not supported on this board"); }
bool nodeflash_supported() { return false; }
#endif
