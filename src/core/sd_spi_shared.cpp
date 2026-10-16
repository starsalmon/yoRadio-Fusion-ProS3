#include "sd_spi_shared.h"

#if defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI && SDC_CS != 255

#include <Arduino.h>
#include "spidog.h"

extern "C" void yoradio_sd_spi_acquire() {
  sdog.begin();
  sdog.takeMutex();
  // Do NOT touch TFT_CS here. Adafruit_SPITFT raises CS at endWrite(); forcing it from the SD
  // task (pinMode+digitalWrite before beginTransaction) could yank CS in the middle of a TFT
  // command issued outside this lock (sendCommand/invert/flip/sleep) -> panel desync/white screen.
}

extern "C" void yoradio_sd_spi_release() {
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
