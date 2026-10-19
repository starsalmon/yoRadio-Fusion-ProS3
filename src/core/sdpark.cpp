#include "sdpark.h"
#ifdef SDPARK_ENABLED
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "sdmanager.h"
#include "sd_spi_shared.h"

#if DSP_HSPI
extern SPIClass SPI2;   // TFT + SD share the display's SPI2 (main.cpp)
#define SDPARK_SPI SPI2
#else
#define SDPARK_SPI SPI
#endif
#include "dsplog.h"

// Card power-up: the SD spec allows up to 250 ms after VDD is stable before the card accepts
// commands. LDO2 is switched on in yoradio_on_setup() a few tens of ms after reset.
#ifndef SDPARK_MIN_UPTIME_MS
  #define SDPARK_MIN_UPTIME_MS 400
#endif

static bool s_present = false;

// One command frame at 400 kHz, CS already low. Returns R1 (0xFF = no response).
static uint8_t sdpark_cmd(const uint8_t (&frame)[6], uint8_t extraBytes) {
  SDPARK_SPI.transfer(0xFF);
  for (uint8_t b : frame) SDPARK_SPI.transfer(b);
  uint8_t r1 = 0xFF;
  for (int i = 0; i < 10 && (r1 & 0x80); i++) r1 = SDPARK_SPI.transfer(0xFF);
  if (!(r1 & 0x80)) for (uint8_t i = 0; i < extraBytes; i++) SDPARK_SPI.transfer(0xFF);  // rest of R7
  return r1;
}

// Caller holds the shared-bus lock. Never touches TFT_CS: the TFT driver keeps it HIGH outside
// its own transactions, and this runs inside an SPI transaction, so no TFT transfer is active.
static bool sdpark_probeLocked(uint8_t *r1out) {
  static const uint8_t CMD0[6] = {0x40, 0x00, 0x00, 0x00, 0x00, 0x95};
  static const uint8_t CMD8[6] = {0x48, 0x00, 0x00, 0x01, 0xAA, 0x87};
  SDPARK_SPI.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
  digitalWrite(SDC_CS, HIGH);
  for (int i = 0; i < 10; i++) SDPARK_SPI.transfer(0xFF);   // >= 74 clocks with CS high
  digitalWrite(SDC_CS, LOW);
  uint8_t r1 = sdpark_cmd(CMD0, 0);                  // -> idle, SPI mode: expect 0x01
  bool present = false;
  if (r1 == 0x01) {
    // Confirm with CMD8 so a floating MISO can't fake a card: 0x01 (v2 card) or 0x05 (v1 card).
    const uint8_t r8 = sdpark_cmd(CMD8, 4);
    present = (r8 == 0x01 || r8 == 0x05);
  }
  digitalWrite(SDC_CS, HIGH);
  SDPARK_SPI.transfer(0xFF);                                // 8 clocks with CS high: card releases DO
  SDPARK_SPI.endTransaction();
  if (r1out) *r1out = r1;
  return present;
}

void sdpark_boot() {
  pinMode(SDC_CS, OUTPUT);
  digitalWrite(SDC_CS, HIGH);
  SPI.begin();   // same default pins (SCK 36 / MISO 37 / MOSI 35) the TFT driver uses; its begin() is then a no-op
  const uint32_t now = millis();
  if (now < SDPARK_MIN_UPTIME_MS) delay(SDPARK_MIN_UPTIME_MS - now);
  uint8_t r1 = 0xFF;
  yoradio_sd_spi_acquire();
  s_present = sdpark_probeLocked(&r1);
  yoradio_sd_spi_release();
  DSPLOG("SD park: %s (CMD0 R1=0x%02X)", s_present ? "card in SPI idle" : "no card", r1);
}

int sdpark_poll() {
  int change = 0;
  yoradio_sd_spi_acquire();
  // Checked under the lock: SDManager::start() sets ready while holding it, so a mount that just
  // finished is never hit by CMD0 (that would reset a mounted card behind FatFS's back).
  if (!sdman.ready && !sdman.indexing()) {
    const bool present = sdpark_probeLocked(nullptr);
    if (present != s_present) {
      change = present ? 1 : -1;
      s_present = present;
    }
  }
  yoradio_sd_spi_release();
  if (change) DSPLOG("SD park: card %s", change > 0 ? "inserted (parked in SPI idle)" : "removed");
  return change;
}
#endif
