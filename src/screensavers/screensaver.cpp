#include "../core/options.h"   // DSP_MODEL etc. (config.h/display.h do not pull it in)
#include "screensaver.h"
#include "../core/config.h"
#include "../core/display.h"
#include "../displays/dspcore.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

bool ss_fluid_begin();
void ss_fluid_end();
void ss_fluid_tick();
bool ss_gol_begin();
void ss_gol_end();
void ss_gol_tick();

static uint8_t s_active = SS_CLOCK;

uint8_t screensaver_styleClamp(uint8_t v) {
  if (v >= SS__COUNT) return SS_CLOCK;
  return v;
}

uint8_t screensaver_golStyleClamp(uint8_t v) {
  if (v >= SS_GOL_STYLE_COUNT) return SS_GOL_STYLE_AUTO;
  return v;
}

bool screensaver_isAnimated() {
  return config.isScreensaver && s_active != SS_CLOCK;
}

void screensaver_enter(displayMode_e mode) {
  screensaver_leave();
  if (mode == SCREENBLANK) return;
  const uint8_t want = screensaver_styleClamp(config.store.screensaverStyle);
#if !defined(BOARD_HAS_PSRAM) || DSP_MODEL != DSP_ILI9341
  s_active = SS_CLOCK;
  (void)want;
  return;
#else
  switch (want) {
    case SS_FLUID:
      s_active = ss_fluid_begin() ? SS_FLUID : SS_CLOCK;
      break;
    case SS_GOL:
      s_active = ss_gol_begin() ? SS_GOL : SS_CLOCK;
      break;
    default:
      s_active = SS_CLOCK;
      break;
  }
#endif
}

void screensaver_leave() {
  ss_fluid_end();
  ss_gol_end();
  s_active = SS_CLOCK;
}

void screensaver_tick() {
  if (!config.isScreensaver || display.mode() != SCREENSAVER) return;
  if (!config.store.dspon) return;
  switch (s_active) {
    case SS_FLUID: ss_fluid_tick(); break;
    case SS_GOL:   ss_gol_tick(); break;
    default: break;
  }
}

void screensaver_blit(const uint16_t* fb, const uint8_t* stripMask) {
  if (!fb) return;
  const int w = dsp.width(), h = dsp.height();
  const int STRIP = SS_BLIT_STRIP;  // 320x16x2 = 10 KB, ~2 ms at 40 MHz per bus hold
  for (int y = 0, s = 0; y < h; y += STRIP, s++) {
    if (stripMask && !stripMask[s]) continue;
    const int rows = (h - y < STRIP) ? (h - y) : STRIP;
    dsp.startWrite();
    dsp.setAddrWindow(0, y, w, rows);
    dsp.writePixels((uint16_t*)(fb + (size_t)y * w), (uint32_t)w * (uint32_t)rows);
    dsp.endWrite();
    taskYIELD();
  }
}
