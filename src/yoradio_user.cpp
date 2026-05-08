#include <Arduino.h>
#include <SPI.h>
#include "core/options.h"
#include "../myoptions.h"
#include "battery.h"

void yoradio_on_setup() {

    Serial.println(">>> yoradio_on_setup() CALLED <<<");

    pinMode(LDO2_ENABLE, OUTPUT);
    pinMode(RF_SWITCH, OUTPUT);

    digitalWrite(LDO2_ENABLE, HIGH);   // enable LDO2 (3V3_AUX)
    Serial.println(">>> LDO2 (3V3_AUX) enabled! <<<");

    // Force external antenna on ProS3 Series-D    
    digitalWrite(RF_SWITCH, HIGH);
    Serial.println(">>> Forced external antenna! <<<");

    battery_init();

}
