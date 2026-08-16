#include "neostatus.h"

#include "../../core/options.h"

// Only build when we have an onboard NeoPixel pin.
#if defined(BUILTIN_NEOPIXEL_PIN) && (BUILTIN_NEOPIXEL_PIN != 255)

#include <Adafruit_NeoPixel.h>

#include "../../core/network.h"
#include "../../core/config.h"
#include "../../core/bt_companion.h"
#include "../../core/player.h"
#include "../../core/podcasts.h"
#include "../../battery/battery.h"
#include "../backlight/backlight.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <Arduino.h>
#include "driver/gpio.h"
#include "driver/rtc_io.h"

namespace {

#ifndef NEOSTATUS_ENABLE
  #define NEOSTATUS_ENABLE 1
#endif
#ifndef NEO_STATUS_ENABLE
  #define NEO_STATUS_ENABLE NEOSTATUS_ENABLE
#endif

// Optional serial diagnostics.
#ifndef NEO_DIAG_LOG
  #ifdef NEOSTATUS_DIAG_LOG
    #define NEO_DIAG_LOG NEOSTATUS_DIAG_LOG
  #else
    #define NEO_DIAG_LOG 0
  #endif
#endif

// Colorful/rainbow mode (uses multiple colors/patterns on the ring).
#ifndef NEO_COLORFUL_ENABLE
  #ifdef NEOSTATUS_COLORFUL_ENABLE
    #define NEO_COLORFUL_ENABLE NEOSTATUS_COLORFUL_ENABLE
  #else
    #define NEO_COLORFUL_ENABLE 0
  #endif
#endif

// Full rainbow mode (spinning hue shift). When enabled, this overrides the
// simple 2-color "dual comet" look.
#ifndef NEO_RAINBOW_ENABLE
  #ifdef NEOSTATUS_RAINBOW_ENABLE
    #define NEO_RAINBOW_ENABLE NEOSTATUS_RAINBOW_ENABLE
  #else
    #define NEO_RAINBOW_ENABLE 0
  #endif
#endif

// Podcast indexing animation (while RSS playlist is being built).
#ifndef NEO_POD_INDEX_ANIM_ENABLE
  #ifdef NEOSTATUS_POD_INDEX_ANIM_ENABLE
    #define NEO_POD_INDEX_ANIM_ENABLE NEOSTATUS_POD_INDEX_ANIM_ENABLE
  #else
    #define NEO_POD_INDEX_ANIM_ENABLE 1
  #endif
#endif
#ifndef NEO_POD_INDEX_SPIN_PERIOD_MS
  #ifdef NEOSTATUS_POD_INDEX_SPIN_PERIOD_MS
    #define NEO_POD_INDEX_SPIN_PERIOD_MS NEOSTATUS_POD_INDEX_SPIN_PERIOD_MS
  #else
    #define NEO_POD_INDEX_SPIN_PERIOD_MS 380u
  #endif
#endif
#ifndef NEO_POD_INDEX_MIN_PCT
  #ifdef NEOSTATUS_POD_INDEX_MIN_PCT
    #define NEO_POD_INDEX_MIN_PCT NEOSTATUS_POD_INDEX_MIN_PCT
  #else
    #define NEO_POD_INDEX_MIN_PCT 0u
  #endif
#endif
#ifndef NEO_POD_INDEX_MAX_PCT
  #ifdef NEOSTATUS_POD_INDEX_MAX_PCT
    #define NEO_POD_INDEX_MAX_PCT NEOSTATUS_POD_INDEX_MAX_PCT
  #else
    #define NEO_POD_INDEX_MAX_PCT 85u
  #endif
#endif

// Which core to run the animation task on (dual-core ESP32 only).
// Default: core 1 to avoid fighting Wi-Fi (usually core 0).
#ifndef NEO_TASK_CORE
  #ifdef NEOSTATUS_TASK_CORE
    #define NEO_TASK_CORE NEOSTATUS_TASK_CORE
  #else
    #if defined(portNUM_PROCESSORS) && (portNUM_PROCESSORS > 1)
      #define NEO_TASK_CORE 1
    #else
      #define NEO_TASK_CORE 0
    #endif
  #endif
#endif

#ifndef NEO_TASK_STACK
  #ifdef NEOSTATUS_TASK_STACK
    #define NEO_TASK_STACK NEOSTATUS_TASK_STACK
  #else
    // Cosmetic task, but a bit more stack helps avoid rare canary panics during heavy boot.
    #define NEO_TASK_STACK 3072
  #endif
#endif

// User config surface (set in `myoptions.h`) uses NEOSTATUS_* knobs.
#if !defined(NEO_BOOT_DELAY_MS) && defined(NEOSTATUS_BOOT_DELAY_MS)
  #define NEO_BOOT_DELAY_MS NEOSTATUS_BOOT_DELAY_MS
#endif
#if !defined(NEO_NET_DELAY_MS) && defined(NEOSTATUS_NET_DELAY_MS)
  #define NEO_NET_DELAY_MS NEOSTATUS_NET_DELAY_MS
#endif

// Boot progress-wheel animation (~5s).
#ifndef NEO_BOOT_ANIM_ENABLE
  #ifdef NEOSTATUS_BOOT_ANIM_ENABLE
    #define NEO_BOOT_ANIM_ENABLE NEOSTATUS_BOOT_ANIM_ENABLE
  #else
    #define NEO_BOOT_ANIM_ENABLE 1
  #endif
#endif
#ifndef NEO_BOOT_ANIM_MS
  #ifdef NEOSTATUS_BOOT_ANIM_MS
    #define NEO_BOOT_ANIM_MS NEOSTATUS_BOOT_ANIM_MS
  #else
    // Match the previously-tuned boot progress feel.
    #define NEO_BOOT_ANIM_MS 5600u
  #endif
#endif
#ifndef NEO_BOOT_ANIM_MIN_PCT
  #ifdef NEOSTATUS_BOOT_ANIM_MIN_PCT
    #define NEO_BOOT_ANIM_MIN_PCT NEOSTATUS_BOOT_ANIM_MIN_PCT
  #else
    #define NEO_BOOT_ANIM_MIN_PCT 3u
  #endif
#endif
#ifndef NEO_BOOT_ANIM_MAX_PCT
  #ifdef NEOSTATUS_BOOT_ANIM_MAX_PCT
    #define NEO_BOOT_ANIM_MAX_PCT NEOSTATUS_BOOT_ANIM_MAX_PCT
  #else
    #define NEO_BOOT_ANIM_MAX_PCT 85u
  #endif
#endif
#if !defined(NEO_BOOT_RGB) && defined(NEOSTATUS_BOOT_RGB)
  #define NEO_BOOT_RGB NEOSTATUS_BOOT_RGB
#endif
#if !defined(NEO_NET_RGB) && defined(NEOSTATUS_NET_RGB)
  #define NEO_NET_RGB NEOSTATUS_NET_RGB
#endif
#if !defined(NEO_SD_RGB) && defined(NEOSTATUS_SD_RGB)
  #define NEO_SD_RGB NEOSTATUS_SD_RGB
#endif
#if !defined(NEO_BT_SEARCH_RGB) && defined(NEOSTATUS_BT_SEARCH_RGB)
  #define NEO_BT_SEARCH_RGB NEOSTATUS_BT_SEARCH_RGB
#endif
#if !defined(NEO_BT_CONN_RGB) && defined(NEOSTATUS_BT_CONN_RGB)
  #define NEO_BT_CONN_RGB NEOSTATUS_BT_CONN_RGB
#endif
#if !defined(NEO_RADIO_START_RGB) && defined(NEOSTATUS_RADIO_START_RGB)
  #define NEO_RADIO_START_RGB NEOSTATUS_RADIO_START_RGB
#endif
#if !defined(NEO_PODCAST_START_RGB) && defined(NEOSTATUS_PODCAST_START_RGB)
  #define NEO_PODCAST_START_RGB NEOSTATUS_PODCAST_START_RGB
#endif
#if !defined(NEO_SPK_SELECT_RGB) && defined(NEOSTATUS_SPK_SELECT_RGB)
  #define NEO_SPK_SELECT_RGB NEOSTATUS_SPK_SELECT_RGB
#endif
#if !defined(NEO_WIFI_LOST_RGB) && defined(NEOSTATUS_WIFI_LOST_RGB)
  #define NEO_WIFI_LOST_RGB NEOSTATUS_WIFI_LOST_RGB
#endif
#if !defined(NEO_SLEEP_RGB) && defined(NEOSTATUS_SLEEP_RGB)
  #define NEO_SLEEP_RGB NEOSTATUS_SLEEP_RGB
#endif
#if !defined(NEO_LOW_BATT_RGB) && defined(NEOSTATUS_LOW_BATT_RGB)
  #define NEO_LOW_BATT_RGB NEOSTATUS_LOW_BATT_RGB
#endif

#ifndef NEO_STATUS_BRIGHTNESS
  #ifdef NEOSTATUS_BRIGHTNESS
    #define NEO_STATUS_BRIGHTNESS NEOSTATUS_BRIGHTNESS
  #elif defined(BUILTIN_NEOPIXEL_BOOT_BRIGHTNESS)
    // Fall back to the brightness used during the boot clear.
    #define NEO_STATUS_BRIGHTNESS BUILTIN_NEOPIXEL_BOOT_BRIGHTNESS
  #else
    #define NEO_STATUS_BRIGHTNESS 100
  #endif
#endif

// Optional: scale NeoStatus brightness by the current display brightness percent.
// This makes the status LEDs follow ambient auto-brightness (BH1750 or LIGHT_SENSOR),
// since those update `config.store.brightness`.
#ifndef NEO_STATUS_FOLLOW_SCREEN_BRIGHTNESS
  #ifdef NEOSTATUS_FOLLOW_SCREEN_BRIGHTNESS
    #define NEO_STATUS_FOLLOW_SCREEN_BRIGHTNESS NEOSTATUS_FOLLOW_SCREEN_BRIGHTNESS
  #else
    #define NEO_STATUS_FOLLOW_SCREEN_BRIGHTNESS 0
  #endif
#endif
#ifndef NEO_STATUS_FOLLOW_SCREEN_MIN_PCT
  #ifdef NEOSTATUS_FOLLOW_SCREEN_MIN_PCT
    #define NEO_STATUS_FOLLOW_SCREEN_MIN_PCT NEOSTATUS_FOLLOW_SCREEN_MIN_PCT
  #else
    #define NEO_STATUS_FOLLOW_SCREEN_MIN_PCT 8u
  #endif
#endif
#ifndef NEO_STATUS_FOLLOW_SCREEN_MAX_PCT
  #ifdef NEOSTATUS_FOLLOW_SCREEN_MAX_PCT
    #define NEO_STATUS_FOLLOW_SCREEN_MAX_PCT NEOSTATUS_FOLLOW_SCREEN_MAX_PCT
  #else
    #define NEO_STATUS_FOLLOW_SCREEN_MAX_PCT 100u
  #endif
#endif

// Allow driving a different NeoPixel device than the ProS3 onboard pixel.
// Typical use: an external NeoPixel ring behind the rotary knob.
#if !defined(NEO_STATUS_PIN) && defined(NEOSTATUS_PIN)
  #define NEO_STATUS_PIN NEOSTATUS_PIN
#endif
#if !defined(NEO_STATUS_PIN)
  #define NEO_STATUS_PIN BUILTIN_NEOPIXEL_PIN
#endif
#if !defined(NEO_STATUS_COUNT) && defined(NEOSTATUS_COUNT)
  #define NEO_STATUS_COUNT NEOSTATUS_COUNT
#endif
#if !defined(NEO_STATUS_COUNT)
  #define NEO_STATUS_COUNT 1
#endif

// Spin animation for rings (pulses become "spins").
#if !defined(NEO_SPIN_PERIOD_MS) && defined(NEOSTATUS_SPIN_PERIOD_MS)
  #define NEO_SPIN_PERIOD_MS NEOSTATUS_SPIN_PERIOD_MS
#endif
#ifndef NEO_SPIN_PERIOD_MS
  // Requested: 1 rotation in 0.5 seconds.
  #define NEO_SPIN_PERIOD_MS 500u
#endif

// Ring trail fade-out time (ms). Higher = longer tail.
#if !defined(NEO_RING_FADE_MS) && defined(NEOSTATUS_RING_FADE_MS)
  #define NEO_RING_FADE_MS NEOSTATUS_RING_FADE_MS
#endif
#ifndef NEO_RING_FADE_MS
  #define NEO_RING_FADE_MS 160u
#endif

// Volume feedback: spins on the ring while volume is adjusted.
#if !defined(NEO_VOL_RGB) && defined(NEOSTATUS_VOL_RGB)
  #define NEO_VOL_RGB NEOSTATUS_VOL_RGB
#endif
#ifndef NEO_VOL_RGB
  #define NEO_VOL_RGB 0, 255, 0
#endif
#if !defined(NEO_VOL_MAX_SPINS) && defined(NEOSTATUS_VOL_MAX_SPINS)
  #define NEO_VOL_MAX_SPINS NEOSTATUS_VOL_MAX_SPINS
#endif
#ifndef NEO_VOL_MAX_SPINS
  // Match the previously-tuned volume animation behavior.
  #define NEO_VOL_MAX_SPINS 3u
#endif
#if !defined(NEO_VOL_STEPS_PER_SPIN) && defined(NEOSTATUS_VOL_STEPS_PER_SPIN)
  #define NEO_VOL_STEPS_PER_SPIN NEOSTATUS_VOL_STEPS_PER_SPIN
#endif
#ifndef NEO_VOL_STEPS_PER_SPIN
  // 1 = one spin per volume step; 2 = one spin per 2 steps, etc.
  #define NEO_VOL_STEPS_PER_SPIN 1u
#endif
#if !defined(NEO_VOL_SPIN_PERIOD_MS) && defined(NEOSTATUS_VOL_SPIN_PERIOD_MS)
  #define NEO_VOL_SPIN_PERIOD_MS NEOSTATUS_VOL_SPIN_PERIOD_MS
#endif
#ifndef NEO_VOL_SPIN_PERIOD_MS
  // Keep volume spin cadence stable even if other spin periods change.
  #define NEO_VOL_SPIN_PERIOD_MS 500u
#endif
#if !defined(NEO_VOL_MIN_PCT) && defined(NEOSTATUS_VOL_MIN_PCT)
  #define NEO_VOL_MIN_PCT NEOSTATUS_VOL_MIN_PCT
#endif
#ifndef NEO_VOL_MIN_PCT
  #define NEO_VOL_MIN_PCT 0u
#endif
#if !defined(NEO_VOL_MAX_PCT) && defined(NEOSTATUS_VOL_MAX_PCT)
  #define NEO_VOL_MAX_PCT NEOSTATUS_VOL_MAX_PCT
#endif
#ifndef NEO_VOL_MAX_PCT
  #define NEO_VOL_MAX_PCT 100u
#endif
// Ignore volume changes during early boot/config init.
#ifndef NEO_VOL_ARM_DELAY_MS
  #ifdef NEOSTATUS_VOL_ARM_DELAY_MS
    #define NEO_VOL_ARM_DELAY_MS NEOSTATUS_VOL_ARM_DELAY_MS
  #else
    #define NEO_VOL_ARM_DELAY_MS 1500u
  #endif
#endif

// "Now playing" idle animation: very dim slow spin in media color.
#if !defined(NEO_PLAY_ENABLE) && defined(NEOSTATUS_PLAY_ENABLE)
  #define NEO_PLAY_ENABLE NEOSTATUS_PLAY_ENABLE
#endif
#ifndef NEO_PLAY_ENABLE
  #define NEO_PLAY_ENABLE 1
#endif
#if !defined(NEO_PLAY_PERIOD_MS) && defined(NEOSTATUS_PLAY_PERIOD_MS)
  #define NEO_PLAY_PERIOD_MS NEOSTATUS_PLAY_PERIOD_MS
#endif
#ifndef NEO_PLAY_PERIOD_MS
  #define NEO_PLAY_PERIOD_MS 2600u
#endif
#if !defined(NEO_PLAY_MIN_PCT) && defined(NEOSTATUS_PLAY_MIN_PCT)
  #define NEO_PLAY_MIN_PCT NEOSTATUS_PLAY_MIN_PCT
#endif
#ifndef NEO_PLAY_MIN_PCT
  // Keep a small floor; extremely low PWM can distort perceived color.
  #define NEO_PLAY_MIN_PCT 2u
#endif
#if !defined(NEO_PLAY_MAX_PCT) && defined(NEOSTATUS_PLAY_MAX_PCT)
  #define NEO_PLAY_MAX_PCT NEOSTATUS_PLAY_MAX_PCT
#endif
#ifndef NEO_PLAY_MAX_PCT
  #define NEO_PLAY_MAX_PCT 18u
#endif

// Extra-long tail for the slow "now playing" animation (ring only).
#ifndef NEO_PLAY_RING_FADE_MS
  #ifdef NEOSTATUS_PLAY_RING_FADE_MS
    #define NEO_PLAY_RING_FADE_MS NEOSTATUS_PLAY_RING_FADE_MS
  #else
    #define NEO_PLAY_RING_FADE_MS 520u
  #endif
#endif

// Idle glow (dim static color when truly idle).
#ifndef NEO_IDLE_GLOW_ENABLE
  #ifdef NEOSTATUS_IDLE_GLOW_ENABLE
    #define NEO_IDLE_GLOW_ENABLE NEOSTATUS_IDLE_GLOW_ENABLE
  #else
    #define NEO_IDLE_GLOW_ENABLE 0
  #endif
#endif
#ifndef NEO_IDLE_GLOW_RGB
  #ifdef NEOSTATUS_IDLE_GLOW_RGB
    #define NEO_IDLE_GLOW_RGB NEOSTATUS_IDLE_GLOW_RGB
  #else
    #define NEO_IDLE_GLOW_RGB 0, 0, 0
  #endif
#endif
#ifndef NEO_IDLE_GLOW_PCT
  #ifdef NEOSTATUS_IDLE_GLOW_PCT
    #define NEO_IDLE_GLOW_PCT NEOSTATUS_IDLE_GLOW_PCT
  #else
    // Make sure the idle glow is actually visible at typical caps.
    #define NEO_IDLE_GLOW_PCT 12u
  #endif
#endif

// Optional: Mode/power button LED (PWM) for idle/play indication.
#ifndef MODE_BUTTON_LED_PIN
  #define MODE_BUTTON_LED_PIN 255
#endif
#ifndef MODE_BUTTON_LED_ACTIVE_HIGH
  #define MODE_BUTTON_LED_ACTIVE_HIGH 1
#endif
#ifndef MODE_BUTTON_LED_BREATHE_PERIOD_MS
  #define MODE_BUTTON_LED_BREATHE_PERIOD_MS 2500u
#endif
#ifndef MODE_BUTTON_LED_BREATHE_MIN_PCT
  #define MODE_BUTTON_LED_BREATHE_MIN_PCT 2u
#endif
#ifndef MODE_BUTTON_LED_BREATHE_MAX_PCT
  #define MODE_BUTTON_LED_BREATHE_MAX_PCT 35u
#endif
#ifndef MODE_BUTTON_LED_IDLE_BREATHE_ENABLE
  // If 1: when NOT playing, breathe continuously.
  // If 0: when NOT playing, use MODE_BUTTON_LED_IDLE_SOLID_PCT (except optional boot-breathe window).
  #define MODE_BUTTON_LED_IDLE_BREATHE_ENABLE 1
#endif
#ifndef MODE_BUTTON_LED_SOLID_PCT
  #define MODE_BUTTON_LED_SOLID_PCT 100u
#endif
#ifndef MODE_BUTTON_LED_IDLE_SOLID_PCT
  // Solid brightness when NOT playing (after boot breathe window ends).
  // Default: same as playing brightness.
  #define MODE_BUTTON_LED_IDLE_SOLID_PCT MODE_BUTTON_LED_SOLID_PCT
#endif
#ifndef MODE_BUTTON_LED_BOOT_BREATHE_MS
  // If non-zero: breathe for this many ms after boot (regardless of idle mode),
  // then fall back to the idle behavior below.
  //
  // To get "breathe only during boot, solid when idle": set
  //   MODE_BUTTON_LED_IDLE_BREATHE_ENABLE 0
  //   MODE_BUTTON_LED_BOOT_BREATHE_MS <non-zero>
  #define MODE_BUTTON_LED_BOOT_BREATHE_MS 0u
#endif

// Quick pulse on mode button press.
#ifndef MODE_BUTTON_LED_PRESS_PULSE_ENABLE
  #define MODE_BUTTON_LED_PRESS_PULSE_ENABLE 1
#endif
#ifndef MODE_BUTTON_LED_PRESS_PULSE_COUNT
  #define MODE_BUTTON_LED_PRESS_PULSE_COUNT 2u
#endif
#ifndef MODE_BUTTON_LED_PRESS_PULSE_MS
  #define MODE_BUTTON_LED_PRESS_PULSE_MS 70u
#endif
#ifndef MODE_BUTTON_LED_PRESS_PULSE_GAP_MS
  #define MODE_BUTTON_LED_PRESS_PULSE_GAP_MS 55u
#endif
#ifndef MODE_BUTTON_LED_PRESS_PULSE_MIN_PCT
  #define MODE_BUTTON_LED_PRESS_PULSE_MIN_PCT 0u
#endif
#ifndef MODE_BUTTON_LED_PRESS_PULSE_MAX_PCT
  #define MODE_BUTTON_LED_PRESS_PULSE_MAX_PCT 100u
#endif

// Media start/stop tuning.
#ifndef NEO_MEDIA_START_SPIN_MS
  #ifdef NEOSTATUS_MEDIA_START_SPIN_MS
    #define NEO_MEDIA_START_SPIN_MS NEOSTATUS_MEDIA_START_SPIN_MS
  #else
    #define NEO_MEDIA_START_SPIN_MS 360u
  #endif
#endif
#ifndef NEO_MEDIA_START_BOOST_MS
  #ifdef NEOSTATUS_MEDIA_START_BOOST_MS
    #define NEO_MEDIA_START_BOOST_MS NEOSTATUS_MEDIA_START_BOOST_MS
  #else
    #define NEO_MEDIA_START_BOOST_MS 600u
  #endif
#endif
#ifndef NEO_MEDIA_STOP_DECEL_MS
  #ifdef NEOSTATUS_MEDIA_STOP_DECEL_MS
    #define NEO_MEDIA_STOP_DECEL_MS NEOSTATUS_MEDIA_STOP_DECEL_MS
  #else
    #define NEO_MEDIA_STOP_DECEL_MS 850u
  #endif
#endif
#ifndef NEO_MEDIA_STOP_STEPS
  #ifdef NEOSTATUS_MEDIA_STOP_STEPS
    #define NEO_MEDIA_STOP_STEPS NEOSTATUS_MEDIA_STOP_STEPS
  #else
    #define NEO_MEDIA_STOP_STEPS 4u
  #endif
#endif

#ifndef NEO_MEDIA_STOP_HOLD_MS
  #ifdef NEOSTATUS_MEDIA_STOP_HOLD_MS
    #define NEO_MEDIA_STOP_HOLD_MS NEOSTATUS_MEDIA_STOP_HOLD_MS
  #else
    // Requested: after slowing down, stop "static" briefly before fading out.
    #define NEO_MEDIA_STOP_HOLD_MS 520u
  #endif
#endif
#ifndef NEO_MEDIA_STOP_FADE_MS
  #ifdef NEOSTATUS_MEDIA_STOP_FADE_MS
    #define NEO_MEDIA_STOP_FADE_MS NEOSTATUS_MEDIA_STOP_FADE_MS
  #else
    #define NEO_MEDIA_STOP_FADE_MS 650u
  #endif
#endif

// Suppress volume spin when output device toggles (volume is restored).
#ifndef NEO_VOL_SUPPRESS_ON_OUTPUT_SWITCH_MS
  #ifdef NEOSTATUS_VOL_SUPPRESS_ON_OUTPUT_SWITCH_MS
    #define NEO_VOL_SUPPRESS_ON_OUTPUT_SWITCH_MS NEOSTATUS_VOL_SUPPRESS_ON_OUTPUT_SWITCH_MS
  #else
    #define NEO_VOL_SUPPRESS_ON_OUTPUT_SWITCH_MS 1200u
  #endif
#endif

// Colors (RGB) + timing. Override any of these in `myoptions.h`.
#ifndef NEO_BOOT_RGB
  #define NEO_BOOT_RGB  128, 0, 255
#endif
#ifndef NEO_NET_RGB
  #define NEO_NET_RGB   0, 255, 80
#endif
#ifndef NEO_SD_RGB
  #define NEO_SD_RGB    255, 180, 0
#endif
#ifndef NEO_BT_SEARCH_RGB
  // Requested: make all BT colors identical (pure blue).
  #define NEO_BT_SEARCH_RGB  0, 0, 255
#endif
#ifndef NEO_BT_CONN_RGB
  // Requested: make all BT colors identical (pure blue).
  #define NEO_BT_CONN_RGB    0, 0, 255
#endif

// Two-tone BT palette (A = "connect/search", B = "audio"). User requested
// the same two colors for all BT events; only speed changes per event.
#ifndef NEO_BT_RGB_A
  #ifdef NEOSTATUS_BT_CONN_RGB
    #define NEO_BT_RGB_A NEOSTATUS_BT_CONN_RGB
  #else
    #define NEO_BT_RGB_A NEO_BT_CONN_RGB
  #endif
#endif
#ifndef NEO_BT_RGB_B
  #ifdef NEOSTATUS_BT_AUDIO_RGB
    #define NEO_BT_RGB_B NEOSTATUS_BT_AUDIO_RGB
  #else
    // Fallback: slightly deeper blue.
    #define NEO_BT_RGB_B 0, 0, 255
  #endif
#endif
#ifndef NEO_RADIO_START_RGB
  #define NEO_RADIO_START_RGB 255, 255, 255
#endif
#ifndef NEO_PODCAST_START_RGB
  // Distinct from web radio start.
  #define NEO_PODCAST_START_RGB 255, 0, 200
#endif
#ifndef NEO_SPK_SELECT_RGB
  // Requested: speaker select is greener (reserve red for low battery).
  #define NEO_SPK_SELECT_RGB 0, 255, 160
#endif
#ifndef NEO_WIFI_LOST_RGB
  #define NEO_WIFI_LOST_RGB 255, 140, 0
#endif
#ifndef NEO_SLEEP_RGB
  #define NEO_SLEEP_RGB 128, 0, 255
#endif
#ifndef NEO_LOW_BATT_RGB
  #define NEO_LOW_BATT_RGB 255, 0, 0
#endif

#ifndef NEO_BT_SEARCH_PERIOD_MS
  // Slow blue pulse while waiting for BT connect.
  #define NEO_BT_SEARCH_PERIOD_MS 1600u
#endif
#ifndef NEO_BT_CONNECTED_PERIOD_MS
  // Fast constant pulse while connected (waiting for audio).
  #define NEO_BT_CONNECTED_PERIOD_MS 500u
#endif

// Pulse sequence (smooth pulses, not flashes).
#if !defined(NEO_SEQ_PULSE_MS) && defined(NEOSTATUS_SEQ_PULSE_MS)
  #define NEO_SEQ_PULSE_MS NEOSTATUS_SEQ_PULSE_MS
#endif
#ifndef NEO_SEQ_PULSE_MS
  // Longer = smoother "breathing" (not a blink).
  #define NEO_SEQ_PULSE_MS 360u
#endif
#if !defined(NEO_SEQ_GAP_MS) && defined(NEOSTATUS_SEQ_GAP_MS)
  #define NEO_SEQ_GAP_MS NEOSTATUS_SEQ_GAP_MS
#endif
#ifndef NEO_SEQ_GAP_MS
  #define NEO_SEQ_GAP_MS 120u
#endif
#if !defined(NEO_SEQ_MIN_PCT) && defined(NEOSTATUS_SEQ_MIN_PCT)
  #define NEO_SEQ_MIN_PCT NEOSTATUS_SEQ_MIN_PCT
#endif
#ifndef NEO_SEQ_MIN_PCT
  #define NEO_SEQ_MIN_PCT 0u
#endif
#if !defined(NEO_SEQ_MAX_PCT) && defined(NEOSTATUS_SEQ_MAX_PCT)
  #define NEO_SEQ_MAX_PCT NEOSTATUS_SEQ_MAX_PCT
#endif
#ifndef NEO_SEQ_MAX_PCT
  #define NEO_SEQ_MAX_PCT 100u
#endif

#ifndef NEO_BOOT_PULSES
  #define NEO_BOOT_PULSES 3u
#endif
#ifdef NEOSTATUS_BOOT_PULSES
  #undef NEO_BOOT_PULSES
  #define NEO_BOOT_PULSES NEOSTATUS_BOOT_PULSES
#endif
#ifndef NEO_NET_PULSES
  #define NEO_NET_PULSES 3u
#endif
#ifdef NEOSTATUS_NET_PULSES
  #undef NEO_NET_PULSES
  #define NEO_NET_PULSES NEOSTATUS_NET_PULSES
#endif
#ifndef NEO_SD_PULSES
  #define NEO_SD_PULSES 2u
#endif
#ifdef NEOSTATUS_SD_PULSES
  #undef NEO_SD_PULSES
  #define NEO_SD_PULSES NEOSTATUS_SD_PULSES
#endif
#ifndef NEO_RADIO_START_PULSES
  #define NEO_RADIO_START_PULSES 2u
#endif
#ifdef NEOSTATUS_RADIO_START_PULSES
  #undef NEO_RADIO_START_PULSES
  #define NEO_RADIO_START_PULSES NEOSTATUS_RADIO_START_PULSES
#endif
#ifndef NEO_PODCAST_START_PULSES
  #define NEO_PODCAST_START_PULSES 2u
#endif
#ifdef NEOSTATUS_PODCAST_START_PULSES
  #undef NEO_PODCAST_START_PULSES
  #define NEO_PODCAST_START_PULSES NEOSTATUS_PODCAST_START_PULSES
#endif
#ifndef NEO_BT_CONNECTED_PULSES
  #define NEO_BT_CONNECTED_PULSES 2u
#endif
#ifdef NEOSTATUS_BT_CONNECTED_PULSES
  #undef NEO_BT_CONNECTED_PULSES
  #define NEO_BT_CONNECTED_PULSES NEOSTATUS_BT_CONNECTED_PULSES
#endif
#ifndef NEO_BT_AUDIO_PULSES
  #define NEO_BT_AUDIO_PULSES 2u
#endif
#ifdef NEOSTATUS_BT_AUDIO_PULSES
  #undef NEO_BT_AUDIO_PULSES
  #define NEO_BT_AUDIO_PULSES NEOSTATUS_BT_AUDIO_PULSES
#endif
#ifndef NEO_SPK_SELECT_PULSES
  #define NEO_SPK_SELECT_PULSES 2u
#endif
#ifdef NEOSTATUS_SPK_SELECT_PULSES
  #undef NEO_SPK_SELECT_PULSES
  #define NEO_SPK_SELECT_PULSES NEOSTATUS_SPK_SELECT_PULSES
#endif
#ifndef NEO_WIFI_LOST_PULSES
  #define NEO_WIFI_LOST_PULSES 3u
#endif
#ifdef NEOSTATUS_WIFI_LOST_PULSES
  #undef NEO_WIFI_LOST_PULSES
  #define NEO_WIFI_LOST_PULSES NEOSTATUS_WIFI_LOST_PULSES
#endif
#ifndef NEO_SLEEP_PULSES
  #define NEO_SLEEP_PULSES 3u
#endif
#ifdef NEOSTATUS_SLEEP_PULSES
  #undef NEO_SLEEP_PULSES
  #define NEO_SLEEP_PULSES NEOSTATUS_SLEEP_PULSES
#endif

// Sleep cue timing (used by the pre-sleep pulse sequence).
// Requested: each pulse is longer than the last (so it feels calmer).
#ifndef NEO_SLEEP_CUE_BASE_MS
  #ifdef NEOSTATUS_SLEEP_CUE_BASE_MS
    #define NEO_SLEEP_CUE_BASE_MS NEOSTATUS_SLEEP_CUE_BASE_MS
  #else
    // Slightly slower than the default sequence pulse.
    #define NEO_SLEEP_CUE_BASE_MS ((uint32_t)NEO_SEQ_PULSE_MS + 150u)
  #endif
#endif
#ifndef NEO_SLEEP_CUE_RISE_MS
  #ifdef NEOSTATUS_SLEEP_CUE_RISE_MS
    #define NEO_SLEEP_CUE_RISE_MS NEOSTATUS_SLEEP_CUE_RISE_MS
  #else
    // Constant rise time for all pulses; only the fade-out stretches.
    #define NEO_SLEEP_CUE_RISE_MS 220u
  #endif
#endif
#ifndef NEO_SLEEP_CUE_STEP_MS
  #ifdef NEOSTATUS_SLEEP_CUE_STEP_MS
    #define NEO_SLEEP_CUE_STEP_MS NEOSTATUS_SLEEP_CUE_STEP_MS
  #else
    #define NEO_SLEEP_CUE_STEP_MS 150u
  #endif
#endif
#ifndef NEO_SLEEP_CUE_GAP_MS
  #ifdef NEOSTATUS_SLEEP_CUE_GAP_MS
    #define NEO_SLEEP_CUE_GAP_MS NEOSTATUS_SLEEP_CUE_GAP_MS
  #else
    #define NEO_SLEEP_CUE_GAP_MS ((uint32_t)NEO_SEQ_GAP_MS)
  #endif
#endif

// Event pulse brightness (start/stop cues etc). This is separate from:
// - NEO_STATUS_BRIGHTNESS (global strip brightness cap, 0..255)
// - NEO_PLAY_MAX_PCT (now-playing idle animation)
#ifndef NEO_EVENT_MIN_PCT
  #ifdef NEOSTATUS_EVENT_MIN_PCT
    #define NEO_EVENT_MIN_PCT NEOSTATUS_EVENT_MIN_PCT
  #else
    #define NEO_EVENT_MIN_PCT NEO_SEQ_MIN_PCT
  #endif
#endif
#ifndef NEO_EVENT_MAX_PCT
  #ifdef NEOSTATUS_EVENT_MAX_PCT
    #define NEO_EVENT_MAX_PCT NEOSTATUS_EVENT_MAX_PCT
  #else
    // Slightly lower than 100% by default so events don't blast at night.
    #define NEO_EVENT_MAX_PCT 70u
  #endif
#endif
#ifndef NEO_LOW_BATT_PULSES
  #define NEO_LOW_BATT_PULSES 3u
#endif
#ifdef NEOSTATUS_LOW_BATT_PULSES
  #undef NEO_LOW_BATT_PULSES
  #define NEO_LOW_BATT_PULSES NEOSTATUS_LOW_BATT_PULSES
#endif

// Per-event ring spin speed overrides (optional).
#ifndef NEO_BOOT_SPIN_PERIOD_MS
  #define NEO_BOOT_SPIN_PERIOD_MS NEO_SPIN_PERIOD_MS
#endif
#ifdef NEOSTATUS_BOOT_SPIN_PERIOD_MS
  #undef NEO_BOOT_SPIN_PERIOD_MS
  #define NEO_BOOT_SPIN_PERIOD_MS NEOSTATUS_BOOT_SPIN_PERIOD_MS
#endif
#ifndef NEO_NET_SPIN_PERIOD_MS
  #define NEO_NET_SPIN_PERIOD_MS NEO_SPIN_PERIOD_MS
#endif
#ifdef NEOSTATUS_NET_SPIN_PERIOD_MS
  #undef NEO_NET_SPIN_PERIOD_MS
  #define NEO_NET_SPIN_PERIOD_MS NEOSTATUS_NET_SPIN_PERIOD_MS
#endif
#ifndef NEO_SD_SPIN_PERIOD_MS
  #define NEO_SD_SPIN_PERIOD_MS NEO_SPIN_PERIOD_MS
#endif
#ifdef NEOSTATUS_SD_SPIN_PERIOD_MS
  #undef NEO_SD_SPIN_PERIOD_MS
  #define NEO_SD_SPIN_PERIOD_MS NEOSTATUS_SD_SPIN_PERIOD_MS
#endif
#ifndef NEO_RADIO_START_SPIN_PERIOD_MS
  #define NEO_RADIO_START_SPIN_PERIOD_MS NEO_SPIN_PERIOD_MS
#endif
#ifdef NEOSTATUS_RADIO_START_SPIN_PERIOD_MS
  #undef NEO_RADIO_START_SPIN_PERIOD_MS
  #define NEO_RADIO_START_SPIN_PERIOD_MS NEOSTATUS_RADIO_START_SPIN_PERIOD_MS
#endif
#ifndef NEO_PODCAST_START_SPIN_PERIOD_MS
  #define NEO_PODCAST_START_SPIN_PERIOD_MS NEO_SPIN_PERIOD_MS
#endif
#ifdef NEOSTATUS_PODCAST_START_SPIN_PERIOD_MS
  #undef NEO_PODCAST_START_SPIN_PERIOD_MS
  #define NEO_PODCAST_START_SPIN_PERIOD_MS NEOSTATUS_PODCAST_START_SPIN_PERIOD_MS
#endif
#ifndef NEO_SPK_SELECT_SPIN_PERIOD_MS
  #define NEO_SPK_SELECT_SPIN_PERIOD_MS NEO_SPIN_PERIOD_MS
#endif
#ifdef NEOSTATUS_SPK_SELECT_SPIN_PERIOD_MS
  #undef NEO_SPK_SELECT_SPIN_PERIOD_MS
  #define NEO_SPK_SELECT_SPIN_PERIOD_MS NEOSTATUS_SPK_SELECT_SPIN_PERIOD_MS
#endif
#ifndef NEO_WIFI_LOST_SPIN_PERIOD_MS
  #define NEO_WIFI_LOST_SPIN_PERIOD_MS NEO_SPIN_PERIOD_MS
#endif
#ifdef NEOSTATUS_WIFI_LOST_SPIN_PERIOD_MS
  #undef NEO_WIFI_LOST_SPIN_PERIOD_MS
  #define NEO_WIFI_LOST_SPIN_PERIOD_MS NEOSTATUS_WIFI_LOST_SPIN_PERIOD_MS
#endif
#ifndef NEO_SLEEP_SPIN_PERIOD_MS
  #define NEO_SLEEP_SPIN_PERIOD_MS NEO_SPIN_PERIOD_MS
#endif
#ifdef NEOSTATUS_SLEEP_SPIN_PERIOD_MS
  #undef NEO_SLEEP_SPIN_PERIOD_MS
  #define NEO_SLEEP_SPIN_PERIOD_MS NEOSTATUS_SLEEP_SPIN_PERIOD_MS
#endif
#ifndef NEO_LOW_BATT_SPIN_PERIOD_MS
  #define NEO_LOW_BATT_SPIN_PERIOD_MS NEO_SPIN_PERIOD_MS
#endif
#ifdef NEOSTATUS_LOW_BATT_SPIN_PERIOD_MS
  #undef NEO_LOW_BATT_SPIN_PERIOD_MS
  #define NEO_LOW_BATT_SPIN_PERIOD_MS NEOSTATUS_LOW_BATT_SPIN_PERIOD_MS
#endif

#ifndef NEO_BOOT_DELAY_MS
  // Delay boot pulses so they align with the splash/logo drawing.
  #define NEO_BOOT_DELAY_MS 2800u
#endif

#ifndef NEO_NET_DELAY_MS
  // Optional delay before the Wi‑Fi-joined cue (typically none).
  #define NEO_NET_DELAY_MS 0u
#endif

enum class Mode : uint8_t {
  OFF = 0,
  BT_SEARCHING,
  BT_CONNECTED_WAIT_AUDIO,
};

struct Rgb { uint8_t r, g, b; };

enum class SeqStyle : uint8_t {
  Solid = 0,
  DualComplement,
  RainbowDual,
  RainbowTriple,
  RainbowStrobe,
  RainbowScanner,
  SolidWiggle,  // alternate direction each spin
  SolidStrobe,  // base-color flash at start of each spin
  SolidScanner, // reverse direction mid-spin (base color)
  GreenWave,    // green-focused wave/chase
  SolidDualFixed, // primary + fixed accent opposite (no auto-complement)
};

static inline uint8_t clampU8(int v) {
  if (v < 0) return 0;
  if (v > 255) return 255;
  return (uint8_t)v;
}

static inline uint8_t scale8(uint8_t c, uint8_t br) {
  return (uint8_t)(((uint16_t)c * (uint16_t)br + 127u) / 255u);
}

static inline Rgb rgbComplement(Rgb c) {
  return Rgb{(uint8_t)(255u - c.r), (uint8_t)(255u - c.g), (uint8_t)(255u - c.b)};
}

static inline Rgb hsvToRgb(uint16_t hue, uint8_t sat, uint8_t val) {
  // hue: 0..65535, sat/val: 0..255
  if (sat == 0) return Rgb{val, val, val};
  const uint8_t region = (uint8_t)(hue / 10923u); // 0..5 (65535/6≈10922.5)
  const uint16_t rem = (uint16_t)((hue - (uint16_t)region * 10923u) * 6u); // 0..65535
  const uint8_t p = (uint8_t)((uint16_t)val * (255u - sat) / 255u);
  const uint8_t q = (uint8_t)((uint16_t)val * (255u - (uint16_t)sat * (uint16_t)rem / 65535u) / 255u);
  const uint8_t t = (uint8_t)((uint16_t)val * (255u - (uint16_t)sat * (uint16_t)(65535u - rem) / 65535u) / 255u);
  switch (region) {
    default:
    case 0: return Rgb{val, t, p};
    case 1: return Rgb{q, val, p};
    case 2: return Rgb{p, val, t};
    case 3: return Rgb{p, q, val};
    case 4: return Rgb{t, p, val};
    case 5: return Rgb{val, p, q};
  }
}

static inline Rgb rgbLerp(Rgb a, Rgb b, uint8_t t) {
  // t: 0..255
  return Rgb{
    (uint8_t)((uint16_t)a.r + (((int)b.r - (int)a.r) * (int)t) / 255),
    (uint8_t)((uint16_t)a.g + (((int)b.g - (int)a.g) * (int)t) / 255),
    (uint8_t)((uint16_t)a.b + (((int)b.b - (int)a.b) * (int)t) / 255),
  };
}

static inline Rgb accentForBase(Rgb base) {
  // Pick an accent that stays "in family" for common colors, otherwise use complement.
  // (We want multi-color but still immediately recognizable per event.)
  if (base.r > 220 && base.g > 220 && base.b > 220) return Rgb{0, 191, 255};        // white -> sky blue
  if (base.r > 220 && base.g > 140 && base.b < 90)  return Rgb{255, 140, 0};       // gold/yellow -> orange
  if (base.r > 220 && base.b > 120 && base.g < 120) return Rgb{102, 51, 153};      // pink/magenta -> purple
  if (base.g > 220 && base.b > 160 && base.r < 120) return Rgb{0, 128, 255};       // cyan/teal -> blue
  if (base.b > 220 && base.r < 90 && base.g < 120)  return Rgb{0, 191, 255};       // blue -> sky blue
  if (base.r > 150 && base.b > 150 && base.g < 120) return Rgb{0, 191, 255};       // purple -> sky blue
  return rgbComplement(base);
}

static uint8_t breathePhase(Adafruit_NeoPixel& px, uint32_t phaseMs, uint32_t periodMs, uint8_t minBr, uint8_t maxBr) {
  if (periodMs == 0) return maxBr;
  const uint32_t t = phaseMs % periodMs;
  // triangle 0..255..0
  uint32_t tri = (t * 510u) / periodMs;
  if (tri > 255u) tri = 510u - tri;
  // gamma-correct for prettier "pulse" (no harsh linear ramps)
  const uint8_t g = px.gamma8((uint8_t)tri);
  const uint16_t span = (uint16_t)(maxBr - minBr);
  return (uint8_t)(minBr + (uint16_t)g * span / 255u);
}

class NeoStatusPlugin final : public Plugin {
public:
  NeoStatusPlugin() = default;

