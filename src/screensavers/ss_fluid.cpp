// FLIP fluid screensaver — fluid2d core (MIT, embedded-fluid2d / Ten Minute Physics).
#include "screensaver.h"
#include "psram_alloc.h"
#include "fluid2d/fluid_solver.h"
#include "../core/orient.h"
#include "../displays/dspcore.h"
#include <math.h>

#if defined(BOARD_HAS_PSRAM) && DSP_MODEL == DSP_ILI9341

namespace {

fluid2d::FluidSolver g_solver;
void* g_workspace = nullptr;
uint16_t* g_fb = nullptr;
uint32_t g_lastMs = 0;
bool g_ok = false;

static fluid2d::Config makeConfig() {
  fluid2d::Config c;
  c.simulation_width = 4.0f;
  c.simulation_height = 3.0f;
  c.grid_width = 32;
  c.grid_height = 24;
  c.max_particles = 1600;
  c.initial_fill_width = 0.88f;
  c.initial_fill_height = 0.62f;
  c.pressure_iterations = 8;
  c.particle_push_iterations = 2;
  c.flip_ratio = 0.88f;
  c.gravity_scale = 12.0f;
  return c;
}

static uint16_t densityTo565(float d, float rest) {
  if (rest <= 0.0f) rest = 1.0f;
  float t = d / (rest * 1.15f);
  if (t < 0.04f) return 0x0000;
  if (t > 1.0f) t = 1.0f;
  t = powf(t, 0.65f);
  const uint8_t b = (uint8_t)(t * 255.0f);
  const uint8_t g = (uint8_t)(t * 170.0f);
  const uint8_t r = (uint8_t)(t * 40.0f);
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static void renderDensity(const fluid2d::GridView& grid) {
  const int sw = dsp.width();
  const int sh = dsp.height();
  const int gw = (int)grid.width;
  const int gh = (int)grid.height;
  const float rest = grid.rest_density;
  for (int y = 0; y < sh; y++) {
    const int gy = (y * gh) / sh;
    uint16_t* row = g_fb + y * sw;
    for (int x = 0; x < sw; x++) {
      const int gx = (x * gw) / sw;
      const float d = grid.values[(size_t)gx * grid.stride + (size_t)gy];
      row[x] = densityTo565(d, rest);
    }
  }
}

static void blit() { screensaver_blit(g_fb); }

}  // namespace

void ss_fluid_end();

bool ss_fluid_begin() {
  ss_fluid_end();
  fluid2d::Config cfg = makeConfig();
  size_t bytes = fluid2d::FluidSolver::requiredMemory(cfg);
  if (!bytes) {
    fluid2d::Config fb;
    fb.simulation_width = 1.0f;
    fb.simulation_height = 1.0f;
    fb.grid_width = 20;
    fb.grid_height = 20;
    fb.max_particles = 900;
    cfg = fb;
    bytes = fluid2d::FluidSolver::requiredMemory(cfg);
  }
  if (!bytes) return false;
  g_workspace = yo_psram_malloc(bytes);
  g_fb = (uint16_t*)yo_psram_malloc((size_t)dsp.width() * (size_t)dsp.height() * sizeof(uint16_t));
  if (!g_workspace || !g_fb) {
    ss_fluid_end();
    return false;
  }
  if (g_solver.initialize(cfg, g_workspace, bytes) != fluid2d::Status::Ok) {
    ss_fluid_end();
    return false;
  }
  g_ok = true;
  g_lastMs = 0;
  return true;
}

void ss_fluid_end() {
  // Solver is non-copyable; initialize() fully re-inits state, and g_ok gates every use.
  yo_psram_free(g_workspace);
  g_workspace = nullptr;
  yo_psram_free(g_fb);
  g_fb = nullptr;
  g_ok = false;
}

void ss_fluid_tick() {
  if (!g_ok || !g_fb) return;
  const uint32_t now = millis();
  if (g_lastMs && (uint32_t)(now - g_lastMs) < 45u) return;
  g_lastMs = now;

  float gx = 0.f, gy = -1.f;
  if (orient_gravityScreen(&gx, &gy)) {
    const float scale = 1.25f;
    g_solver.setGravity(gx * scale, gy * scale);
  } else {
    g_solver.setGravity(0.f, -9.81f);
  }

  const float dt = 0.033f;
  if (g_solver.step(dt) == fluid2d::Status::Ok) {
    renderDensity(g_solver.densityGrid());
    blit();
  }
}

#else

bool ss_fluid_begin() { return false; }
void ss_fluid_end() {}
void ss_fluid_tick() {}

#endif
