#ifndef dsfont_h
#define dsfont_h

#pragma once
#define CLOCKFONT_MONO true
#include <Adafruit_GFX.h>
#include "clockfont_api.h"

// -- DS_DIGI56pt7b --
#define Clock_GFXfont DS_DIGI56pt7b_mono
#include "DS_DIGI56pt7b_mono.h"        // https://tchapi.github.io/Adafruit-GFX-Font-Customiser/
#undef  Clock_GFXfont

// -- DS_DIGI28pt7b --
#define Clock_GFXfont DS_DIGI28pt7b_mono
#include "DS_DIGI28pt7b_mono.h"
#undef  Clock_GFXfont

// -- "PointedlyMad51pt7b --
#define Clock_GFXfont PointedlyMad51pt7b_mono
#include "PointedlyMad51pt7b_mono.h"
#undef  Clock_GFXfont

// -- "PointedlyMad25pt7b --
#define Clock_GFXfont PointedlyMad25pt7b_mono
#include "PointedlyMad25pt7b_mono.h"
#undef  Clock_GFXfont

// -- Office52pt7b --
#define Clock_GFXfont Office52pt7b_mono
#include "Office52pt7b_mono.h"
#undef  Clock_GFXfont

// -- Office26pt7b --
#define Clock_GFXfont Office26pt7b_mono
#include "Office26pt7b_mono.h"
#undef  Clock_GFXfont

// -- Oldtimer30pt7b --
#define Clock_GFXfont Oldtimer41pt7b_mono
#include "Oldtimer41pt7b_mono.h"
#undef  Clock_GFXfont

// -- Oldtimer20pt7b --
#define Clock_GFXfont Oldtimer20pt7b_mono
#include "Oldtimer20pt7b_mono.h"
#undef  Clock_GFXfont

// -- LaradotSerif50pt7b--
#define Clock_GFXfont LaradotSerif50pt7b_mono
#include "LaradotSerif50pt7b_mono.h"
#undef  Clock_GFXfont

// -- LaradotSerif25pt7b--
#define Clock_GFXfont LaradotSerif25pt7b_mono
#include "LaradotSerif25pt7b_mono.h"
#undef  Clock_GFXfont

// -- SquareFont48pt7b--
#define Clock_GFXfont SquareFont48pt7b_mono
#include "SquareFont48pt7b_mono.h"
#undef  Clock_GFXfont

// -- SquareFont25pt7b--
#define Clock_GFXfont SquareFont25pt7b_mono
#include "SquareFont25pt7b_mono.h"
#undef  Clock_GFXfont

// -- Decoderr52pt7b_mono--
#define Clock_GFXfont Decoderr52pt7b_mono
#include "Decoderr52pt7b_mono.h"
#undef  Clock_GFXfont

// -- Decoderr26pt7b_mono--
#define Clock_GFXfont Decoderr26pt7b_mono
#include "Decoderr26pt7b_mono.h"
#undef  Clock_GFXfont

struct ClockFontSpec { const GFXfont* font; const GFXfont* fontSmall; int8_t  baseline; int8_t  baselineSmall; uint8_t adv; };

static const ClockFontSpec CLOCK_FONTS[] PROGMEM = {
  {
    &DS_DIGI56pt7b_mono,   // big
    &DS_DIGI28pt7b_mono,   // small (seconds)
     0,                    // baseline big
    -3,                    // baseline small (jó kezdőérték)
    54
  },
  { &PointedlyMad51pt7b_mono, &PointedlyMad25pt7b_mono, 0, -3, 54 },
  { &Office52pt7b_mono, &Office26pt7b_mono, 0, -3, 54 },
  { &Oldtimer41pt7b_mono, &Oldtimer20pt7b_mono, 0, -3, 54 },
  { &LaradotSerif50pt7b_mono, &LaradotSerif25pt7b_mono, 0, -3, 54 },
  { &SquareFont48pt7b_mono, &SquareFont25pt7b_mono, 0, -3, 54 },
  { &Decoderr52pt7b_mono, &Decoderr26pt7b_mono, 0, -3, 54 },
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
