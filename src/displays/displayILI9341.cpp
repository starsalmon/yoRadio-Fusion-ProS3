#include "../core/options.h"
#if DSP_MODEL==DSP_ILI9341
#include "dspcore.h"
#include "../core/config.h"

#ifndef TFT_SPI_FREQ
// Default matches Adafruit_SPITFT SPI_DEFAULT_FREQ (40MHz).
#define TFT_SPI_FREQ 40000000UL
#endif

#if DSP_HSPI
DspCore::DspCore(): Adafruit_ILI9341(&SPI2, TFT_DC, TFT_CS, TFT_RST) {}
#else
DspCore::DspCore(): Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST) {}
#endif

void DspCore::initDisplay() {
  begin(TFT_SPI_FREQ);
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
#else
struct TftBusLock {};
#endif
void DspCore::clearDsp(bool black){ fillScreen(black?0:config.theme.background); }
void DspCore::flip(){ TftBusLock l; setRotation(config.store.flipscreen?1:3); }
void DspCore::invert(){ TftBusLock l; invertDisplay(config.store.invertdisplay); }
void DspCore::sleep(void){ TftBusLock l; sendCommand(ILI9341_SLPIN); delay(150); sendCommand(ILI9341_DISPOFF); delay(150);}
void DspCore::wake(void){ TftBusLock l; sendCommand(ILI9341_DISPON); delay(150); sendCommand(ILI9341_SLPOUT); delay(150);}

#endif
