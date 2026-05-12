#include <Arduino.h>
#include <SPI.h>
#include "core/options.h"
#include "../myoptions.h"
#include "battery.h"
#include <Adafruit_NeoPixel.h>

#ifndef BUILTIN_NEOPIXEL_BOOT_BRIGHTNESS
  #define BUILTIN_NEOPIXEL_BOOT_BRIGHTNESS 10
#endif

void yoradio_on_setup() {

    Serial.println(">>> yoradio_on_setup() CALLED <<<");
    Serial.println(">>> Custom options (myoptions.h) <<<");
#if defined(ARDUINO_PROS3)
    Serial.println("board: PROS3");
#endif
#if defined(PROS3_USE_EXTERNAL_ANTENNA)
    Serial.printf("PROS3_USE_EXTERNAL_ANTENNA=%d\n", (int)PROS3_USE_EXTERNAL_ANTENNA);
#endif
#if defined(BUILTIN_NEOPIXEL_PIN)
    Serial.printf("BUILTIN_NEOPIXEL_PIN=%d\n", (int)BUILTIN_NEOPIXEL_PIN);
#endif
#if defined(BUILTIN_NEOPIXEL_BOOT_BRIGHTNESS)
    Serial.printf("BUILTIN_NEOPIXEL_BOOT_BRIGHTNESS=%d\n", (int)BUILTIN_NEOPIXEL_BOOT_BRIGHTNESS);
#endif
#if defined(SD_AUTORESUME_ON_MODE_SWITCH)
    Serial.printf("SD_AUTORESUME_ON_MODE_SWITCH=%d\n", (int)SD_AUTORESUME_ON_MODE_SWITCH);
#endif
#if defined(WAKE_PIN1)
    Serial.printf("WAKE_PIN1=%d\n", (int)WAKE_PIN1);
#endif
#if defined(WAKE_PIN2)
    Serial.printf("WAKE_PIN2=%d\n", (int)WAKE_PIN2);
#endif
#if defined(AUTO_DEEPSLEEP_IDLE_MINUTES)
    Serial.printf("AUTO_DEEPSLEEP_IDLE_MINUTES=%d\n", (int)AUTO_DEEPSLEEP_IDLE_MINUTES);
#endif
#if defined(AUTO_DEEPSLEEP_BATT_PCT)
    Serial.printf("AUTO_DEEPSLEEP_BATT_PCT=%d\n", (int)AUTO_DEEPSLEEP_BATT_PCT);
#endif
#if defined(MQTT_DISABLE)
    Serial.printf("MQTT_DISABLE=%d\n", (int)MQTT_DISABLE);
#endif
#if defined(MQTT_QUIET_LOGS)
    Serial.printf("MQTT_QUIET_LOGS=%d\n", (int)MQTT_QUIET_LOGS);
#endif

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
    // Ensure the onboard NeoPixel is off (WS2812 can latch random colors if its
    // data line floats during boot). Keep this minimal; future behavior can be
    // implemented separately.
    {
        static Adafruit_NeoPixel s_px(1, BUILTIN_NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
        s_px.begin();
        s_px.setBrightness(BUILTIN_NEOPIXEL_BOOT_BRIGHTNESS); // low brightness even if something sets a color later
        s_px.clear();
        s_px.show();
        delay(1);
        s_px.show(); // double-show helps on some WS2812 power-paths
    }
#endif

    #if !defined(BATTERY_ENABLED) || (BATTERY_ENABLED != 0)
      battery_init();
    #endif

}
