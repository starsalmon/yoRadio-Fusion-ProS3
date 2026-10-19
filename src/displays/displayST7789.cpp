#include "../core/options.h"
#if DSP_MODEL==DSP_ST7789 || DSP_MODEL==DSP_ST7789_240 || DSP_MODEL==DSP_ST7789_76 || DSP_MODEL==DSP_ST7789_170
#include "dspcore.h"
#include "../core/config.h"
#if defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI && SDC_CS != 255
#include "../core/spidog.h"
#endif

#ifndef DEF_SPI_FREQ
  #if defined(TFT_SPI_FREQ)
    #define DEF_SPI_FREQ TFT_SPI_FREQ
  #else
    #define DEF_SPI_FREQ 40000000UL
  #endif
#endif

#if DSP_HSPI
DspCore::DspCore(): Adafruit_ST7789(&SPI2, TFT_CS, TFT_DC, TFT_RST) {}
#else
DspCore::DspCore(): Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST) {}
#endif

#if defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI && SDC_CS != 255
// Same scheme as displayILI9341.cpp: panel commands that can come from non-display tasks
// (web/MQTT/config) and the init sequence hold the shared-bus lock (recursive), so SD traffic
// can't interleave with them. Normal drawing relies on SPIClass transaction serialisation.
struct TftBusLock { TftBusLock(){ sdog.begin(); sdog.takeMutex(); } ~TftBusLock(){ sdog.giveMutex(); } };
#else
struct TftBusLock {};
#endif

void DspCore::initDisplay() {
#if defined(TFT_ROW_OFFSET) && defined(TFT_COL_OFFSET)
  Serial.printf("##[BOOT]# ST7789 offset col=%d row=%d SPI=%lu\n",
                (int)TFT_COL_OFFSET, (int)TFT_ROW_OFFSET, (unsigned long)DEF_SPI_FREQ);
#endif
  {
  TftBusLock l;   // keep SD off the bus for the whole init sequence
  if(DSP_MODEL==DSP_ST7789_76){
    init(76,284);
  }else if(DSP_MODEL==DSP_ST7789_170){
    init(170,320);
  }else{
    init(240,(DSP_MODEL==DSP_ST7789)?320:240);
  }
  }
  if(DEF_SPI_FREQ > 0) setSPISpeed(DEF_SPI_FREQ);
  invert();
  cp437(true);
  flip();
  setTextWrap(false);
  setTextSize(1);
  fillScreen(0x0000);
}

void DspCore::clearDsp(bool black){ fillScreen(black?0:config.theme.background); }
void DspCore::flip(){
  TftBusLock l;
#if defined(TFT_ROTATION)
  setRotation((uint8_t)TFT_ROTATION);
#elif DSP_MODEL==DSP_ST7789 || DSP_MODEL==DSP_ST7789_76 || DSP_MODEL==DSP_ST7789_170
  setRotation(config.store.flipscreen?3:1);
#elif DSP_MODEL==DSP_ST7789_240
  if(ROTATE_90){
    setRotation(config.store.flipscreen?3:1);
  }else{
    setRotation(config.store.flipscreen?2:0);
  }
#endif
}
void DspCore::invert(){ TftBusLock l; invertDisplay(
#if DSP_MODEL==DSP_ST7789_170
!
#endif
config.store.invertdisplay); }
void DspCore::sleep(void){ TftBusLock l; enableSleep(true); delay(150); enableDisplay(false); delay(150); }
void DspCore::wake(void){ TftBusLock l; enableDisplay(true); delay(150); enableSleep(false); delay(150); }

#endif
