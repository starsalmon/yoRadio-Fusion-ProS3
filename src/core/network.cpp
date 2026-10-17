#include "esp_wifi.h"
//v0.9.720
#include "options.h"
#include <ESPmDNS.h>
#include "time.h"
#include "rtcsupport.h"
#include "network.h"
#include "display.h"
#include "config.h"
#include "telnet.h"
#include "netserver.h"
#include "player.h"
#include "mqtt.h"
#include "timekeeper.h"
#include "../pluginsManager/pluginsManager.h"

#include <WiFi.h>

#ifndef WIFI_ATTEMPTS
  #define WIFI_ATTEMPTS  16
#endif

#ifndef SEARCH_WIFI_CORE_ID
  #define SEARCH_WIFI_CORE_ID  0
#endif
MyNetwork network;

static void printWifiDiag(const char* tag) {
  const wl_status_t st = WiFi.status();
  if (st != WL_CONNECTED) {
    Serial.printf("[wifi] %s: not connected (status=%d)\n", tag, (int)st);
    return;
  }

  const uint8_t* b = WiFi.BSSID();
  uint8_t bssid[6] = {0, 0, 0, 0, 0, 0};
  if (b) memcpy(bssid, b, 6);

  Serial.printf(
    "[wifi] %s: ssid=\"%s\" ip=%s rssi=%d ch=%d bssid=%02X:%02X:%02X:%02X:%02X:%02X\n",
    tag,
    WiFi.SSID().c_str(),
    config.ipToStr(WiFi.localIP()),
    WiFi.RSSI(),
    WiFi.channel(),
    bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]
  );
}

void MyNetwork::WiFiReconnected(WiFiEvent_t event, WiFiEventInfo_t info){
  network.beginReconnect = false;
  player.lockOutput = false;
  delay(100);
  display.putRequest(NEWMODE, PLAYER);
  if(config.getMode()==PM_SDCARD || config.getMode()==PM_BLUETOOTH) {  // offline-capable modes
    network.status=CONNECTED;
    display.putRequest(NEWIP, 0);
    display.putRequest(DSPRSSI, WiFi.RSSI());
  }else{
    display.putRequest(NEWMODE, PLAYER);
    if (network.lostPlaying) player.sendCommand({PR_PLAY, config.lastStation()});
    display.putRequest(NEWIP, 0);
    display.putRequest(DSPRSSI, WiFi.RSSI());
  }
  #ifdef MQTT_ROOT_TOPIC
    connectToMqtt();
  #endif
  printWifiDiag("reconnected");
}

void MyNetwork::WiFiLostConnection(WiFiEvent_t event, WiFiEventInfo_t info){
  if(!network.beginReconnect){
    Serial.printf("Lost connection, reconnecting to %s...\n", config.ssids[config.store.lastSSID-1].ssid);
    if(config.getMode()==PM_SDCARD || config.getMode()==PM_BLUETOOTH) {  // keep playing, no LOST screen
      network.status=SDREADY;
      display.putRequest(NEWIP, 0);
      display.putRequest(DSPRSSI, 0);
    }else{
      network.lostPlaying = player.isRunning();
      if (network.lostPlaying) { player.lockOutput = true; player.sendCommand({PR_STOP, 0}); }
      display.putRequest(NEWMODE, LOST);
      display.putRequest(NEWIP, 0);
      display.putRequest(DSPRSSI, 0);
    }
  }
  network.beginReconnect = true;
  WiFi.reconnect();
}

static const char *authName(wifi_auth_mode_t a) {
  switch (a) {
    case WIFI_AUTH_OPEN: return "OPEN"; case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA"; case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2"; case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-ENT";
    case WIFI_AUTH_WPA3_PSK: return "WPA3"; case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
    default: return "other";
  }
}
// Diagnostic: list every AP the radio can see, plus the saved SSIDs (quoted, with length).
void wifiScanLog(const char *tag) {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  esp_wifi_set_country_code("AU", true);   // AU: channels 1-13 (was world default 01: 1-11 active)
  delay(50);
  wifi_country_t cc = {};
  esp_wifi_get_country(&cc);
  int8_t txp = 0; esp_wifi_get_max_tx_power(&txp);
  Serial.printf("[WIFISCAN] %s: country=%.2s ch%u-%u txpower=%.1fdBm saved=%u\n", tag, cc.cc,
                (unsigned)cc.schan, (unsigned)(cc.schan + cc.nchan - 1), txp / 4.0f, (unsigned)config.ssidsCount);
  for (uint8_t i = 0; i < config.ssidsCount; i++)
    Serial.printf("[WIFISCAN]   saved[%u]='%s' (len %u)\n", i, config.ssids[i].ssid, (unsigned)strlen(config.ssids[i].ssid));
  const int n = WiFi.scanNetworks(false, true);   // blocking, include hidden
  Serial.printf("[WIFISCAN]   found %d AP(s)\n", n);
  for (int i = 0; i < n; i++) {
    Serial.printf("[WIFISCAN]   '%s' rssi=%d ch=%d auth=%s\n", WiFi.SSID(i).c_str(), WiFi.RSSI(i),
                  WiFi.channel(i), authName(WiFi.encryptionType(i)));
  }
  WiFi.scanDelete();
}

