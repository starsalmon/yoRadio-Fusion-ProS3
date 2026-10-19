// Battery via an ADC divider tap (e.g. UM FeatherS3: VBAT sense on GPIO2, 442k / 160k), used when the
// board header sets BATTERY_ADC_PIN. The MAX17048 fuel-gauge path (ProS3) is in battery.cpp.
//   BATTERY_ADC_SCALE    cell volts = tap volts * scale (FeatherS3: (442+160)/160 = 3.7625)
//   BATTERY_VOLT_EMPTY / BATTERY_VOLT_FULL   linear 0..100 % mapping
//   CHARGE_SENSE_PIN     optional 5 V / VBUS presence input (FeatherS3: GPIO34, HIGH = USB power)
#include "battery.h"
#include "../core/options.h"
#include "../../myoptions.h"

#if defined(BATTERY_ENABLED) && (BATTERY_ENABLED != 0) && defined(BATTERY_ADC_PIN) && (BATTERY_ADC_PIN != 255)

#include <math.h>
#include "../core/display.h"
#ifdef MQTT_ROOT_TOPIC
#include "../core/mqtt.h"
#endif

extern Display display;

#ifndef BATTERY_ADC_SCALE
#error "BATTERY_ADC_PIN needs BATTERY_ADC_SCALE (divider ratio)"
#endif
#ifndef BATTERY_VOLT_EMPTY
#define BATTERY_VOLT_EMPTY 3.30f
#endif
#ifndef BATTERY_VOLT_FULL
#define BATTERY_VOLT_FULL 4.20f
#endif
#ifndef BATTERY_VOLT_MIN_VALID
#define BATTERY_VOLT_MIN_VALID 2.80f   // below this the reading is treated as "no battery"
#endif
#ifndef BATTERY_ALERT_PCT
#define BATTERY_ALERT_PCT 5
#endif

static float s_lastPercent = 0.0f;
static float s_lastVoltage = 0.0f;
static bool s_ready = false;
static bool s_primed = false;
static uint32_t s_lastSampleMs = 0;
static bool s_lastUsb = false;
static uint32_t s_lastUsbCheckMs = 0;

static float voltageToPercent(float v) {
  if (v >= BATTERY_VOLT_FULL) return 100.0f;
  if (v <= BATTERY_VOLT_EMPTY) return 0.0f;
  return ((v - BATTERY_VOLT_EMPTY) / (BATTERY_VOLT_FULL - BATTERY_VOLT_EMPTY)) * 100.0f;
}

// Median of 8 calibrated readings (analogReadMilliVolts uses the eFuse ADC calibration).
static uint32_t readTapMv() {
  uint32_t s[8];
  for (int i = 0; i < 8; i++) s[i] = analogReadMilliVolts(BATTERY_ADC_PIN);
  for (int i = 0; i < 7; i++)
    for (int j = i + 1; j < 8; j++)
      if (s[j] < s[i]) { const uint32_t t = s[i]; s[i] = s[j]; s[j] = t; }
  return (s[3] + s[4]) / 2;
}

static bool readCellVoltage(float &outV) {
  const float v = (readTapMv() / 1000.0f) * (float)BATTERY_ADC_SCALE;
  if (v < BATTERY_VOLT_MIN_VALID || v > 4.45f) return false;
  if (!s_primed) {
    outV = v;
    s_primed = true;
  } else if (fabsf(v - s_lastVoltage) > 0.35f) {
    return false;                       // reject single outliers (e.g. amp current spikes)
  } else {
    outV = 0.5f * s_lastVoltage + 0.5f * v;
  }
  return true;
}

static bool usbNow() {
#if defined(CHARGE_SENSE_PIN) && (CHARGE_SENSE_PIN != 255)
  return digitalRead(CHARGE_SENSE_PIN) == HIGH;
#else
  return false;
#endif
}

void battery_init() {
#if defined(CHARGE_SENSE_PIN) && (CHARGE_SENSE_PIN != 255)
  pinMode(CHARGE_SENSE_PIN, INPUT);
  s_lastUsb = usbNow();
#endif
  analogReadResolution(12);
  // 11 dB: ~0..3.1 V range. 4.2 V / 3.7625 = 1.12 V at the tap, above the 2.5 dB range's ~1.05 V.
  analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);
  for (int i = 0; i < 8; i++) { (void)analogReadMilliVolts(BATTERY_ADC_PIN); delay(2); }

  float v = 0.0f;
  if (readCellVoltage(v)) {
    s_lastVoltage = v;
    s_lastPercent = voltageToPercent(v);
    s_ready = true;
    Serial.printf("Battery ADC GPIO%d: %.1f%% @ %.2f V (scale %.4f, usb=%d)\n", BATTERY_ADC_PIN,
                  s_lastPercent, s_lastVoltage, (double)BATTERY_ADC_SCALE, s_lastUsb ? 1 : 0);
  } else {
    Serial.printf("Battery ADC GPIO%d: no valid reading (no battery?)\n", BATTERY_ADC_PIN);
  }
#ifdef MQTT_ROOT_TOPIC
  mqttPublishBattery();
#endif
  s_lastSampleMs = millis();
}

void battery_update() {
  const uint32_t now = millis();
#if defined(CHARGE_SENSE_PIN) && (CHARGE_SENSE_PIN != 255)
  if ((uint32_t)(now - s_lastUsbCheckMs) >= 1000u) {
    s_lastUsbCheckMs = now;
    const bool usb = usbNow();
    if (usb != s_lastUsb) {
      s_lastUsb = usb;
      display.putRequest(NEWBATTERY);
#ifdef MQTT_ROOT_TOPIC
      mqttPublishBattery();
#endif
    }
  }
#endif
  const uint32_t intervalMs = s_ready ? 30000u : 5000u;
  if ((uint32_t)(now - s_lastSampleMs) < intervalMs) return;
  s_lastSampleMs = now;

  float v = 0.0f;
  if (!readCellVoltage(v)) return;
  s_ready = true;
  s_lastVoltage = v;
  s_lastPercent = voltageToPercent(v);
  display.putRequest(NEWBATTERY);
#ifdef MQTT_ROOT_TOPIC
  mqttPublishBattery();
#endif
}

float battery_get_percent() { return s_lastPercent; }
bool battery_is_ready() { return s_ready; }
bool battery_is_present() { return s_ready && s_lastVoltage >= BATTERY_VOLT_MIN_VALID; }
float battery_get_voltage() { return s_lastVoltage; }
float battery_get_charge_rate() { return 0.0f; }   // no gauge: unknown
bool battery_usb_present() { return s_lastUsb; }
// No ALRT pin: "alert" = at/below the low threshold on a valid reading.
bool battery_alert_active() { return s_ready && s_lastPercent <= (float)BATTERY_ALERT_PCT; }
void battery_prepare_for_deepsleep() {}

bool battery_read_vcell_mv(uint16_t* mv) {
  if (!mv) return false;
  const float v = (readTapMv() / 1000.0f) * (float)BATTERY_ADC_SCALE;
  if (v < BATTERY_VOLT_MIN_VALID || v > 4.45f) return false;
  *mv = (uint16_t)lroundf(v * 1000.0f);
  return true;
}

#endif
