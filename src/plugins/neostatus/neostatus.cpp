#include "neostatus.h"

#include "../../core/options.h"

// Only build when we have an onboard NeoPixel pin.
#if defined(BUILTIN_NEOPIXEL_PIN) && (BUILTIN_NEOPIXEL_PIN != 255)

#include <Adafruit_NeoPixel.h>

#include "../../core/network.h"
#include "../../core/config.h"
#include "../../core/bt_companion.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

#ifndef NEO_STATUS_ENABLE
  #define NEO_STATUS_ENABLE 1
#endif

// Convenience aliases (requested): allow `myoptions.h` to define friendlier
// `BUILTIN_NEOPIXEL_*` knobs without having to remember internal `NEO_*` names.
#if !defined(NEO_BOOT_DELAY_MS) && defined(BUILTIN_NEOPIXEL_BOOT_DELAY_MS)
  #define NEO_BOOT_DELAY_MS BUILTIN_NEOPIXEL_BOOT_DELAY_MS
#endif
#if !defined(NEO_BOOT_RGB) && defined(BUILTIN_NEOPIXEL_BOOT_RGB)
  #define NEO_BOOT_RGB BUILTIN_NEOPIXEL_BOOT_RGB
#endif
#if !defined(NEO_NET_RGB) && defined(BUILTIN_NEOPIXEL_NET_RGB)
  #define NEO_NET_RGB BUILTIN_NEOPIXEL_NET_RGB
#endif
#if !defined(NEO_SD_RGB) && defined(BUILTIN_NEOPIXEL_SD_RGB)
  #define NEO_SD_RGB BUILTIN_NEOPIXEL_SD_RGB
#endif
#if !defined(NEO_BT_SEARCH_RGB) && defined(BUILTIN_NEOPIXEL_BT_SEARCH_RGB)
  #define NEO_BT_SEARCH_RGB BUILTIN_NEOPIXEL_BT_SEARCH_RGB
#endif
#if !defined(NEO_BT_CONN_RGB) && defined(BUILTIN_NEOPIXEL_BT_CONN_RGB)
  #define NEO_BT_CONN_RGB BUILTIN_NEOPIXEL_BT_CONN_RGB
#endif
#if !defined(NEO_RADIO_START_RGB) && defined(BUILTIN_NEOPIXEL_RADIO_START_RGB)
  #define NEO_RADIO_START_RGB BUILTIN_NEOPIXEL_RADIO_START_RGB
#endif
#if !defined(NEO_SPK_SELECT_RGB) && defined(BUILTIN_NEOPIXEL_SPK_SELECT_RGB)
  #define NEO_SPK_SELECT_RGB BUILTIN_NEOPIXEL_SPK_SELECT_RGB
#endif
#if !defined(NEO_WIFI_LOST_RGB) && defined(BUILTIN_NEOPIXEL_WIFI_LOST_RGB)
  #define NEO_WIFI_LOST_RGB BUILTIN_NEOPIXEL_WIFI_LOST_RGB
#endif
#if !defined(NEO_SLEEP_RGB) && defined(BUILTIN_NEOPIXEL_SLEEP_RGB)
  #define NEO_SLEEP_RGB BUILTIN_NEOPIXEL_SLEEP_RGB
#endif
#if !defined(NEO_LOW_BATT_RGB) && defined(BUILTIN_NEOPIXEL_LOW_BATT_RGB)
  #define NEO_LOW_BATT_RGB BUILTIN_NEOPIXEL_LOW_BATT_RGB
#endif

#ifndef BUILTIN_NEOPIXEL_STATUS_BRIGHTNESS
  // 0..255. Default to the same brightness used during boot, if available.
  #ifdef BUILTIN_NEOPIXEL_BOOT_BRIGHTNESS
    #define BUILTIN_NEOPIXEL_STATUS_BRIGHTNESS BUILTIN_NEOPIXEL_BOOT_BRIGHTNESS
  #else
    #define BUILTIN_NEOPIXEL_STATUS_BRIGHTNESS 100
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
#ifndef NEO_RADIO_START_RGB
  #define NEO_RADIO_START_RGB 255, 255, 255
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
#ifndef NEO_SEQ_PULSE_MS
  // Longer = smoother "breathing" (not a blink).
  #define NEO_SEQ_PULSE_MS 360u