  void on_setup() override {
    if (!NEO_STATUS_ENABLE) return;
    if (!_mtx) _mtx = xSemaphoreCreateMutex();

    // If we held GPIOs during deep sleep, release them now (fresh boot after wake).
    gpio_deep_sleep_hold_dis();
    _deepSleepArmed = false;
    if (NEO_STATUS_PIN != 255) {
      gpio_hold_dis((gpio_num_t)NEO_STATUS_PIN);
    }
#if (MODE_BUTTON_LED_PIN != 255)
    gpio_hold_dis((gpio_num_t)MODE_BUTTON_LED_PIN);
#endif

    _px.begin();
    const uint8_t capPct = config.store.neoStatusBrightnessPct;
    const uint8_t baseBr = (uint8_t)((uint16_t)NEO_STATUS_BRIGHTNESS * (uint16_t)capPct / 100u);
    _px.setBrightness(baseBr);
    _lastStripBrightness = baseBr;
    _px.clear();
    _px.show();
    _pxR = _pxG = _pxB = 0;

#if (MODE_BUTTON_LED_PIN != 255)
    pinMode(MODE_BUTTON_LED_PIN, OUTPUT);
    // Make the LED visible immediately at boot (don't wait for the render task).
    // Default boot state is "not playing", so use the idle target.
    const uint8_t pct = (uint8_t)MODE_BUTTON_LED_IDLE_SOLID_PCT;
    uint16_t br16 = (uint16_t)clampU8((int)pct * 255 / 100);
    // Apply the same runtime LED brightness knobs as the NeoPixel strip.
    br16 = (br16 * (uint16_t)config.store.neoStatusBrightnessPct) / 100u;
    if (config.store.neoStatusFollowScreen) {
      int sp = (int)backlightPlugin.getCurrentBrightnessPct();
      if (sp < 0) sp = 0;
      if (sp > 100) sp = 100;
      if (sp < (int)NEO_STATUS_FOLLOW_SCREEN_MIN_PCT) sp = (int)NEO_STATUS_FOLLOW_SCREEN_MIN_PCT;
      if (sp > (int)NEO_STATUS_FOLLOW_SCREEN_MAX_PCT) sp = (int)NEO_STATUS_FOLLOW_SCREEN_MAX_PCT;
      br16 = (br16 * (uint16_t)sp) / 100u;
    }
    const uint8_t br = (uint8_t)min<uint16_t>(255u, br16);
    const uint8_t out = (MODE_BUTTON_LED_ACTIVE_HIGH ? br : (uint8_t)(255u - br));
    analogWrite(MODE_BUTTON_LED_PIN, out);
#endif

    _lastBt = BtCompanionLinkState::OFF;
    _lastBtEnabled = false;
    _netWasConnected = false;
    _loopSeen = false;
    _seqActive = false;
    _lastFrameMs = 0;
    _lastVol = (int)config.store.volume;
    _volIgnoreUntilMs = millis() + (uint32_t)NEO_VOL_ARM_DELAY_MS;
    _volSuppressUntilMs = _volIgnoreUntilMs;
    _hadPlay = false;
    _lastPlayMode = (uint8_t)config.getMode();
    _bootMs = millis();
    _bootAnimUntilMs = _bootMs + (uint32_t)NEO_BOOT_ANIM_MS;
    _lastOutputDevice = (int)config.store.outputDevice;

    // Run animations from a small dedicated task so early boot / setup-time pulses
    // don't depend on Arduino loop cadence.
    ensureTask();

    // Boot: show a smooth progress wheel for ~5s (ring only).
    if (NEO_BOOT_ANIM_ENABLE && _px.numPixels() > 1) {
      return;
    }

    // Fallback: pulse sequence (single pixel).
#if NEO_DIAG_LOG
    Serial.printf("[neo] boot seq pulses=%u delay_ms=%u rgb=(%u,%u,%u)\n",
                  (unsigned)NEO_BOOT_PULSES, (unsigned)NEO_BOOT_DELAY_MS,
                  (unsigned)NEO_BOOT_RGB);
#endif
    // Boot: green-focused (black case + green trim).
    startSeqEx(
      {NEO_BOOT_RGB},
      NEO_BOOT_PULSES,
      (_px.numPixels() > 1) ? (uint16_t)NEO_BOOT_SPIN_PERIOD_MS : (uint16_t)NEO_SEQ_PULSE_MS,
      NEO_SEQ_GAP_MS,
      NEO_SEQ_MIN_PCT,
      NEO_SEQ_MAX_PCT,
      NEO_BOOT_DELAY_MS,
      true,
      SeqStyle::GreenWave
    );
  }

