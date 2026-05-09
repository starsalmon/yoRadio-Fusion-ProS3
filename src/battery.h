#pragma once
#include <Arduino.h>

void battery_init();
void battery_update();
float battery_get_percent();

// True if the MAX17048 initialized successfully.
bool battery_is_ready();

// True if 5V sense indicates external power present.
// Returns false if CHARGE_SENSE_PIN isn't configured.
bool battery_usb_present();
