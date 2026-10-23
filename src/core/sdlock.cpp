#include "sdlock.h"
#if defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI && SDC_CS != 255
#include "spidog.h"

// Shared TFT/SD bus: the SD lock is the bus lock itself (recursive spiMutex). One mutex means
// one order (see sdlock.h); the old s_sdMtx -> spiMutex pair could invert against
// spiMutex holders that then needed SD.
bool sdlock_take(TickType_t timeoutTicks) {
  sdog.begin();
  return sdog.takeMutex(timeoutTicks);
}

void sdlock_give() {
  sdog.giveMutex();
}

#else

namespace {
SemaphoreHandle_t s_sdMtx = nullptr;
static inline void ensureInit() {
  if (s_sdMtx) return;
  s_sdMtx = xSemaphoreCreateRecursiveMutex();
}
} // namespace

bool sdlock_take(TickType_t timeoutTicks) {
  ensureInit();
  if (!s_sdMtx) return false;
  return xSemaphoreTakeRecursive(s_sdMtx, timeoutTicks) == pdTRUE;
}

void sdlock_give() {
  if (!s_sdMtx) return;
  xSemaphoreGiveRecursive(s_sdMtx);
}
#endif