#endif
#ifndef NEO_SEQ_GAP_MS
  #define NEO_SEQ_GAP_MS 120u
#endif
#ifndef NEO_SEQ_MIN_PCT
  #define NEO_SEQ_MIN_PCT 0u
#endif
#ifndef NEO_SEQ_MAX_PCT
  #define NEO_SEQ_MAX_PCT 100u
#endif

#ifndef NEO_BOOT_PULSES
  #define NEO_BOOT_PULSES 3u
#endif
#ifndef NEO_NET_PULSES
  #define NEO_NET_PULSES 3u
#endif
#ifndef NEO_SD_PULSES
  #define NEO_SD_PULSES 2u
#endif
#ifndef NEO_RADIO_START_PULSES
  #define NEO_RADIO_START_PULSES 2u
#endif
#ifndef NEO_BT_CONNECTED_PULSES
  #define NEO_BT_CONNECTED_PULSES 2u
#endif
#ifndef NEO_BT_AUDIO_PULSES
  #define NEO_BT_AUDIO_PULSES 2u
#endif
#ifndef NEO_SPK_SELECT_PULSES
  #define NEO_SPK_SELECT_PULSES 2u
#endif
#ifndef NEO_WIFI_LOST_PULSES
  #define NEO_WIFI_LOST_PULSES 3u
#endif
#ifndef NEO_SLEEP_PULSES
  #define NEO_SLEEP_PULSES 3u
#endif
#ifndef NEO_LOW_BATT_PULSES
  #define NEO_LOW_BATT_PULSES 3u
#endif

#ifndef NEO_BOOT_DELAY_MS
  // Delay boot pulses so they align with the splash/logo drawing.
  #define NEO_BOOT_DELAY_MS 2800u
#endif

enum class Mode : uint8_t {
  OFF = 0,
  BT_SEARCHING,
  BT_CONNECTED_WAIT_AUDIO,
};

struct Rgb { uint8_t r, g, b; };

static inline uint8_t clampU8(int v) {
  if (v < 0) return 0;
  if (v > 255) return 255;
  return (uint8_t)v;
}