  void on_end_setup() override {}

  void on_connect() override {
    Guard g(_mtx, pdMS_TO_TICKS(30));
    if (!g.ok()) return;
    // Requested: network connect = green pulse sequence.
#if NEO_DIAG_LOG
    Serial.printf("[neo] net-joined seq pulses=%u delay_ms=%u rgb=(%u,%u,%u)\n",
                  (unsigned)NEO_NET_PULSES, (unsigned)NEO_NET_DELAY_MS,
                  (unsigned)NEO_NET_RGB);
#endif
    startSeqEx({NEO_NET_RGB}, NEO_NET_PULSES,
             (_px.numPixels() > 1) ? (uint16_t)NEO_NET_SPIN_PERIOD_MS : (uint16_t)NEO_SEQ_PULSE_MS,
             NEO_SEQ_GAP_MS,
             NEO_SEQ_MIN_PCT,
             NEO_SEQ_MAX_PCT,
             (uint32_t)NEO_NET_DELAY_MS,
             true,
             SeqStyle::Solid);
  }

  void on_start_play() override {
    Guard g(_mtx, pdMS_TO_TICKS(30));
    if (!g.ok()) return;
    // Requested: white pulse when a radio (web streaming) station starts.
    // SD playback also triggers on_start_play(), so gate on mode.
    _hadPlay = true;
    _stopActive = false;
    _lastPlayMode = (uint8_t)config.getMode();
    if (config.getMode() == PM_WEB) {
      startSeqEx({NEO_RADIO_START_RGB}, NEO_RADIO_START_PULSES,
                 (_px.numPixels() > 1) ? (uint16_t)NEO_RADIO_START_SPIN_PERIOD_MS : (uint16_t)NEO_SEQ_PULSE_MS,
                 NEO_SEQ_GAP_MS,
                 NEO_EVENT_MIN_PCT,
                 NEO_EVENT_MAX_PCT,
                 0u,
                 true,
                 SeqStyle::Solid);
    } else if (config.getMode() == PM_PODCAST) {
      startSeqEx({NEO_PODCAST_START_RGB}, NEO_PODCAST_START_PULSES,
                 (_px.numPixels() > 1) ? (uint16_t)NEO_PODCAST_START_SPIN_PERIOD_MS : (uint16_t)NEO_SEQ_PULSE_MS,
                 NEO_SEQ_GAP_MS,
                 NEO_EVENT_MIN_PCT,
                 NEO_EVENT_MAX_PCT,
                 0u,
                 true,
                 SeqStyle::Solid);
    } else if (config.getMode() == PM_SDCARD) {
      // Requested: SD indication should trigger on SD playback start (not indexing).
      startSeqEx({NEO_SD_RGB}, NEO_SD_PULSES,
                 (_px.numPixels() > 1) ? (uint16_t)NEO_SD_SPIN_PERIOD_MS : (uint16_t)NEO_SEQ_PULSE_MS,
                 NEO_SEQ_GAP_MS,
                 NEO_EVENT_MIN_PCT,
                 NEO_EVENT_MAX_PCT,
                 0u,
                 true,
                 SeqStyle::Solid);
    }
  }

