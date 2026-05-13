#include "Arduino.h"
#include "core/options.h"
#include "core/config.h"
#include "pluginsManager/pluginsManager.h"
#include "plugins/backlight/backlight.h" // backlight plugin
#include "plugins/ledstrip/ledstrip.h" // ledstrip plugin
#include "core/telnet.h"
#include "core/player.h"
#include "core/display.h"
#include "core/network.h"
#include "core/netserver.h"
#include "core/controls.h"
#ifdef MQTT_ROOT_TOPIC
  #include "core/mqtt.h"
#endif
#include "core/optionschecker.h"
#include "core/timekeeper.h"
#include "clock/clock_tts.h"
#include "driver/rtc_io.h"
#include "battery.h"

#ifdef USE_NEXTION
#include "displays/nextion.h"
#endif
#include "core/audiohandlers.h"
#ifdef USE_DLNA //DLNA mod
#include "network/dlna_service.h"
extern volatile bool g_dlnaPlaylistDirty;
#endif

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

void setup() {
  Serial.begin(115200);
    EEPROM.begin(EEPROM_SIZE);
#if IR_PIN != 255
    irQueue = xQueueCreate(4, sizeof(IRCommand));
    config.eepromRead(EEPROM_START_IR, config.ircodes);
    irWakeup();
#endif
#ifdef USE_LEDSTRIP_PLUGIN
  ledstripPluginInit();
#endif
#if (BRIGHTNESS_PIN!=255)
  backlightPluginInit();
#endif
  if(REAL_LEDBUILTIN!=255) pinMode(REAL_LEDBUILTIN, OUTPUT);
  if (yoradio_on_setup) yoradio_on_setup();
  pm.init();     // pluginsManager
  pm.on_setup();
  config.init();
  clock_tts_setup();

  display.init();
  player.init();
  network.begin();
  if (network.status != CONNECTED && network.status!=SDREADY) {
    // Still initialize playlist/station so the player UI can show the last station number
    // even when we boot into "not connected yet" state (no autoplay).
    config.initPlaylistMode();
    netserver.begin();
    initControls();
    display.putRequest(DSP_START);
    while(!display.ready()) delay(10);
    return;
  }
  if(SDC_CS!=255) {
    display.putRequest(WAITFORSD, 0);
    Serial.print("##[BOOT]#\tSD search\t");
  }
  config.initPlaylistMode();
  netserver.begin();
  telnet.begin();
  initControls();
  display.putRequest(DSP_START);
  while(!display.ready()) delay(10);
  #if USE_OTA
    setupOTA();
  #endif
  if (config.getMode()==PM_SDCARD) player.initHeaders(config.station.url);
  player.lockOutput=false;
  if (config.store.smartstart == 1) {
    player.sendCommand({PR_PLAY, config.lastStation()});
  }
  clock_tts_setup();
  Audio::audio_info_callback =  my_audio_info;   // "audio_change" audiohandlers.h ban kezelve.
  pm.on_end_setup();
}

void loop() {
  timekeeper.loop1();
  telnet.loop();
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
  if (network.status == CONNECTED || network.status==SDREADY) {
    player.loop();
#ifdef MQTT_ROOT_TOPIC
    mqttLoop();
#endif
#if USE_OTA
    ArduinoOTA.handle();
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

#if (WAKE_PIN1 != 255) || (WAKE_PIN2 != 255)
  #ifndef AUTO_DEEPSLEEP_BATT_PCT
    #define AUTO_DEEPSLEEP_BATT_PCT 5
  #endif
  #ifndef AUTO_DEEPSLEEP_IDLE_MINUTES
    #define AUTO_DEEPSLEEP_IDLE_MINUTES 15
  #endif

  static uint32_t s_lastPlayingMs = 0;
  static uint32_t s_lastPowerCheckMs = 0;
  const uint32_t now = millis();

  if (s_lastPlayingMs == 0) s_lastPlayingMs = now;
  if (player.isRunning()) s_lastPlayingMs = now;

  // Check at a low cadence (avoid extra work in the main loop).
  if (s_lastPowerCheckMs == 0 || (uint32_t)(now - s_lastPowerCheckMs) >= 1000) {
    s_lastPowerCheckMs = now;

    if (battery_is_ready()) {
      const float pct = battery_get_percent();
      if (pct > 0.1f && pct <= (float)AUTO_DEEPSLEEP_BATT_PCT && !battery_usb_present()) {
        Serial.printf("Auto deep sleep: battery low (%.1f%%) and no 5V.\n", pct);
        display.putRequest(NEWMODE, SLEEPING);
        player.sendCommand({PR_STOP, 0});
        delay(150);
        Serial.flush();
        config.doSleepW();
      }
    }

    if (!player.isRunning()) {
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
  loopControls();
  #ifdef NETSERVER_LOOP1
  netserver.loop();
  #endif
}
