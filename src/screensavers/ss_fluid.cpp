// FLIP fluid screensaver — fluid2d core (MIT, embedded-fluid2d / Ten Minute Physics).
//
// Pipeline: a low-priority solver task on core 1 (audio task there is prio 4 and preempts it)
// steps the sim at <= 30 Hz and publishes an 8-bit density grid. The display task (core 0)
// bilinear-upscales that grid to 2x2-pixel blocks with a smooth surface edge and pushes only
// strips that have (or just had) water, in 16-row bus-lock-released strips.
// Solver coordinates are y-up; the density grid is flipped to screen rows when published.
#include "screensaver.h"
#include "psram_alloc.h"
#include "fluid2d/fluid_solver.h"
#include "../core/config.h"
#include "../core/display.h"
#include "../core/orient.h"
#include "../displays/dspcore.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <math.h>
#include <string.h>

#if defined(BOARD_HAS_PSRAM) && DSP_MODEL == DSP_ILI9341

#ifndef FLUID_SOLVER_CORE
  #define FLUID_SOLVER_CORE 1
#endif
#ifndef FLUID_SOLVER_PRIO
  #define FLUID_SOLVER_PRIO 1
#endif
#ifndef FLUID_STATS_MS
  #define FLUID_STATS_MS 10000u
#endif

