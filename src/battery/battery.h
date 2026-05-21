#pragma once
#include <Arduino.h>

void battery_init();
void battery_update();
float battery_get_percent();

// True if the MAX17048 initialized successfully.
bool battery_is_ready();

// True if a battery appears connected (based on MAX17048 voltage).
bool battery_is_present();

// Last sampled battery voltage (0 if unknown/unavailable).
float battery_get_voltage();

// Last sampled charge rate in %/hr. Positive means charging, negative discharging.
// Returns 0 if unknown/unavailable.
float battery_get_charge_rate();

// True if 5V sense indicates external power present.
// Returns false if CHARGE_SENSE_PIN isn't configured.
bool battery_usb_present();

// True if the MAX17048 alert pin is currently active (best-effort).
// Requires `BATTERY_INT` to be defined and wired to MAX17048 ALRT.
bool battery_alert_active();

// Put the MAX17048 into ultra-low-power sleep mode right before ESP deep sleep.
// Safe to call even if the gauge is unavailable.
void battery_prepare_for_deepsleep();

