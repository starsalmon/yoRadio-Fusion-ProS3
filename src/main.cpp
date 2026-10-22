#include "Arduino.h"
#include "core/options.h"
#include "core/config.h"
#include "pluginsManager/pluginsManager.h"
#include "plugins/backlight/backlight.h" // backlight plugin
#include "plugins/ledstrip/ledstrip.h" // ledstrip plugin
#ifdef USE_NEOSTATUS_PLUGIN
  #include "plugins/neostatus/neostatus.h"
#endif
#include "core/telnet.h"
#include "core/player.h"
#include "core/display.h"
#include "core/sdpark.h"
#include "core/network.h"
#include "core/netserver.h"
#include "core/controls.h"
#include "core/bt_companion.h"
#ifdef MQTT_ROOT_TOPIC
  #include "core/mqtt.h"
#endif
#include "core/optionschecker.h"
#include "core/timekeeper.h"
#include "clock/clock_tts.h"
#include "driver/rtc_io.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "battery/battery.h"
#include "core/podcast_resume.h"
#include "core/hangdiag.h"
#include "core/webgain.h"

#ifdef USE_NEXTION
#include "displays/nextion.h"
#endif
#include "core/audiohandlers.h"
#ifdef USE_DLNA //DLNA mod
#include "network/dlna_service.h"
extern volatile bool g_dlnaPlaylistDirty;
#endif

// Mirrored output device selection for the audio engine (0=speaker, 1=BT companion).
// Kept as a simple global so `src/audioI2S/Audio.cpp` can gate optional DSP
// without including core config headers (avoids heavy dependencies in the hot path).
volatile uint8_t g_outputDeviceForAudio = 0;
// Bi-amp DSP runtime settings (mirrored from config.store).
volatile uint8_t  g_biampEnableForAudio = 0;
volatile uint8_t  g_biampLowOnLeftForAudio = 1;
volatile uint16_t g_biampCrossoverHzForAudio = 2500;

#if USE_OTA
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
#include <NetworkUdp.h>
#else
#include <WiFiUdp.h>
#endif
#include <ArduinoOTA.h>
#endif

#if DSP_HSPI || TS_HSPI || VS_HSPI
SPIClass  SPI2(HSPI);
#endif

extern __attribute__((weak)) void yoradio_on_setup();
extern void backlightPluginInit();

#if USE_OTA
void setupOTA(){
  if(strlen(config.store.mdnsname)>0)
    ArduinoOTA.setHostname(config.store.mdnsname);
#ifdef OTA_PASS
  ArduinoOTA.setPassword(OTA_PASS);
#endif
  ArduinoOTA
    .onStart([]() {
      player.sendCommand({PR_STOP, 0});
      display.putRequest(NEWMODE, UPDATING);
      telnet.printf("Start OTA updating %s\n", ArduinoOTA.getCommand() == U_FLASH?"firmware":"filesystem");
    })
    .onEnd([]() {
      telnet.printf("\nEnd OTA update, Rebooting...\n");
      ESP.restart();
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      telnet.printf("Progress OTA: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      telnet.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) {
        telnet.printf("Auth Failed\n");
      } else if (error == OTA_BEGIN_ERROR) {
        telnet.printf("Begin Failed\n");
      } else if (error == OTA_CONNECT_ERROR) {
        telnet.printf("Connect Failed\n");
      } else if (error == OTA_RECEIVE_ERROR) {
        telnet.printf("Receive Failed\n");
      } else if (error == OTA_END_ERROR) {
        telnet.printf("End Failed\n");
      }
    });
  ArduinoOTA.begin();
}
#endif

#include "IRremoteESP8266/IRrecv.h"
#include "IRremoteESP8266/IRutils.h"

extern IRrecv         irrecv;
extern decode_results irResults;

