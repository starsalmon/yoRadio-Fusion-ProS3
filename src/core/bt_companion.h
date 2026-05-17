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

