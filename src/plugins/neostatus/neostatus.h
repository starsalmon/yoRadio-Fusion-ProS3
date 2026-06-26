#pragma once

#include "../../pluginsManager/pluginsManager.h"

// Register the built-in NeoPixel status plugin (if enabled).
void neostatusPluginInit();

// Optional helper pulses that can be triggered from non-plugin code paths
// (e.g. right before deep sleep).
void neostatusPulseSleep();
void neostatusPulseLowBattery();

// How long the pre-sleep cue runs (ms), so callers can delay before deep sleep.
uint16_t neostatusSleepCueMs();

// Arm NeoStatus for deep sleep (prevent further writes) without forcing any LED state.
// Useful if another subsystem wants to fade a GPIO LED before sleeping.
void neostatusArmForDeepSleep();

// Best-effort: force NeoStatus LEDs fully off and hold the data pin low.
// This reduces WS2812 "flash" artifacts during power rail collapse / deep sleep entry.
void neostatusPrepareForDeepSleep();