// Amp/boost enable (GPIO39 on ProS3) low as early as possible: runs during C++ static init,
// well before setup(). Releases any deep-sleep hold while keeping the level low.
#if MUTE_PIN != 255
static void __attribute__((constructor)) yoradio_early_boost_off() {
  gpio_set_direction((gpio_num_t)MUTE_PIN, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)MUTE_PIN, MUTE_VAL == HIGH ? 1 : 0);
  gpio_hold_dis((gpio_num_t)MUTE_PIN);
  // SD_CS / TFT_CS / TFT_RST have no external pull-ups: stop them floating from reset.
  // LDO2 (TFT/SD supply) is still off here, so only weak internal pull-ups (no back-powering
  // the unpowered parts through a hard HIGH); they are driven HIGH right after LDO2 is enabled.
  const gpio_num_t early[] = {(gpio_num_t)SDC_CS, (gpio_num_t)TFT_CS, (gpio_num_t)TFT_RST};
  for (gpio_num_t p : early) {
    if ((int)p < 0 || (int)p == 255) continue;
    gpio_set_direction(p, GPIO_MODE_INPUT);
    gpio_set_pull_mode(p, GPIO_PULLUP_ONLY);
  }
}
static void yoradio_boost_off_on_restart() {
  gpio_set_level((gpio_num_t)MUTE_PIN, MUTE_VAL == HIGH ? 1 : 0);
}
#endif

void setup() {
#if MUTE_PIN != 255
  player_boostHardOff(false);
  esp_register_shutdown_handler(yoradio_boost_off_on_restart);   // ESP.restart()/OTA
#endif
  Serial.begin(115200);
#ifdef YORADIO_HANG_DIAG
  hangdiag_boot();
#endif
    EEPROM.begin(EEPROM_SIZE);
  webgain_init();
#if IR_PIN != 255
    irQueue = xQueueCreate(4, sizeof(IRCommand));
    config.eepromRead(EEPROM_START_IR, config.ircodes);
    irWakeup();
#endif
#ifdef USE_LEDSTRIP_PLUGIN
  ledstripPluginInit();
#endif
#ifdef USE_NEOSTATUS_PLUGIN
  neostatusPluginInit();
#endif
#if (BRIGHTNESS_PIN!=255)
  backlightPluginInit();
#endif
  if(REAL_LEDBUILTIN!=255) pinMode(REAL_LEDBUILTIN, OUTPUT);
  if (yoradio_on_setup) yoradio_on_setup();
#if DSP_HSPI && defined(TFT_SCLK) && defined(TFT_MOSI)
  // Display (and shared SD) on SPI2 with board pins (April's Speaker: SCK 12, MOSI 13, MISO 43).
  // CS is driven by Adafruit_SPITFT; tools/pio/patch_adafruit_spitft_pins.py stops it re-running begin().
  #if defined(TFT_MISO) && (TFT_MISO >= 0)
    SPI2.begin(TFT_SCLK, TFT_MISO, TFT_MOSI);
  #else
    SPI2.begin(TFT_SCLK, -1, TFT_MOSI);
  #endif
  Serial.printf("##[BOOT]# SPI2 SCK=%d MOSI=%d CS=%d DC=%d\n", (int)TFT_SCLK, (int)TFT_MOSI, (int)TFT_CS, (int)TFT_DC);
#endif
  pm.init();     // pluginsManager
  pm.on_setup();
  config.init();
  clock_tts_setup();

#ifdef SDPARK_ENABLED
  sdpark_boot();    // SD card out of SD-native mode before the panel is initialised (see sdpark.h)
#endif
  display.init();   // main's boot order: display first

#if defined(BT_COMPANION_ENABLE) && (BT_COMPANION_ENABLE != 0)
  btcompanion_init();
#endif

  player.init();

  // Apply last output device selection (speaker vs BT companion) before any auto-play.
#ifdef BT_COMPANION_ENABLE
  player.setSpeakerForceMuted(config.store.outputDevice == 1);
#else
  player.setSpeakerForceMuted(false);
#endif
  g_outputDeviceForAudio = config.store.outputDevice;
  g_biampEnableForAudio = config.store.biampEnable;
  g_biampLowOnLeftForAudio = config.store.biampLowOnLeft;
  g_biampCrossoverHzForAudio = config.store.biampCrossoverHz;
  // Apply mono state at boot.
  player.forceMono(config.store.forceMono != 0);
  network.begin();
  if(SDC_CS!=255) {
    display.putRequest(WAITFORSD, 0);
    if (config.getMode() == PM_PODCAST) Serial.print("##[BOOT]#\tPodcast index\t");
    else Serial.print("##[BOOT]#\tSD search\t");
  }
  config.initPlaylistMode();
  netserver.begin();
  telnet.begin();
  initControls();
  startControlsTask();
  // If last output was BT, start the companion connect flow now.
#ifdef BT_COMPANION_ENABLE
  btcompanion_setEnabled(config.store.outputDevice == 1);
  // Prime companion with the current PCM sample rate (best-effort).
  btcompanion_setPcmSampleRate(player.getSampleRate());
  if (config.store.outputDevice != 1 && config.getMode() != PM_BLUETOOTH) {
    // Ensure companion isn't left streaming if ProS3 rebooted.
    // (Not when booting into BT RX: the HiGrow may still be connected to the phone.)
    btcompanion_forceSleep();
  }
#endif
  display.putRequest(DSP_START);
  while (!display.ready()) {
#if defined(BT_COMPANION_ENABLE) && (BT_COMPANION_ENABLE != 0)
    btcompanion_pump();
#endif
    delay(10);
  }
  #if USE_OTA
    if (network.status == CONNECTED || network.status == SOFT_AP) setupOTA();
  #endif
  if (config.getMode()==PM_SDCARD) player.initHeaders(config.station.url);
  player.lockOutput=false;
#if defined(BT_COMPANION_ENABLE) && (BT_COMPANION_ENABLE != 0)
  if (config.getMode() == PM_BLUETOOTH) {
    if (!player.releaseI2sBusToCompanion()) {
      Serial.println("##[WARN]# Boot BT RX: I2S release failed");
    }
    config.startBluetoothRxSession();
    config.refreshPlayModeDisplay();
  }
#endif
  if (config.store.smartstart == 1) {
    // Only auto-play when we actually have a usable playback path:
    // - CONNECTED: web streaming is possible
    // - PM_SDCARD: SD playback is possible (even if we're currently in SoftAP mode)
    if (network.status == CONNECTED || config.getMode() == PM_SDCARD) {
      if (config.getMode() != PM_BLUETOOTH) {
        player.sendCommand({PR_PLAY, config.lastStation()});
      }
    }
  }
  Audio::audio_info_callback =  my_audio_info;   // "audio_change" audiohandlers.h ban kezelve.
  pm.on_end_setup();
}

