/*************************************************************************************
    ST7789 320x240 - coordinates tuned for DejaVu Sans fonts

    yAdvance values (DejaVu, FT_RENDER_MODE_MONO, 96dpi):
      DejaVuSansBold14 = 23px  → metaConf   (textsize=3)
      DejaVuSansBold12 = 19px  → title1Conf (textsize=2)
      DejaVuSans13     = 22px  → title2Conf (textsize=1 <- changed!)
      DejaVuSans8      = 13px  → ip/rssi/vol%

    Everything else (VU layouts, clock, weather, date, moves) is unchanged.
*************************************************************************************/

#ifndef displayILI9341conf_h
#define displayILI9341conf_h

#include "../../core/config.h"

#define DSP_WIDTH       320
#define TFT_FRAMEWDT    8
#define MAX_WIDTH       DSP_WIDTH-TFT_FRAMEWDT*2

#if BITRATE_FULL
  #define TITLE_FIX 44
#else
  #define TITLE_FIX 0
#endif
#define bootLogoTop     20

/* SCROLLS */
const ScrollConfig metaConf     PROGMEM = {{ TFT_FRAMEWDT+38, 6, 3, WA_CENTER }, 140, true, MAX_WIDTH-84, 5000, 5, 30 };
const ScrollConfig title1Conf   PROGMEM = {{ TFT_FRAMEWDT, 36, 2, WA_CENTER   }, 140, true,  MAX_WIDTH, 5000, 4, 30 };
const ScrollConfig title2Conf   PROGMEM = {{ TFT_FRAMEWDT, 56, 1, WA_CENTER   }, 140, false, MAX_WIDTH, 5000, 4, 30 };  // textsize=1 → DejaVuSans13
const ScrollConfig playlistConf PROGMEM = {{ TFT_FRAMEWDT,112, 1, WA_LEFT   }, 140, false, MAX_WIDTH, 1000, 2, 30 };  // textsize=1 → DejaVuSans13
const ScrollConfig apTitleConf  PROGMEM = {{ TFT_FRAMEWDT, TFT_FRAMEWDT, 3, WA_CENTER }, 140, false, MAX_WIDTH, 0, 4, 20 };
const ScrollConfig apSettConf   PROGMEM = {{ TFT_FRAMEWDT, 240-TFT_FRAMEWDT-16, 2, WA_LEFT }, 140, false, MAX_WIDTH, 0, 3, 35 };
const ScrollConfig weatherConf  PROGMEM = {{ TFT_FRAMEWDT, 80, 2, WA_CENTER }, 300, false, MAX_WIDTH, 0, 3, 60 };

/* BACKGROUNDS */
const FillConfig metaBGConf     PROGMEM = {{ 0,  0, 0, WA_LEFT }, DSP_WIDTH, 32, false };  // Bold14 23px + 3px
const FillConfig metaBGConfLine PROGMEM = {{ 3, 31, 0, WA_CENTER }, DSP_WIDTH - 6, 1, true };
const FillConfig metaBGConfInv  PROGMEM = {{ 0, 32, 0, WA_LEFT }, DSP_WIDTH, 1, false };
const FillConfig volbarConf     PROGMEM = {{ TFT_FRAMEWDT, 240-TFT_FRAMEWDT-6, 0, WA_LEFT }, MAX_WIDTH, 6, true };
const FillConfig playlBGConf    PROGMEM = {{ 0, 107, 0, WA_LEFT }, DSP_WIDTH, 24, false };
const FillConfig heapbarConf    PROGMEM = {{ 0, 239, 0, WA_LEFT }, DSP_WIDTH, 1, false };

/* WIDGETS */
const WidgetConfig bootstrConf  PROGMEM = { 0, 190, 0, WA_CENTER };
const WidgetConfig bitrateConf  PROGMEM = { 70, 191, 1, WA_LEFT };

// Footer vertical alignment:
// Anchor all footer icons off the volume bar so spacing stays consistent.
#define VOLBAR_TOP         (240 - TFT_FRAMEWDT - 6) // matches volbarConf top
#define ICON_PAD_ABOVE_BAR 1
#define ICON_BOTTOM_Y      (VOLBAR_TOP - ICON_PAD_ABOVE_BAR)

// Icon sizes (px)
// Keep widths/heights together so layout math is consistent.
#define VOL_ICON_W         18
#define VOL_ICON_H         18
#define BAT_ICON_W         22
#define BAT_ICON_H         12
#define BAT_BOLT_W         9
#define BAT_BOLT_H         12
#define CLASSIC_ICON_H     16  // classic 5x7 @2x
#define RSSI_ICON_W        24  // classic bars "\001\002" at 2x: 2 chars * 12px
#define LAN_ICON_W         18
#define LAN_ICON_H         20

