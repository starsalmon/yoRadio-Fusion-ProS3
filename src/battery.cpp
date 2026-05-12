#include "battery.h"
#include <Wire.h>
#include <Adafruit_MAX1704X.h>
#include "core/options.h"
#include "../myoptions.h"
#include "core/display.h"
#ifdef MQTT_ROOT_TOPIC
#include "core/mqtt.h"
#endif

extern Display display;

static Adafruit_MAX17048 maxlipo;
static uint32_t lastSample = 0;
static float lastPercent = 0;
static float lastVoltage = 0;
static float lastChargeRate = 0;
static bool batteryReady = false;
static TwoWire batteryWire(1);
static uint8_t fastSamplesRemaining = 0;

static uint32_t lastChargeCheck = 0;
static bool lastUsbPresent = false;
static bool usbSenseReady = false;

static uint32_t lastQuickStartMs = 0;
static bool pendingQuickStart = false;

static bool voltageDefinitelyNoBattery(float v) {
    // On some boards the BAT node can float high (esp. on USB power),
    // so we only treat *very low* voltage as a reliable "no battery".
    return isfinite(v) && v > 0.0f && v < 0.50f;
}

#if defined(BATTERY_ENABLED) && (BATTERY_ENABLED == 0)

// Battery feature hard-disabled via `myoptions.h`.
// Keep the API surface available so the rest of the codebase doesn't need #ifdefs.
void battery_init() {
    batteryReady = false;
    usbSenseReady = false;
    lastUsbPresent = false;
    lastSample = 0;
    lastPercent = 0.0f;
    lastVoltage = 0.0f;
    lastChargeRate = 0.0f;
}

void battery_update() {}
float battery_get_percent() { return 0.0f; }
bool battery_is_ready() { return false; }
bool battery_is_present() { return false; }
float battery_get_voltage() { return 0.0f; }
float battery_get_charge_rate() { return 0.0f; }
bool battery_usb_present() { return false; }
void battery_prepare_for_deepsleep() {}

#else

struct BatterySample {
    float pct;
    float v;
    float rate;
};

static BatterySample sampleBatteryClamped() {
    float pct = maxlipo.cellPercent();
    float v = maxlipo.cellVoltage();
    float rate = maxlipo.chargeRate();

    // Occasionally a sample can be out of bounds; retry once immediately.
    if (
        !isfinite(pct) || pct < -1.0f || pct > 101.0f ||
        !isfinite(v) || v < 0.0f || v > 5.0f ||
        !isfinite(rate) || rate < -1000.0f || rate > 1000.0f
    ) {
        delay(60);
        pct = maxlipo.cellPercent();
        v = maxlipo.cellVoltage();
        rate = maxlipo.chargeRate();
    }

    if (!isfinite(pct)) pct = 0.0f;
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;

    if (!isfinite(v) || v < 0.0f) v = 0.0f;
    if (!isfinite(rate)) rate = 0.0f;

    return {pct, v, rate};
}

static bool usbPresentNow() {
    return usbSenseReady && lastUsbPresent;
}

static bool canQuickStartNow() {
    const uint32_t now = millis();
    // QuickStart too often can destabilize SOC; keep a cooldown.
    return lastQuickStartMs == 0 || (uint32_t)(now - lastQuickStartMs) >= 60000;
}

static void doQuickStartAndResample(BatterySample& s) {
    lastQuickStartMs = millis();
    maxlipo.quickStart();
    delay(250);
    s = sampleBatteryClamped();
}

// If the gauge was reset or hasn't converged yet, SOC can look stuck at 100%
// while the voltage clearly isn't full. In that case, a one-time quickStart()
// kick can help it converge faster without forcing quickStart on every boot.
static bool sampleLooksLikeStaleFull(const BatterySample& s) {
    if (s.v < 3.0f || s.v > 4.35f) return false; // ignore obviously bad/noisy voltage reads
    // 4.11V @ 100% after a hot-plug is a common "stale" pattern.
    return (s.pct >= 99.0f && s.v <= 4.15f);
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
    BatterySample s = sampleBatteryClamped();
    lastVoltage = s.v;
    lastChargeRate = s.rate;

    if (sampleLooksLikeStaleFull(s) && canQuickStartNow()) {
        Serial.printf("Battery(init): %.1f%% @ %.3fV looks stale; quickStart()\n", s.pct, s.v);
        doQuickStartAndResample(s);
    }

    // If we can confidently tell there's no battery, don't show a bogus 100%.
    lastPercent = voltageDefinitelyNoBattery(s.v) ? 0.0f : s.pct;
    lastVoltage = s.v;
    lastChargeRate = s.rate;
    Serial.printf("Battery(init): %.1f%% @ %.3fV\n", lastPercent, lastVoltage);

#ifdef MQTT_ROOT_TOPIC
    mqttPublishBattery();
#endif

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
            // USB power-path changes can shift BAT node behavior; re-seed SOC once.
            pendingQuickStart = true;
            // Update display immediately (charging indicator depends on 5V sense).
            display.putRequest(NEWBATTERY);
#ifdef MQTT_ROOT_TOPIC
            mqttPublishBattery();
#endif
        }
    }
#endif

    if (!batteryReady) return;

    // First sample should happen immediately, then do a few faster "settling"
    // samples after boot, then every 30 seconds.
    const uint32_t intervalMs = (fastSamplesRemaining > 0) ? 2000 : 30000;
    if (lastSample != 0 && (millis() - lastSample < intervalMs)) return;
    lastSample = millis();

    BatterySample s = sampleBatteryClamped();

    // If voltage steps a lot between samples (e.g., battery hot-plug / power-path),
    // give the estimator a kick.
    if (isfinite(lastVoltage) && fabsf(s.v - lastVoltage) >= 0.20f) {
        pendingQuickStart = true;
    }

    if ((pendingQuickStart || sampleLooksLikeStaleFull(s)) && canQuickStartNow()) {
        Serial.printf("Battery: quickStart() (pct=%.1f%% v=%.3fV usb=%d)\n", s.pct, s.v, (int)usbPresentNow());
        pendingQuickStart = false;
        doQuickStartAndResample(s);
    }

    lastPercent = voltageDefinitelyNoBattery(s.v) ? 0.0f : s.pct;
    lastVoltage = s.v;
    lastChargeRate = s.rate;
    Serial.printf("Battery: %.1f%% @ %.3fV\n", lastPercent, lastVoltage);

    display.putRequest(NEWBATTERY);
#ifdef MQTT_ROOT_TOPIC
    mqttPublishBattery();
#endif

    if (fastSamplesRemaining > 0) fastSamplesRemaining--;
}

float battery_get_percent() {
    return lastPercent;
}

bool battery_is_ready() {
    return batteryReady;
}

bool battery_is_present() {
    // Best-effort only: on some power paths the BAT node can float high on USB,
    // so "present" can't be detected reliably in all cases.
    return batteryReady && !voltageDefinitelyNoBattery(lastVoltage);
}

float battery_get_voltage() {
    return lastVoltage;
}

float battery_get_charge_rate() {
    return lastChargeRate;
}

bool battery_usb_present() {
#ifdef CHARGE_SENSE_PIN
    return usbSenseReady && lastUsbPresent;
#else
    return false;
#endif
}

void battery_prepare_for_deepsleep() {
    if (!batteryReady) return;
    // MAX17048 "sleep" stops updating readings and drops current draw.
    // This is only safe to use immediately before ESP deep sleep.
    maxlipo.enableSleep(true);
    maxlipo.sleep(true);
    delay(5);
}

#endif
