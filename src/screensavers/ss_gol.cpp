// Game of Life screensaver — logic ported from starsalmon/GoL (128×128 desk toy).
#include "screensaver.h"
#include "psram_alloc.h"
#include "../displays/dspcore.h"
#include "config.h"
#include <string.h>
#include <math.h>

#if defined(BOARD_HAS_PSRAM) && DSP_MODEL == DSP_ILI9341

namespace {

static const int GW = 80;
static const int GH = 60;
static const int SCALE = 4;  // 80*4 x 60*4 => 320x240
static const int HISTORY = 6;

uint8_t* g_grid = nullptr;
uint8_t* g_next = nullptr;
uint8_t* g_age = nullptr;
uint8_t (*g_history)[(GW * GH) / 8] = nullptr;
int g_histIdx = 0;
uint16_t* g_fb = nullptr;
uint16_t g_hueShift = 0;
int g_style = 0;
uint32_t g_lastMs = 0;
bool g_ok = false;

inline int idx(int x, int y) { return y * GW + x; }

inline bool getCell(const uint8_t* buf, int x, int y) {
  const int i = idx(x, y);
  return (buf[i >> 3] >> (i & 7)) & 1;
}

inline void setCell(uint8_t* buf, int x, int y, bool v) {
  const int i = idx(x, y);
  const uint8_t mask = 1 << (i & 7);
  if (v) buf[i >> 3] |= mask;
  else buf[i >> 3] &= ~mask;
}

static uint16_t hsvTo565(uint16_t h, uint8_t s, uint8_t v) {
  const uint8_t region = h / 60;
  const uint16_t remainder = (h - region * 60) * 255 / 60;
  const uint8_t p = (v * (255 - s)) >> 8;
  const uint8_t q = (v * (255 - ((s * remainder) >> 8))) >> 8;
  const uint8_t t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;
  uint8_t r, g, b;
  switch (region) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static uint16_t heatColor(uint8_t v) {
  uint8_t r, g, b;
  if (v < 64) { r = 0; g = v * 4; b = 255; }
  else if (v < 128) { r = 0; g = 255; b = 255 - (v - 64) * 4; }
  else if (v < 192) { r = (v - 128) * 4; g = 255; b = 0; }
  else { r = 255; g = 255 - (v - 192) * 4; b = 0; }
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static uint16_t styleColorAt(int x, int y, uint8_t ageVal, int style) {
  switch (style) {
    case 0: return 0xFFFF;
    case 1: return hsvTo565((g_hueShift + (uint16_t)x + (uint16_t)y) % 360, 255, 255);
    case 2: return heatColor(ageVal);
    case 3: return hsvTo565((ageVal * 3) % 360, 255, (uint8_t)min(255, ageVal * 2));
    case 4: {
      const float v = sinf(x * 0.12f) + sinf(y * 0.15f) + sinf((x + y) * 0.08f) +
                      sinf(sqrtf((float)(x * x + y * y)) * 0.05f);
      uint16_t h = (uint16_t)((v + 4.0f) * 45.0f) + g_hueShift;
      h %= 360;
      return hsvTo565(h, 255, 255);
    }
    case 5:
      return (uint16_t)(((min(255, ageVal * 2) & 0xF8) << 8) |
                        ((min(255, ageVal / 2) & 0xFC) << 3));
    default: return 0xFFFF;
  }
}

static void randomizeGrid() {
  memset(g_grid, 0, (size_t)GW * GH / 8);
  memset(g_next, 0, (size_t)GW * GH / 8);
  memset(g_age, 0, (size_t)GW * GH);
  for (int y = 0; y < GH; y++) {
    for (int x = 0; x < GW; x++) {
      setCell(g_grid, x, y, random(100) < 25);
    }
  }
}

static int countNeighbors(int x, int y) {
  int c = 0;
  for (int dy = -1; dy <= 1; dy++) {
    for (int dx = -1; dx <= 1; dx++) {
      if (dx == 0 && dy == 0) continue;
      const int nx = (x + dx + GW) % GW;
      const int ny = (y + dy + GH) % GH;
      if (getCell(g_grid, nx, ny)) c++;
    }
  }
  return c;
}

static void stepLife() {
  for (int y = 0; y < GH; y++) {
    for (int x = 0; x < GW; x++) {
      const bool alive = getCell(g_grid, x, y);
      const int n = countNeighbors(x, y);
      const bool nextAlive = alive ? (n == 2 || n == 3) : (n == 3);
      setCell(g_next, x, y, nextAlive);
      const int i = idx(x, y);
      if (nextAlive) g_age[i] = (uint8_t)min(255, (int)g_age[i] + 8);
      else g_age[i] = (uint8_t)max(0, (int)g_age[i] - 3);
    }
  }
  memcpy(g_grid, g_next, (size_t)GW * GH / 8);
}

static bool isExtinct(const uint8_t* g) {
  const size_t n = (size_t)GW * GH / 8;
  for (size_t i = 0; i < n; i++)
    if (g[i]) return false;
  return true;
}

static void clearHistory() {
  for (int i = 0; i < HISTORY; i++) memset(g_history[i], 0, (size_t)GW * GH / 8);
  g_histIdx = 0;
}

static bool cycleDetected() {
  for (int i = 0; i < HISTORY; i++) {
    if (memcmp(g_grid, g_history[i], (size_t)GW * GH / 8) == 0) return true;
  }
  return false;
}

static void renderScaled() {
  const int sw = dsp.width();
  const int sh = dsp.height();
  for (int sy = 0; sy < GH; sy++) {
    for (int sx = 0; sx < GW; sx++) {
      const bool alive = getCell(g_grid, sx, sy);
      uint16_t color = 0;
      if (alive) color = styleColorAt(sx, sy, g_age[idx(sx, sy)], g_style);
      const int px0 = sx * SCALE;
      const int py0 = sy * SCALE;
      for (int dy = 0; dy < SCALE; dy++) {
        uint16_t* row = g_fb + (py0 + dy) * sw;
        for (int dx = 0; dx < SCALE; dx++) {
          if (px0 + dx < sw && py0 + dy < sh) row[px0 + dx] = color;
        }
      }
    }
  }
}

static void blit() {
  dsp.startWrite();
  dsp.setAddrWindow(0, 0, dsp.width(), dsp.height());
  dsp.writePixels(g_fb, (uint32_t)dsp.width() * (uint32_t)dsp.height());
  dsp.endWrite();
}

static int effectiveStyle() {
  const uint8_t cfg = screensaver_golStyleClamp(config.store.screensaverGolStyle);
  if (cfg <= SS_GOL_STYLE_COUNT - 1) return (int)cfg;
  return g_style;
}

static void maybeReseed() {
  if (!isExtinct(g_grid) && !cycleDetected()) return;
  randomizeGrid();
  clearHistory();
  const uint8_t cfg = config.store.screensaverGolStyle;
  if (cfg == SS_GOL_STYLE_AUTO) {
    g_style = random(0, SS_GOL_STYLE_COUNT);
  }
}

}  // namespace

bool ss_gol_begin() {
  ss_gol_end();
  const size_t cells = (size_t)GW * GH / 8;
  g_grid = (uint8_t*)yo_psram_malloc(cells);
  g_next = (uint8_t*)yo_psram_malloc(cells);
  g_age = (uint8_t*)yo_psram_malloc((size_t)GW * GH);
  g_history = (uint8_t (*)[(GW * GH) / 8])yo_psram_malloc(HISTORY * cells);
  g_fb = (uint16_t*)yo_psram_malloc((size_t)dsp.width() * (size_t)dsp.height() * sizeof(uint16_t));
  if (!g_grid || !g_next || !g_age || !g_history || !g_fb) {
    ss_gol_end();
    return false;
  }
  g_style = random(0, SS_GOL_STYLE_COUNT);
  g_hueShift = 0;
  randomizeGrid();
  clearHistory();
  g_ok = true;
  g_lastMs = 0;
  return true;
}

void ss_gol_end() {
  yo_psram_free(g_grid); g_grid = nullptr;
  yo_psram_free(g_next); g_next = nullptr;
  yo_psram_free(g_age); g_age = nullptr;
  yo_psram_free(g_history); g_history = nullptr;
  yo_psram_free(g_fb); g_fb = nullptr;
  g_ok = false;
}

void ss_gol_tick() {
  if (!g_ok || !g_fb) return;
  const uint32_t now = millis();
  if (g_lastMs && (uint32_t)(now - g_lastMs) < 80u) return;
  g_lastMs = now;

  g_style = effectiveStyle();
  g_hueShift = (g_hueShift + 2) % 360;
  stepLife();
  maybeReseed();
  memcpy(g_history[g_histIdx], g_grid, (size_t)GW * GH / 8);
  g_histIdx = (g_histIdx + 1) % HISTORY;
  renderScaled();
  blit();
}

#else

bool ss_gol_begin() { return false; }
void ss_gol_end() {}
void ss_gol_tick() {}

#endif
