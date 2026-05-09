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
static uint8_t fastSamplesRemaining = 0;

static uint32_t lastChargeCheck = 0;
static bool lastUsbPresent = false;
static bool usbSenseReady = false;

static float samplePercentClamped() {
    float pct = maxlipo.cellPercent();
    // Occasionally a sample can be out of bounds; retry once immediately.
    if (!isfinite(pct) || pct < -1.0f || pct > 101.0f) {
        delay(60);
        pct = maxlipo.cellPercent();
    }
    if (!isfinite(pct)) pct = 0.0f;
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    return pct;
}

void battery_init() {
#ifdef CHARGE_SENSE_PIN
    pinMode(CHARGE_SENSE_PIN, INPUT);
    // Establish initial state
    lastUsbPresent = (digitalRead(CHARGE_SENSE_PIN) == HIGH);
    usbSenseReady = true;
    Serial.printf("5V sense initial: %s\n", lastUsbPresent ? "ON" : "OFF");
#endif

    // Adafruit_MAX1704X (via Adafruit_BusIO) calls wire->begin() internally.
    // On ESP32 this would re-begin I2C without pins unless we pre-set them.
#if defined(ESP32)
    batteryWire.setPins(BATTERY_SDA, BATTERY_SCL);
#endif

    if (!maxlipo.begin(&batteryWire)) {
        Serial.println("MAX17048 not found!");
        batteryReady = false;
        return;
    }

    Serial.println("MAX17048 detected");
    batteryWire.setClock(400000);
    // NOTE: We intentionally do NOT call quickStart() here.
    // On always-powered fuel gauges, forcing quickStart on every MCU reset can
    // produce misleading SOC (e.g. "stuck at 100%") until it settles.
    batteryReady = true;

    // Prime a reasonable value immediately (without touching the display yet).
    lastPercent = samplePercentClamped();
    Serial.printf("Battery(init): %.1f%%\n", lastPercent);

    // Force an initial update right away once the main loop starts.
    lastSample = 0;
    fastSamplesRemaining = 3; // a few quick follow-up samples after boot
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

    // First sample should happen immediately, then do a few faster "settling"
    // samples after boot, then every 30 seconds.
    const uint32_t intervalMs = (fastSamplesRemaining > 0) ? 2000 : 30000;
    if (lastSample != 0 && (millis() - lastSample < intervalMs)) return;
    lastSample = millis();

    lastPercent = samplePercentClamped();
    Serial.printf("Battery: %.1f%%\n", lastPercent);

    display.putRequest(NEWBATTERY);

    if (fastSamplesRemaining > 0) fastSamplesRemaining--;
}

float battery_get_percent() {
    return lastPercent;
}

bool battery_is_ready() {
    return batteryReady;
}

bool battery_usb_present() {
#ifdef CHARGE_SENSE_PIN
    return usbSenseReady && lastUsbPresent;
#else
    return false;
#endif
}