  void on_stop_play() override {
    Guard g(_mtx, pdMS_TO_TICKS(30));
    if (!g.ok()) return;
    if (!_hadPlay) return;
    Rgb rgb = {NEO_RADIO_START_RGB};
    if (_lastPlayMode == (uint8_t)PM_SDCARD) {
      rgb = {NEO_SD_RGB};
    } else if (_lastPlayMode == (uint8_t)PM_PODCAST) {
      rgb = {NEO_PODCAST_START_RGB};
    }
    // STOP cue: decelerate smoothly, then STOP (static hold), then fade out.
    // This runs independently of the sequence engine so it doesn't feel like "step, step, stop".
    const uint32_t now = millis();
    _stopActive = true;
    _stopStartMs = now;
    _stopDecelEndMs = now + (uint32_t)NEO_MEDIA_STOP_DECEL_MS;
    _stopHoldEndMs = _stopDecelEndMs + (uint32_t)NEO_MEDIA_STOP_HOLD_MS;
    _stopFadeEndMs = _stopHoldEndMs + (uint32_t)NEO_MEDIA_STOP_FADE_MS;
    _stopRgb = rgb;
    _seqActive = false;
  }

  void on_station_change() override {
    Guard g(_mtx, pdMS_TO_TICKS(30));
    if (!g.ok()) return;
    // Media "starting" cue should trigger ASAP from user action (station selection),
    // and should blend into the ongoing play animation (CW, same cadence).
    if (!NEO_STATUS_ENABLE) return;
    if (_px.numPixels() <= 1) return;

    Rgb rgb = {NEO_RADIO_START_RGB};
    const uint8_t pmode = (uint8_t)config.getMode();
    if (pmode == (uint8_t)PM_SDCARD) rgb = {NEO_SD_RGB};
    else if (pmode == (uint8_t)PM_PODCAST) rgb = {NEO_PODCAST_START_RGB};

    // Make it instantly respond but not "stop": we keep the play cadence and just
    // boost brightness/tail briefly. That way it naturally blends into playing.
    _startBoostUntilMs = millis() + (uint32_t)NEO_MEDIA_START_BOOST_MS;
  }

  void pulseSleep() {
    Guard g(_mtx, pdMS_TO_TICKS(30));
    if (!g.ok()) return;
    // Sleep: calm pulses, then fade down (no spin / "starting" vibe).
    const uint32_t now = millis();
    _sleepCueStartMs = now;
    _sleepCuePulses = (uint8_t)NEO_SLEEP_PULSES;
    _sleepCueBaseMs = (uint32_t)NEO_SLEEP_CUE_BASE_MS;
    _sleepCueStepMs = (uint32_t)NEO_SLEEP_CUE_STEP_MS;
    _sleepCueGapMs = (uint32_t)NEO_SLEEP_CUE_GAP_MS;

    uint32_t total = 0;
    for (uint8_t i = 0; i < _sleepCuePulses; i++) {
      total += (_sleepCueBaseMs + (uint32_t)i * _sleepCueStepMs);
      if (i + 1u < _sleepCuePulses) total += _sleepCueGapMs;
    }
    _sleepCueUntilMs = now + total;

    _sleepFadeStartMs = _sleepCueUntilMs;
    _sleepFadeUntilMs = _sleepFadeStartMs + 1400u;
    _sleepFadeRgb = {NEO_SLEEP_RGB};
    _seqActive = false;
    _stopActive = false;
  }

  void pulseLowBattery() {
    Guard g(_mtx, pdMS_TO_TICKS(30));
    if (!g.ok()) return;
    startSeqEx({NEO_LOW_BATT_RGB}, NEO_LOW_BATT_PULSES,
               (_px.numPixels() > 1) ? (uint16_t)NEO_LOW_BATT_SPIN_PERIOD_MS : (uint16_t)NEO_SEQ_PULSE_MS,
               NEO_SEQ_GAP_MS,
               NEO_SEQ_MIN_PCT,
               NEO_SEQ_MAX_PCT,
               0u,
               true,
               SeqStyle::Solid);
  }

  void prepareForDeepSleep() {
    Guard g(_mtx, pdMS_TO_TICKS(60));
    if (!g.ok()) return;

    // Prevent any subsequent ticks from re-lighting LEDs before deep sleep starts.
    _deepSleepArmed = true;

    // Force mode/power button LED off and hold it for deep sleep.
#if (MODE_BUTTON_LED_PIN != 255)
    {
      // Detach PWM and hard-drive the pin OFF (more reliable than analogWrite during sleep entry).
      ledcDetach((uint8_t)MODE_BUTTON_LED_PIN);
      pinMode(MODE_BUTTON_LED_PIN, OUTPUT);
      const uint8_t level = (MODE_BUTTON_LED_ACTIVE_HIGH ? LOW : HIGH);
      digitalWrite(MODE_BUTTON_LED_PIN, level);
      // Prefer a pulldown so if the pad ever floats, it biases "off" for active-high wiring.
      gpio_pullup_dis((gpio_num_t)MODE_BUTTON_LED_PIN);
      gpio_pulldown_en((gpio_num_t)MODE_BUTTON_LED_PIN);
      gpio_hold_en((gpio_num_t)MODE_BUTTON_LED_PIN);
    }
#endif

    // Stop any active animations and force a black frame.
    _seqActive = false;
    _pendCount = 0;
    _stopActive = false;
    _sleepCueUntilMs = 0;
    _sleepFadeUntilMs = 0;

    if (_px.numPixels() > 1) {
      for (uint16_t i = 0; i < (uint16_t)_px.numPixels() && i < kMaxRingPixels; i++) {
        _ringR[i] = _ringG[i] = _ringB[i] = 0;
      }
      _px.clear();
      _px.show();
    } else {
      _pxR = _pxG = _pxB = 0;
      _px.setPixelColor(0, 0);
      _px.show();
    }

    // Force the data pin low and hold it there in deep sleep. This prevents
    // random WS2812 latch flashes when VDD collapses slowly (large caps / LDO rails).
    if (rtc_gpio_is_valid_gpio((gpio_num_t)NEO_STATUS_PIN)) {
      rtc_gpio_deinit((gpio_num_t)NEO_STATUS_PIN);
    }
    ledcDetach((uint8_t)NEO_STATUS_PIN);
    pinMode(NEO_STATUS_PIN, OUTPUT);
    digitalWrite(NEO_STATUS_PIN, LOW);
    gpio_pullup_dis((gpio_num_t)NEO_STATUS_PIN);
    gpio_pulldown_en((gpio_num_t)NEO_STATUS_PIN);
    gpio_hold_en((gpio_num_t)NEO_STATUS_PIN);
    gpio_deep_sleep_hold_en();

    // Stop the render task (cosmetic) to avoid any last-moment writes.
    if (_taskHandle) {
      vTaskSuspend(_taskHandle);
    }
  }

  void armForDeepSleep() {
    Guard g(_mtx, pdMS_TO_TICKS(60));
    if (!g.ok()) return;
    _deepSleepArmed = true;
    if (_taskHandle) vTaskSuspend(_taskHandle);
  }

  void on_display_queue(requestParams_t& request, bool& result) override {
    Guard g(_mtx, pdMS_TO_TICKS(30));
    if (!g.ok()) return;
    (void)result;
    // Trigger an SD indexing animation while the UI is showing SD wait/index states.
    if (request.type == WAITFORSD || request.type == SDFILEINDEX) {
      _sdIndexUntilMs = millis() + 1200u;
    }
  }

  void on_btn_click(controlEvt_e& btnid) override {
#if (MODE_BUTTON_LED_PIN != 255)
    Guard g(_mtx, pdMS_TO_TICKS(30));
    if (!g.ok()) return;
    if (!MODE_BUTTON_LED_PRESS_PULSE_ENABLE) return;
    if (btnid == EVT_BTNMODE) {
      _btnPulseStartMs = millis();
    }
#else
    (void)btnid;
#endif
  }

  void on_loop() override {
    if (!NEO_STATUS_ENABLE) return;
    if (_taskHandle) return; // task owns rendering
    tickNow();
  }