namespace {

constexpr uint32_t SIM_PERIOD_MS = 33;   // sim cap ~30 Hz, dt matches -> real time
constexpr float    SIM_DT = 0.033f;
constexpr float    GRAVITY_G = 1.25f;    // * gravity_scale (12) = 15 sim units/s^2
constexpr int      BLOCK = 2;            // render block (px)

fluid2d::FluidSolver g_solver;
void*     g_workspace = nullptr;
uint16_t* g_fb = nullptr;           // PSRAM, full frame
uint16_t* g_lut = nullptr;          // PSRAM, 256 x RGB565
uint8_t*  g_pub = nullptr;          // PSRAM, published density (gw*gh, screen rows top-down)
uint8_t*  g_draw = nullptr;         // PSRAM, render-side copy
uint8_t*  g_tmp = nullptr;          // PSRAM, solver-side scratch
int       g_gw = 0, g_gh = 0;
volatile uint32_t g_seq = 0;        // bumped per published frame
uint32_t  g_drawnSeq = 0;
uint8_t   g_prevMask[32];
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t g_task = nullptr;
SemaphoreHandle_t g_done = nullptr;
volatile bool g_run = false;
bool g_ok = false;

// stats (written by owner task only; read for the log)
volatile uint32_t st_steps = 0, st_stepUs = 0;
uint32_t st_draws = 0, st_renderUs = 0, st_pushUs = 0, st_strips = 0, st_lastLog = 0;

fluid2d::Config makeConfig() {
  fluid2d::Config c;
  // Grid must keep square padded cells: (gw+2):(gh+2) == 4:3 -> 46x34 (~7 px cells).
  c.simulation_width = 4.0f;
  c.simulation_height = 3.0f;
  c.grid_width = 46;
  c.grid_height = 34;
  c.max_particles = 1700;            // initial fill is 1674
  c.initial_fill_width = 0.88f;
  c.initial_fill_height = 0.62f;
  c.pressure_iterations = 8;
  c.particle_push_iterations = 1;
  c.flip_ratio = 0.88f;
  c.gravity_scale = 12.0f;
  return c;
}
fluid2d::Config fallbackConfig() {  // smaller, same 4:3 sim (38x28, 1100 particles)
  fluid2d::Config c = makeConfig();
  c.grid_width = 38; c.grid_height = 28; c.max_particles = 1120;
  return c;
}

inline uint16_t rgb565(int r, int g, int b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
// q = density/rest * 128. Smooth surface edge between ~0.12 and ~0.32 of rest (low enough that
// FLIP density noise inside the body doesn't speckle); light blue near the surface, deeper in the body.
void buildLut() {
  for (int q = 0; q < 256; q++) {
    const float t = q / 128.0f;
    float a = (t - 0.12f) / 0.20f; a = a < 0 ? 0 : (a > 1 ? 1 : a); a = a * a * (3 - 2 * a);
    float k = (t - 0.35f) / 0.85f; k = k < 0 ? 0 : (k > 1 ? 1 : k);
    const float r = (70 + (0 - 70) * k), g = (190 + (70 - 190) * k), b = (255 + (205 - 255) * k);
    g_lut[q] = (a <= 0.0f) ? 0 : rgb565((int)(r * a), (int)(g * a), (int)(b * a));
  }
}

void publish(uint8_t* tmp, uint8_t* pub) {
  const fluid2d::GridView gv = g_solver.densityGrid();
  if (!gv.values || gv.width != g_gw || gv.height != g_gh) return;
  const float rest = gv.rest_density > 0.0f ? gv.rest_density : 1.0f;
  const float k = 128.0f / rest;
  // Quantise into the solver's scratch, then swap in under the lock (grid is ~1.5 KB).
  for (int y = 0; y < g_gh; y++) {
    uint8_t* row = tmp + (size_t)(g_gh - 1 - y) * g_gw;   // y-up -> screen rows
    for (int x = 0; x < g_gw; x++) {
      float v = gv.values[(size_t)x * gv.stride + (size_t)y] * k;
      row[x] = v >= 255.0f ? 255 : (v <= 0.0f ? 0 : (uint8_t)v);
    }
  }
  portENTER_CRITICAL(&g_mux);
  memcpy(pub, tmp, (size_t)g_gw * g_gh);
  g_seq = g_seq + 1;
  portEXIT_CRITICAL(&g_mux);
}

void solverTask(void*) {
  // Own copies: if ss_fluid_end() ever times out it leaks these rather than freeing them under us.
  uint8_t* const tmp = g_tmp;
  uint8_t* const pub = g_pub;
  TickType_t last = xTaskGetTickCount();
  while (g_run) {
    // Screen off / panel asleep: don't burn CPU.
    if (!config.store.dspon || display.panelAsleep) { vTaskDelay(pdMS_TO_TICKS(100)); last = xTaskGetTickCount(); continue; }
    float gx = 0.f, gy = -1.f;
    orient_gravityScreen(&gx, &gy);          // displayed frame, y-up; untouched (0,-1) if no MPU
    g_solver.setGravity(gx * GRAVITY_G, gy * GRAVITY_G);
    const uint32_t t0 = micros();
    if (g_solver.step(SIM_DT) == fluid2d::Status::Ok) publish(tmp, pub);
    st_stepUs += micros() - t0; st_steps++;
    // Pace to the sim period; always yield at least one tick (loopTask/idle share core 1).
    const TickType_t now = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(SIM_PERIOD_MS);
    if ((TickType_t)(now - last) < period) vTaskDelayUntil(&last, period);
    else { vTaskDelay(1); last = xTaskGetTickCount(); }
  }
  xSemaphoreGive(g_done);
  vTaskDelete(nullptr);
}

void render(uint8_t* mask, int strips) {
  const int sw = dsp.width(), sh = dsp.height();
  const int bw = sw / BLOCK, bh = sh / BLOCK;
  // Column sample positions (8.8 fixed) in grid space, cell centres.
  uint16_t cx0[400]; uint8_t cfx[400];
  for (int bx = 0; bx < bw && bx < 400; bx++) {
    int fx = (int)(((bx + 0.5f) * g_gw / bw - 0.5f) * 256.0f);
    if (fx < 0) fx = 0;
    const int maxf = (g_gw - 1) * 256 - 1;
    if (fx > maxf) fx = maxf;
    cx0[bx] = (uint16_t)(fx >> 8); cfx[bx] = (uint8_t)(fx & 255);
  }
  memset(mask, 0, strips);
  for (int by = 0; by < bh; by++) {
    int fy = (int)(((by + 0.5f) * g_gh / bh - 0.5f) * 256.0f);
    if (fy < 0) fy = 0;
    const int maxf = (g_gh - 1) * 256 - 1;
    if (fy > maxf) fy = maxf;
    const uint8_t* r0 = g_draw + (size_t)(fy >> 8) * g_gw;
    const uint8_t* r1 = r0 + g_gw;
    const int wy = fy & 255;
    uint16_t* p0 = g_fb + (size_t)(by * BLOCK) * sw;
    uint16_t* p1 = p0 + sw;
    bool any = false;
    for (int bx = 0; bx < bw; bx++) {
      const int i = cx0[bx], wx = cfx[bx];
      const int top = r0[i] * (256 - wx) + r0[i + 1] * wx;
      const int bot = r1[i] * (256 - wx) + r1[i + 1] * wx;
      const int q = (top * (256 - wy) + bot * wy) >> 16;
      const uint16_t c = g_lut[q];
      any |= (c != 0);
      p0[2 * bx] = c; p0[2 * bx + 1] = c; p1[2 * bx] = c; p1[2 * bx + 1] = c;
    }
    if (any) mask[(by * BLOCK) / SS_BLIT_STRIP] = 1;
  }
}

}  // namespace

void ss_fluid_end();

bool ss_fluid_begin() {
  ss_fluid_end();
  fluid2d::Config cfg = makeConfig();
  size_t bytes = fluid2d::FluidSolver::requiredMemory(cfg);
  if (!bytes) { cfg = fallbackConfig(); bytes = fluid2d::FluidSolver::requiredMemory(cfg); }
  if (!bytes || cfg.grid_width * cfg.grid_height > 64 * 48) return false;
  const size_t cells = (size_t)cfg.grid_width * cfg.grid_height;
  g_workspace = yo_psram_malloc(bytes);
  g_fb   = (uint16_t*)yo_psram_malloc((size_t)dsp.width() * (size_t)dsp.height() * sizeof(uint16_t));
  g_lut  = (uint16_t*)yo_psram_malloc(256 * sizeof(uint16_t));
  g_pub  = (uint8_t*)yo_psram_malloc(cells);
  g_draw = (uint8_t*)yo_psram_malloc(cells);
  g_tmp  = (uint8_t*)yo_psram_malloc(cells);
  if (!g_workspace || !g_fb || !g_lut || !g_pub || !g_draw || !g_tmp ||
      g_solver.initialize(cfg, g_workspace, bytes) != fluid2d::Status::Ok) {
    ss_fluid_end();
    return false;
  }
  g_gw = cfg.grid_width; g_gh = cfg.grid_height;
  memset(g_pub, 0, cells); memset(g_draw, 0, cells);
  memset(g_prevMask, 1, sizeof(g_prevMask));   // first frame: push everything
  buildLut();
  g_seq = 0; g_drawnSeq = 0;
  st_steps = st_stepUs = 0; st_draws = st_renderUs = st_pushUs = st_strips = 0; st_lastLog = millis();
  if (!g_done) g_done = xSemaphoreCreateBinary();
  if (!g_done) { ss_fluid_end(); return false; }
  g_run = true;
  if (xTaskCreatePinnedToCore(solverTask, "fluidSim", 4096, nullptr, FLUID_SOLVER_PRIO, &g_task, FLUID_SOLVER_CORE) != pdPASS) {
    g_run = false; g_task = nullptr; ss_fluid_end(); return false;
  }
  g_ok = true;
  Serial.printf("[FLUID] start grid %dx%d particles %u ws %u B (PSRAM)\n", g_gw, g_gh,
                (unsigned)g_solver.particles().count, (unsigned)bytes);
  return true;
}

void ss_fluid_end() {
  g_ok = false;
  if (g_task) {
    g_run = false;
    // Wait for the solver to finish its current step before freeing its workspace.
    if (xSemaphoreTake(g_done, pdMS_TO_TICKS(2000)) != pdTRUE) {
      Serial.println("[FLUID] solver did not stop; leaking workspace");
      g_workspace = nullptr; g_pub = nullptr; g_tmp = nullptr;   // never free memory a live task may touch
    }
    g_task = nullptr;
  }
  yo_psram_free(g_workspace); g_workspace = nullptr;
  yo_psram_free(g_fb);   g_fb = nullptr;
  yo_psram_free(g_lut);  g_lut = nullptr;
  yo_psram_free(g_pub);  g_pub = nullptr;
  yo_psram_free(g_draw); g_draw = nullptr;
  yo_psram_free(g_tmp);  g_tmp = nullptr;
}

void ss_fluid_tick() {
  if (!g_ok || !g_fb) return;
  if (g_seq == g_drawnSeq) return;              // nothing new from the solver
  portENTER_CRITICAL(&g_mux);
  memcpy(g_draw, g_pub, (size_t)g_gw * g_gh);
  g_drawnSeq = g_seq;
  portEXIT_CRITICAL(&g_mux);

  const int strips = (dsp.height() + SS_BLIT_STRIP - 1) / SS_BLIT_STRIP;
  uint8_t mask[32], push[32];
  const uint32_t t0 = micros();
  render(mask, strips);
  int n = 0;
  for (int s = 0; s < strips; s++) { push[s] = mask[s] | g_prevMask[s]; n += push[s]; g_prevMask[s] = mask[s]; }
  const uint32_t t1 = micros();
  screensaver_blit(g_fb, push);
  const uint32_t t2 = micros();
  st_renderUs += t1 - t0; st_pushUs += t2 - t1; st_strips += n; st_draws++;

  const uint32_t now = millis();
  if ((uint32_t)(now - st_lastLog) >= FLUID_STATS_MS) {
    const float secs = (now - st_lastLog) / 1000.0f;
    const uint32_t steps = st_steps, stepUs = st_stepUs;
    Serial.printf("[FLUID] sim %.1f fps (step %u us) | draw %.1f fps (render %u us, push %u us, %u/%d strips)\n",
                  steps / secs, steps ? (unsigned)(stepUs / steps) : 0u,
                  st_draws / secs, st_draws ? (unsigned)(st_renderUs / st_draws) : 0u,
                  st_draws ? (unsigned)(st_pushUs / st_draws) : 0u,
                  st_draws ? (unsigned)(st_strips / st_draws) : 0u, strips);
    st_steps = 0; st_stepUs = 0; st_draws = st_renderUs = st_pushUs = st_strips = 0; st_lastLog = now;
  }
}

#else

bool ss_fluid_begin() { return false; }
void ss_fluid_end() {}
void ss_fluid_tick() {}

#endif
