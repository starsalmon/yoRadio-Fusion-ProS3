#ifndef dsfont_h
#define dsfont_h

#pragma once
#define CLOCKFONT_MONO true
#include <Adafruit_GFX.h>
#include "clockfont_api.h"

// -- DS_DIGI15pt7b --
#define Clock_GFXfont DS_DIGI15pt7b_mono
#include "DS_DIGI15pt7b_mono.h"
#undef  Clock_GFXfont

// -- PointedlyMad25pt7b --
#define Clock_GFXfont PointedlyMad14pt7b_mono
#include "PointedlyMad14pt7b_mono.h"
#undef  Clock_GFXfont

// -- Office26pt7b --
#define Clock_GFXfont Office14pt7b_mono
#include "Office14pt7b_mono.h"
#undef  Clock_GFXfont

// -- Oldtimer20pt7b --
#define Clock_GFXfont Oldtimer10pt7b_mono
#include "Oldtimer10pt7b_mono.h"
#undef  Clock_GFXfont

// -- LaradotSerif25pt7b--
#define Clock_GFXfont LaradotSerif14pt7b_mono
#include "LaradotSerif14pt7b_mono.h"
#undef  Clock_GFXfont

// -- SquareFont25pt7b--
#define Clock_GFXfont SquareFont14pt7b_mono
#include "SquareFont14pt7b_mono.h"
#undef  Clock_GFXfont

// -- Decoderr26pt7b--
#define Clock_GFXfont Decoderr14pt7b_mono
#include "Decoderr14pt7b_mono.h"
#undef  Clock_GFXfont

struct ClockFontSpec { const GFXfont* font; const GFXfont* fontSmall; int8_t  baseline; int8_t  baselineSmall; uint8_t adv; };

static const ClockFontSpec CLOCK_FONTS[] PROGMEM = {
  { &DS_DIGI15pt7b_mono,         nullptr, 0, 0, 14 },
  { &PointedlyMad14pt7b_mono,    nullptr, 0, 0, 14 },
  { &Office14pt7b_mono,          nullptr, 0, 0, 14 },
  { &Oldtimer10pt7b_mono,        nullptr, 0, 0, 14 },
  { &LaradotSerif14pt7b_mono,    nullptr, 0, 0, 14 },
  { &SquareFont14pt7b_mono,      nullptr, 0, 0, 14 },
  { &Decoderr14pt7b_mono,        nullptr, 0, 0, 14 },
};

inline uint8_t clockfont_count() {
  return (uint8_t)(sizeof(CLOCK_FONTS)/sizeof(CLOCK_FONTS[0]));
}

inline uint8_t clockfont_clamp_id(uint8_t id){
  uint8_t n = clockfont_count();
  return (id < n) ? id : 0;
}

inline const GFXfont* clockfont_get(uint8_t id){
  return CLOCK_FONTS[clockfont_clamp_id(id)].font;
}

inline int8_t clockfont_baseline(uint8_t id){
  return CLOCK_FONTS[clockfont_clamp_id(id)].baseline;
}

inline uint8_t clockfont_advance(uint8_t id){
  return CLOCK_FONTS[clockfont_clamp_id(id)].adv;
}

inline const GFXfont* clockfont_get_small(uint8_t id){
  return CLOCK_FONTS[clockfont_clamp_id(id)].fontSmall;
}

inline int8_t clockfont_baseline_small(uint8_t id){
  return CLOCK_FONTS[clockfont_clamp_id(id)].baselineSmall;
}

#endif

