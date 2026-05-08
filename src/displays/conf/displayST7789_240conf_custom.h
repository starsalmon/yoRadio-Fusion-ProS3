/*************************************************************************************
    ST7789 240x240 displays configuration file.
*************************************************************************************/

#ifndef displayST7789conf_h
#define displayST7789conf_h

#include "../../core/config.h"

#define DSP_WIDTH       240
#define TFT_FRAMEWDT    8
#define MAX_WIDTH       DSP_WIDTH-TFT_FRAMEWDT*2
#define PLMITEMS        11
#define PLMITEMLENGHT   40
#define PLMITEMHEIGHT   22

#if BITRATE_FULL
  #define TITLE_FIX 64
#else
  #define TITLE_FIX 0
#endif

#define bootLogoTop     20

/* SROLLS  */
// metaConf szűkítve: bal +48px (sorszám 46px + 2px gap), jobb -48px (mód 46px + 2px gap)
// metaBG: top=0, height=38, box_h=23 → box_top=(38-23)/2=7
const ScrollConfig metaConf       PROGMEM = {{ 36, 4, 3, WA_CENTER }, 140, true, MAX_WIDTH-72, 5000, 5, 30 };
const ScrollConfig title1Conf     PROGMEM = {{ TFT_FRAMEWDT, 40, 2, WA_LEFT }, 140, true, MAX_WIDTH-TITLE_FIX, 5000, 4, 30 };
const ScrollConfig title2Conf     PROGMEM = {{ TFT_FRAMEWDT, 60, 2, WA_LEFT }, 140, false, MAX_WIDTH, 5000, 4, 30 };
const ScrollConfig playlistConf   PROGMEM = {{ TFT_FRAMEWDT, 112, 2, WA_LEFT }, 140, true, MAX_WIDTH, 0, 2, 30 };
const ScrollConfig apTitleConf    PROGMEM = {{ TFT_FRAMEWDT, TFT_FRAMEWDT, 3, WA_CENTER }, 140, false, MAX_WIDTH, 0, 4, 20 };
const ScrollConfig apSettConf     PROGMEM = {{ TFT_FRAMEWDT, 240-TFT_FRAMEWDT-16, 2, WA_LEFT }, 140, false, MAX_WIDTH, 0, 4, 30 };
const ScrollConfig weatherConf    PROGMEM = {{ TFT_FRAMEWDT, 80, 2, WA_CENTER }, 140, false, MAX_WIDTH, 0, 1, 25 };

/* BACKGROUNDS  */
const FillConfig   metaBGConf     PROGMEM = {{ 0, 0, 0, WA_LEFT }, DSP_WIDTH, 34, false };
const FillConfig   metaBGConfLine PROGMEM = {{ 3, 33, 0, WA_CENTER }, DSP_WIDTH - 2, 1, true };
const FillConfig   metaBGConfInv  PROGMEM = {{ 0, 34, 0, WA_LEFT }, DSP_WIDTH, 1, false };
const FillConfig   volbarConf     PROGMEM = {{ TFT_FRAMEWDT, 240-TFT_FRAMEWDT-6, 0, WA_LEFT }, MAX_WIDTH, 6, true };
const FillConfig  playlBGConf     PROGMEM = {{ 0, 107, 0, WA_LEFT }, DSP_WIDTH, 24, false };
const FillConfig  heapbarConf     PROGMEM = {{ 0, 239, 0, WA_LEFT }, DSP_WIDTH, 1, false };

/* WIDGETS  */
const WidgetConfig bootstrConf    PROGMEM = { 0, 152, 0, WA_CENTER };
const WidgetConfig bitrateConf    PROGMEM = { TFT_FRAMEWDT, 175, 0, WA_LEFT };
const WidgetConfig voltxtConf     PROGMEM = { 80, 210, 0, WA_RIGHT };
const WidgetConfig  iptxtConf     PROGMEM = { TFT_FRAMEWDT, 210, 0, WA_LEFT };
const WidgetConfig   rssiConf     PROGMEM = { TFT_FRAMEWDT, 210, 0, WA_RIGHT };
const WidgetConfig numConf        PROGMEM = { 0, 120+30, 0, WA_CENTER };
const WidgetConfig apNameConf     PROGMEM = { TFT_FRAMEWDT, 66, 2, WA_CENTER };
const WidgetConfig apName2Conf    PROGMEM = { TFT_FRAMEWDT, 90, 2, WA_CENTER };
const WidgetConfig apPassConf     PROGMEM = { TFT_FRAMEWDT, 130, 2, WA_CENTER };
const WidgetConfig apPass2Conf    PROGMEM = { TFT_FRAMEWDT, 154, 2, WA_CENTER };
const WidgetConfig vuConf         PROGMEM = { TFT_FRAMEWDT, 94, 1, WA_CENTER };

const WidgetConfig bootWdtConf    PROGMEM = { 0, 162, 1, WA_CENTER };
const ProgressConfig bootPrgConf  PROGMEM = { 90, 14, 4 };

