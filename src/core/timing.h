// Shared small timing helpers (debounce / rate-limit).
#pragma once

#include <Arduino.h>
#include <stdint.h>

// Returns true once per `intervalMs`, updating `lastMs` when it fires.
static inline bool yoEveryMs(uint32_t intervalMs, uint32_t& lastMs) {
  const uint32_t now = millis();
  if ((uint32_t)(now - lastMs) >= intervalMs) {
    lastMs = now;
    return true;
  }
  return false;
}

