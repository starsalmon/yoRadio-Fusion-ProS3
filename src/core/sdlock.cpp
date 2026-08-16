#include "sdlock.h"

namespace {
SemaphoreHandle_t s_sdMtx = nullptr;

static inline void ensureInit() {
  if (s_sdMtx) return;
  s_sdMtx = xSemaphoreCreateMutex();
}
} // namespace

bool sdlock_take(TickType_t timeoutTicks) {
  ensureInit();
  if (!s_sdMtx) return false;
  return xSemaphoreTake(s_sdMtx, timeoutTicks) == pdTRUE;
}

void sdlock_give() {
  if (!s_sdMtx) return;
  xSemaphoreGive(s_sdMtx);
}

