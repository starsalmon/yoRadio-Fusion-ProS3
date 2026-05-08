#ifndef dsfont_h
#define dsfont_h

#pragma once
#define CLOCKFONT_MONO true
#include <Adafruit_GFX.h>
#include "clockfont_api.h"

// -- DS_DIGI42pt7b --
#define Clock_GFXfont DS_DIGI42pt7b_mono
#include "DS_DIGI42pt7b_mono.h"
#undef  Clock_GFXfont

// -- DS_DIGI14pt7b --
#define Clock_GFXfont DS_DIGI15pt7b_mono
#include "DS_DIGI15pt7b_mono.h"
#undef  Clock_GFXfont

// -- PointedlyMad38pt7b --
#define Clock_GFXfont PointedlyMad38pt7b_mono
#include "PointedlyMad38pt7b_mono.h"
#undef  Clock_GFXfont

// -- PointedlyMad14pt7b --
#define Clock_GFXfont PointedlyMad14pt7b_mono
#include "PointedlyMad14pt7b_mono.h"
#undef  Clock_GFXfont

// -- Office39pt7b --
#define Clock_GFXfont Office39pt7b_mono
#include "Office39pt7b_mono.h"
#undef  Clock_GFXfont

// -- Office14pt7b --
#define Clock_GFXfont Office14pt7b_mono
#include "Office14pt7b_mono.h"
#undef  Clock_GFXfont

// -- Oldtimer30pt7b --
#define Clock_GFXfont Oldtimer30pt7b_mono
#include "Oldtimer30pt7b_mono.h"
#undef  Clock_GFXfont

// -- Oldtimer10pt7b --
#define Clock_GFXfont Oldtimer10pt7b_mono
#include "Oldtimer10pt7b_mono.h"
#undef  Clock_GFXfont

// -- LaradotSerif38pt7b--
#define Clock_GFXfont LaradotSerif38pt7b_mono
#include "LaradotSerif38pt7b_mono.h"
#undef  Clock_GFXfont

// -- LaradotSerif14pt7b--
#define Clock_GFXfont LaradotSerif14pt7b_mono
#include "LaradotSerif14pt7b_mono.h"
#undef  Clock_GFXfont

// -- SquareFont48pt7b--
#define Clock_GFXfont SquareFont37pt7b_mono
#include "SquareFont37pt7b_mono.h"
#undef  Clock_GFXfont

// -- SquareFont14pt7b--
#define Clock_GFXfont SquareFont14pt7b_mono
#include "SquareFont14pt7b_mono.h"
#undef  Clock_GFXfont

// -- Decoderr39pt7b_mono--
#define Clock_GFXfont Decoderr39pt7b_mono
#include "Decoderr39pt7b_mono.h"
#undef  Clock_GFXfont

// -- Decoderr14pt7b_mono--
#define Clock_GFXfont Decoderr14pt7b_mono
#include "Decoderr14pt7b_mono.h"
#undef  Clock_GFXfont

struct ClockFontSpec { const GFXfont* font; const GFXfont* fontSmall; int8_t  baseline; int8_t  baselineSmall; uint8_t adv; };

static const ClockFontSpec CLOCK_FONTS[] PROGMEM = {
  { &DS_DIGI42pt7b_mono, &DS_DIGI15pt7b_mono, 0, -3, 40 },
  { &PointedlyMad38pt7b_mono, &PointedlyMad14pt7b_mono, 0, -3, 40 },
  { &Office39pt7b_mono, &Office14pt7b_mono, 0, -3, 40 },
  { &Oldtimer30pt7b_mono, &Oldtimer10pt7b_mono, 0, -3, 40 },
  { &LaradotSerif38pt7b_mono, &LaradotSerif14pt7b_mono, 0, -3, 40 },
  { &SquareFont37pt7b_mono, &SquareFont14pt7b_mono, 0, -3, 40 },
  { &Decoderr39pt7b_mono, &Decoderr14pt7b_mono, 0, -3, 40 },
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

