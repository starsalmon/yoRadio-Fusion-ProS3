#include "backlight.h"
#include <Arduino.h>
#include <Wire.h>
#include "../../core/config.h"
#include "../../core/display.h"
#include "../../core/network.h"
#include "../../core/options.h"
#ifdef MQTT_ROOT_TOPIC
#include "../../core/mqtt.h"
#endif

#if (BRIGHTNESS_PIN != 255)

#define DEFAULT_DIM_LEVEL     2
#define DEFAULT_DIM_INTERVAL  60

#ifndef FADE_STEP
#define FADE_STEP 1
#endif

#ifndef FADE_PERIOD
    #define FADE_PERIOD 1000
#endif

BacklightPlugin backlightPlugin; // Global Instance

BacklightPlugin::BacklightPlugin() {}

// ---- Optional BH1750 ambient light sensor (no extra deps) ----
// Enabled via build flags/macros (typically in myoptions.h).
#if defined(BH1750_ENABLE) && (BH1750_ENABLE != 0)
namespace {
// Default I2C address: 0x23 (ADDR pin low), alternate: 0x5C (ADDR pin high)
#ifndef BH1750_I2C_ADDR
  #define BH1750_I2C_ADDR 0x23
#endif
#ifndef BH1750_UPDATE_MS
  #define BH1750_UPDATE_MS 1000u
#endif
#ifndef BH1750_LUX_MIN
  #define BH1750_LUX_MIN 1.0f
#endif
#ifndef BH1750_LUX_MAX
  #define BH1750_LUX_MAX 800.0f
#endif
#ifndef BH1750_BRIGHTNESS_MIN_PCT
  #define BH1750_BRIGHTNESS_MIN_PCT 5
#endif
#ifndef BH1750_BRIGHTNESS_MAX_PCT
  #define BH1750_BRIGHTNESS_MAX_PCT 100
#endif
#ifndef BH1750_SMOOTH_ALPHA_X100
  // 0..100 (higher = faster response). 25 = ~4s to settle at 1Hz updates.
  #define BH1750_SMOOTH_ALPHA_X100 25
#endif
#ifndef BH1750_GAMMA
  // <1.0 biases brighter at low lux; >1.0 biases darker at low lux.
  #define BH1750_GAMMA 0.60f
#endif
#ifndef BH1750_RESPECT_USER_MAX
  // If 1: user's brightness slider caps auto brightness.
  #define BH1750_RESPECT_USER_MAX 1
#endif
#ifndef BH1750_DIAG_LOG
  #define BH1750_DIAG_LOG 0
#endif

static uint32_t s_lastBhReadMs = 0;

static bool bhWrite1(uint8_t cmd) {
  Wire.beginTransmission((uint8_t)BH1750_I2C_ADDR);
  Wire.write(cmd);
  return Wire.endTransmission() == 0;
}

static bool bhReadLux_x10(uint16_t* outLux_x10) {
  if (!outLux_x10) return false;
  const uint8_t want = 2;
  const uint8_t got = Wire.requestFrom((uint8_t)BH1750_I2C_ADDR, want);
  if (got != want) return false;
  const uint16_t raw = ((uint16_t)Wire.read() << 8) | (uint16_t)Wire.read();
  // BH1750: raw / 1.2 = lux. Keep lux*10 to avoid floats in the hot path.
  const float lux = (float)raw / 1.2f;
  uint32_t lx10 = (uint32_t)lroundf(max(0.0f, lux) * 10.0f);
  if (lx10 > 65535u) lx10 = 65535u;
  *outLux_x10 = (uint16_t)lx10;
  return true;
}

static uint8_t bhLuxToBrightnessPct(uint16_t lux_x10, uint8_t userCapPct) {
  float lux = (float)lux_x10 / 10.0f;
  if (lux < BH1750_LUX_MIN) lux = BH1750_LUX_MIN;
  if (lux > BH1750_LUX_MAX) lux = BH1750_LUX_MAX;

  float n = 0.0f;
  if (BH1750_LUX_MAX > BH1750_LUX_MIN) {
    n = (lux - BH1750_LUX_MIN) / (BH1750_LUX_MAX - BH1750_LUX_MIN);
  }
  n = powf(n, (float)BH1750_GAMMA);
  float pct = (float)BH1750_BRIGHTNESS_MIN_PCT +
              n * (float)(BH1750_BRIGHTNESS_MAX_PCT - BH1750_BRIGHTNESS_MIN_PCT);

  int ip = (int)lroundf(pct);
  if (ip < 0) ip = 0;
  if (ip > 100) ip = 100;
#if BH1750_RESPECT_USER_MAX
  if (ip > (int)userCapPct) ip = (int)userCapPct;
#endif
  return (uint8_t)ip;
}
} // namespace
#endif // BH1750_ENABLE

