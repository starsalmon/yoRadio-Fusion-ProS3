#ifndef widgetsconfig_h
#define widgetsconfig_h

enum WidgetAlign { WA_LEFT, WA_CENTER, WA_RIGHT };
enum BitrateFormat { BF_UNKNOWN, BF_MP3, BF_AAC, BF_FLAC, BF_OGG, BF_WAV, BF_OPU, BF_VOR };

struct WidgetConfig {
  uint16_t left; 
  uint16_t top; 
  uint16_t textsize;
  WidgetAlign align;
};

struct ScrollConfig {
  WidgetConfig widget;
  uint16_t buffsize;
  bool uppercase;
  uint16_t width;
  uint16_t startscrolldelay;
  uint8_t scrolldelta;
  uint16_t scrolltime;
};

struct FillConfig {
  WidgetConfig widget;
  uint16_t width;
  uint16_t height;
  bool outlined;
};

struct ProgressConfig {
  uint16_t speed;
  uint16_t width;
  uint16_t barwidth;
};

struct VUBandsConfig {
  uint16_t width;
  uint16_t height;
  uint8_t  space;
  uint8_t  vspace;
  uint8_t  perheight;
  uint8_t  fadespeed;
};

struct MoveConfig {
  uint16_t x;
  uint16_t y;
  int16_t width;
};

struct BitrateConfig {
  WidgetConfig widget;
  uint16_t dimension;
};

// StationNumConfig: állomás sorszám widget (outline box, mint a bitrate szám oldala)
// PlayModeConfig:   lejátszás mód widget (filled box, mint a bitrate codec oldala)
// Mindkettő BitrateConfig-kompatibilis struktúra
struct StationNumConfig {
  WidgetConfig widget;
  uint16_t dimension;   // magasság-alap (boxH = dimension/2, boxW = dimension)
};

struct PlayModeConfig {
  WidgetConfig widget;
  uint16_t dimension;
};

// StatusWidgetConfig: fix feliratú státuszjelző widget (pl. SPEECH / BLFADE / LSTRIP)
// A widget csak feliratot mutat; setActive(true) = theme.clock szín, setActive(false) = theme.div szín
struct StatusWidgetConfig {
  WidgetConfig widget;
  uint16_t width;   // widget doboz szélessége (px)
  uint16_t height;  // widget doboz magassága (px)
};

#endif
