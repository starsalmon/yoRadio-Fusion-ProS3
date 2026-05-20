// Podcast mode support (RSS -> flat episode playlist).
#pragma once

#include <stdint.h>

// Build `/data/podcast_episodes.csv` from `/data/podcasts.csv`.
// Returns number of episodes written.
uint32_t podcasts_buildEpisodesPlaylist();

// Schedule an RSS refresh in the background.
// This keeps mode switching responsive (avoids watchdog resets).
void podcasts_requestBuild(bool force = false);
bool podcasts_buildInProgress();

// Progress for the current build (best-effort).
// `showOut` may be empty when idle.
void podcasts_getProgress(uint16_t* cur, uint16_t* total, char* showOut, uint16_t showOutSz);

