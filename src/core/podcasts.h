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