#define VOL_ICON_TOP       (ICON_BOTTOM_Y - VOL_ICON_H)
#define BAT_ICON_TOP       (ICON_BOTTOM_Y - BAT_ICON_H - 1)
#define BAT_BOLT_TOP       (ICON_BOTTOM_Y - BAT_BOLT_H)
#define CLASSIC_ICON_TOP   (ICON_BOTTOM_Y - CLASSIC_ICON_H)
#define LAN_ICON_TOP       (ICON_BOTTOM_Y - LAN_ICON_H)

// Footer volume layout:
// - Speaker icon stays fixed
// - Volume text is left-anchored right next to it (so 9% vs 99% doesn't shift spacing)
#define VOL_ICON_LEFT      ((DSP_WIDTH / 2) - 6)
#define VOL_ICON_TEXT_PAD  2
#define VOL_TEXT_LEFT      (VOL_ICON_LEFT + VOL_ICON_W + VOL_ICON_TEXT_PAD)

// Footer battery layout (right side):
// - Battery icon stays fixed
// - Battery percent is left-anchored next to it
// Compute battery cluster from a single right limit so it stays clear/consistent.
#define FOOTER_GAP         12   // space between battery cluster and RSSI bars (move cluster left)
#define BAT_GROUP_W        50  // reserves room for icon + "100%" (tight, to pull cluster right)
#define BAT_RIGHT_LIMIT    (DSP_WIDTH - TFT_FRAMEWDT - RSSI_ICON_W - FOOTER_GAP)
#define BAT_ICON_LEFT      (BAT_RIGHT_LIMIT - BAT_GROUP_W)
#define BAT_ICON_TEXT_PAD  2
#define BAT_TEXT_LEFT      (BAT_ICON_LEFT + BAT_ICON_W + BAT_ICON_TEXT_PAD)

// Optional charging indicator (bolt) shown when 5V sense is on.
#define BAT_BOLT_PAD       2
#define BAT_BOLT_LEFT      (BAT_ICON_LEFT - BAT_BOLT_PAD - BAT_BOLT_W)
// Footer (bottom row): use a slightly larger DejaVu GFXfont (smooth, not blocky).
// textsize=7 maps to DejaVuSans8_HU via yoScrollFont() default branch.
const WidgetConfig voltxtConf   PROGMEM = { VOL_TEXT_LEFT, 210, 7, WA_LEFT };
// LAN/IP icon + text
#define LAN_ICON_LEFT      (TFT_FRAMEWDT)
#define LAN_ICON_TEXT_PAD  2
#define IP_TEXT_LEFT       (LAN_ICON_LEFT + LAN_ICON_W + LAN_ICON_TEXT_PAD)

const WidgetConfig iptxtConf    PROGMEM = { IP_TEXT_LEFT, 210, 7, WA_LEFT };
const WidgetConfig rssiConf     PROGMEM = { TFT_FRAMEWDT + 26, 210, 7, WA_RIGHT };   // leave room for wifi bars icon
const WidgetConfig battxtConf   PROGMEM = { BAT_TEXT_LEFT, 210, 7, WA_LEFT };

// Footer icons (classic glcdfont, 1x) drawn as separate widgets so we can keep
// smooth DejaVu text while still showing icon glyphs.
// Icons at 2x (classic) to match the earlier “big icons” look.
// 100+X forces classic font scaling (see TextWidget::init).
const WidgetConfig ipiconConf   PROGMEM = { LAN_ICON_LEFT, LAN_ICON_TOP, 102, WA_LEFT };
const WidgetConfig voliconConf  PROGMEM = { VOL_ICON_LEFT, VOL_ICON_TOP, 102, WA_LEFT };
// Battery bitmap is 22x12; top is tuned to align visually with footer text.
const WidgetConfig baticonConf  PROGMEM = { BAT_ICON_LEFT, BAT_ICON_TOP, 102, WA_LEFT };
const WidgetConfig batchgConf   PROGMEM = { BAT_BOLT_LEFT, BAT_BOLT_TOP, 102, WA_LEFT };
const WidgetConfig rssibarConf  PROGMEM = { TFT_FRAMEWDT, CLASSIC_ICON_TOP, 102, WA_RIGHT };
const WidgetConfig numConf      PROGMEM = { 0, 150, 0, WA_CENTER };
const WidgetConfig apNameConf   PROGMEM = { TFT_FRAMEWDT, 66, 2, WA_CENTER };
const WidgetConfig apName2Conf  PROGMEM = { TFT_FRAMEWDT, 90, 2, WA_CENTER };
const WidgetConfig apPassConf   PROGMEM = { TFT_FRAMEWDT, 130, 2, WA_CENTER };
const WidgetConfig apPass2Conf  PROGMEM = { TFT_FRAMEWDT, 154, 2, WA_CENTER };
const WidgetConfig bootWdtConf  PROGMEM = { 0, 162, 1, WA_CENTER };
const ProgressConfig bootPrgConf PROGMEM = { 90, 14, 4 };

