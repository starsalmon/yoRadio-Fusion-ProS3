/*************************************************************************************
    ST7789 240x240 displays configuration file.
    Copy this file to yoRadio/src/displays/conf/displayST7789conf_custom.h
    and modify it
    More info on https://github.com/e2002/yoradio/wiki/Widgets#widgets-description
*************************************************************************************/

#ifndef displayGC9A01Aconf_h
#define displayGC9A01Aconf_h

#include "../../core/config.h"

#define DSP_WIDTH       240
#define TFT_FRAMEWDT    8
#define MAX_WIDTH       DSP_WIDTH-TFT_FRAMEWDT*2
#define bootLogoTop     15
#define HIDE_TITLE2

/* SROLLS  */                            /* {{ left, top, fontsize, align }, buffsize, uppercase, width, scrolldelay, scrolldelta, scrolltime } */
// metaConf szűkítve: bal +48px (sorszám 46px + 2px gap), jobb -48px (mód 46px + 2px gap)
const ScrollConfig metaConf       PROGMEM = {{ TFT_FRAMEWDT, TFT_FRAMEWDT+28+18, 4, WA_CENTER }, 140, true, MAX_WIDTH-16, 5000, 5, 30 };
const ScrollConfig title1Conf     PROGMEM = {{ TFT_FRAMEWDT, /*70*/90, 2, WA_CENTER }, 140, true, MAX_WIDTH, 5000, 4, 30 };
const ScrollConfig title2Conf     PROGMEM = {{ TFT_FRAMEWDT, 90, 2, WA_CENTER }, 140, false, MAX_WIDTH, 5000, 4, 30 };
const ScrollConfig playlistConf   PROGMEM = {{ TFT_FRAMEWDT, 112, 2, WA_LEFT }, 140, true, MAX_WIDTH, 0, 2, 30 };
const ScrollConfig apTitleConf    PROGMEM = {{ TFT_FRAMEWDT+12, TFT_FRAMEWDT+28+20, 3, WA_CENTER }, 140, false, MAX_WIDTH-24, 0, 4, 20 };
const ScrollConfig apSettConf     PROGMEM = {{ TFT_FRAMEWDT+32, 240-TFT_FRAMEWDT-34, 2, WA_LEFT }, 140, false, MAX_WIDTH-64, 0, 4, 30 };
const ScrollConfig weatherConf    PROGMEM = {{ TFT_FRAMEWDT+30, 30, 1, WA_CENTER }, 140, false, MAX_WIDTH-60, 0, 2, 30 };

/* BACKGROUNDS  */                       /* {{ left, top, fontsize, align }, width, height, outlined } */
const FillConfig   metaBGConf     PROGMEM = {{ 0, 52, 0, WA_LEFT }, DSP_WIDTH, 30, false };
const FillConfig   metaBGConfLine PROGMEM = {{ 3, 81, 0, WA_LEFT }, DSP_WIDTH-6, 1, true };
const FillConfig   metaBGConfInv  PROGMEM = {{ 0, 82, 0, WA_LEFT }, DSP_WIDTH, 1, false };
const FillConfig   volbarConf     PROGMEM = {{ TFT_FRAMEWDT+56, 240-TFT_FRAMEWDT-6, 0, WA_LEFT }, MAX_WIDTH-112, 6+TFT_FRAMEWDT+1, true };
const FillConfig  playlBGConf     PROGMEM = {{ 0, 107, 0, WA_LEFT }, DSP_WIDTH, 24, false };
const FillConfig  heapbarConf     PROGMEM = {{ 0, 85, 0, WA_LEFT }, DSP_WIDTH, 2, false };

/* WIDGETS  */                           /* { left, top, fontsize, align } */
const WidgetConfig bootstrConf    PROGMEM = { 0, 182, 0, WA_CENTER };
const WidgetConfig bitrateConf    PROGMEM = { 134, 23, 0, WA_RIGHT };
const WidgetConfig voltxtConf     PROGMEM = { 80, 12, 0, WA_CENTER };
const WidgetConfig  iptxtConf     PROGMEM = { TFT_FRAMEWDT, 214, 0, WA_CENTER };
const WidgetConfig   rssiConf     PROGMEM = { 134, 23, 0, WA_LEFT };
const WidgetConfig numConf        PROGMEM = { 0, 120+30+20, 0, WA_CENTER };
const WidgetConfig apNameConf     PROGMEM = { TFT_FRAMEWDT, 96, 5, WA_CENTER };
const WidgetConfig apName2Conf    PROGMEM = { TFT_FRAMEWDT, 118, 5, WA_CENTER };
const WidgetConfig apPassConf     PROGMEM = { TFT_FRAMEWDT, 146, 5, WA_CENTER };
const WidgetConfig apPass2Conf    PROGMEM = { TFT_FRAMEWDT, 168, 5, WA_CENTER };