/* BITRÁTA */
inline BitrateConfig getfullbitrateConf() {
  switch (config.store.vuLayout) {
    case 1: return {{DSP_WIDTH-TFT_FRAMEWDT-60, 40, 1, WA_LEFT}, 30 };
    case 2: return {{DSP_WIDTH-TFT_FRAMEWDT-60, 40, 1, WA_LEFT}, 30 };
    case 3: return {{DSP_WIDTH-TFT_FRAMEWDT-60, 40, 1, WA_LEFT}, 30 };
    default: return {{DSP_WIDTH-TFT_FRAMEWDT-60, 86, 1, WA_LEFT}, 30 };
  }
}

/* ÁLLOMÁS SORSZÁM (outline box, bal oldal – meta sor) */
// metaBG: top=0, height=38, box_h=23 → box_top=7
inline StationNumConfig getstationNumConf() {
  return {{ 4, 7, 0, WA_LEFT }, 30 };
}

/* LEJÁTSZÁS MÓD (filled box, jobb oldal – meta sor) */
inline PlayModeConfig getplayModeConf() {
  return {{ DSP_WIDTH - 4 - 30, 7, 0, WA_LEFT }, 30 };
}

/* BANDS  */
inline VUBandsConfig getbandsConf() {
  switch (config.store.vuLayout) {
    case 1:  return { 109, config.store.vuBarHeightStr, 6, config.store.vuBarGapStr,  config.store.vuBarCountStr,  config.store.vuFadeSpeedStr };
    case 2:  return { 109, config.store.vuBarHeightBbx, 6, config.store.vuBarGapBbx,  config.store.vuBarCountBbx,  config.store.vuFadeSpeedBbx };
    case 3:  return { 224, config.store.vuBarHeightStd, config.store.vuBarGapStd, 2, config.store.vuBarCountStd,  config.store.vuFadeSpeedStd };
    default: return { config.store.vuBarHeightDef, 20, 6, config.store.vuBarGapDef,  config.store.vuBarCountDef,  config.store.vuFadeSpeedDef };
  }
}

inline WidgetConfig getvuConf() {
  switch (config.store.vuLayout) {
    case 1: return { TFT_FRAMEWDT, 186, 1, WA_CENTER };
    case 2: return { TFT_FRAMEWDT, 186, 1, WA_CENTER };
    case 3: return { TFT_FRAMEWDT, 186, 1, WA_CENTER };
    default: return { TFT_FRAMEWDT, 186, 1, WA_LEFT };
  }
}

inline WidgetConfig getclockConf() {
  switch (config.store.vuLayout) {
    case 1: case 2: case 3: return { 10, 155, 0, WA_RIGHT };
    default: return { 0, 168, 0, WA_RIGHT };
  }
}

static constexpr ScrollConfig kDateBase = {
  { TFT_FRAMEWDT, 175, 1, WA_RIGHT }, 128, false, 202, 5000, 1, 50 };

static inline uint16_t dateTopByLayout(uint8_t ly) {
  switch (ly) { case 1: case 2: case 3: case 0: default: return 157; }
}
static inline uint16_t dateLeftByLayout(uint8_t ly) {
  (void)ly; return TFT_FRAMEWDT+6;
}

inline ScrollConfig getDateConf(uint8_t ly) {
  ScrollConfig c = kDateBase;
  c.widget.top  = dateTopByLayout(ly);
  c.widget.left = dateLeftByLayout(ly);
  return c;
}
inline ScrollConfig getDateConf() { return getDateConf(config.store.vuLayout); }

/* STRINGS  */
const char         numtxtFmt[]    PROGMEM = "%d";
const char           rssiFmt[]    PROGMEM = "WiFi %d";
const char          iptxtFmt[]    PROGMEM = "%s";
const char         voltxtFmt[]    PROGMEM = "\023\025%d%%";
const char        bitrateFmt[]    PROGMEM = "%d kBs";

/* MOVES  */
inline MoveConfig getclockMove() {
  switch (config.store.vuLayout) {
    case 1: case 2: case 3: default: return { 0, 155, -1 };
  }
}

inline MoveConfig getdateMove() {
  const ScrollConfig& dc = getDateConf();
  const auto cc = getclockConf();
  const auto cm = getclockMove();
  MoveConfig m;
  m.x = (uint16_t)((int16_t)dc.widget.left + (int16_t)cm.x - (int16_t)cc.left);
  m.y = (uint16_t)((int16_t)dc.widget.top  + (int16_t)cm.y - (int16_t)cc.top);
  m.width = dc.width;
  return m;
}

const MoveConfig   weatherMove    PROGMEM = { TFT_FRAMEWDT, 80, MAX_WIDTH };
const MoveConfig   weatherMoveVU  PROGMEM = { TFT_FRAMEWDT, 80, MAX_WIDTH };

#endif
