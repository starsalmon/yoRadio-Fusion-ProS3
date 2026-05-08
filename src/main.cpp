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
//#include "core/mqtt.h"
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
  //SPI.setFrequency(40000000);
  Serial.printf("SPI clock after display.init: %u\n", SPI2.getClockDivider());
  player.init();
  network.begin();
  if (network.status != CONNECTED && network.status!=SDREADY) {
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
  // DLNA build/append után runtime playlist refresh (main contextben!)
  static uint32_t dlnaReloadAt = 0;

  if (g_dlnaPlaylistDirty) {
    g_dlnaPlaylistDirty = false;
    dlnaReloadAt = millis() + 150; // kis debounce/flush idő SPIFFS rename után
  }

  if (dlnaReloadAt && (int32_t)(millis() - dlnaReloadAt) >= 0) {
    dlnaReloadAt = 0;

    // újratölti a playlist/index cache-t → playlistLength() innentől már nem 0
    config.indexDLNAPlaylist();

    // WebUI/index frissítés
    netserver.resetQueue();
    netserver.requestOnChange(GETINDEX, 0);

    // ha épp DLNA nézetben vagy, frissítsd a képernyőt is
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
#if USE_OTA
    ArduinoOTA.handle();
#endif
  }

  battery_update();

  pm.on_loop();   // ledstrip plugin – mindig fut (screensaver is)
  loopControls();
  #ifdef NETSERVER_LOOP1
  netserver.loop();
  #endif
}
