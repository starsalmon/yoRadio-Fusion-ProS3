// Companion ESP32 (Classic BT) controller for A2DP TX.
// This is used when the main MCU is ESP32-S3 (BLE-only) but you still want A2DP output.
#pragma once

#include <Arduino.h>

// Initialize UART + wake pin (no-op unless BT_COMPANION_ENABLE is set).
void btcompanion_init();

// Poll UART input and maintain status (no-op unless enabled).
void btcompanion_loop();

// Enable/disable BT output.
void btcompanion_setEnabled(bool enable);
bool btcompanion_enabled();
void btcompanion_toggle();

// Best-effort: ask the companion to disconnect and deep sleep, even if our internal
// enabled state is currently false (useful on boot/shutdown).
void btcompanion_forceSleep();

// Set/get the target sink name used for CONNECT.
// - Default is BT_COMPANION_SINK_NAME (from myoptions.h).
// - This is runtime-only (not persisted) unless you also change BT_COMPANION_SINK_NAME.
void btcompanion_setSinkName(const char* name);
const char* btcompanion_sinkName();

// Ask the companion to (re)connect to the current sink name.
// If audio is already started, this is a no-op to avoid disrupting playback.
void btcompanion_requestConnect();

// Optional: inform the companion about the current PCM sample rate on the I2S lines.
// This is used to keep A2DP TX audio pitched correctly when streams switch between
// 44.1k and 48k content (podcasts are often 48k).
void btcompanion_setPcmSampleRate(uint32_t hz);

// Best-effort link state based on periodic STATUS polling.
// Used for UI icon selection (idle/searching/connected).
enum class BtCompanionLinkState : uint8_t {
  OFF = 0,
  SEARCHING = 1,
  CONNECTED = 2,
  AUDIO = 3,
};
BtCompanionLinkState btcompanion_linkState();

