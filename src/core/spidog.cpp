#include "spidog.h"

SemaphoreHandle_t spiMutex = nullptr;
SPIDog sdog;

bool SPIDog::begin() {
  if (spiMutex == nullptr) {
    spiMutex = xSemaphoreCreateRecursiveMutex();
  }
  return spiMutex != nullptr;
}

// Bus-lock take with a slow-wait diagnostic: if the lock is not obtained within 200 ms,
// log which task holds it, then keep waiting (up to timeoutTicks).
bool SPIDog::takeMutex(TickType_t timeoutTicks) {
  if (spiMutex == nullptr) return false;
  const TickType_t slow = pdMS_TO_TICKS(200);
  if (timeoutTicks != portMAX_DELAY && timeoutTicks <= slow)
    return xSemaphoreTakeRecursive(spiMutex, timeoutTicks) == pdTRUE;
  if (xSemaphoreTakeRecursive(spiMutex, slow) == pdTRUE) return true;
  TaskHandle_t h = xSemaphoreGetMutexHolder(spiMutex);
  const char* me = pcTaskGetName(nullptr);
  Serial.printf("[BUSLOCK] %s waiting >200ms, holder=%s\n", me ? me : "?", h ? pcTaskGetName(h) : "none");
  uint32_t t0 = millis();
  TickType_t rest = (timeoutTicks == portMAX_DELAY) ? portMAX_DELAY : (timeoutTicks - slow);
  bool ok = xSemaphoreTakeRecursive(spiMutex, rest) == pdTRUE;
  Serial.printf("[BUSLOCK] %s %s after +%lums\n", me ? me : "?", ok ? "got lock" : "TIMEOUT", (unsigned long)(millis() - t0));
  return ok;
}

void SPIDog::giveMutex() {
  if (spiMutex == nullptr) return;
  xSemaphoreGiveRecursive(spiMutex);
}

bool SPIDog::isLocked() const {
  if (spiMutex == nullptr) return false;
  return xSemaphoreGetMutexHolder(spiMutex) != nullptr;
}
