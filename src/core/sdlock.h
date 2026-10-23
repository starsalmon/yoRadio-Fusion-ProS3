#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Global SD/SPI access lock.
// The Arduino SD (FATFS + sd_diskio SPI) stack is not thread-safe; use this lock around ANY
// application-level operation that touches the SD card (open/read/seek/exists/size).
//
// LOCK ORDER (single, global - see docs/KNOWN_ISSUES.md "SD/TFT lock order"):
//   1. bus lock  = spiMutex (recursive; sdog / sdlock_take / yoradio_sd_spi_acquire / TftBusLock /
//                  DspCore::startWrite on SD_SHARE_TFT_SPI boards)
//   2. FATFS volume mutex (taken inside f_open/f_read...)
//   3. SPIClass paramLock (beginTransaction; NON-recursive)
// Rules:
//   - Always take the bus lock BEFORE FATFS. sd_diskio's AcquireSPI takes the bus lock again
//     (recursive, fine) under FATFS; an SD op without the bus lock inverts 1<->2 and stalls
//     for CONFIG_FATFS_TIMEOUT_MS (10 s) - the old "playlist menu lockup".
//   - Never start an SD op inside a TFT transaction (between startWrite/endWrite): paramLock is
//     non-recursive -> self-deadlock. Read data first, then draw.
//   - With SD_SHARE_TFT_SPI the sdlock IS the bus lock (one recursive mutex), so nesting
//     sdlock_take inside a TFT bus-lock section or another sdlock section is safe.
// Waits >200 ms are logged as "[BUSLOCK] <task> waiting >200ms, holder=<task>".
bool sdlock_take(TickType_t timeoutTicks = portMAX_DELAY);
void sdlock_give();

// RAII helper.
struct SdLockGuard {
  bool ok;
  explicit SdLockGuard(TickType_t t = portMAX_DELAY) : ok(sdlock_take(t)) {}
  ~SdLockGuard() { if (ok) sdlock_give(); }
};