/* BITRATE */
inline BitrateConfig getfullbitrateConf() {
  switch (config.store.vuLayout) {
    case 1: return {{DSP_WIDTH-TFT_FRAMEWDT-75, 102, 1, WA_LEFT}, 30 };
    case 2: return {{DSP_WIDTH-TFT_FRAMEWDT-75, 102, 1, WA_LEFT}, 30 };
    case 3: return {{DSP_WIDTH-TFT_FRAMEWDT-75, 102, 1, WA_LEFT}, 30 };
    default: return {{180, 110, 1, WA_LEFT}, 30 };
  }
}

/* STATION NUMBER WIDGET */
inline StationNumConfig getstationNumConf() {
  return {{ 4, 8, 0, WA_LEFT }, 38 };
}

/* PLAY MODE WIDGET */
inline PlayModeConfig getplayModeConf() {
  return {{ DSP_WIDTH - 4 - 38, 8, 6, WA_LEFT }, 38 };
}

/* VU */
inline VUBandsConfig getbandsConf() {
  switch (config.store.vuLayout) {
    case 1: return { 142, config.store.vuBarHeightStr, 15, config.store.vuBarGapStr,  config.store.vuBarCountStr,  config.store.vuFadeSpeedStr };
    case 2: return { 142, config.store.vuBarHeightBbx, 15, config.store.vuBarGapBbx,  config.store.vuBarCountBbx,  config.store.vuFadeSpeedBbx };
    case 3: return { 312, config.store.vuBarHeightStd, config.store.vuBarGapStd, 3, config.store.vuBarCountStd,  config.store.vuFadeSpeedStd };
    default: return { config.store.vuBarHeightDef, 100, 6, config.store.vuBarGapDef,  config.store.vuBarCountDef,  config.store.vuFadeSpeedDef };
  }
}

inline WidgetConfig getvuConf() {
  switch (config.store.vuLayout) {
    case 1: return { TFT_FRAMEWDT, 185, 1, WA_CENTER };
    case 2: return { TFT_FRAMEWDT, 185, 1, WA_CENTER };
    case 3: return { TFT_FRAMEWDT, 185, 1, WA_CENTER };
    default: return { TFT_FRAMEWDT, 100, 1, WA_LEFT };
  }
}

/* CLOCK */
inline WidgetConfig getclockConf() {
  switch (config.store.vuLayout) {
    case 1: return { 20, 157, 52, WA_RIGHT };
    case 2: return { 20, 157, 52, WA_RIGHT };
    case 3: return { 20, 157, 52, WA_RIGHT };
    default: return { TFT_FRAMEWDT+70, 167, 52, WA_RIGHT };
  }
}

/* WEATHER ICON */
inline WidgetConfig getWeatherIconConf() {
  switch (config.store.vuLayout) {
    case 1: return { TFT_FRAMEWDT, 105, 2, WA_LEFT };
    case 2: return { TFT_FRAMEWDT, 105, 2, WA_LEFT };
    case 3: return { TFT_FRAMEWDT, 105, 2, WA_LEFT };
    default: return { DSP_WIDTH-TFT_FRAMEWDT-55, 125, 2, WA_LEFT };
  }
}

/* DATE */
static constexpr ScrollConfig kDateBase = {
  { TFT_FRAMEWDT, 186, 5, WA_RIGHT }, 128, false, 220, 5000, 3, 35 };

static inline uint16_t dateTopByLayout(uint8_t ly) {
  switch (ly) { case 1: case 2: case 3: return 160; default: return 170; }
}
static inline uint16_t dateLeftByLayout(uint8_t ly) {
  switch (ly) { case 1: case 2: case 3: return 20; default: return TFT_FRAMEWDT+70; }
}

inline ScrollConfig getDateConf(uint8_t ly) {
  ScrollConfig c = kDateBase;
  c.widget.top  = dateTopByLayout(ly);
  c.widget.left = dateLeftByLayout(ly);
  return c;
}
inline ScrollConfig getDateConf() { return getDateConf(config.store.vuLayout); }

/* STRINGEK */
const char numtxtFmt[]  PROGMEM = "%d";
const char   rssiFmt[]  PROGMEM = "%d";
const char  iptxtFmt[]  PROGMEM = "%s";
const char voltxtFmt[]  PROGMEM = "%d%%";
const char bitrateFmt[] PROGMEM = "%d kBs";
const char battxtFmt[]  PROGMEM = "%d%%";

/* MOVES */
inline MoveConfig getclockMove() {
  switch (config.store.vuLayout) {
    case 1: case 2: case 3: return { 20, 157, -1 };
    default: return { TFT_FRAMEWDT+70, 167, -1 };
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

const MoveConfig weatherMove   PROGMEM = { 8, 80, MAX_WIDTH };
const MoveConfig weatherMoveVU PROGMEM = { 8, 80, MAX_WIDTH-8+TFT_FRAMEWDT };

#endif