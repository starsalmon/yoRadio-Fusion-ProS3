#pragma once

#include <stdint.h>
#include <pgmspace.h>

#include "station_logos_types.hpp"

// Default (built-in) logo pixel data was generated at 64x64.
// Playlist-generated logos can be other sizes (e.g. 80x80).
#ifndef STATION_LOGO_W
  #define STATION_LOGO_W 64
  #define STATION_LOGO_H 64
#endif

#include "station_logos_autogen.hpp"         // default pixel arrays (64x64)
#include "station_logos_table_default.hpp"   // default table (matches playlist names)
#include "station_logos_playlist.hpp"        // generated from station_logos/bulk_logos.txt