void loop() {
#ifdef YORADIO_HANG_DIAG
  hangdiag_hb("loop");
#endif
  timekeeper.loop1();
  telnet.loop();
  btcompanion_loop();
#if defined(BT_COMPANION_ENABLE) && (BT_COMPANION_ENABLE != 0)
  if (btcompanion_rxExitComplete()) {
    if (!player.reclaimI2sBusFromCompanion()) {
      Serial.println("##[WARN]# I2S bus reclaim failed after BT RX");
    }
    player.setSpeakerForceMuted(config.store.outputDevice == 1);
    delay(30);                                   // let the ProS3 I2S clock out silence first
    player.setOutputPins(player.isRunning());
  }
  // BT RX: re-evaluate the amp enable whenever "HiGrow is master + settled" changes.
  {
    static bool s_lastBtAmp = false;
    const bool btAmp = (config.getMode() == PM_BLUETOOTH) && btcompanion_rxBusReady();
    if (btAmp != s_lastBtAmp) {
      s_lastBtAmp = btAmp;
      player.setOutputPins(player.isRunning());
    }
  }
#endif
  processControlsEvents();
  // Runtime audio toggles (avoid doing work every loop unless the value changes).
  {
    static uint8_t s_lastMono = 255;
    const uint8_t wantMono = (uint8_t)(config.store.forceMono ? 1 : 0);
    if (wantMono != s_lastMono) {
      s_lastMono = wantMono;
      player.forceMono(wantMono != 0);
      Serial.printf("##INFO#:\tforceMono\t%u\n", (unsigned)wantMono);
    }
  }
  g_outputDeviceForAudio = config.store.outputDevice;
  g_biampEnableForAudio = config.store.biampEnable;
  g_biampLowOnLeftForAudio = config.store.biampLowOnLeft;
  g_biampCrossoverHzForAudio = config.store.biampCrossoverHz;
#ifdef USE_DLNA
  // After DLNA build/append: refresh playlist at runtime (main context)
  static uint32_t dlnaReloadAt = 0;

  if (g_dlnaPlaylistDirty) {
    g_dlnaPlaylistDirty = false;
    dlnaReloadAt = millis() + 150; // small debounce/flush delay after SPIFFS rename
  }

  if (dlnaReloadAt && (int32_t)(millis() - dlnaReloadAt) >= 0) {
    dlnaReloadAt = 0;

    // Reload the playlist/index cache -> playlistLength() will no longer be 0
    config.indexDLNAPlaylist();

    // WebUI/index refresh
    netserver.resetQueue();
    netserver.requestOnChange(GETINDEX, 0);

    // If you're currently in DLNA view, refresh the display too
    if (config.getMode() == PM_WEB &&
        config.store.playlistSource == (uint8_t)PL_SRC_DLNA) {
      display.resetQueue();
      display.putRequest(NEWMODE, PLAYER);
      display.putRequest(NEWSTATION);
    }
  }
#endif
  // SD playback should stay usable even when WiFi fails and we fall back to SoftAP.
  // (network.status == SDREADY is only set when booting in SD mode; it does not
  // represent "SD available" in all cases.)
  if (network.status == CONNECTED || network.status==SDREADY || config.getMode() == PM_SDCARD ||
      config.getMode() == PM_BLUETOOTH) {
    player.loop();
#ifdef BT_COMPANION_ENABLE
    // Keep the companion informed if content changes the PCM sample rate.
    // This is especially important for podcasts (often 48kHz).
    static uint32_t s_lastBtSr = 0;
    if (config.store.outputDevice == 1 && btcompanion_enabled()) {
      const uint32_t sr = player.getSampleRate();
      if (sr != 0 && sr != s_lastBtSr) {
        s_lastBtSr = sr;
        btcompanion_setPcmSampleRate(sr);
      }
    }
#endif
#ifdef MQTT_ROOT_TOPIC
    if (network.status == CONNECTED) mqttLoop();
#endif
#if USE_OTA
    if (network.status == CONNECTED || network.status == SOFT_AP) ArduinoOTA.handle();
#endif
  }

  #if !defined(BATTERY_ENABLED) || (BATTERY_ENABLED != 0)
    battery_update();
  #endif

#ifdef USE_SD
  // SD resume checkpoint: periodically persist a "good enough" resume time while
  // the track is playing. This avoids relying on stop-time file-position math
  // which can be skewed by large buffers.
  if (config.getMode() == PM_SDCARD && player.isRunning()) {
    static uint32_t s_lastSdResumeSaveMs = 0;
    const uint32_t now2 = millis();
    if (s_lastSdResumeSaveMs == 0 || (uint32_t)(now2 - s_lastSdResumeSaveMs) >= 10000) {
      s_lastSdResumeSaveMs = now2;
      const uint32_t curSec = player.getAudioCurrentTime();
      const uint32_t durSec = player.getAudioFileDuration();
      if (durSec > 0 && curSec > 0 && curSec + 2 < durSec) {
        const uint32_t encoded = 0x80000000u | (curSec & 0x7FFFFFFFu);
        config.saveValue(&config.store.lastSdResumePos, (uint32_t)encoded);
        config.sdResumePos = encoded;
      }
    }
  }
#endif

  // Podcast resume checkpoint: store current play time by episode URL (bounded cache).
  if (config.getMode() == PM_PODCAST && player.isRunning()) {
    static uint32_t s_lastPodcastResumeSaveMs = 0;
    const uint32_t now3 = millis();
    if (s_lastPodcastResumeSaveMs == 0 || (uint32_t)(now3 - s_lastPodcastResumeSaveMs) >= 10000) {
      s_lastPodcastResumeSaveMs = now3;
      const uint32_t curSec = player.getAudioCurrentTime();
      const uint32_t durSec = player.getAudioFileDuration();
      if (durSec > 0 && curSec > 0 && curSec + 2 < durSec) {
        podcast_resume_update_sec(config.station.url, curSec, durSec, false);
      }
    }
  }

#if (WAKE_PIN1 != 255) || (WAKE_PIN2 != 255)
  #ifndef AUTO_DEEPSLEEP_BATT_PCT
    #define AUTO_DEEPSLEEP_BATT_PCT 5
  #endif
  #ifndef AUTO_DEEPSLEEP_IDLE_MINUTES
    #define AUTO_DEEPSLEEP_IDLE_MINUTES 15
  #endif

  static uint32_t s_lastPlayingMs = 0;
  static uint32_t s_lastPowerCheckMs = 0;
  static uint32_t s_lastLowBattWarnMs = 0;
  const uint32_t now = millis();

  if (s_lastPlayingMs == 0) s_lastPlayingMs = now;
  // BT RX: audio comes from the HiGrow, so player.isRunning() is always false. Count BT as
  // playing only while the HiGrow reports A2DP streaming (STATUS audio=1 -> link AUDIO), sticky for
  // 60 s after the last streaming status. Idle/paused/disconnected BT still auto-sleeps.
  bool s_activeLike = player.isRunning();
#if defined(BT_COMPANION_ENABLE) && (BT_COMPANION_ENABLE != 0)
  {
    static uint32_t s_btLastStreamMs = 0;
    static bool s_btSeenStream = false;
    if (config.getMode() == PM_BLUETOOTH) {
      if (btcompanion_rxLinkState() == BtCompanionLinkState::AUDIO) {
        s_btLastStreamMs = now;
        s_btSeenStream = true;
      }
      if (s_btSeenStream && (uint32_t)(now - s_btLastStreamMs) < 60000u) s_activeLike = true;
    }
  }
#endif
  if (s_activeLike) s_lastPlayingMs = now;

  // Check at a low cadence (avoid extra work in the main loop).
  if (s_lastPowerCheckMs == 0 || (uint32_t)(now - s_lastPowerCheckMs) >= 1000) {
    s_lastPowerCheckMs = now;

    if (battery_is_ready()) {
      const float pct = battery_get_percent();
      const bool alertLow = battery_alert_active();
      if (!battery_usb_present()) {
        const float warnPct = (float)AUTO_DEEPSLEEP_BATT_PCT + 2.0f;
        if ((alertLow || (pct > 0.1f && pct <= warnPct && pct > (float)AUTO_DEEPSLEEP_BATT_PCT))) {
          if (s_lastLowBattWarnMs == 0 || (uint32_t)(now - s_lastLowBattWarnMs) >= 60000u) {
            s_lastLowBattWarnMs = now;
          #ifdef USE_NEOSTATUS_PLUGIN
            neostatusPulseLowBattery();
          #endif
          }
        }
      }

      if ((alertLow || (pct > 0.1f && pct <= (float)AUTO_DEEPSLEEP_BATT_PCT)) && !battery_usb_present()) {
        Serial.printf("Auto deep sleep: battery low (%.1f%%, alert=%d) and no 5V.\n", pct, (int)alertLow);
        display.putRequest(NEWMODE, SLEEPING);
        player.sendCommand({PR_STOP, 0});
        delay(150);
        Serial.flush();
        config.doSleepW();
      }
    }

    if (!s_activeLike) {
      const uint32_t idleMs = (uint32_t)(now - s_lastPlayingMs);
      const uint32_t idleLimitMs = (uint32_t)AUTO_DEEPSLEEP_IDLE_MINUTES * 60UL * 1000UL;
      if (AUTO_DEEPSLEEP_IDLE_MINUTES > 0 && idleMs >= idleLimitMs) {
        Serial.printf("Auto deep sleep: idle for %lu ms.\n", (unsigned long)idleMs);
        display.putRequest(NEWMODE, SLEEPING);
        delay(150);
        Serial.flush();
        config.doSleepW();
      }
    }
  }
#endif

  pm.on_loop();   // ledstrip plugin - always runs (even in screensaver)
  // Always poll encoders/IR/touch in the main loop. Button clicks are captured by
  // ControlsTask when enabled, so loopControls() will skip button.tick() in that case.
  loopControls();
  #ifdef NETSERVER_LOOP1
  netserver.loop();
  #endif
}