bool MyNetwork::wifiBegin(bool silent){
  { static uint32_t s_lastScanMs = 0;   // boot scan, then at most every 30 s while disconnected
    if (s_lastScanMs == 0 || (uint32_t)(millis() - s_lastScanMs) >= 30000u) {
      s_lastScanMs = millis() | 1u;
      wifiScanLog(silent ? "background" : "boot");
    } }
  uint8_t ls = (config.store.lastSSID == 0 || config.store.lastSSID > config.ssidsCount) ? 0 : config.store.lastSSID - 1;
  uint8_t startedls = ls;
  uint8_t errcnt = 0;
  //WiFi.mode(WIFI_STA);
  while (true) {
    if(!silent){
      Serial.printf("##[BOOT]#\tAttempt to connect to %s\n", config.ssids[ls].ssid);
      Serial.flush();
      Serial.print("##[BOOT]#\t");
      if (display.ready()) {
        display.putRequest(BOOTSTRING, ls);
      }
    }
    //WiFi.disconnect(true, true); //disconnect & erase internal credentials https://github.com/e2002/yoradio/pull/164/commits/89d8b4450dde99cd7930b84bb14d81dab920b879
    //delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.begin(config.ssids[ls].ssid, config.ssids[ls].password);
    while (WiFi.status() != WL_CONNECTED) {
      if(!silent) {
        Serial.print(".");
        if ((errcnt & 7u) == 7u) Serial.flush();
      }
      delay(500);
      if(REAL_LEDBUILTIN!=255 && !silent) digitalWrite(REAL_LEDBUILTIN, !digitalRead(REAL_LEDBUILTIN));
      errcnt++;
      if (errcnt > WIFI_ATTEMPTS) {
        errcnt = 0;
        ls++;
        if (ls > config.ssidsCount - 1) ls = 0;
        if(!silent) Serial.println();
        WiFi.mode(WIFI_OFF);
        break;
      }
    }
    if (WiFi.status() != WL_CONNECTED && ls == startedls) {
      return false; break;
    }
    if (WiFi.status() == WL_CONNECTED) {
      config.setLastSSID(ls + 1);
      return true; break;
    }
  }
  return false;
}

void searchWiFi(void * pvParameters){
  if(!network.wifiBegin(true)){
    delay(10000);
    xTaskCreatePinnedToCore(searchWiFi, "searchWiFi", 1024 * 4, NULL, 3, NULL, SEARCH_WIFI_CORE_ID); // "task_prioritas" 0 eredeti, új 3
  }else{
    network.status = CONNECTED;
    netserver.begin(true);
    telnet.begin(true);
    network.setWifiParams();
    display.putRequest(NEWIP, 0);
    display.putRequest(DSPRSSI, WiFi.RSSI());
    printWifiDiag("boot(sdready)");
    #if defined(MQTT_ROOT_TOPIC) && !(defined(MQTT_DISABLE) && MQTT_DISABLE)
      mqttInit();
    #endif
  }
  vTaskDelete( NULL );
}

#define DBGAP false

void MyNetwork::begin() {
  BOOTLOG("network.begin");
  config.initNetwork();
  if (config.ssidsCount == 0 || DBGAP) {
    raiseSoftAP();
    return;
  }
  // SD and BT play without Wi-Fi: don't block boot; search for Wi-Fi in the background.
  if(config.getMode()!=PM_SDCARD && config.getMode()!=PM_BLUETOOTH){
    if(!wifiBegin()){
      raiseSoftAP();
      Serial.println("##[BOOT]#\tdone");
      return;
    }
    Serial.println(".");
    status = CONNECTED;
    setWifiParams();
    printWifiDiag("boot");
    #if defined(MQTT_ROOT_TOPIC) && !(defined(MQTT_DISABLE) && MQTT_DISABLE)
      mqttInit();
    #endif
  }else{
    status = SDREADY;
    xTaskCreatePinnedToCore(searchWiFi, "searchWiFi", 1024 * 4, NULL, 3, NULL, SEARCH_WIFI_CORE_ID); // "task_prioritas" 0 eredeti, 3 új
  }
  
  Serial.println("##[BOOT]#\tdone");
  if(REAL_LEDBUILTIN!=255) digitalWrite(REAL_LEDBUILTIN, LOW);
  
#if RTCSUPPORTED
  if(config.isRTCFound()){
    rtc.getTime(&network.timeinfo);
    mktime(&network.timeinfo);
    if (display.ready()) {
      display.putRequest(CLOCK);
    }
  }
#endif
  if (network_on_connect) network_on_connect();
  pm.on_connect();
}

void MyNetwork::setWifiParams(){
  WiFi.setSleep(false);
  WiFi.onEvent(WiFiReconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(WiFiLostConnection, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  //config.setTimeConf(); //??
  if(strlen(config.store.mdnsname)>0)
    MDNS.begin(config.store.mdnsname);
}

void MyNetwork::requestTimeSync(bool withTelnetOutput, uint8_t clientId) {
  if (withTelnetOutput) {
    char timeStringBuff[50];
    strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%dT%H:%M:%S", &timeinfo);
    if (config.store.tzHour < 0) {
      telnet.printf(clientId, "##SYS.DATE#: %s%03d:%02d\r\n> ", timeStringBuff, config.store.tzHour, config.store.tzMin);
    } else {
      telnet.printf(clientId, "##SYS.DATE#: %s+%02d:%02d\r\n> ", timeStringBuff, config.store.tzHour, config.store.tzMin);
    }
  }
}

void rebootTime() {
  ESP.restart();
}

void MyNetwork::raiseSoftAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid, apPassword);
  Serial.println("##[BOOT]#");
  BOOTLOG("************************************************");
  BOOTLOG("Running in AP mode");
  BOOTLOG("Connect to AP %s with password %s", apSsid, apPassword);
  BOOTLOG("and go to http:/192.168.4.1/ to configure");
  BOOTLOG("************************************************");
  status = SOFT_AP;
  if(config.store.softapdelay>0)
    timekeeper.waitAndDo(config.store.softapdelay*60, rebootTime);
}

void MyNetwork::requestWeatherSync(){
  display.putRequest(NEWWEATHER);
}