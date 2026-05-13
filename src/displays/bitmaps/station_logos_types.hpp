#pragma once

#include <stdint.h>
#include <stddef.h>

struct StationLogoEntry {
  const char* match;           // station name to match (normalized compare)
  const uint16_t* pixels;      // RGB565 pixels (row-major)
  uint16_t w;
  uint16_t h;
};

