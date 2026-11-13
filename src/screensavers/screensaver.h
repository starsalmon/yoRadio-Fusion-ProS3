#pragma once
#include <stdint.h>
#include "../core/common.h"

// Visual screensaver styles (clock uses the legacy bouncing clock widgets).
enum screensaverStyle_e : uint8_t {
  SS_CLOCK = 0,
  SS_FLUID = 1,
  SS_GOL   = 2,
  SS__COUNT = 3
};

// GoL palette (screensaverGolStyle): 0..5 fixed (white, rainbow, heatmap, neon trails, plasma,
// fire); 7 = random palette, re-picked on each auto-reseed. 6 is accepted as an alias for 7.
#define SS_GOL_STYLE_AUTO 7
#define SS_GOL_STYLE_COUNT 6

void screensaver_enter(displayMode_e mode);
void screensaver_leave();
bool screensaver_isAnimated();
void screensaver_tick();
uint8_t screensaver_styleClamp(uint8_t v);
uint8_t screensaver_golStyleClamp(uint8_t v);
// Push a full-screen RGB565 frame in short row strips, releasing the shared TFT/SD bus lock
// between strips so SD reads are never blocked for a whole ~30 ms frame.
void screensaver_blit(const uint16_t* fb);
