#include "sd_spi_shared.h"

#if defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI && SDC_CS != 255

#include <Arduino.h>
#include "spidog.h"

#ifdef YORADIO_STREAM_DIAG
#include "esp_timer.h"
#include "freertos/task.h"
// [BUSDIAG] per-task shared-bus lock stats (outermost acquire only). Slots: 0 audio, 1 dsp(display), 2 loop, 3 other.
struct BusStat { volatile uint32_t n, waitUs, waitMax, holdUs, holdMax; };
static BusStat s_bs[4];
static int64_t s_holdStart = 0;
static int s_holder = -1;
static int s_depth = 0;
volatile uint32_t g_plLenCalls = 0;   // config.playlistLength() calls (config.cpp)
volatile uint32_t g_plActive = 0;     // set while PlayListWidget::drawPlaylist runs (widgets.cpp)
static int busSlot() {
  const char* n = pcTaskGetName(NULL);
  if (!n) return 3;
  if (strncasecmp(n, "audio", 5) == 0) return 0;  // audio task
  if (strcmp(n, "DspTask") == 0) return 1;         // display task
  if (strcmp(n, "loopTask") == 0) return 2;        // Arduino loop
  return 3;
}
static const char* holderName() {
  TaskHandle_t h = spiMutex ? xSemaphoreGetMutexHolder(spiMutex) : nullptr;
  return h ? pcTaskGetName(h) : "-";
}
static void busDiagTask(void*) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (!g_plActive) continue;
    BusStat c[4]; for (int i=0;i<4;i++){ c[i]=*(BusStat*)&s_bs[i]; memset((void*)&s_bs[i],0,sizeof(BusStat)); }
    uint32_t pl = g_plLenCalls; g_plLenCalls = 0;
    printf("[BUSDIAG] holder=%s plLen=%u | aud n=%u w=%ums wmax=%ums h=%ums hmax=%ums | dsp n=%u w=%ums wmax=%ums h=%ums hmax=%ums | loop n=%u w=%ums h=%ums | oth n=%u w=%ums h=%ums\n",
      holderName(), (unsigned)pl,
      (unsigned)c[0].n,(unsigned)(c[0].waitUs/1000),(unsigned)(c[0].waitMax/1000),(unsigned)(c[0].holdUs/1000),(unsigned)(c[0].holdMax/1000),
      (unsigned)c[1].n,(unsigned)(c[1].waitUs/1000),(unsigned)(c[1].waitMax/1000),(unsigned)(c[1].holdUs/1000),(unsigned)(c[1].holdMax/1000),
      (unsigned)c[2].n,(unsigned)(c[2].waitUs/1000),(unsigned)(c[2].holdUs/1000),
      (unsigned)c[3].n,(unsigned)(c[3].waitUs/1000),(unsigned)(c[3].holdUs/1000));
  }
}
#endif

extern "C" void yoradio_sd_spi_acquire() {
  sdog.begin();
#ifdef YORADIO_STREAM_DIAG
  static bool started = false;
  if (!started) { started = true; xTaskCreatePinnedToCore(busDiagTask, "busdiag", 3072, nullptr, 5, nullptr, 0); }
  const bool mine = spiMutex && xSemaphoreGetMutexHolder(spiMutex) == xTaskGetCurrentTaskHandle();
  const int64_t t0 = esp_timer_get_time();
  sdog.takeMutex();
  if (!mine) {
    const int64_t t1 = esp_timer_get_time();
    const int sl = busSlot(); const uint32_t w = (uint32_t)(t1 - t0);
    s_bs[sl].n++; s_bs[sl].waitUs += w; if (w > s_bs[sl].waitMax) s_bs[sl].waitMax = w;
    s_holder = sl; s_holdStart = t1; s_depth = 0;
  }
  s_depth++;
  return;
#endif
  sdog.takeMutex();
  // Do NOT touch TFT_CS here. Adafruit_SPITFT raises CS at endWrite(); forcing it from the SD
  // task (pinMode+digitalWrite before beginTransaction) could yank CS in the middle of a TFT
  // command issued outside this lock (sendCommand/invert/flip/sleep) -> panel desync/white screen.
}

extern "C" void yoradio_sd_spi_release() {
#ifdef YORADIO_STREAM_DIAG
  if (--s_depth <= 0 && s_holder >= 0) {
    const uint32_t h = (uint32_t)(esp_timer_get_time() - s_holdStart);
    s_bs[s_holder].holdUs += h; if (h > s_bs[s_holder].holdMax) s_bs[s_holder].holdMax = h;
    s_holder = -1; s_depth = 0;
  }
#endif
  sdog.giveMutex();
}

bool yoradio_sd_spi_try_acquire_for_display() {
  sdog.begin();
  if (spiMutex == nullptr) return true;
  return xSemaphoreTakeRecursive(spiMutex, 0) == pdTRUE;
}

void yoradio_sd_spi_release_for_display() {
  if (spiMutex == nullptr) return;
  xSemaphoreGiveRecursive(spiMutex);
}

bool yoradio_sd_spi_bus_busy() {
  sdog.begin();
  return sdog.isLocked();
}

#endif
