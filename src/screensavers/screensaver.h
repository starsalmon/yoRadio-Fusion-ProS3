#pragma once
#include <stdint.h>
#include "common.h"

// Visual screensaver styles (clock uses the legacy bouncing clock widgets).
enum screensaverStyle_e : uint8_t {
  SS_CLOCK = 0,
  SS_FLUID = 1,
  SS_GOL   = 2,
  SS__COUNT = 3
};

// GoL palette (screensaverGolStyle): 0..6 fixed; 7 = pick a random style on auto-reseed.
#define SS_GOL_STYLE_AUTO 7
#define SS_GOL_STYLE_COUNT 6

void screensaver_enter(displayMode_e mode);
void screensaver_leave();
bool screensaver_isAnimated();
void screensaver_tick();
uint8_t screensaver_styleClamp(uint8_t v);
uint8_t screensaver_golStyleClamp(uint8_t v);
