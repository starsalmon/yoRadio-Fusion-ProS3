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
