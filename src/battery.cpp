#include "battery.h"
#include <Wire.h>
#include <Adafruit_MAX1704X.h>
#include "../myoptions.h"
#include "core/display.h"

extern Display display;

static Adafruit_MAX17048 maxlipo;
static uint32_t lastSample = 0;
static float lastPercent = 0;
static bool batteryReady = false;
static TwoWire batteryWire(1);

static uint32_t lastChargeCheck = 0;
static bool lastUsbPresent = false;
static bool usbSenseReady = false;

void battery_init() {
#ifdef CHARGE_SENSE_PIN
    pinMode(CHARGE_SENSE_PIN, INPUT);
    // Establish initial state
    lastUsbPresent = (digitalRead(CHARGE_SENSE_PIN) == HIGH);
    usbSenseReady = true;
    Serial.printf("5V sense initial: %s\n", lastUsbPresent ? "ON" : "OFF");
#endif

    batteryWire.begin(BATTERY_SDA, BATTERY_SCL);

    if (!maxlipo.begin(&batteryWire)) {
        Serial.println("MAX17048 not found!");
        batteryReady = false;
        return;
    }

    Serial.println("MAX17048 detected");
    maxlipo.quickStart();   // recommended by Adafruit
    batteryReady = true;

    // Force an initial update right away
    lastSample = 0;
}

void battery_update() {
#ifdef CHARGE_SENSE_PIN
    // Check charge/USB sense more frequently than battery sampling
    if (usbSenseReady && (lastChargeCheck == 0 || (millis() - lastChargeCheck) > 1000)) {
        lastChargeCheck = millis();
        bool usbPresent = (digitalRead(CHARGE_SENSE_PIN) == HIGH);
        if (usbPresent != lastUsbPresent) {
            lastUsbPresent = usbPresent;
            Serial.printf("5V sense changed: %s\n", usbPresent ? "ON" : "OFF");
        }
    }
#endif

    if (!batteryReady) return;

    // First sample should happen immediately, then every 30s
    if (lastSample != 0 && (millis() - lastSample < 30000)) return;  // 30 seconds
    lastSample = millis();

    lastPercent = maxlipo.cellPercent();
    Serial.printf("Battery: %.1f%%\n", lastPercent);

    display.putRequest(NEWBATTERY);
}

float battery_get_percent() {
    return lastPercent;
}
