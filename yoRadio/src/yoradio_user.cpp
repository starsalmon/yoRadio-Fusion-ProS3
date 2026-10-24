#include <Arduino.h>
#include <SPI.h>
#include "core/options.h"
#include "../myoptions.h"
#include <Adafruit_NeoPixel.h>

#if __has_include("battery.h")
  #include "battery.h"
  #define YORADIO_HAS_CUSTOM_BATTERY 1
#else
  #define YORADIO_HAS_CUSTOM_BATTERY 0
#endif

#ifndef BUILTIN_NEOPIXEL_BOOT_BRIGHTNESS
  #define BUILTIN_NEOPIXEL_BOOT_BRIGHTNESS 10
#endif

void yoradio_on_setup() {
    Serial.println(">>> yoradio_on_setup() CALLED <<<");

    // PROS3-only hardware init. Guarded so this repo can be built on other boards.
    #if defined(ARDUINO_PROS3)
      #if defined(LDO2_ENABLE) && (LDO2_ENABLE != 255)
        pinMode(LDO2_ENABLE, OUTPUT);
        digitalWrite(LDO2_ENABLE, HIGH);   // enable LDO2 (3V3_AUX)
        Serial.println(">>> LDO2 (3V3_AUX) enabled! <<<");
      #endif

      #if defined(RF_SWITCH) && (RF_SWITCH != 255) && defined(PROS3_USE_EXTERNAL_ANTENNA) && (PROS3_USE_EXTERNAL_ANTENNA)
        pinMode(RF_SWITCH, OUTPUT);
        digitalWrite(RF_SWITCH, HIGH); // external antenna
        Serial.println(">>> External antenna enabled! <<<");
      #endif
    #endif

#if defined(BUILTIN_NEOPIXEL_PIN) && (BUILTIN_NEOPIXEL_PIN != 255)
    {
        static Adafruit_NeoPixel s_px(1, BUILTIN_NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
        s_px.begin();
        s_px.setBrightness(BUILTIN_NEOPIXEL_BOOT_BRIGHTNESS);
        s_px.clear();
        s_px.show();
        delay(1);
        s_px.show();
    }
#endif

    #if YORADIO_HAS_CUSTOM_BATTERY
      battery_init();
    #endif
}

