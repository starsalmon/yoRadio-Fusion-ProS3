#include "backlight.h"
#include <Arduino.h>
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
    Serial.println("backlight.cpp-->notifyActivity()");
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
    Serial.println("backlight.cpp-->wake()");
    if (!config.store.blDimEnable) return;

    // Ha még nincs baseline, most rögzítsük
    if (!brightnessCaptured) {
        normalBrightness  = config.store.brightness;
        currentBrightness = normalBrightness;
        targetBrightness  = normalBrightness;
        brightnessCaptured = true;
    }

    currentBrightness = normalBrightness;
    targetBrightness  = normalBrightness;

    // USER értékre áll vissza (ez oké)
    config.store.brightness = normalBrightness;
    config.setBrightness(false);

#ifdef MQTT_ROOT_TOPIC
    mqttPublishStatus(); // brightness topic/state update for HA
#endif

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

    switch (state) {

        case WAIT:
            if (now - lastActivity > (uint32_t)config.store.blDimInterval * 1000) {
  Serial.printf(
    "[BL] WAIT->FADING now=%lu lastAct=%lu diff=%lu interval=%u\n",
    (unsigned long)now,
    (unsigned long)lastActivity,
    (unsigned long)(now - lastActivity),
    config.store.blDimInterval
  );
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

                config.store.brightness = currentBrightness;
                config.setBrightness(false);

#ifdef MQTT_ROOT_TOPIC
                mqttPublishStatus(); // brightness topic/state update for HA
#endif

            } else {
                Serial.println("[BL] FADING->DIMMED");
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
  config.store.brightness = normalBrightness;
  config.setBrightness(false);

  lastActivity = millis();
  lastFadeStep = millis();
  state = WAIT;
}

void BacklightPlugin::on_setup() {
    Serial.printf("BL.on_setup this=%p\n", this);
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
