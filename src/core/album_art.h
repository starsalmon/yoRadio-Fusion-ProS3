#pragma once

#include <stdint.h>

// SD album art support (first pass):
// - looks for a JPEG file in the same folder as the current SD track
// - decodes it off the display task and exposes an RGB565 buffer for the UI

void album_art_request_for_sd_track(const char* audioPath);
bool album_art_get_current(const uint16_t** outPixels, uint16_t* outW, uint16_t* outH);
void album_art_clear();