  void tickNow() {
    Guard g(_mtx, pdMS_TO_TICKS(30));
    if (!g.ok()) return;
    if (_deepSleepArmed) {
      // Deep sleep entry path: keep everything OFF. (Do not attempt animations.)
#if (MODE_BUTTON_LED_PIN != 255)
      ledcDetach((uint8_t)MODE_BUTTON_LED_PIN);
      pinMode(MODE_BUTTON_LED_PIN, OUTPUT);
      digitalWrite(MODE_BUTTON_LED_PIN, (MODE_BUTTON_LED_ACTIVE_HIGH ? LOW : HIGH));
#endif
      ledcDetach((uint8_t)NEO_STATUS_PIN);
      pinMode(NEO_STATUS_PIN, OUTPUT);
      digitalWrite(NEO_STATUS_PIN, LOW);
      return;
    }
    const uint32_t now = millis();
    if (!_loopSeen) {
      _loopSeen = true;
      if (!_seqActive && _pendCount > 0) startPendingSeq(now);
    }
    // Higher update rate -> smoother breathing/pulses.
    const uint32_t dtMs = (_lastFrameMs == 0) ? 10u : (uint32_t)(now - _lastFrameMs);
    if (_lastFrameMs != 0 && dtMs < 8u) return;
    _lastFrameMs = now;
    _dtMs = dtMs;

    // Dynamic global brightness:
    // - Apply runtime "NeoStatus brightness" percent cap (MQTT/HA).
    // - Optionally follow the current backlight level (so it tracks ALS + dimming).
    {
      const uint8_t capPct = config.store.neoStatusBrightnessPct;
      const uint8_t baseBr = (uint8_t)((uint16_t)NEO_STATUS_BRIGHTNESS * (uint16_t)capPct / 100u);
      uint8_t br = baseBr;
      if (config.store.neoStatusFollowScreen) {
        int pct = (int)backlightPlugin.getCurrentBrightnessPct();
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        if (pct < (int)NEO_STATUS_FOLLOW_SCREEN_MIN_PCT) pct = (int)NEO_STATUS_FOLLOW_SCREEN_MIN_PCT;
        if (pct > (int)NEO_STATUS_FOLLOW_SCREEN_MAX_PCT) pct = (int)NEO_STATUS_FOLLOW_SCREEN_MAX_PCT;
        br = (uint8_t)((uint16_t)baseBr * (uint16_t)pct / 100u);
      }
      if (br != _lastStripBrightness) {
        _lastStripBrightness = br;
        _px.setBrightness(br);
      }
    }

    // Boot animation: smooth green progress wheel (ring only).
    if (NEO_BOOT_ANIM_ENABLE && _px.numPixels() > 1 && (int32_t)(now - _bootAnimUntilMs) < 0) {
      renderBootProgressWheel(now);
      return;
    }

    // SD indexing animation (ring only).
    if (_px.numPixels() > 1 && (int32_t)(now - _sdIndexUntilMs) < 0 && !player.isRunning() && config.getMode() == PM_SDCARD) {
      renderSdIndexing(now);
      return;
    }

    // Stop animation: decelerate -> STOP (hold) -> fade out.
    if (_stopActive && !player.isRunning()) {
      if ((int32_t)(now - _stopFadeEndMs) < 0) {
        renderStopCue(now);
        return;
      }
      _stopActive = false;
    }

    // Sleep cue pulses (calm), then fade down.
    if ((int32_t)(now - _sleepCueUntilMs) < 0) {
      renderSleepCue(now);
      return;
    }

    // Sleep fade animation (calm fade down).
    if ((int32_t)(now - _sleepFadeUntilMs) < 0) {
      renderSleepFade(now);
      return;
    }

    // Suppress volume spin during output-device toggles (speaker <-> BT).
    // The toggle restores a different stored volume, which otherwise looks like a "user volume change".
    const int outDev = (int)config.store.outputDevice;
    if (_lastOutputDevice < 0) _lastOutputDevice = outDev;
    if (outDev != _lastOutputDevice) {
      _lastOutputDevice = outDev;
      _volSuppressUntilMs = now + (uint32_t)NEO_VOL_SUPPRESS_ON_OUTPUT_SWITCH_MS;
      _lastVol = (int)config.store.volume;
    }

    // Wi-Fi lost / reconnecting cue: amber double pulse when we drop from CONNECTED.
    const bool netConnectedNow = (network.status == CONNECTED);
    if (_netWasConnected && !netConnectedNow) {
      startSeqEx({NEO_WIFI_LOST_RGB}, NEO_WIFI_LOST_PULSES,
                 (_px.numPixels() > 1) ? (uint16_t)NEO_WIFI_LOST_SPIN_PERIOD_MS : (uint16_t)NEO_SEQ_PULSE_MS,
                 NEO_SEQ_GAP_MS,
                 NEO_SEQ_MIN_PCT,
                 NEO_SEQ_MAX_PCT,
                 0u,
                 true,
                 SeqStyle::Solid);
    }
    _netWasConnected = netConnectedNow;

    // Track BT link transitions for optional overlays.
    const bool btEnabled = btcompanion_enabled();
    const BtCompanionLinkState bt = btEnabled ? btcompanion_linkState() : BtCompanionLinkState::OFF;

    // Charging detected cue (only while charging detected).
    const bool usbNow = battery_usb_present();
    const float rate = battery_get_charge_rate();
    const bool chargingNow = usbNow && (rate > 0.05f);
    if (!_lastCharging && chargingNow) {
      // Short green/teal confirmation.
      startSeqEx({0, 255, 140}, 2u, 260u, 80u, 0u, 85u, 0u, true, SeqStyle::Solid);
    }
    _lastCharging = chargingNow;

    if (_lastBtEnabled && !btEnabled) {
      startSeqEx({NEO_SPK_SELECT_RGB}, NEO_SPK_SELECT_PULSES,
                 (_px.numPixels() > 1) ? (uint16_t)NEO_SPK_SELECT_SPIN_PERIOD_MS : (uint16_t)NEO_SEQ_PULSE_MS,
                 NEO_SEQ_GAP_MS,
                 NEO_SEQ_MIN_PCT,
                 NEO_SEQ_MAX_PCT,
                 0u,
                 false,
                 SeqStyle::SolidWiggle);
    }
    _lastBtEnabled = btEnabled;

    if (bt != _lastBt) {
      if (bt == BtCompanionLinkState::AUDIO) {
        // Short confirmation "spin" on AUDIO.
        startSeqEx({NEO_BT_RGB_A}, NEO_BT_AUDIO_PULSES, seqPulseMsDefault(), 90u, 1u, 75u, 0u, true, SeqStyle::SolidDualFixed);
        _seqAccent = Rgb{NEO_BT_RGB_B};
      }
      _lastBt = bt;
    }

    // Volume feedback on ring: spin CW for up, CCW for down.
    // (Do this before renderSeq so it can interrupt a steady mode.)
    if (_px.numPixels() > 1) {
      const int curVol = (int)config.store.volume;
      // During early boot/config init we keep syncing the baseline but don't animate.
      if ((int32_t)(now - _volIgnoreUntilMs) < 0) {
        _lastVol = curVol;
      } else if ((int32_t)(now - _volSuppressUntilMs) < 0) {
        _lastVol = curVol;
      } else if (NEO_VOL_MAX_SPINS == 0u) {
        _lastVol = curVol;
      } else if (_lastVol >= 0 && curVol != _lastVol) {
        const int delta = curVol - _lastVol;
        _lastVol = curVol;

        const bool clockwise = (delta > 0);
        const uint32_t absd = (uint32_t)abs(delta);
        const uint32_t per = (NEO_VOL_STEPS_PER_SPIN == 0u) ? 1u : (uint32_t)NEO_VOL_STEPS_PER_SPIN;
        uint32_t spins = (absd + per - 1u) / per;
        if (spins < 1u) spins = 1u;
        if (spins > (uint32_t)NEO_VOL_MAX_SPINS) spins = (uint32_t)NEO_VOL_MAX_SPINS;

#if NEO_DIAG_LOG
        Serial.printf("[neo] vol delta=%d spins=%u dir=%s\n", delta, (unsigned)spins, clockwise ? "CW" : "CCW");
#endif
        // Start/extend a green spin sequence. Minimum 1 full spin even for small changes.
        startOrExtendVolSeq(now, {NEO_VOL_RGB}, (uint8_t)spins, clockwise);
      } else if (_lastVol < 0) {
        _lastVol = curVol;
      }
    }

    // Mode/power button LED: snappy double-pulse on press, then boot-breathe/solid.
#if (MODE_BUTTON_LED_PIN != 255)
    {
      const bool playing = player.isRunning();
      uint8_t br = 0;
      const uint32_t pulseMs = (uint32_t)MODE_BUTTON_LED_PRESS_PULSE_MS;
      const uint32_t gapMs = (uint32_t)MODE_BUTTON_LED_PRESS_PULSE_GAP_MS;
      const uint32_t cycle = pulseMs + gapMs;
      const uint32_t total = cycle * (uint32_t)MODE_BUTTON_LED_PRESS_PULSE_COUNT;
      if (_btnPulseStartMs != 0 && (uint32_t)(now - _btnPulseStartMs) < total) {
        const uint32_t e = (uint32_t)(now - _btnPulseStartMs);
        const uint32_t inCycle = (cycle == 0) ? 0u : (e % cycle);
        if (inCycle < pulseMs && pulseMs > 0) {
          const uint8_t minBr = clampU8((int)MODE_BUTTON_LED_PRESS_PULSE_MIN_PCT * 255 / 100);
          const uint8_t maxBr = clampU8((int)MODE_BUTTON_LED_PRESS_PULSE_MAX_PCT * 255 / 100);
          // Triangle pulse.
          uint32_t tri = (inCycle * 510u) / pulseMs;
          if (tri > 255u) tri = 510u - tri;
          const uint8_t g = _px.gamma8((uint8_t)tri);
          const uint16_t span = (uint16_t)(maxBr - minBr);
          br = (uint8_t)(minBr + (uint16_t)g * span / 255u);
        } else {
          br = 0;
        }
      } else if (!playing && MODE_BUTTON_LED_BOOT_BREATHE_MS > 0u && (uint32_t)(now - _bootMs) < (uint32_t)MODE_BUTTON_LED_BOOT_BREATHE_MS) {
        const uint8_t minBr = clampU8((int)MODE_BUTTON_LED_BREATHE_MIN_PCT * 255 / 100);
        const uint8_t maxBr = clampU8((int)MODE_BUTTON_LED_BREATHE_MAX_PCT * 255 / 100);
        br = breathePhase(_px, now, MODE_BUTTON_LED_BREATHE_PERIOD_MS, minBr, maxBr);
      } else if (!playing && MODE_BUTTON_LED_IDLE_BREATHE_ENABLE) {
        const uint8_t minBr = clampU8((int)MODE_BUTTON_LED_BREATHE_MIN_PCT * 255 / 100);
        const uint8_t maxBr = clampU8((int)MODE_BUTTON_LED_BREATHE_MAX_PCT * 255 / 100);
        br = breathePhase(_px, now, MODE_BUTTON_LED_BREATHE_PERIOD_MS, minBr, maxBr);
      } else {
        const uint8_t pct = playing ? (uint8_t)MODE_BUTTON_LED_SOLID_PCT : (uint8_t)MODE_BUTTON_LED_IDLE_SOLID_PCT;
        br = clampU8((int)pct * 255 / 100);
      }
      // Apply runtime LED brightness knobs (cap + optional follow-screen) to the button LED too.
      uint16_t br16 = (uint16_t)br;
      br16 = (br16 * (uint16_t)config.store.neoStatusBrightnessPct) / 100u;
      if (config.store.neoStatusFollowScreen) {
        int sp = (int)backlightPlugin.getCurrentBrightnessPct();
        if (sp < 0) sp = 0;
        if (sp > 100) sp = 100;
        if (sp < (int)NEO_STATUS_FOLLOW_SCREEN_MIN_PCT) sp = (int)NEO_STATUS_FOLLOW_SCREEN_MIN_PCT;
        if (sp > (int)NEO_STATUS_FOLLOW_SCREEN_MAX_PCT) sp = (int)NEO_STATUS_FOLLOW_SCREEN_MAX_PCT;
        br16 = (br16 * (uint16_t)sp) / 100u;
      }
      const uint8_t brScaled = (uint8_t)min<uint16_t>(255u, br16);
      const uint8_t out = (MODE_BUTTON_LED_ACTIVE_HIGH ? brScaled : (uint8_t)(255u - brScaled));
      analogWrite(MODE_BUTTON_LED_PIN, out);
    }
#endif

    // Pulse sequences have priority over steady modes.
    if (renderSeq(now)) {
      return;
    }

    // If a sequence finished and we have pending pulses, start the next one.
    if (!_seqActive && _pendCount > 0) {
      startPendingSeq(now);
      if (renderSeq(now)) return;
    }

    // Podcast indexing animation (while building RSS playlist).
    if (NEO_POD_INDEX_ANIM_ENABLE && config.getMode() == PM_PODCAST && podcasts_buildInProgress() && !player.isRunning()) {
      renderPodcastIndexing(now);
      return;
    }

    const Mode m = pickMode(now, btEnabled, bt);
    if (m != Mode::OFF) {
      renderMode(now, m, btEnabled, bt);
      return;
    }

    // Requested: while playing, show a very dim, slow clockwise spin in the
    // color of the current media (web / SD / podcast). This should not override
    // BT searching/connecting indications (handled by pickMode()).
    if (NEO_PLAY_ENABLE && player.isRunning()) {
      renderNowPlaying(now);
      return;
    }

    // Idle glow (dim static color) when truly idle.
    if (NEO_IDLE_GLOW_ENABLE && !player.isRunning() && !(NEO_POD_INDEX_ANIM_ENABLE && config.getMode() == PM_PODCAST && podcasts_buildInProgress())) {
      renderIdleGlow();
      return;
    }

    renderMode(now, Mode::OFF, btEnabled, bt);
  }

private:
  Adafruit_NeoPixel _px{NEO_STATUS_COUNT, NEO_STATUS_PIN, NEO_GRB + NEO_KHZ800};

  static constexpr uint8_t kMaxRingPixels = 16;
  // Per-pixel intensity storage for smooth fades to OFF (and multi-color trails).
  uint8_t _ringR[kMaxRingPixels] = {0};
  uint8_t _ringG[kMaxRingPixels] = {0};
  uint8_t _ringB[kMaxRingPixels] = {0};
  // Fixed-point decay remainders (0..254) to reduce "steppy" fades at low brightness.
  uint8_t _ringRRem[kMaxRingPixels] = {0};
  uint8_t _ringGRem[kMaxRingPixels] = {0};
  uint8_t _ringBRem[kMaxRingPixels] = {0};
  // Single-pixel fade storage (for smooth OFF transitions too).
  uint8_t _pxR = 0, _pxG = 0, _pxB = 0;
  uint8_t _pxRRem = 0, _pxGRem = 0, _pxBRem = 0;
  int _lastVol = -1;
  uint32_t _volIgnoreUntilMs = 0;
  bool _hadPlay = false;
  uint8_t _lastPlayMode = 0;
  uint32_t _startBoostUntilMs = 0;

  BtCompanionLinkState _lastBt = BtCompanionLinkState::OFF;
  bool _lastBtEnabled = false;
  bool _netWasConnected = false;
  bool _lastCharging = false;
  uint32_t _sdIndexUntilMs = 0;
  bool _stopActive = false;
  uint32_t _stopStartMs = 0;
  uint32_t _stopDecelEndMs = 0;
  uint32_t _stopHoldEndMs = 0;
  uint32_t _stopFadeEndMs = 0;
  Rgb _stopRgb{0, 0, 0};

  uint32_t _sleepCueStartMs = 0;
  uint32_t _sleepCueUntilMs = 0;
  uint8_t  _sleepCuePulses = 0;
  uint32_t _sleepCueBaseMs = 0;
  uint32_t _sleepCueStepMs = 0;
  uint32_t _sleepCueGapMs = 0;
  uint32_t _sleepFadeStartMs = 0;
  uint32_t _sleepFadeUntilMs = 0;
  Rgb _sleepFadeRgb{0, 0, 0};

  // Pulse-sequence engine (N smooth pulses, with gaps).
  bool _seqActive = false;
  Rgb _seqRgb{0, 0, 0};
  Rgb _seqAccent{0, 0, 0}; // used only for SeqStyle::SolidDualFixed
  uint32_t _seqStartMs = 0;
  uint16_t _seqPulseMs = 0;
  uint16_t _seqGapMs = 0;
  uint8_t _seqCount = 0;
  uint8_t _seqMinPct = 0;
  uint8_t _seqMaxPct = 0;
  bool _seqClockwise = true;
  SeqStyle _seqStyle = SeqStyle::Solid;
  enum class SeqKind : uint8_t { Generic = 0, Volume = 1 };
  SeqKind _seqKind = SeqKind::Generic;

  // Events can happen during setup() before the first pm.on_loop().
  // Keep a tiny FIFO so those sequences are still visible.
  bool _loopSeen = false;
  struct PendingSeq {
    Rgb rgb;
    uint8_t pulses;
    uint16_t pulseMs;
    uint16_t gapMs;
    uint8_t minPct;
    uint8_t maxPct;
    uint32_t startAtMs;
    bool clockwise;
    SeqStyle style;
  };
  PendingSeq _pend[4]{};
  uint8_t _pendHead = 0;
  uint8_t _pendTail = 0;
  uint8_t _pendCount = 0;

  uint32_t _lastFrameMs = 0;
  uint32_t _dtMs = 12u;
  uint32_t _bootMs = 0;
  uint32_t _bootAnimUntilMs = 0;
  uint8_t _lastStripBrightness = 255u;
  uint32_t _btnPulseStartMs = 0;
  uint32_t _volSuppressUntilMs = 0;
  int _lastOutputDevice = -1;
  TaskHandle_t _taskHandle = nullptr;
  SemaphoreHandle_t _mtx = nullptr;
  bool _deepSleepArmed = false;

  struct Guard {
    SemaphoreHandle_t h = nullptr;
    bool locked = false;
    Guard(SemaphoreHandle_t in, TickType_t to) : h(in) {
      if (!h) { locked = true; return; }
      locked = (xSemaphoreTake(h, to) == pdTRUE);
    }
    ~Guard() {
      if (h && locked) xSemaphoreGive(h);
    }
    bool ok() const { return locked; }
  };

