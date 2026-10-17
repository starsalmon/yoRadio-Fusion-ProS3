#pragma once
#include <Arduino.h>
#include <time.h>
// Timestamped event log for white-screen hunting: "[EVT hh:mm:ss.mmm +ms] ..."
#define DSPLOG(fmt, ...) do { \
    time_t _t = time(nullptr); struct tm _tm; localtime_r(&_t, &_tm); \
    Serial.printf("[EVT %02d:%02d:%02d.%03lu +%lu] " fmt "\n", _tm.tm_hour, _tm.tm_min, _tm.tm_sec, \
                  (unsigned long)(millis() % 1000), (unsigned long)millis(), ##__VA_ARGS__); \
  } while (0)