static inline uint8_t scale8(uint8_t c, uint8_t br) {
  return (uint8_t)(((uint16_t)c * (uint16_t)br + 127u) / 255u);
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
    _px.begin();
    _px.setBrightness(BUILTIN_NEOPIXEL_STATUS_BRIGHTNESS);
    _px.clear();
    _px.show();

    _lastBt = BtCompanionLinkState::OFF;
    _lastBtEnabled = false;
    _netWasConnected = false;
    _loopSeen = false;
    _seqActive = false;
    _lastFrameMs = 0;

    // Run animations from a small dedicated task so early boot / setup-time pulses
    // don't depend on Arduino loop cadence.
    ensureTask();

    // Requested: boot should be a short pulse sequence, delayed so it lands after
    // the splash/logo draw.
    startSeq({NEO_BOOT_RGB}, NEO_BOOT_PULSES, NEO_SEQ_PULSE_MS, NEO_SEQ_GAP_MS, NEO_SEQ_MIN_PCT, NEO_SEQ_MAX_PCT, NEO_BOOT_DELAY_MS);
  }

  void on_end_setup() override {}

  void on_connect() override {
    // Requested: network connect = green pulse sequence.
    startSeq({NEO_NET_RGB}, NEO_NET_PULSES, NEO_SEQ_PULSE_MS, NEO_SEQ_GAP_MS);
  }

  void on_start_play() override {
    // Requested: white pulse when a radio (web streaming) station starts.
    // SD playback also triggers on_start_play(), so gate on mode.
    if (config.getMode() == PM_WEB) {
      startSeq({NEO_RADIO_START_RGB}, NEO_RADIO_START_PULSES, NEO_SEQ_PULSE_MS, NEO_SEQ_GAP_MS);
    } else if (config.getMode() == PM_SDCARD) {
      // Requested: SD indication should trigger on SD playback start (not indexing).
      startSeq({NEO_SD_RGB}, NEO_SD_PULSES, NEO_SEQ_PULSE_MS, NEO_SEQ_GAP_MS);
    }
  }

  void pulseSleep() {
    startSeq({NEO_SLEEP_RGB}, NEO_SLEEP_PULSES, NEO_SEQ_PULSE_MS, NEO_SEQ_GAP_MS);
  }

  void pulseLowBattery() {
    startSeq({NEO_LOW_BATT_RGB}, NEO_LOW_BATT_PULSES, NEO_SEQ_PULSE_MS, NEO_SEQ_GAP_MS);
  }

  void on_display_queue(requestParams_t& request, bool& result) override {
    (void)result;
    // SD indexing no longer triggers pulses; we indicate SD when playback starts.
    (void)request;
  }

  void on_loop() override {
    if (!NEO_STATUS_ENABLE) return;
    if (_taskHandle) return; // task owns rendering
    tickNow();
  }

  void tickNow() {
    const uint32_t now = millis();
    if (!_loopSeen) {
      _loopSeen = true;
      if (!_seqActive && _pendCount > 0) startPendingSeq(now);
    }
    // Higher update rate -> smoother breathing/pulses.
    if (_lastFrameMs != 0 && (uint32_t)(now - _lastFrameMs) < 12u) return;
    _lastFrameMs = now;

    // Wi-Fi lost / reconnecting cue: amber double pulse when we drop from CONNECTED.
    const bool netConnectedNow = (network.status == CONNECTED);
    if (_netWasConnected && !netConnectedNow) {
      startSeq({NEO_WIFI_LOST_RGB}, NEO_WIFI_LOST_PULSES, NEO_SEQ_PULSE_MS, NEO_SEQ_GAP_MS);
    }
    _netWasConnected = netConnectedNow;

    // Track BT link transitions for optional overlays.
    const bool btEnabled = btcompanion_enabled();
    const BtCompanionLinkState bt = btEnabled ? btcompanion_linkState() : BtCompanionLinkState::OFF;

    if (_lastBtEnabled && !btEnabled) {
      startSeq({NEO_SPK_SELECT_RGB}, NEO_SPK_SELECT_PULSES, NEO_SEQ_PULSE_MS, NEO_SEQ_GAP_MS);
    }
    _lastBtEnabled = btEnabled;

    if (bt != _lastBt) {
      if (bt == BtCompanionLinkState::AUDIO) {
        startSeq({NEO_BT_CONN_RGB}, NEO_BT_AUDIO_PULSES, 160u, 110u, 1u, 65u);
      }
      _lastBt = bt;
    }

    // Pulse sequences have priority over steady modes.
    if (renderSeq(now)) {
      return;
    }

    // If a sequence finished and we have pending pulses, start the next one.
    if (!_seqActive && _pendCount > 0) {
      startPendingSeq(now);
      if (renderSeq(now)) return;
    }

    const Mode m = pickMode(now, btEnabled, bt);
    renderMode(now, m, btEnabled, bt);
  }