  static void taskTrampoline(void* arg) {
    auto* self = reinterpret_cast<NeoStatusPlugin*>(arg);
    for (;;) {
      self->tickNow();
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }

  void ensureTask() {
    if (_taskHandle) return;
    // Keep it low priority; this is purely cosmetic.
    (void)xTaskCreatePinnedToCore(taskTrampoline, "NeoStatus", (uint32_t)NEO_TASK_STACK, this, 1, &_taskHandle, (BaseType_t)NEO_TASK_CORE);
  }

  void enqueueSeq(Rgb rgb, uint8_t pulses, uint16_t pulseMs, uint16_t gapMs, uint8_t minPct, uint8_t maxPct, uint32_t startAtMs, bool clockwise, SeqStyle style) {
    if (pulses == 0 || pulseMs == 0) return;
    // If full, drop the oldest.
    if (_pendCount >= (uint8_t)(sizeof(_pend) / sizeof(_pend[0]))) {
      _pendHead = (uint8_t)((_pendHead + 1u) % (uint8_t)(sizeof(_pend) / sizeof(_pend[0])));
      _pendCount--;
    }
    _pend[_pendTail] = PendingSeq{rgb, pulses, pulseMs, gapMs, minPct, maxPct, startAtMs, clockwise, style};
    _pendTail = (uint8_t)((_pendTail + 1u) % (uint8_t)(sizeof(_pend) / sizeof(_pend[0])));
    _pendCount++;
  }

  void startSeqNow(uint32_t startMs, Rgb rgb, uint8_t pulses, uint16_t pulseMs, uint16_t gapMs, uint8_t minPct, uint8_t maxPct, bool clockwise, SeqStyle style) {
    _seqActive = (pulses > 0 && pulseMs > 0);
    _seqRgb = rgb;
    _seqAccent = Rgb{0, 0, 0};
    _seqStartMs = startMs;
    _seqPulseMs = pulseMs;
    _seqGapMs = gapMs;
    _seqCount = pulses;
    _seqMinPct = minPct;
    _seqMaxPct = maxPct;
    _seqClockwise = clockwise;
    _seqStyle = style;
    _seqKind = SeqKind::Generic;
  }

  void startOrExtendVolSeq(uint32_t now, Rgb rgb, uint8_t spins, bool clockwise) {
    if (spins == 0) return;
    if (NEO_VOL_MAX_SPINS == 0u) return;
    // If we're already running a volume sequence in the same direction, extend it in-place.
    if (_seqActive && _seqKind == SeqKind::Volume && _seqClockwise == clockwise && _seqPulseMs == (uint16_t)NEO_VOL_SPIN_PERIOD_MS) {
      // Volume feedback must remain directional and never inherit "wiggle/scanner" styles.
      _seqStyle = SeqStyle::Solid;
      uint16_t n = (uint16_t)_seqCount + (uint16_t)spins;
      const uint16_t maxn = (uint16_t)NEO_VOL_MAX_SPINS;
      if (maxn > 0 && n > maxn) n = maxn;
      if (n > 255u) n = 255u;
      _seqCount = (uint8_t)n;
      _seqRgb = rgb;
      return;
    }
    _seqActive = true;
    _seqRgb = rgb;
    _seqAccent = Rgb{0, 0, 0};
    _seqStartMs = now;
    _seqPulseMs = (uint16_t)NEO_VOL_SPIN_PERIOD_MS;
    _seqGapMs = 0;
    if (spins > (uint8_t)NEO_VOL_MAX_SPINS) spins = (uint8_t)NEO_VOL_MAX_SPINS;
    _seqCount = spins;
    _seqMinPct = (uint8_t)NEO_VOL_MIN_PCT;
    _seqMaxPct = (uint8_t)NEO_VOL_MAX_PCT;
    _seqClockwise = clockwise;
    _seqStyle = SeqStyle::Solid;
    _seqKind = SeqKind::Volume;
  }

  void startPendingSeq(uint32_t now) {
    if (_pendCount == 0) return;
    const PendingSeq p = _pend[_pendHead];
    _pendHead = (uint8_t)((_pendHead + 1u) % (uint8_t)(sizeof(_pend) / sizeof(_pend[0])));
    _pendCount--;
    const uint32_t startAt = (p.startAtMs > now) ? p.startAtMs : now;
    startSeqNow(startAt, p.rgb, p.pulses, p.pulseMs, p.gapMs, p.minPct, p.maxPct, p.clockwise, p.style);
  }

  void startSeqEx(Rgb rgb, uint8_t pulses, uint16_t pulseMs, uint16_t gapMs,
                  uint8_t minPct, uint8_t maxPct, uint32_t delayMs, bool clockwise, SeqStyle style) {
    const uint32_t startAt = millis() + delayMs;
    if (!_loopSeen) {
      enqueueSeq(rgb, pulses, pulseMs, gapMs, minPct, maxPct, startAt, clockwise, style);
      return;
    }
    startSeqNow(startAt, rgb, pulses, pulseMs, gapMs, minPct, maxPct, clockwise, style);
  }

  void startSeq(Rgb rgb, uint8_t pulses, uint16_t pulseMs, uint16_t gapMs,
                uint8_t minPct = NEO_SEQ_MIN_PCT, uint8_t maxPct = NEO_SEQ_MAX_PCT, uint32_t delayMs = 0, bool clockwise = true) {
    startSeqEx(rgb, pulses, pulseMs, gapMs, minPct, maxPct, delayMs, clockwise, SeqStyle::Solid);
  }

  Mode pickMode(uint32_t now, bool btEnabled, BtCompanionLinkState bt) const {
    if (btEnabled) {
      if (bt == BtCompanionLinkState::SEARCHING) return Mode::BT_SEARCHING;
      if (bt == BtCompanionLinkState::CONNECTED) return Mode::BT_CONNECTED_WAIT_AUDIO;
    }

    return Mode::OFF;
  }

  void renderMode(uint32_t now, Mode m, bool btEnabled, BtCompanionLinkState bt) {
    (void)btEnabled;
    (void)bt;
    switch (m) {
      case Mode::BT_SEARCHING:
        renderPulseDual(now, {NEO_BT_RGB_A}, {NEO_BT_RGB_B}, NEO_BT_SEARCH_PERIOD_MS, 1, 65);
        return;
      case Mode::BT_CONNECTED_WAIT_AUDIO:
        renderPulseDual(now, {NEO_BT_RGB_A}, {NEO_BT_RGB_B}, NEO_BT_CONNECTED_PERIOD_MS, 1, 80);
        return;
      case Mode::OFF:
      default:
        // Fade to OFF smoothly (ring or single pixel).
        if (_px.numPixels() > 1) {
          ringDecay((uint16_t)NEO_RING_FADE_MS);
          if (NEO_IDLE_GLOW_ENABLE) applyIdleGlowFloorRing();
          ringShow();
          return;
        }
        pixelDecay((uint16_t)NEO_RING_FADE_MS);
        if (NEO_IDLE_GLOW_ENABLE) applyIdleGlowFloorPixel();
        pixelShow();
        return;
    }
  }

  void renderNowPlaying(uint32_t now) {
    if (_px.numPixels() <= 1) {
      // Single pixel: keep a gentle pulse.
      Rgb rgb = {NEO_RADIO_START_RGB};
      const uint8_t pmode = (uint8_t)config.getMode();
      if (pmode == (uint8_t)PM_SDCARD) rgb = {NEO_SD_RGB};
      else if (pmode == (uint8_t)PM_PODCAST) rgb = {NEO_PODCAST_START_RGB};
      renderPulse(now, rgb, (uint32_t)NEO_PLAY_PERIOD_MS, (uint8_t)NEO_PLAY_MIN_PCT, (uint8_t)NEO_PLAY_MAX_PCT);
      return;
    }

    // Ring: smooth multi-color comet with a long tail (no "stepping" LED-to-LED).
    Rgb base = {NEO_RADIO_START_RGB};
    const uint8_t pmode = (uint8_t)config.getMode();
    if (pmode == (uint8_t)PM_SDCARD) base = {NEO_SD_RGB};
    else if (pmode == (uint8_t)PM_PODCAST) base = {NEO_PODCAST_START_RGB};
    const Rgb accent = accentForBase(base);
    uint16_t fadeMs = (uint16_t)NEO_PLAY_RING_FADE_MS;
    uint8_t maxPct = (uint8_t)NEO_PLAY_MAX_PCT;
    if ((int32_t)(now - _startBoostUntilMs) < 0) {
      // Temporary "start" boost that blends into play (same cadence, just brighter/longer tail).
      fadeMs = (uint16_t)(NEO_PLAY_RING_FADE_MS + 260u);
      maxPct = (uint8_t)clampU8((int)NEO_PLAY_MAX_PCT + 18);
    }
    renderCometSmooth(now, (uint32_t)NEO_PLAY_PERIOD_MS, fadeMs, (uint8_t)NEO_PLAY_MIN_PCT, maxPct, base, accent, true);
  }

  void renderIdleGlow() {
    const Rgb c = {NEO_IDLE_GLOW_RGB};
    const uint8_t pct = (uint8_t)NEO_IDLE_GLOW_PCT;
    if (_px.numPixels() > 1) {
      ringDecay((uint16_t)NEO_RING_FADE_MS);
      applyIdleGlowFloorRing(c, pct);
      ringShow();
      return;
    }
    pixelDecay((uint16_t)NEO_RING_FADE_MS);
    applyIdleGlowFloorPixel(c, pct);
    pixelShow();
  }

  void renderBootProgressWheel(uint32_t now) {
    const uint16_t n = (uint16_t)_px.numPixels();
    if (n == 0 || n > kMaxRingPixels) return;
    const uint32_t elapsed = (uint32_t)(now - _bootMs);
    const uint32_t totalMs = (uint32_t)NEO_BOOT_ANIM_MS;
    const uint32_t t = (totalMs == 0) ? 0u : (elapsed > totalMs ? totalMs : elapsed);

    const uint8_t maxBr = clampU8((int)NEO_BOOT_ANIM_MAX_PCT * 255 / 100);
    const uint8_t minBr = clampU8((int)NEO_BOOT_ANIM_MIN_PCT * 255 / 100);
    const uint16_t filled = (totalMs == 0) ? n : (uint16_t)((uint32_t)t * (uint32_t)n / totalMs);

    const Rgb base = {NEO_BOOT_RGB};          // should be green
    const Rgb accent = accentForBase(base);  // teal-ish highlight

    ringDecay((uint16_t)NEO_RING_FADE_MS);

    // Progress fill.
    for (uint16_t i = 0; i < n; i++) {
      const bool on = (i < filled);
      const uint8_t br = on ? maxBr : minBr;
      // Subtle green gradient around the wheel (still "green focused").
      const uint8_t mix = (uint8_t)((uint32_t)i * 255u / (n ? n : 1u));
      ringHit(i, br, rgbLerp(base, accent, (uint8_t)(mix / 3u)));
    }

    // Head marker.
    if (filled < n) {
      ringHit(filled, 255u, accent);
    } else if (n > 0) {
      // End flourish for the last ~350ms: sparkle each segment CW.
      if (elapsed + 350u >= totalMs) {
        const uint16_t k = (uint16_t)((elapsed / 60u) % n);
        ringHit(k, 255u, Rgb{255, 255, 255});
      }
    }

    if (NEO_IDLE_GLOW_ENABLE) applyIdleGlowFloorRing();
    ringShow();
  }

  void renderSdIndexing(uint32_t now) {
    // A smooth "loading wheel" in SD colors.
    const Rgb base = {NEO_SD_RGB};
    const Rgb accent = accentForBase(base);
    renderCometSmooth(now, 520u, 420u, 0u, 90u, base, accent, true);
  }

  void renderStopCue(uint32_t now) {
    // Stop = decelerate from play cadence -> STOP (static hold) -> fade out.
    if (!_stopActive) return;

    const Rgb base = _stopRgb;
    const Rgb accent = accentForBase(base);

    if ((int32_t)(now - _stopDecelEndMs) < 0) {
      const uint32_t span = (_stopDecelEndMs > _stopStartMs) ? (_stopDecelEndMs - _stopStartMs) : 1u;
      const uint32_t e = (now > _stopStartMs) ? (now - _stopStartMs) : 0u;
      const uint32_t t = (e >= span) ? 255u : (e * 255u / span); // 0..255
      const uint32_t basePer = (uint32_t)NEO_PLAY_PERIOD_MS;
      const uint32_t per = basePer + (basePer * 2u * t) / 255u; // 1x -> 3x slower

      if (_px.numPixels() <= 1) {
        renderPulse(now, base, per, 0u, 70u);
        return;
      }

      renderCometSmooth(now, per, (uint16_t)(NEO_PLAY_RING_FADE_MS + 200u), 0u, 55u, base, accent, true);
      return;
    }

    if ((int32_t)(now - _stopHoldEndMs) < 0) {
      // Static hold: keep a steady ring briefly.
      const uint8_t br = clampU8(22 * 255 / 100);
      if (_px.numPixels() > 1) {
        ringDecay(220u);
        const uint16_t n = (uint16_t)_px.numPixels();
        for (uint16_t i = 0; i < n && i < kMaxRingPixels; i++) {
          ringHit(i, br, base);
        }
        if (NEO_IDLE_GLOW_ENABLE) applyIdleGlowFloorRing();
        ringShow();
        return;
      }
      pixelDecay(220u);
      _pxR = (uint8_t)(((uint16_t)_pxR > (uint16_t)scale8(base.r, br) ? _pxR : scale8(base.r, br)));
      _pxG = (uint8_t)(((uint16_t)_pxG > (uint16_t)scale8(base.g, br) ? _pxG : scale8(base.g, br)));
      _pxB = (uint8_t)(((uint16_t)_pxB > (uint16_t)scale8(base.b, br) ? _pxB : scale8(base.b, br)));
      if (NEO_IDLE_GLOW_ENABLE) applyIdleGlowFloorPixel();
      pixelShow();
      return;
    }

    // Fade-out: no new energy, just decay to idle/off.
    if (_px.numPixels() > 1) {
      ringDecay((uint16_t)NEO_MEDIA_STOP_FADE_MS);
      if (NEO_IDLE_GLOW_ENABLE) applyIdleGlowFloorRing();
      ringShow();
      return;
    }
    pixelDecay((uint16_t)NEO_MEDIA_STOP_FADE_MS);
    if (NEO_IDLE_GLOW_ENABLE) applyIdleGlowFloorPixel();
    pixelShow();
  }

  void renderSleepCue(uint32_t now) {
    // Calm "breathing" pulses across all pixels, then sleep fade handles the rest.
    const uint32_t e = (now > _sleepCueStartMs) ? (now - _sleepCueStartMs) : 0u;

    uint8_t br = 0;
    uint32_t t = e;
    bool inPulse = false;
    uint32_t pulseMs = 0;
    uint32_t inPulseMs = 0;
    const uint32_t pulses = (_sleepCuePulses == 0) ? (uint32_t)NEO_SLEEP_PULSES : (uint32_t)_sleepCuePulses;
    const uint32_t baseMs = (_sleepCueBaseMs == 0) ? (uint32_t)NEO_SLEEP_CUE_BASE_MS : _sleepCueBaseMs;
    const uint32_t stepMs = _sleepCueStepMs;
    const uint32_t gapMs  = _sleepCueGapMs;
    for (uint32_t i = 0; i < pulses; i++) {
      const uint32_t dur = baseMs + i * stepMs;
      if (t < dur) {
        inPulse = true;
        pulseMs = dur;
        inPulseMs = t;
        break;
      }
      t -= dur;
      if (i + 1u < pulses) {
        if (t < gapMs) {
          inPulse = false;
          break;
        }
        t -= gapMs;
      }
    }

    if (inPulse && pulseMs > 0u) {
      const uint8_t minBr = 0u;
      const uint8_t maxBr = clampU8(70 * 255 / 100);
      const uint32_t riseMs = (uint32_t)NEO_SLEEP_CUE_RISE_MS;
      uint8_t tri = 0;
      if (riseMs == 0u || pulseMs <= riseMs) {
        // Fallback to symmetric triangle if rise is invalid.
        uint32_t t2 = (inPulseMs * 510u) / pulseMs;
        if (t2 > 255u) t2 = 510u - t2;
        tri = (uint8_t)t2;
      } else if (inPulseMs < riseMs) {
        // Constant rise time for every pulse.
        tri = (uint8_t)((inPulseMs * 255u) / riseMs);
      } else {
        // Variable-length decay (pulseMs - riseMs) stretches with each pulse.
        const uint32_t fallMs = pulseMs - riseMs;
        const uint32_t f = inPulseMs - riseMs;
        tri = (fallMs == 0u) ? 0u : (uint8_t)(255u - (f * 255u) / fallMs);
      }

      const uint8_t g = _px.gamma8(tri);
      const uint16_t span = (uint16_t)(maxBr - minBr);
      br = (uint8_t)(minBr + (uint16_t)g * span / 255u);
    }

    const Rgb c = {NEO_SLEEP_RGB};
    const uint8_t r = scale8(c.r, br);
    const uint8_t g = scale8(c.g, br);
    const uint8_t b = scale8(c.b, br);

    if (_px.numPixels() > 1) {
      ringDecay(240u);
      const uint16_t n = (uint16_t)_px.numPixels();
      for (uint16_t i = 0; i < n && i < kMaxRingPixels; i++) {
        if (r > _ringR[i]) _ringR[i] = r;
        if (g > _ringG[i]) _ringG[i] = g;
        if (b > _ringB[i]) _ringB[i] = b;
      }
      ringShow();
      return;
    }

    pixelDecay(240u);
    if (r > _pxR) _pxR = r;
    if (g > _pxG) _pxG = g;
    if (b > _pxB) _pxB = b;
    pixelShow();
  }

  void renderSleepFade(uint32_t now) {
    // Calm fade down to idle glow (or off).
    const uint32_t span = (_sleepFadeUntilMs > _sleepFadeStartMs) ? (_sleepFadeUntilMs - _sleepFadeStartMs) : 1u;
    const uint32_t e = (now > _sleepFadeStartMs) ? (now - _sleepFadeStartMs) : 0u;
    const uint8_t t = (uint8_t)((e >= span) ? 255u : (uint32_t)e * 255u / span); // 0->255
    const uint8_t inv = (uint8_t)(255u - t);
    const uint8_t br = _px.gamma8(inv);

    if (_px.numPixels() > 1) {
      ringDecay((uint16_t)NEO_RING_FADE_MS);
      // Apply static color at decreasing brightness.
      const uint8_t r = scale8(_sleepFadeRgb.r, br);
      const uint8_t g = scale8(_sleepFadeRgb.g, br);
      const uint8_t b = scale8(_sleepFadeRgb.b, br);
      for (uint16_t i = 0; i < (uint16_t)_px.numPixels() && i < kMaxRingPixels; i++) {
        if (r > _ringR[i]) _ringR[i] = r;
        if (g > _ringG[i]) _ringG[i] = g;
        if (b > _ringB[i]) _ringB[i] = b;
      }
      if (NEO_IDLE_GLOW_ENABLE) applyIdleGlowFloorRing();
      ringShow();
      return;
    }

    pixelDecay((uint16_t)NEO_RING_FADE_MS);
    _pxR = scale8(_sleepFadeRgb.r, br);
    _pxG = scale8(_sleepFadeRgb.g, br);
    _pxB = scale8(_sleepFadeRgb.b, br);
    if (NEO_IDLE_GLOW_ENABLE) applyIdleGlowFloorPixel();
    pixelShow();
  }

  void renderPodcastIndexing(uint32_t now) {
    if (_px.numPixels() <= 1) {
      renderPulse(now, {0, 255, 255}, 900u, 0u, 80u);
      return;
    }
    // Progress-aware rainbow bar so it's clearly "indexing".
    uint16_t cur = 0, total = 0;
    char show[64];
    show[0] = '\0';
    podcasts_getProgress(&cur, &total, show, sizeof(show));

    const uint16_t n = (uint16_t)_px.numPixels();
    const uint8_t maxBr = clampU8((int)NEO_POD_INDEX_MAX_PCT * 255 / 100);
    const uint8_t minBr = clampU8((int)NEO_POD_INDEX_MIN_PCT * 255 / 100);
    const uint16_t filled = (total > 0) ? (uint16_t)((uint32_t)cur * (uint32_t)n / (uint32_t)total) : 0u;

    ringDecay((uint16_t)NEO_RING_FADE_MS);
    const uint16_t baseHue = (uint16_t)((uint32_t)now * 96u);
    const uint16_t stepHue = (uint16_t)(65535u / (n ? n : 1u));
    for (uint16_t i = 0; i < n && i < kMaxRingPixels; i++) {
      const bool on = (i < filled);
      const uint8_t br = on ? maxBr : minBr;
      ringHit(i, br, hsvToRgb((uint16_t)(baseHue + (uint16_t)(i * stepHue)), 255u, 255u));
    }
    // Head marker (distinct) at the current position.
    if (filled < n) {
      ringHit(filled, maxBr, Rgb{255, 255, 255});
    }
    ringShow();
  }

  uint16_t seqPulseMsDefault() const {
    // For rings: pulses become "spins".
    return (_px.numPixels() > 1) ? (uint16_t)NEO_SPIN_PERIOD_MS : (uint16_t)NEO_SEQ_PULSE_MS;
  }

  bool renderSeq(uint32_t now) {
    if (!_seqActive) return false;
    if ((int32_t)(now - _seqStartMs) < 0) {
    if (_px.numPixels() > 1) {
      ringDecay((uint16_t)NEO_RING_FADE_MS);
      ringShow();
      return true;
    }
    pixelDecay((uint16_t)NEO_RING_FADE_MS);
    pixelShow();
      return true;
    }
    const uint32_t elapsed = (uint32_t)(now - _seqStartMs);
    // Ring: make multi-spin sequences continuous (no stop/start between spins).
    const uint32_t gap = (_px.numPixels() > 1) ? 0u : (uint32_t)_seqGapMs;
    const uint32_t cycle = (uint32_t)_seqPulseMs + gap;
    if (cycle == 0) { _seqActive = false; return false; }

    const uint32_t idx = elapsed / cycle;
    if (idx >= _seqCount) {
      _seqActive = false;
      return false;
    }

    const uint32_t phase = elapsed % cycle;
    if (_px.numPixels() <= 1 && phase >= _seqPulseMs) {
      // Gap (single pixel only): keep LED off (true "separated" pulses).
      _px.clear();
      _px.show();
      return true;
    }

    if (_px.numPixels() <= 1) {
      const uint8_t minBr = clampU8((int)_seqMinPct * 255 / 100);
      const uint8_t maxBr = clampU8((int)_seqMaxPct * 255 / 100);
      const uint8_t br = breathePhase(_px, phase, _seqPulseMs, minBr, maxBr);
      _px.setPixelColor(0, _px.Color(scale8(_seqRgb.r, br), scale8(_seqRgb.g, br), scale8(_seqRgb.b, br)));
      _px.show();
      return true;
    }

    // Ring: "pulse" == one spin. Smoothly fade between adjacent pixels.
    const uint16_t n = (uint16_t)_px.numPixels();
    const uint8_t maxBr = clampU8((int)_seqMaxPct * 255 / 100);

    // Fixed-point position: 0..(n<<16)
    const uint32_t pos16 = (uint32_t)((uint64_t)(phase % _seqPulseMs) * (uint64_t)n * 65536ULL / (uint64_t)_seqPulseMs);
    uint16_t i0 = (uint16_t)((pos16 >> 16) % n);
    const uint16_t frac = (uint16_t)(pos16 & 0xFFFFu);
    uint16_t i1 = (uint16_t)((i0 + 1u) % n);
    bool cw = _seqClockwise;
    if (_seqStyle == SeqStyle::SolidWiggle) {
      // Alternate direction each spin.
      if ((idx & 1u) != 0u) cw = !cw;
    }
    if (_seqStyle == SeqStyle::RainbowScanner || _seqStyle == SeqStyle::SolidScanner) {
      const uint32_t half = (uint32_t)_seqPulseMs / 2u;
      if (half > 0 && (phase % _seqPulseMs) >= half) cw = !cw;
    }
    if (!cw) {
      i0 = (uint16_t)((n - 1u - i0) % n);
      i1 = (uint16_t)((n - 1u - i1) % n);
    }

    // Linear weights -> gamma -> scale by maxBr.
    uint8_t w0 = (uint8_t)((uint32_t)(65535u - frac) >> 8); // ~0..255
    uint8_t w1 = (uint8_t)((uint32_t)frac >> 8);
    w0 = _px.gamma8(w0);
    w1 = _px.gamma8(w1);
    const uint8_t br0 = (uint8_t)(((uint16_t)w0 * (uint16_t)maxBr + 127u) / 255u);
    const uint8_t br1 = (uint8_t)(((uint16_t)w1 * (uint16_t)maxBr + 127u) / 255u);

    ringDecay((uint16_t)NEO_RING_FADE_MS);
    // NOTE: Keep BT indicators blue and avoid "everything is rainbow" by making
    // rainbow a per-style decision (global rainbow is used for now-playing/indexing only).
    const bool wantRainbow = (_seqStyle == SeqStyle::RainbowDual) || (_seqStyle == SeqStyle::RainbowTriple) || (_seqStyle == SeqStyle::RainbowStrobe) || (_seqStyle == SeqStyle::RainbowScanner) || (_seqStyle == SeqStyle::GreenWave);
    if (wantRainbow) {
      uint16_t baseHue = (uint16_t)((uint32_t)now * 24u); // slower hue motion = less shimmer
      if (_seqStyle == SeqStyle::GreenWave) {
        // Focused around "green" (1/3 of the hue wheel).
        baseHue = (uint16_t)(21845u + (uint16_t)((uint32_t)now * 32u));
      }
      const uint16_t stepHue = (uint16_t)(65535u / (n ? n : 1u));
      auto hitRainbow = [&](uint16_t idx, uint8_t br, uint16_t hueBias) {
        ringHit(idx, br, hsvToRgb((uint16_t)(baseHue + (uint16_t)(idx * stepHue) + hueBias), 255u, 255u));
      };

      const uint32_t pms = (uint32_t)(phase % _seqPulseMs);
      if (_seqStyle == SeqStyle::RainbowStrobe && pms < 55u) {
        // Recognizable "flash" at the start of each spin.
        ringHit(i0, maxBr, Rgb{255, 255, 255});
        ringHit(i1, maxBr, Rgb{255, 255, 255});
      } else {
        hitRainbow(i0, br0, 0u);
        hitRainbow(i1, br1, 4000u);
      }

      // Add extra comets depending on style.
      if (_seqStyle == SeqStyle::RainbowTriple || _seqStyle == SeqStyle::GreenWave) {
        const uint16_t a = (uint16_t)((n / 3u) ? (n / 3u) : 1u);
        hitRainbow((uint16_t)((i0 + a) % n), br0, 12000u);
        hitRainbow((uint16_t)((i1 + a) % n), br1, 12000u);
        hitRainbow((uint16_t)((i0 + 2u * a) % n), br0, 24000u);
        hitRainbow((uint16_t)((i1 + 2u * a) % n), br1, 24000u);
      } else {
        const uint16_t o0 = (uint16_t)((i0 + (n / 2u)) % n);
        const uint16_t o1 = (uint16_t)((i1 + (n / 2u)) % n);
        hitRainbow(o0, br0, 32768u);
        hitRainbow(o1, br1, 32768u);
      }
    } else if (_seqStyle == SeqStyle::DualComplement) {
      const Rgb c2 = rgbComplement(_seqRgb);
      const uint16_t o0 = (uint16_t)((i0 + (n / 2u)) % n);
      const uint16_t o1 = (uint16_t)((i1 + (n / 2u)) % n);
      ringHit(i0, br0, _seqRgb);
      ringHit(i1, br1, _seqRgb);
      ringHit(o0, br0, c2);
      ringHit(o1, br1, c2);
    } else if (_seqStyle == SeqStyle::SolidStrobe) {
      // Base-color "pop" at the start of each spin.
      const uint32_t pms = (uint32_t)(phase % _seqPulseMs);
      if (pms < 60u) {
        ringHit(i0, maxBr, _seqRgb);
        ringHit(i1, maxBr, _seqRgb);
      } else {
        ringHit(i0, br0, _seqRgb);
        ringHit(i1, br1, _seqRgb);
      }
    } else if (_seqStyle == SeqStyle::SolidDualFixed) {
      ringHit(i0, br0, _seqRgb);
      ringHit(i1, br1, _seqRgb);
      const uint16_t o0 = (uint16_t)((i0 + (n / 2u)) % n);
      const uint16_t o1 = (uint16_t)((i1 + (n / 2u)) % n);
      ringHit(o0, (uint8_t)((uint16_t)br0 * 2u / 3u), _seqAccent);
      ringHit(o1, (uint8_t)((uint16_t)br1 * 2u / 3u), _seqAccent);
    } else {
      // Consistent multi-color: primary event color + an accent opposite.
      ringHit(i0, br0, _seqRgb);
      ringHit(i1, br1, _seqRgb);
      if (_seqKind != SeqKind::Volume) {
        const Rgb a = accentForBase(_seqRgb);
        const uint16_t o0 = (uint16_t)((i0 + (n / 2u)) % n);
        const uint16_t o1 = (uint16_t)((i1 + (n / 2u)) % n);
        ringHit(o0, (uint8_t)((uint16_t)br0 * 2u / 3u), a);
        ringHit(o1, (uint8_t)((uint16_t)br1 * 2u / 3u), a);
      }
    }
    ringShow();
    return true;
  }

  void renderPulse(uint32_t now, Rgb rgb, uint32_t periodMs, uint8_t minPct, uint8_t maxPct) {
    // For a ring: steady "pulse" becomes a continuous spin.
    if (_px.numPixels() > 1) {
      const uint16_t n = (uint16_t)_px.numPixels();
      const uint8_t maxBr = clampU8((int)maxPct * 255 / 100);

      const uint32_t pos16 = (uint32_t)((uint64_t)(now % periodMs) * (uint64_t)n * 65536ULL / (uint64_t)periodMs);
      const uint16_t i0 = (uint16_t)((pos16 >> 16) % n);
      const uint16_t frac = (uint16_t)(pos16 & 0xFFFFu);
      const uint16_t i1 = (uint16_t)((i0 + 1u) % n);

      uint8_t w0 = (uint8_t)((uint32_t)(65535u - frac) >> 8);
      uint8_t w1 = (uint8_t)((uint32_t)frac >> 8);
      w0 = _px.gamma8(w0);
      w1 = _px.gamma8(w1);
      const uint8_t br0 = (uint8_t)(((uint16_t)w0 * (uint16_t)maxBr + 127u) / 255u);
      const uint8_t br1 = (uint8_t)(((uint16_t)w1 * (uint16_t)maxBr + 127u) / 255u);

      // Keep BT indicators pure blue and avoid global-rainbow affecting all modes:
      // continuous pulses always use the provided color.
      ringDecay((uint16_t)NEO_RING_FADE_MS);
      ringHit(i0, br0, rgb);
      ringHit(i1, br1, rgb);
      // Add a second shade of blue opposite for richer BT look.
      const Rgb a = accentForBase(rgb);
      const uint16_t o0 = (uint16_t)((i0 + (n / 2u)) % n);
      const uint16_t o1 = (uint16_t)((i1 + (n / 2u)) % n);
      ringHit(o0, (uint8_t)((uint16_t)br0 * 2u / 3u), a);
      ringHit(o1, (uint8_t)((uint16_t)br1 * 2u / 3u), a);
      ringShow();
      return;
    }

    // Single pixel: Convert pct 0..100 to 0..255.
    const uint8_t minBr = clampU8((int)minPct * 255 / 100);
    const uint8_t maxBr = clampU8((int)maxPct * 255 / 100);
    const uint8_t br = breathePhase(_px, now, periodMs, minBr, maxBr);
    _pxR = scale8(rgb.r, br);
    _pxG = scale8(rgb.g, br);
    _pxB = scale8(rgb.b, br);
    pixelShow();
  }

  // Two-tone pulse/spin: primary color on the moving head, secondary color opposite.
  // Used for BT so all BT events share the same 2-color palette.
  void renderPulseDual(uint32_t now, Rgb primary, Rgb secondary, uint32_t periodMs, uint8_t minPct, uint8_t maxPct) {
    if (_px.numPixels() > 1) {
      const uint16_t n = (uint16_t)_px.numPixels();
      const uint8_t maxBr = clampU8((int)maxPct * 255 / 100);
      if (periodMs == 0 || n == 0) return;

      const uint32_t pos16 = (uint32_t)((uint64_t)(now % periodMs) * (uint64_t)n * 65536ULL / (uint64_t)periodMs);
      const uint16_t i0 = (uint16_t)((pos16 >> 16) % n);
      const uint16_t frac = (uint16_t)(pos16 & 0xFFFFu);
      const uint16_t i1 = (uint16_t)((i0 + 1u) % n);

      uint8_t w0 = (uint8_t)((uint32_t)(65535u - frac) >> 8);
      uint8_t w1 = (uint8_t)((uint32_t)frac >> 8);
      w0 = _px.gamma8(w0);
      w1 = _px.gamma8(w1);
      const uint8_t br0 = (uint8_t)(((uint16_t)w0 * (uint16_t)maxBr + 127u) / 255u);
      const uint8_t br1 = (uint8_t)(((uint16_t)w1 * (uint16_t)maxBr + 127u) / 255u);

      ringDecay((uint16_t)NEO_RING_FADE_MS);
      ringHit(i0, br0, primary);
      ringHit(i1, br1, primary);
      const uint16_t o0 = (uint16_t)((i0 + (n / 2u)) % n);
      const uint16_t o1 = (uint16_t)((i1 + (n / 2u)) % n);
      ringHit(o0, (uint8_t)((uint16_t)br0 * 2u / 3u), secondary);
      ringHit(o1, (uint8_t)((uint16_t)br1 * 2u / 3u), secondary);
      ringShow();
      return;
    }
    // Single pixel: just use the primary color (no “opposite”).
    renderPulse(now, primary, periodMs, minPct, maxPct);
  }

  void ringDecay(uint16_t fadeMs) {
    const uint16_t n = (uint16_t)_px.numPixels();
    if (n == 0) return;
    const uint16_t lim = (n <= kMaxRingPixels) ? n : kMaxRingPixels;

    // Linear decay per frame based on fade time.
    const uint32_t dt = (_dtMs == 0) ? 12u : _dtMs;
    uint8_t mul = 0;
    if (fadeMs == 0) mul = 0;
    else {
      const uint32_t drop = (255u * dt) / (uint32_t)fadeMs;
      mul = (drop >= 255u) ? 0u : (uint8_t)(255u - drop);
    }
    for (uint16_t i = 0; i < lim; i++) {
      if (mul == 0u) {
        _ringR[i] = _ringG[i] = _ringB[i] = 0u;
        _ringRRem[i] = _ringGRem[i] = _ringBRem[i] = 0u;
        continue;
      }

      // Fixed-point multiply with remainder carry to smooth low-level fades.
      // t = v*mul + rem; v' = t/255; rem' = t%255
      {
        const uint32_t t = (uint32_t)_ringR[i] * (uint32_t)mul + (uint32_t)_ringRRem[i];
        _ringR[i] = (uint8_t)(t / 255u);
        _ringRRem[i] = (uint8_t)(t % 255u);
      }
      {
        const uint32_t t = (uint32_t)_ringG[i] * (uint32_t)mul + (uint32_t)_ringGRem[i];
        _ringG[i] = (uint8_t)(t / 255u);
        _ringGRem[i] = (uint8_t)(t % 255u);
      }
      {
        const uint32_t t = (uint32_t)_ringB[i] * (uint32_t)mul + (uint32_t)_ringBRem[i];
        _ringB[i] = (uint8_t)(t / 255u);
        _ringBRem[i] = (uint8_t)(t % 255u);
      }
    }
  }

  void ringHit(uint16_t idx, uint8_t br, Rgb rgb) {
    const uint16_t n = (uint16_t)_px.numPixels();
    if (n == 0) return;
    if (n > kMaxRingPixels) return; // safety: avoid out-of-bounds
    if (idx >= n) return;
    // Store intensity directly so OFF transitions always fade smoothly and
    // multi-color trails are possible.
    const uint8_t r = scale8(rgb.r, br);
    const uint8_t g = scale8(rgb.g, br);
    const uint8_t b = scale8(rgb.b, br);
    if (r > _ringR[idx]) { _ringR[idx] = r; _ringRRem[idx] = 0u; }
    if (g > _ringG[idx]) { _ringG[idx] = g; _ringGRem[idx] = 0u; }
    if (b > _ringB[idx]) { _ringB[idx] = b; _ringBRem[idx] = 0u; }
  }

  void applyIdleGlowFloorRing(Rgb c = {NEO_IDLE_GLOW_RGB}, uint8_t pct = (uint8_t)NEO_IDLE_GLOW_PCT) {
    const uint16_t n = (uint16_t)_px.numPixels();
    if (n == 0 || n > kMaxRingPixels) return;
    const uint8_t br = clampU8((int)pct * 255 / 100);
    const uint8_t fr = scale8(c.r, br);
    const uint8_t fg = scale8(c.g, br);
    const uint8_t fb = scale8(c.b, br);
    for (uint16_t i = 0; i < n; i++) {
      if (fr > _ringR[i]) { _ringR[i] = fr; _ringRRem[i] = 0u; }
      if (fg > _ringG[i]) { _ringG[i] = fg; _ringGRem[i] = 0u; }
      if (fb > _ringB[i]) { _ringB[i] = fb; _ringBRem[i] = 0u; }
    }
  }

  void applyIdleGlowFloorPixel(Rgb c = {NEO_IDLE_GLOW_RGB}, uint8_t pct = (uint8_t)NEO_IDLE_GLOW_PCT) {
    const uint8_t br = clampU8((int)pct * 255 / 100);
    const uint8_t fr = scale8(c.r, br);
    const uint8_t fg = scale8(c.g, br);
    const uint8_t fb = scale8(c.b, br);
    if (fr > _pxR) { _pxR = fr; _pxRRem = 0u; }
    if (fg > _pxG) { _pxG = fg; _pxGRem = 0u; }
    if (fb > _pxB) { _pxB = fb; _pxBRem = 0u; }
  }

  void ringShow() {
    const uint16_t n = (uint16_t)_px.numPixels();
    if (n == 0) return;
    if (n > kMaxRingPixels) {
      // Fallback: just show a single pixel (shouldn't happen with 8px ring).
      _px.clear();
      _px.show();
      return;
    }
    for (uint16_t i = 0; i < n; i++) {
      _px.setPixelColor(i, _px.Color(_ringR[i], _ringG[i], _ringB[i]));
    }
    _px.show();
  }

  void pixelDecay(uint16_t fadeMs) {
    const uint32_t dt = (_dtMs == 0) ? 12u : _dtMs;
    uint8_t mul = 0;
    if (fadeMs == 0) mul = 0;
    else {
      const uint32_t drop = (255u * dt) / (uint32_t)fadeMs;
      mul = (drop >= 255u) ? 0u : (uint8_t)(255u - drop);
    }
    if (mul == 0u) {
      _pxR = _pxG = _pxB = 0u;
      _pxRRem = _pxGRem = _pxBRem = 0u;
      return;
    }
    {
      const uint32_t t = (uint32_t)_pxR * (uint32_t)mul + (uint32_t)_pxRRem;
      _pxR = (uint8_t)(t / 255u);
      _pxRRem = (uint8_t)(t % 255u);
    }
    {
      const uint32_t t = (uint32_t)_pxG * (uint32_t)mul + (uint32_t)_pxGRem;
      _pxG = (uint8_t)(t / 255u);
      _pxGRem = (uint8_t)(t % 255u);
    }
    {
      const uint32_t t = (uint32_t)_pxB * (uint32_t)mul + (uint32_t)_pxBRem;
      _pxB = (uint8_t)(t / 255u);
      _pxBRem = (uint8_t)(t % 255u);
    }
  }

  void pixelShow() {
    _px.setPixelColor(0, _px.Color(_pxR, _pxG, _pxB));
    _px.show();
  }

  void renderRainbowComet(uint32_t now, uint32_t periodMs, uint16_t fadeMs, uint8_t minPct, uint8_t maxPct) {
    const uint16_t n = (uint16_t)_px.numPixels();
    if (n == 0 || n > kMaxRingPixels) return;
    if (periodMs == 0) periodMs = 1;

    const uint8_t maxBr = clampU8((int)maxPct * 255 / 100);
    const uint8_t minBr = clampU8((int)minPct * 255 / 100);

    // Fixed-point position around ring.
    const uint32_t pos16 = (uint32_t)((uint64_t)(now % periodMs) * (uint64_t)n * 65536ULL / (uint64_t)periodMs);
    const uint16_t i0 = (uint16_t)((pos16 >> 16) % n);
    const uint16_t frac = (uint16_t)(pos16 & 0xFFFFu);
    const uint16_t i1 = (uint16_t)((i0 + 1u) % n);

    uint8_t w0 = (uint8_t)((uint32_t)(65535u - frac) >> 8); // ~0..255
    uint8_t w1 = (uint8_t)((uint32_t)frac >> 8);
    w0 = _px.gamma8(w0);
    w1 = _px.gamma8(w1);

    const uint8_t br0 = (uint8_t)(minBr + (uint16_t)w0 * (uint16_t)(maxBr - minBr) / 255u);
    const uint8_t br1 = (uint8_t)(minBr + (uint16_t)w1 * (uint16_t)(maxBr - minBr) / 255u);

    // Hue advances smoothly with time, and we also offset by pixel index for "more color".
    const uint16_t baseHue = (uint16_t)((uint32_t)now * 64u); // speed knob (empirical)
    const uint16_t stepHue = (uint16_t)(65535u / (n ? n : 1u));

    ringDecay(fadeMs);
    ringHit(i0, br0, hsvToRgb((uint16_t)(baseHue + (uint16_t)(i0 * stepHue)), 255u, 255u));
    ringHit(i1, br1, hsvToRgb((uint16_t)(baseHue + (uint16_t)(i1 * stepHue)), 255u, 255u));

    // Optional opposite comet for extra richness.
    const uint16_t o0 = (uint16_t)((i0 + (n / 2u)) % n);
    const uint16_t o1 = (uint16_t)((i1 + (n / 2u)) % n);
    ringHit(o0, br0, hsvToRgb((uint16_t)(baseHue + 32768u + (uint16_t)(o0 * stepHue)), 255u, 255u));
    ringHit(o1, br1, hsvToRgb((uint16_t)(baseHue + 32768u + (uint16_t)(o1 * stepHue)), 255u, 255u));

    ringShow();
  }

  void renderCometSmooth(uint32_t now, uint32_t periodMs, uint16_t fadeMs, uint8_t minPct, uint8_t maxPct, Rgb base, Rgb accent, bool clockwise) {
    const uint16_t n = (uint16_t)_px.numPixels();
    if (n == 0 || n > kMaxRingPixels) return;
    if (periodMs == 0) periodMs = 1;

    const uint8_t maxBr = clampU8((int)maxPct * 255 / 100);
    const uint8_t minBr = clampU8((int)minPct * 255 / 100);

    // Fixed-point position around ring.
    const uint32_t pos16 = (uint32_t)((uint64_t)(now % periodMs) * (uint64_t)n * 65536ULL / (uint64_t)periodMs);
    uint16_t i0 = (uint16_t)((pos16 >> 16) % n);
    const uint16_t frac = (uint16_t)(pos16 & 0xFFFFu);
    uint16_t i1 = (uint16_t)((i0 + 1u) % n);

    if (!clockwise) {
      i0 = (uint16_t)((n - 1u - i0) % n);
      i1 = (uint16_t)((n - 1u - i1) % n);
    }

    // Main weights.
    uint8_t w0 = (uint8_t)((uint32_t)(65535u - frac) >> 8);
    uint8_t w1 = (uint8_t)((uint32_t)frac >> 8);
    w0 = _px.gamma8(w0);
    w1 = _px.gamma8(w1);

    const uint8_t br0 = (uint8_t)(minBr + (uint16_t)w0 * (uint16_t)(maxBr - minBr) / 255u);
    const uint8_t br1 = (uint8_t)(minBr + (uint16_t)w1 * (uint16_t)(maxBr - minBr) / 255u);

    // Neighbor pixels for extra smoothness.
    const uint16_t im1 = (uint16_t)((i0 + n - 1u) % n);
    const uint16_t ip2 = (uint16_t)((i1 + 1u) % n);
    const uint16_t im2 = (uint16_t)((i0 + n - 2u) % n);
    const uint16_t ip3 = (uint16_t)((i1 + 2u) % n);
    const uint8_t brm1 = (uint8_t)((uint16_t)br0 / 2u);
    const uint8_t brp2 = (uint8_t)((uint16_t)br1 / 2u);
    const uint8_t brm2 = (uint8_t)((uint16_t)br0 / 5u);
    const uint8_t brp3 = (uint8_t)((uint16_t)br1 / 5u);

    ringDecay(fadeMs);

    // Primary comet uses base color.
    ringHit(im2, brm2, base);
    ringHit(im1, brm1, base);
    ringHit(i0, br0, base);
    ringHit(i1, br1, base);
    ringHit(ip2, brp2, base);
    ringHit(ip3, brp3, base);

    // Secondary comet opposite uses accent (dimmed).
    const uint16_t o0 = (uint16_t)((i0 + (n / 2u)) % n);
    const uint16_t o1 = (uint16_t)((i1 + (n / 2u)) % n);
    const uint16_t om1 = (uint16_t)((im1 + (n / 2u)) % n);
    const uint16_t op2 = (uint16_t)((ip2 + (n / 2u)) % n);
    const uint16_t om2 = (uint16_t)((im2 + (n / 2u)) % n);
    const uint16_t op3 = (uint16_t)((ip3 + (n / 2u)) % n);
    ringHit(om2, (uint8_t)((uint16_t)brm2 * 2u / 3u), accent);
    ringHit(om1, (uint8_t)((uint16_t)brm1 * 2u / 3u), accent);
    ringHit(o0, (uint8_t)((uint16_t)br0 * 2u / 3u), accent);
    ringHit(o1, (uint8_t)((uint16_t)br1 * 2u / 3u), accent);
    ringHit(op2, (uint8_t)((uint16_t)brp2 * 2u / 3u), accent);
    ringHit(op3, (uint8_t)((uint16_t)brp3 * 2u / 3u), accent);

    ringShow();
  }
};

static NeoStatusPlugin s_plugin;

} // namespace

void neostatusPluginInit() {
  #if NEO_STATUS_ENABLE
    pm.add(&s_plugin);
  #endif
}

void neostatusPulseSleep() {
  #if NEO_STATUS_ENABLE
    s_plugin.pulseSleep();
  #endif
}

void neostatusPulseLowBattery() {
  #if NEO_STATUS_ENABLE
    s_plugin.pulseLowBattery();
  #endif
}

uint16_t neostatusSleepCueMs() {
  #if NEO_STATUS_ENABLE
    // Match the cue sequence used by NeoStatusPlugin::pulseSleep().
    const uint32_t pulses = (uint32_t)NEO_SLEEP_PULSES;
    uint32_t total = 0;
    for (uint32_t i = 0; i < pulses; i++) {
      total += (uint32_t)NEO_SLEEP_CUE_BASE_MS + i * (uint32_t)NEO_SLEEP_CUE_STEP_MS;
      if (i + 1u < pulses) total += (uint32_t)NEO_SLEEP_CUE_GAP_MS;
    }
    // Cap so deep sleep can't be delayed forever by accidental settings.
    if (total > 5000u) total = 5000u;
    return (uint16_t)total;
  #else
    return 0;
  #endif
}

void neostatusArmForDeepSleep() {
  #if NEO_STATUS_ENABLE
    s_plugin.armForDeepSleep();
  #endif
}

void neostatusPrepareForDeepSleep() {
  #if NEO_STATUS_ENABLE
    s_plugin.prepareForDeepSleep();
  #endif
}

#else

void neostatusPluginInit() {}
void neostatusPulseSleep() {}
void neostatusPulseLowBattery() {}
uint16_t neostatusSleepCueMs() { return 0; }
void neostatusArmForDeepSleep() {}
void neostatusPrepareForDeepSleep() {}

#endif

