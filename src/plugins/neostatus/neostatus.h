#pragma once

#include "../../pluginsManager/pluginsManager.h"

// Register the built-in NeoPixel status plugin (if enabled).
void neostatusPluginInit();

// Optional helper pulses that can be triggered from non-plugin code paths
// (e.g. right before deep sleep).
void neostatusPulseSleep();
void neostatusPulseLowBattery();