void backlightPluginInit() {
    pm.add(&backlightPlugin);
   if (!config.store.blDimLevel)    config.store.blDimLevel = DEFAULT_DIM_LEVEL;
   if (!config.store.blDimInterval) config.store.blDimInterval = DEFAULT_DIM_INTERVAL;
}

bool BacklightPlugin::isFading() const {
    return state == FADING;
}

bool BacklightPlugin::isDimmed() const {
    return state == DIMMED;
}

void BacklightPlugin::notifyActivity() {
    activity();
}

void BacklightPlugin::activity() {
    lastActivity = millis();
}

void BacklightPlugin::setUserBrightness(uint8_t pct, bool save) {
    if (pct > 100) pct = 100;

    // Update baseline so future wake() doesn't revert to an old value.
    normalBrightness = pct;
    currentBrightness = pct;
    targetBrightness = pct;
    brightnessCaptured = true;

    config.store.brightness = pct;
    config.setBrightness(save);

    lastUiWakeMs = millis();
    lastActivity = millis();
    lastFadeStep = millis();
    state = WAIT;

#ifdef MQTT_ROOT_TOPIC
    mqttPublishStatus(); // also updates retained brightness topic
#endif
}

bool BacklightPlugin::justWoke() const {
    return (millis() - lastUiWakeMs) < 500; // After waking up, it ignores touches for this amount of time.
}


void BacklightPlugin::wake() {
    if (!config.store.blDimEnable) return;

    // Ha még nincs baseline, most rögzítsük
    if (!brightnessCaptured) {
        // "User brightness" (manual slider) is our awake baseline / cap.
        normalBrightness  = config.store.brightness;
        currentBrightness = normalBrightness;
        targetBrightness  = normalBrightness;
        brightnessCaptured = true;
    }

    currentBrightness = normalBrightness;
    targetBrightness  = normalBrightness;

    // Hardware only (do not overwrite the user's brightness setting).
    config.setBrightnessRaw(currentBrightness);

    lastUiWakeMs = millis();
    lastActivity = millis();
    lastFadeStep = millis();
    state = WAIT;
}

