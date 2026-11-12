#pragma once
#include <stddef.h>
#include <stdlib.h>
#include <esp_heap_caps.h>
#include "options.h"

static inline void* yo_psram_malloc(size_t nbytes) {
#if defined(BOARD_HAS_PSRAM)
  void* p = heap_caps_malloc(nbytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (p) return p;
#endif
  return malloc(nbytes);
}

static inline void yo_psram_free(void* p) {
  if (p) free(p);
}
