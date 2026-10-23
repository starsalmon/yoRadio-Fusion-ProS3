#include "../core/options.h"
#if DSP_MODEL==DSP_ILI9341
#include "dspcore.h"
#include "../core/config.h"
#if defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI && SDC_CS != 255
#include "../core/spidog.h"
#endif

#ifndef TFT_SPI_FREQ
// Default matches Adafruit_SPITFT SPI_DEFAULT_FREQ (40MHz).
#define TFT_SPI_FREQ 40000000UL
#endif

#if DSP_HSPI
DspCore::DspCore(): Adafruit_ILI9341(&SPI2, TFT_DC, TFT_CS, TFT_RST) {}
#else
DspCore::DspCore(): Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST) {}
#endif

#include "../core/dsplog.h"
void DspCore::initDisplay() {
  DSPLOG("TFT initDisplay begin (HW reset via TFT_RST + init sequence)");
#if defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI && SDC_CS != 255
  // Keep SD off the bus for the whole reset+init sequence.
  sdog.begin(); sdog.takeMutex();
  begin(TFT_SPI_FREQ);
  sdog.giveMutex();
#else
  begin(TFT_SPI_FREQ);
#endif
  DSPLOG("TFT initDisplay done");
  Serial.printf("##[BOOT]#\tTFT_SPI_FREQ\t%lu\n", (unsigned long)TFT_SPI_FREQ);
  Serial.flush();
  
  invert();
  cp437(true);
  flip();
  setTextWrap(false);
}

#if defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI && SDC_CS != 255
#include "../core/spidog.h"
// These can be called from non-display tasks (web/MQTT/config). Hold the shared-bus lock
// (recursive, so fine inside the display pass) so SD can't interleave with a command.
struct TftBusLock { TftBusLock(){ sdog.begin(); sdog.takeMutex(); } ~TftBusLock(){ sdog.giveMutex(); } };
// Lock order: bus lock first, then the SPI transaction (paramLock). Never do SD I/O between
// startWrite/endWrite (paramLock is non-recursive). See sdlock.h.
void DspCore::startWrite(void){ sdog.begin(); sdog.takeMutex(); Adafruit_ILI9341::startWrite(); }
void DspCore::endWrite(void){ Adafruit_ILI9341::endWrite(); sdog.giveMutex(); }
#else
struct TftBusLock {};
#endif
void DspCore::clearDsp(bool black){ fillScreen(black?0:config.theme.background); }
void DspCore::flip(){ TftBusLock l; setRotation(config.store.flipscreen?1:3); }
void DspCore::invert(){ TftBusLock l; invertDisplay(config.store.invertdisplay); }
void DspCore::sleep(void){ DSPLOG("TFT SLPIN+DISPOFF"); TftBusLock l; sendCommand(ILI9341_SLPIN); delay(150); sendCommand(ILI9341_DISPOFF); delay(150);}
void DspCore::wake(void){ DSPLOG("TFT SLPOUT+DISPON"); TftBusLock l; sendCommand(ILI9341_SLPOUT); delay(120); sendCommand(ILI9341_DISPON); delay(20);}
// Re-assert the registers that, if lost (reset/DISPOFF/SLPIN), leave this normally-white panel white.
// Does not touch GRAM, so it is invisible when the panel is already fine.
void DspCore::refreshRegs(){
  TftBusLock l;
  sendCommand(ILI9341_SLPOUT); delay(5);
  uint8_t pf = 0x55; sendCommand(ILI9341_PIXFMT, &pf, 1);
  setRotation(config.store.flipscreen?1:3);
  invertDisplay(config.store.invertdisplay);
  sendCommand(ILI9341_DISPON);
}

#endif