const WidgetConfig bootWdtConf    PROGMEM = { 0, 162, 1, WA_CENTER };
const ProgressConfig bootPrgConf  PROGMEM = { 90, 14, 4 };

/* ÁLLOMÁS SORSZÁM (outline box, bal oldal – meta sor) */
// metaBG: top=52, height=30, box_h=23 → box_top=52+(30-23)/2=55
inline StationNumConfig getstationNumConf() {
  return {{ TFT_FRAMEWDT, 55, 3, WA_LEFT }, 46 };
}

/* LEJÁTSZÁS MÓD (filled box, jobb oldal – meta sor) */
inline PlayModeConfig getplayModeConf() {
  return {{ DSP_WIDTH - TFT_FRAMEWDT - 46, 55, 3, WA_LEFT }, 46 };
}

/* BANDS  */                             /* { onebandwidth, onebandheight, bandsHspace, bandsVspace, numofbands, fadespeed } */
inline VUBandsConfig getbandsConf() {
  switch (config.store.vuLayout) {
    case 1:  return { 90, config.store.vuBarHeightStr, 6, config.store.vuBarGapStr,  config.store.vuBarCountStr,  config.store.vuFadeSpeedStr };
    case 2:  return { 90, config.store.vuBarHeightBbx, 6, config.store.vuBarGapBbx,  config.store.vuBarCountBbx,  config.store.vuFadeSpeedBbx };
    case 3:  return { 224, config.store.vuBarHeightStd, config.store.vuBarGapStd, 2, config.store.vuBarCountStd,  config.store.vuFadeSpeedStd };
    default: return { config.store.vuBarHeightDef, 20, 6, config.store.vuBarGapDef,  config.store.vuBarCountDef,  config.store.vuFadeSpeedDef };
  }
}

inline WidgetConfig getvuConf() {
  switch (config.store.vuLayout) {
    case 1: return { TFT_FRAMEWDT+20, 188, 1, WA_CENTER };
    case 2: return { TFT_FRAMEWDT+20, 188, 1, WA_CENTER };
    case 3: return { TFT_FRAMEWDT+20, 188, 1, WA_CENTER };
    default: return { TFT_FRAMEWDT+20, 188, 1, WA_CENTER };
  }
}

inline WidgetConfig getclockConf() {
  switch (config.store.vuLayout) {
    case 1: return { 0, 172, 0, WA_CENTER };
    case 2: return { 0, 172, 0, WA_CENTER };
    case 3: return { 0, 172, 0, WA_CENTER };
    default: return { 0, 172, 0, WA_CENTER };
  }
}

static constexpr ScrollConfig kDateBase = {
  { 10, 173, 1, WA_CENTER }, 128, false, 220, 5000, 1, 50 };

static inline uint16_t dateTopByLayout(uint8_t ly) {
  switch (ly) { case 1: case 2: case 3: return 173; default: return 173; }
}
static inline uint16_t dateLeftByLayout(uint8_t ly) {
  switch (ly) { case 1: case 2: case 3: return 10; default: return 10; }
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
const char          iptxtFmt[]    PROGMEM = "\010 %s";
const char         voltxtFmt[]    PROGMEM = "\023\025%d";
const char        bitrateFmt[]    PROGMEM = "%d KBS";

/* MOVES  */
inline MoveConfig getclockMove() {
  switch (config.store.vuLayout) {
    case 1: case 2: case 3: return { 0, 164, 0 };
    default: return { 0, 164, 0 };
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

const MoveConfig   weatherMove    PROGMEM = { TFT_FRAMEWDT, 202, -1 };
const MoveConfig   weatherMoveVU  PROGMEM = { TFT_FRAMEWDT, 202, -1 };

#endif