void BacklightPlugin::tick() {

    if (!config.store.blDimEnable) return;

    // baseline brightness rögzítés
    if (!brightnessCaptured) {
        normalBrightness = config.store.brightness;
        currentBrightness = normalBrightness;
        brightnessCaptured = true;
        lastActivity = millis();
    }

    if (display.mode() == SCREENSAVER || display.mode() == SCREENBLANK) return;
    if (network.status == SOFT_AP) return;
    if (!display.ready()) return;

    displayMode_e m = display.mode();
    if (m != lastMode) {
        lastMode = m;
        if (state == DIMMED || state == FADING) wake();
        else activity();
    }

    uint32_t now = millis();

#if defined(BH1750_ENABLE) && (BH1750_ENABLE != 0)
    // Ambient auto-brightness: update once per second (or configured interval),
    // adjust the "normal" (awake) brightness smoothly.
    if (bh1750Ready && (s_lastBhReadMs == 0 || (uint32_t)(now - s_lastBhReadMs) >= (uint32_t)BH1750_UPDATE_MS)) {
        s_lastBhReadMs = now;
        uint16_t lux_x10 = 0;
        if (bhReadLux_x10(&lux_x10)) {
            // Exponential smoothing on lux (integer domain).
            if (bh1750Lux_x10 == 0) bh1750Lux_x10 = lux_x10;
            const uint32_t a = (uint32_t)BH1750_SMOOTH_ALPHA_X100;
            bh1750Lux_x10 = (uint16_t)((((uint32_t)bh1750Lux_x10 * (100u - a)) + ((uint32_t)lux_x10 * a)) / 100u);

            // Use the user's current brightness as a cap (if enabled).
            const uint8_t userCap = normalBrightness ? normalBrightness : config.store.brightness;
            bhTargetBrightness = bhLuxToBrightnessPct(bh1750Lux_x10, userCap);

            // If we're not dimmed, gently move current brightness toward ambient target.
            if (state == WAIT) {
                const int cur = (int)currentBrightness;
                const int tgt = (int)normalBrightness;
                // Aim for the ambient target but never exceed the user baseline/cap.
                const int want = min(tgt, (int)bhTargetBrightness);
                if (cur != want) {
                    const int step = 2;
                    int next = cur;
                    if (cur < want) next = min(want, cur + step);
                    else            next = max(want, cur - step);
                    currentBrightness = (uint8_t)next;
                    config.setBrightnessRaw(currentBrightness);
                }
            }

#if BH1750_DIAG_LOG
            {
                static uint32_t s_lastLogMs = 0;
                static uint16_t s_lastLux_x10 = 0;
                static uint8_t  s_lastTgt = 255;
                static uint8_t  s_lastCur = 255;
                const bool changed =
                    (abs((int)bh1750Lux_x10 - (int)s_lastLux_x10) >= 50) || // 5 lux
                    (bhTargetBrightness != s_lastTgt) ||
                    (currentBrightness != s_lastCur);
                if (changed && (s_lastLogMs == 0 || (uint32_t)(now - s_lastLogMs) >= 2000u)) {
                    s_lastLogMs = now;
                    s_lastLux_x10 = bh1750Lux_x10;
                    s_lastTgt = bhTargetBrightness;
                    s_lastCur = currentBrightness;
                    Serial.printf("[BH1750] lux=%.1f cap=%u tgt=%u cur=%u dim=%d\n",
                                  (double)((float)bh1750Lux_x10 / 10.0f),
                                  (unsigned)userCap,
                                  (unsigned)bhTargetBrightness,
                                  (unsigned)currentBrightness,
                                  (int)(state != WAIT));
                }
            }
#endif
        }
    }
#endif

    switch (state) {

        case WAIT:
            if (now - lastActivity > (uint32_t)config.store.blDimInterval * 1000) {
                targetBrightness = config.store.blDimLevel;
                state = FADING;
                lastFadeStep = now;
            }
            break;

        case FADING:
            if (now - lastFadeStep < FADE_PERIOD) break;
            lastFadeStep = now;

            if (currentBrightness > targetBrightness) {

                if (currentBrightness <= targetBrightness + FADE_STEP) {
                    currentBrightness = targetBrightness;
                } else {
                    currentBrightness -= FADE_STEP;
                }

                // Hardware only (do not overwrite the user's brightness setting).
                config.setBrightnessRaw(currentBrightness);

            } else {
                state = DIMMED;
            }
            break;

        case DIMMED:
            break;
    }
}

void BacklightPlugin::restoreNow() {
  if (!brightnessCaptured) {
    normalBrightness  = config.store.brightness;
    currentBrightness = normalBrightness;
    targetBrightness  = normalBrightness;
    brightnessCaptured = true;
  }

  currentBrightness = normalBrightness;
  targetBrightness  = normalBrightness;
  config.setBrightnessRaw(normalBrightness);

  lastActivity = millis();
  lastFadeStep = millis();
  state = WAIT;
}

void BacklightPlugin::on_setup() {
#if defined(BH1750_ENABLE) && (BH1750_ENABLE != 0)
    // Share the same I2C pins as the MAX17048 battery gauge (ProS3: GPIO8/9).
    // Safe even if battery init already called Wire.begin() with the same pins.
    #if defined(BATTERY_SDA) && defined(BATTERY_SCL)
      Wire.begin(BATTERY_SDA, BATTERY_SCL);
    #else
      Wire.begin();
    #endif
    // Power on + reset
    const bool ok1 = bhWrite1(0x01);
    const bool ok2 = bhWrite1(0x07);
    // Continuous H-Resolution mode (1 lx, 120ms typical)
    const bool ok3 = bhWrite1(0x10);
    bh1750Ready = ok1 && ok2 && ok3;
#endif
}

void BacklightPlugin::on_ticker() {
    tick();
}

void BacklightPlugin::on_start_play() {
    wake();
}

void BacklightPlugin::on_stop_play() {
    wake();
}

void BacklightPlugin::on_track_change() {
    // wake();
}

void BacklightPlugin::on_btn_click(controlEvt_e& btnid) {
    wake();
}

void BacklightPlugin::on_display_player() {
    wake();
}

#endif
