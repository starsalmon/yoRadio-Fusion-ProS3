#pragma once
#include <Arduino.h>

void battery_init();
void battery_update();
float battery_get_percent();
