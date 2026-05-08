#include "builtin_led.hpp"

#include <Arduino.h>
#include "options.h"

#if defined(BUILTIN_NEOPIXEL_PIN) && __has_include(<Adafruit_NeoPixel.h>)
#define BUILTIN_NEOPIXEL_ENABLED 1
#include <Adafruit_NeoPixel.h>
#else
#define BUILTIN_NEOPIXEL_ENABLED 0
#endif

static bool s_on = false;

static uint8_t brightnessDuty() {
  uint8_t pct = 100;
#ifdef BUILTIN_LED_BRIGHTNESS_PCT
  pct = (uint8_t)BUILTIN_LED_BRIGHTNESS_PCT;
#endif
  if (pct > 100) pct = 100;
  uint16_t duty = (uint16_t)((pct * 255U + 50U) / 100U);
  if (duty == 0 && pct > 0) duty = 1;
  if (duty > 255) duty = 255;
  return (uint8_t)duty;
}

static void writeLed(bool on) {
#if BUILTIN_NEOPIXEL_ENABLED
  static Adafruit_NeoPixel s_px(1, (int)BUILTIN_NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
  static bool s_inited = false;
  if (!s_inited) {
    s_px.begin();
    s_px.clear();
    s_px.show();
    s_inited = true;
  }
#ifdef BUILTIN_NEOPIXEL_DISABLE
  (void)on;
  s_px.clear();
  s_px.show();
  s_on = false;
  return;
#endif
  s_on = on;
  s_px.setBrightness(brightnessDuty());
  const uint32_t color = on ? s_px.Color(255, 255, 255) : 0;
  s_px.setPixelColor(0, color);
  s_px.show();
  return;
#else
#if REAL_LEDBUILTIN == 255
  (void)on;
  return;
#else
  s_on = on;
  const uint8_t br = brightnessDuty();
  // active-low LEDs need inverted PWM levels:
  // - off: 255
  // - on : 255 - br
  uint8_t duty = 0;
#if LED_INVERT
  duty = on ? (uint8_t)(255 - br) : 255;
#else
  duty = on ? br : 0;
#endif
  analogWrite(REAL_LEDBUILTIN, duty);
#endif
#endif
}

void builtin_led_init() {
#if BUILTIN_NEOPIXEL_ENABLED
  writeLed(false);
  return;
#else
#if REAL_LEDBUILTIN == 255
  return;
#else
  pinMode(REAL_LEDBUILTIN, OUTPUT);
  writeLed(false);
#endif
#endif
}

void builtin_led_set(bool on) { writeLed(on); }

void builtin_led_toggle() { writeLed(!s_on); }

