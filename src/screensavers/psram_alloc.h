#pragma once
#include <stddef.h>
#include <stdlib.h>
#include <esp_heap_caps.h>
#include "../core/options.h"

// PSRAM only. No internal-RAM fallback: a ~150 KB frame buffer in internal heap starves WiFi.
// Callers treat nullptr as "fall back to the clock screensaver".
static inline void* yo_psram_malloc(size_t nbytes) {
#if defined(BOARD_HAS_PSRAM)
  return heap_caps_malloc(nbytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  (void)nbytes;
  return nullptr;
#endif
}

static inline void yo_psram_free(void* p) {
  if (p) free(p);
}
