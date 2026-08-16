#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Global SD/SPI access lock.
// The Arduino SD (FATFS + sd_diskio SPI) stack is not thread-safe; concurrent reads from
// multiple tasks can corrupt SPI transactions and crash the ESP32.
//
// Use this lock around ANY operation that touches the SD card (open/read/seek/exists).
bool sdlock_take(TickType_t timeoutTicks = portMAX_DELAY);
void sdlock_give();

