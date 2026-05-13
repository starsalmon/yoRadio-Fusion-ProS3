#pragma once

// This file must be self-contained for tooling/linting.
#include "station_logos_types.hpp"

// Ensure the default autogen header compiles (it was generated against 64x64).
#ifndef STATION_LOGO_W
  #define STATION_LOGO_W 64
  #define STATION_LOGO_H 64
#endif

#include "station_logos_autogen.hpp"

// Default (built-in) station logos (64x64), generated earlier from chat.
// These stay as a fallback even if the playlist-generated table is empty.

static const StationLogoEntry STATION_LOGOS_DEFAULT[] = {
  { "Double J",              LOGO_DOUBLEJ_64x64,        64, 64 },
  { "Triple J",              LOGO_TRIPLEJ_64x64,        64, 64 },
  { "Sub.FM",                LOGO_SUB_64x64,            64, 64 },
  { "ABC Jazz",              LOGO_ABCJAZZ_64x64,        64, 64 },
  { "FluxFM - 90s",          LOGO_FLUX90S_64x64,        64, 64 },
  { "FluxFM - Chillout Radio", LOGO_FLUX_CHILLOUT_64x64, 64, 64 },
};

static constexpr size_t STATION_LOGOS_DEFAULT_COUNT = sizeof(STATION_LOGOS_DEFAULT) / sizeof(STATION_LOGOS_DEFAULT[0]);