private:
  Adafruit_NeoPixel _px{1, BUILTIN_NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800};

  BtCompanionLinkState _lastBt = BtCompanionLinkState::OFF;
  bool _lastBtEnabled = false;
  bool _netWasConnected = false;

  // Pulse-sequence engine (N smooth pulses, with gaps).
  bool _seqActive = false;
  Rgb _seqRgb{0, 0, 0};
  uint32_t _seqStartMs = 0;
  uint16_t _seqPulseMs = 0;
  uint16_t _seqGapMs = 0;
  uint8_t _seqCount = 0;
  uint8_t _seqMinPct = 0;
  uint8_t _seqMaxPct = 0;

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
  };
  PendingSeq _pend[4]{};
  uint8_t _pendHead = 0;
  uint8_t _pendTail = 0;
  uint8_t _pendCount = 0;

  uint32_t _lastFrameMs = 0;
  TaskHandle_t _taskHandle = nullptr;

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
    (void)xTaskCreatePinnedToCore(taskTrampoline, "NeoStatus", 2048, this, 1, &_taskHandle, 0);
  }

  void enqueueSeq(Rgb rgb, uint8_t pulses, uint16_t pulseMs, uint16_t gapMs, uint8_t minPct, uint8_t maxPct, uint32_t startAtMs) {
    if (pulses == 0 || pulseMs == 0) return;
    // If full, drop the oldest.
    if (_pendCount >= (uint8_t)(sizeof(_pend) / sizeof(_pend[0]))) {
      _pendHead = (uint8_t)((_pendHead + 1u) % (uint8_t)(sizeof(_pend) / sizeof(_pend[0])));
      _pendCount--;
    }
    _pend[_pendTail] = PendingSeq{rgb, pulses, pulseMs, gapMs, minPct, maxPct, startAtMs};
    _pendTail = (uint8_t)((_pendTail + 1u) % (uint8_t)(sizeof(_pend) / sizeof(_pend[0])));
    _pendCount++;
  }

  void startSeqNow(uint32_t startMs, Rgb rgb, uint8_t pulses, uint16_t pulseMs, uint16_t gapMs, uint8_t minPct, uint8_t maxPct) {
    _seqActive = (pulses > 0 && pulseMs > 0);
    _seqRgb = rgb;
    _seqStartMs = startMs;
    _seqPulseMs = pulseMs;
    _seqGapMs = gapMs;
    _seqCount = pulses;
    _seqMinPct = minPct;
    _seqMaxPct = maxPct;
  }

  void startPendingSeq(uint32_t now) {
    if (_pendCount == 0) return;
    const PendingSeq p = _pend[_pendHead];
    _pendHead = (uint8_t)((_pendHead + 1u) % (uint8_t)(sizeof(_pend) / sizeof(_pend[0])));
    _pendCount--;
    const uint32_t startAt = (p.startAtMs > now) ? p.startAtMs : now;
    startSeqNow(startAt, p.rgb, p.pulses, p.pulseMs, p.gapMs, p.minPct, p.maxPct);
  }

  void startSeq(Rgb rgb, uint8_t pulses, uint16_t pulseMs, uint16_t gapMs,
                uint8_t minPct = NEO_SEQ_MIN_PCT, uint8_t maxPct = NEO_SEQ_MAX_PCT, uint32_t delayMs = 0) {
    const uint32_t startAt = millis() + delayMs;
    if (!_loopSeen) {
      enqueueSeq(rgb, pulses, pulseMs, gapMs, minPct, maxPct, startAt);
      return;
    }
    startSeqNow(startAt, rgb, pulses, pulseMs, gapMs, minPct, maxPct);
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
        renderPulse(now, {NEO_BT_SEARCH_RGB}, NEO_BT_SEARCH_PERIOD_MS, 1, 65);
        return;
      case Mode::BT_CONNECTED_WAIT_AUDIO:
        renderPulse(now, {NEO_BT_CONN_RGB}, NEO_BT_CONNECTED_PERIOD_MS, 1, 80);
        return;
      case Mode::OFF:
      default:
        _px.clear();
        _px.show();
        return;
    }
  }

  bool renderSeq(uint32_t now) {
    if (!_seqActive) return false;
    if ((int32_t)(now - _seqStartMs) < 0) {
      _px.clear();
      _px.show();
      return true;
    }
    const uint32_t elapsed = (uint32_t)(now - _seqStartMs);
    const uint32_t cycle = (uint32_t)_seqPulseMs + (uint32_t)_seqGapMs;
    if (cycle == 0) { _seqActive = false; return false; }

    const uint32_t idx = elapsed / cycle;
    if (idx >= _seqCount) {
      _seqActive = false;
      return false;
    }

    const uint32_t phase = elapsed % cycle;
    if (phase >= _seqPulseMs) {
      // Gap: keep LED off (true "separated" pulses).
      _px.clear();
      _px.show();
      return true;
    }

    const uint8_t minBr = clampU8((int)_seqMinPct * 255 / 100);
    const uint8_t maxBr = clampU8((int)_seqMaxPct * 255 / 100);
    const uint8_t br = breathePhase(_px, phase, _seqPulseMs, minBr, maxBr);
    _px.setPixelColor(0, _px.Color(scale8(_seqRgb.r, br), scale8(_seqRgb.g, br), scale8(_seqRgb.b, br)));
    _px.show();
    return true;
  }

  void renderPulse(uint32_t now, Rgb rgb, uint32_t periodMs, uint8_t minPct, uint8_t maxPct) {
    // Convert pct 0..100 to 0..255.
    const uint8_t minBr = clampU8((int)minPct * 255 / 100);
    const uint8_t maxBr = clampU8((int)maxPct * 255 / 100);
    const uint8_t br = breathePhase(_px, now, periodMs, minBr, maxBr);
    _px.setPixelColor(0, _px.Color(scale8(rgb.r, br), scale8(rgb.g, br), scale8(rgb.b, br)));
    _px.show();
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

#else

void neostatusPluginInit() {}
void neostatusPulseSleep() {}
void neostatusPulseLowBattery() {}

#endif

