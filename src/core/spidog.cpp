#include "spidog.h"

SemaphoreHandle_t spiMutex = nullptr;
SPIDog sdog;

bool SPIDog::begin() {
  if (spiMutex == nullptr) {
    spiMutex = xSemaphoreCreateRecursiveMutex();
  }
  return spiMutex != nullptr;
}

bool SPIDog::takeMutex() {
  if (spiMutex == nullptr) return false;
  return xSemaphoreTakeRecursive(spiMutex, portMAX_DELAY) == pdTRUE;
}

void SPIDog::giveMutex() {
  if (spiMutex == nullptr) return;
  xSemaphoreGiveRecursive(spiMutex);
}

bool SPIDog::isLocked() const {
  if (spiMutex == nullptr) return false;
  return xSemaphoreGetMutexHolder(spiMutex) != nullptr;
}
