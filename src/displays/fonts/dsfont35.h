#ifndef dsfont_h
#define dsfont_h

#pragma once
#define CLOCKFONT_MONO true
#include <Adafruit_GFX.h>
#include "clockfont_api.h"

// -- DS_DIGI28pt7b --
#define Clock_GFXfont DS_DIGI28pt7b_mono
#include "DS_DIGI28pt7b_mono.h"
#undef  Clock_GFXfont

// -- DS_DIGI12pt7b --
#define Clock_GFXfont DS_DIGI12pt7b_mono
#include "DS_DIGI12pt7b_mono.h"
#undef  Clock_GFXfont

// -- PointedlyMad25pt7b --
#define Clock_GFXfont PointedlyMad25pt7b_mono
#include "PointedlyMad25pt7b_mono.h"
#undef  Clock_GFXfont

// -- PointedlyMad11pt7b --
#define Clock_GFXfont PointedlyMad11pt7b_mono
#include "PointedlyMad11pt7b_mono.h"
#undef  Clock_GFXfont

// -- Office26pt7b --
#define Clock_GFXfont Office26pt7b_mono
#include "Office26pt7b_mono.h"
#undef  Clock_GFXfont

// -- Office11pt7b --
#define Clock_GFXfont Office11pt7b_mono
#include "Office11pt7b_mono.h"
#undef  Clock_GFXfont

// -- Oldtimer20pt7b --
#define Clock_GFXfont Oldtimer20pt7b_mono
#include "Oldtimer20pt7b_mono.h"
#undef  Clock_GFXfont

// -- Oldtimer8pt7b --
#define Clock_GFXfont Oldtimer8pt7b_mono
#include "Oldtimer8pt7b_mono.h"
#undef  Clock_GFXfont

// -- LaradotSerif25pt7b--
#define Clock_GFXfont LaradotSerif25pt7b_mono
#include "LaradotSerif25pt7b_mono.h"
#undef  Clock_GFXfont

// -- LaradotSerif11pt7b--
#define Clock_GFXfont LaradotSerif11pt7b_mono
#include "LaradotSerif11pt7b_mono.h"
#undef  Clock_GFXfont

// -- SquareFont25pt7b--
#define Clock_GFXfont SquareFont25pt7b_mono
#include "SquareFont25pt7b_mono.h"
#undef  Clock_GFXfont

// -- SquareFont10pt7b--
#define Clock_GFXfont SquareFont10pt7b_mono
#include "SquareFont10pt7b_mono.h"
#undef  Clock_GFXfont

// -- Decoderr26pt7b--
#define Clock_GFXfont Decoderr26pt7b_mono
#include "Decoderr26pt7b_mono.h"
#undef  Clock_GFXfont

// -- Decoderr11pt7b--
#define Clock_GFXfont Decoderr11pt7b_mono
#include "Decoderr11pt7b_mono.h"
#undef  Clock_GFXfont

struct ClockFontSpec { const GFXfont* font; const GFXfont* fontSmall; int8_t  baseline; int8_t  baselineSmall; uint8_t adv; };

static const ClockFontSpec CLOCK_FONTS[] PROGMEM = {
  { &DS_DIGI28pt7b_mono, &DS_DIGI12pt7b_mono, 0, 0, 27 },
  { &PointedlyMad25pt7b_mono, &PointedlyMad11pt7b_mono, 0, 0, 27 },
  { &Office26pt7b_mono, &Office11pt7b_mono, 0, 0, 27 },
  { &Oldtimer20pt7b_mono, &Oldtimer8pt7b_mono, 0, 0, 27 },
  { &LaradotSerif25pt7b_mono, &LaradotSerif11pt7b_mono, 0, 0, 27 },
  { &SquareFont25pt7b_mono, &SquareFont10pt7b_mono, 0, 0, 27 },
  { &Decoderr26pt7b_mono, &Decoderr11pt7b_mono, 0, 0, 27 },
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
