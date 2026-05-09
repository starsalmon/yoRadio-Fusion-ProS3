#include <Arduino.h>
#include <SPI.h>
#include "core/options.h"
#include "../myoptions.h"
#include "battery.h"
#include <Adafruit_NeoPixel.h>

void yoradio_on_setup() {

    Serial.println(">>> yoradio_on_setup() CALLED <<<");

    pinMode(LDO2_ENABLE, OUTPUT);
    pinMode(RF_SWITCH, OUTPUT);

    digitalWrite(LDO2_ENABLE, HIGH);   // enable LDO2 (3V3_AUX)
    Serial.println(">>> LDO2 (3V3_AUX) enabled! <<<");

    // Force external antenna on ProS3 Series-D    
    digitalWrite(RF_SWITCH, HIGH);
    Serial.println(">>> Forced external antenna! <<<");

#if defined(BUILTIN_NEOPIXEL_PIN) && (BUILTIN_NEOPIXEL_PIN != 255)
    // Ensure the onboard NeoPixel is off (WS2812 can latch random colors if its
    // data line floats during boot). Keep this minimal; future behavior can be
    // implemented separately.
    {
        static Adafruit_NeoPixel s_px(1, BUILTIN_NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
        s_px.begin();
        s_px.setBrightness(10); // low brightness even if something sets a color later
        s_px.clear();
        s_px.show();
        delay(1);
        s_px.show(); // double-show helps on some WS2812 power-paths
    }
#endif

    battery_init();

}
