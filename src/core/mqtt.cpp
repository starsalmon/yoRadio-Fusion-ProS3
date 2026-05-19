#include "options.h"
#ifdef MQTT_ROOT_TOPIC

#include "config.h"
#include "mqtt.h"
#include "WiFi.h"
#include "player.h"
#include "display.h"
#include "bt_companion.h"
#if (BRIGHTNESS_PIN != 255)
#include "../plugins/backlight/backlight.h"
#endif
#include "../battery.h"
#include <esp_log.h>
#include <strings.h>

AsyncMqttClient mqttClient;
TimerHandle_t mqttReconnectTimer;
TaskHandle_t mqttHADiscoveryTaskHandle = nullptr;
char topic[160], status[600];
char availabilityTopic[160];

namespace {
static bool s_haDiscoveryPublished = false;
static bool isTruthyPayload(const char* s) {
  if (!s) return false;
  while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
  if (*s == '\0') return false;
  if (strcmp(s, "1") == 0) return true;
  if (strcasecmp(s, "on") == 0) return true;
  if (strcasecmp(s, "true") == 0) return true;
  if (strcasecmp(s, "sleep") == 0) return true;
  if (strcasecmp(s, "deepsleep") == 0) return true;
  return false;
}

static void mqttEnterSleep() {
  // Go to deep sleep without issuing PR_STOP.
  // PR_STOP disables SmartStart (see Player::stopInfo -> config.setSmartStart(0)),
  // which breaks "auto start after waking back up".
  display.putRequest(NEWMODE, SLEEPING);
  delay(50);

  // Best-effort: publish final retained state before power-down.
  mqttPublishStatus();
  mqttPublishBattery();
  delay(50);
  Serial.flush();

  config.doSleepW();
}

static void mqttPublishAvailability(bool online) {
  if (!mqttClient.connected()) return;
  if (availabilityTopic[0] == '\0') return;
  mqttClient.publish(availabilityTopic, 0, true, online ? "online" : "offline");
}

static void mqttPublishOutputDevice(bool force = false) {
  #if defined(MQTT_DISABLE) && MQTT_DISABLE
    (void)force;
    return;
  #endif
  if (!mqttClient.connected()) return;

#if defined(BT_COMPANION_ENABLE) && (BT_COMPANION_ENABLE != 0)
  static int s_last = -1;
  const int cur = (int)config.store.outputDevice;
  if (!force && cur == s_last) return;
  s_last = cur;
  const char* v = (cur == 1) ? "Bluetooth" : "Speaker";

  char t2[160];
  snprintf(t2, sizeof(t2), "%s%s", MQTT_ROOT_TOPIC, "output_device");
  mqttClient.publish(t2, 0, true, v);
#else
  // Feature not compiled: always speaker.
  static bool s_done = false;
  if (!force && s_done) return;
  s_done = true;
  char t2[160];
  snprintf(t2, sizeof(t2), "%s%s", MQTT_ROOT_TOPIC, "output_device");
  mqttClient.publish(t2, 0, true, "Speaker");
#endif
}

static void mqttPublishBtSinkName(bool force = false) {
  #if defined(MQTT_DISABLE) && MQTT_DISABLE
    (void)force;
    return;
  #endif
  if (!mqttClient.connected()) return;

#if defined(BT_COMPANION_ENABLE) && (BT_COMPANION_ENABLE != 0)
  static char s_last[64] = {0};
  const char* cur = btcompanion_sinkName();
  if (!cur) cur = "";
  if (!force && strncmp(cur, s_last, sizeof(s_last)) == 0) return;
  strlcpy(s_last, cur, sizeof(s_last));

  char t2[160];
  snprintf(t2, sizeof(t2), "%s%s", MQTT_ROOT_TOPIC, "bt_sink_name");
  mqttClient.publish(t2, 0, true, cur);
#else
  (void)force;
#endif
}

static void mqttPublishHADiscovery() {
  if (!mqttClient.connected()) return;

#ifndef MQTT_HA_PREFIX
  #define MQTT_HA_PREFIX "homeassistant"
#endif

  // Home Assistant "device" metadata (can be injected at build-time).
#ifndef HA_DEVICE_MANUFACTURER
  #define HA_DEVICE_MANUFACTURER "yoRadio"
#endif
#ifndef HA_DEVICE_MODEL
  #define HA_DEVICE_MODEL "ESP32"
#endif
#ifndef HA_DEVICE_HW_VERSION
  #define HA_DEVICE_HW_VERSION ""
#endif

  // Node/device id used in discovery topics and unique_ids.
  char nodeId[48] = {0};
  const char* mdns = config.store.mdnsname;
  if (mdns && mdns[0] != '\0') {
    // Keep it short and topic-safe.
    strlcpy(nodeId, mdns, sizeof(nodeId));
  } else {
    const uint64_t mac = ESP.getEfuseMac();
    snprintf(nodeId, sizeof(nodeId), "yoradio_%08lx", (unsigned long)(mac & 0xFFFFFFFFULL));
  }

  // Keep buffers generous: HA discovery payloads are easy to truncate otherwise,
  // and truncated JSON configs are silently ignored by Home Assistant.
  char dev[512];
  if (HA_DEVICE_HW_VERSION[0] != '\0') {
    snprintf(dev, sizeof(dev),
             "{\"identifiers\":[\"%s\"],\"name\":\"yoRadio %s\",\"manufacturer\":\"%s\",\"model\":\"%s\",\"hw_version\":\"%s\",\"sw_version\":\"%s\"}",
             nodeId, nodeId, HA_DEVICE_MANUFACTURER, HA_DEVICE_MODEL, HA_DEVICE_HW_VERSION, YOVERSION);
  } else {
    snprintf(dev, sizeof(dev),
             "{\"identifiers\":[\"%s\"],\"name\":\"yoRadio %s\",\"manufacturer\":\"%s\",\"model\":\"%s\",\"sw_version\":\"%s\"}",
             nodeId, nodeId, HA_DEVICE_MANUFACTURER, HA_DEVICE_MODEL, YOVERSION);
  }

  auto pubCfg = [&](const char* component, const char* objectId, const char* json) {
    char t[200];
    snprintf(t, sizeof(t), "%s/%s/%s/%s/config", MQTT_HA_PREFIX, component, nodeId, objectId);
    // AsyncMqttClient can drop publishes if we enqueue too many at once.
    // Retry a few times to make HA discovery reliable (especially right after boot).
    for (int attempt = 0; attempt < 5; attempt++) {
      const uint16_t pid = mqttClient.publish(t, 0, true, json);
      if (pid != 0) break;
      delay(30);
    }
  };

  // --- Sensors (battery) ---
  {
    char cfg[900];
    snprintf(cfg, sizeof(cfg),
             "{\"name\":\"Battery\",\"unique_id\":\"%s_battery_percent\",\"state_topic\":\"%sbattery\","
             "\"value_template\":\"{{ value_json.percent }}\",\"unit_of_measurement\":\"%%\","
             "\"device_class\":\"battery\",\"state_class\":\"measurement\",\"icon\":\"mdi:battery\","
             "\"availability_topic\":\"%s\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\","
             "\"device\":%s}",
             nodeId, MQTT_ROOT_TOPIC, availabilityTopic, dev);
    pubCfg("sensor", "battery_percent", cfg);
  }
  {
    char cfg[900];
    snprintf(cfg, sizeof(cfg),
             "{\"name\":\"Battery Voltage\",\"unique_id\":\"%s_battery_voltage\",\"state_topic\":\"%sbattery/voltage\","
             "\"unit_of_measurement\":\"V\","
             "\"value_template\":\"{{ '%%.2f'|format(value|float(0)) }}\",\"state_class\":\"measurement\",\"icon\":\"mdi:flash\","
             "\"availability_topic\":\"%s\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\","
             "\"device\":%s}",
             nodeId, MQTT_ROOT_TOPIC, availabilityTopic, dev);
    pubCfg("sensor", "battery_voltage", cfg);
  }
  {
    char cfg[900];
    snprintf(cfg, sizeof(cfg),
             "{\"name\":\"Battery Rate\",\"unique_id\":\"%s_battery_rate\",\"state_topic\":\"%sbattery\","
             "\"value_template\":\"{{ value_json.rate }}\",\"unit_of_measurement\":\"%%/h\","
             "\"state_class\":\"measurement\",\"icon\":\"mdi:chart-line\","
             "\"availability_topic\":\"%s\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\","
             "\"device\":%s}",
             nodeId, MQTT_ROOT_TOPIC, availabilityTopic, dev);
    pubCfg("sensor", "battery_rate", cfg);
  }
  {
    char cfg[900];
    snprintf(cfg, sizeof(cfg),
             "{\"name\":\"Battery State\",\"unique_id\":\"%s_battery_state\",\"state_topic\":\"%sbattery\","
             "\"value_template\":\"{{ value_json.state }}\",\"icon\":\"mdi:battery-heart\","
             "\"availability_topic\":\"%s\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\","
             "\"device\":%s}",
             nodeId, MQTT_ROOT_TOPIC, availabilityTopic, dev);
    pubCfg("sensor", "battery_state", cfg);
  }
  {
    char cfg[900];
    snprintf(cfg, sizeof(cfg),
             "{\"name\":\"USB Power\",\"unique_id\":\"%s_usb_present\",\"state_topic\":\"%sbattery\","
             "\"value_template\":\"{{ 'ON' if value_json.usb == 1 else 'OFF' }}\","
             "\"payload_on\":\"ON\",\"payload_off\":\"OFF\",\"device_class\":\"plug\",\"icon\":\"mdi:power-plug\","
             "\"availability_topic\":\"%s\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\","
             "\"device\":%s}",
             nodeId, MQTT_ROOT_TOPIC, availabilityTopic, dev);
    pubCfg("binary_sensor", "usb_present", cfg);
  }

  // --- Sensors (status JSON + volume) ---
  {
    char cfg[560];
    snprintf(cfg, sizeof(cfg),
             "{\"name\":\"Playing\",\"unique_id\":\"%s_playing\",\"state_topic\":\"%sstatus\","
             "\"value_template\":\"{{ 'ON' if value_json.status == 1 else 'OFF' }}\","
             "\"payload_on\":\"ON\",\"payload_off\":\"OFF\",\"device_class\":\"running\",\"icon\":\"mdi:play-circle\","
             "\"availability_topic\":\"%s\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\","
             "\"device\":%s}",
             nodeId, MQTT_ROOT_TOPIC, availabilityTopic, dev);
    pubCfg("binary_sensor", "playing", cfg);
  }

  // Track time/duration (seconds) as simple numeric topics (updated periodically).
  {
    char cfg[560];
    snprintf(cfg, sizeof(cfg),
             "{\"name\":\"Track Time\",\"unique_id\":\"%s_track_time\",\"state_topic\":\"%strack_time\","
             "\"value_template\":\"{{ value|int }}\",\"unit_of_measurement\":\"s\","
             "\"state_class\":\"measurement\",\"icon\":\"mdi:clock-outline\","
             "\"availability_topic\":\"%s\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\","
             "\"device\":%s}",
             nodeId, MQTT_ROOT_TOPIC, availabilityTopic, dev);
    pubCfg("sensor", "track_time", cfg);
  }
  {
    char cfg[560];
    snprintf(cfg, sizeof(cfg),
             "{\"name\":\"Track Duration\",\"unique_id\":\"%s_track_duration\",\"state_topic\":\"%strack_duration\","
             "\"value_template\":\"{{ value|int }}\",\"unit_of_measurement\":\"s\","
             "\"state_class\":\"measurement\",\"icon\":\"mdi:timer-outline\","
             "\"availability_topic\":\"%s\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\","
             "\"device\":%s}",
             nodeId, MQTT_ROOT_TOPIC, availabilityTopic, dev);
    pubCfg("sensor", "track_duration", cfg);
  }
  // NOTE: no separate brightness sensor – the number (slider) is the single "Brightness" entity.
  {
    char cfg[520];
    snprintf(cfg, sizeof(cfg),
             "{\"name\":\"Station\",\"unique_id\":\"%s_station_name\",\"state_topic\":\"%sstatus\","
             "\"value_template\":\"{{ value_json.name }}\",\"icon\":\"mdi:radio\","
             "\"availability_topic\":\"%s\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\","
             "\"device\":%s}",
             nodeId, MQTT_ROOT_TOPIC, availabilityTopic, dev);
    pubCfg("sensor", "station_name", cfg);
  }
  {
    char cfg[520];
    snprintf(cfg, sizeof(cfg),
             "{\"name\":\"Title\",\"unique_id\":\"%s_station_title\",\"state_topic\":\"%sstatus\","
             "\"value_template\":\"{{ value_json.title }}\",\"icon\":\"mdi:music-note\","
             "\"availability_topic\":\"%s\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\","
             "\"device\":%s}",
             nodeId, MQTT_ROOT_TOPIC, availabilityTopic, dev);
    pubCfg("sensor", "station_title", cfg);
  }
  // NOTE: no separate volume sensor – the number (slider) below is the single "Volume" entity.

  // Mode: web/sd/dlna
  {
    char cfg[520];
    snprintf(cfg, sizeof(cfg),
             "{\"name\":\"Mode\",\"unique_id\":\"%s_mode\",\"state_topic\":\"%smode\","
             "\"icon\":\"mdi:layers-triple\","
             "\"availability_topic\":\"%s\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\","
             "\"device\":%s}",
             nodeId, MQTT_ROOT_TOPIC, availabilityTopic, dev);
    pubCfg("sensor", "mode", cfg);
  }

  // Mode as a selector (combined state + control).
  // Prefer this over the legacy "Toggle Mode" button + mode sensor.
  {
    char opts[96] = {0};
    // Options must be a JSON array; keep this deterministic and small.
#ifdef USE_DLNA
    strlcpy(opts, "[\"Web Streaming\",\"SD Card\",\"DLNA\"]", sizeof(opts));
#else
    strlcpy(opts, "[\"Web Streaming\",\"SD Card\"]", sizeof(opts));
#endif

    char cfg[700];
    snprintf(cfg, sizeof(cfg),
             "{\"name\":\"Mode\",\"unique_id\":\"%s_mode_select\","
             "\"state_topic\":\"%smode\","
             "\"command_topic\":\"%scmd/playback_mode\","
             "\"options\":%s,"
             "\"icon\":\"mdi:layers-triple\","
             "\"availability_topic\":\"%s\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\","
             "\"device\":%s}",
             nodeId, MQTT_ROOT_TOPIC, MQTT_ROOT_TOPIC, opts, availabilityTopic, dev);
    pubCfg("select", "mode", cfg);
  }

#if defined(BT_COMPANION_ENABLE) && (BT_COMPANION_ENABLE != 0)
  // Output device as a selector (combined state + control).
  {
    char cfg[700];
    snprintf(cfg, sizeof(cfg),
             "{\"name\":\"Audio Output\",\"unique_id\":\"%s_output_device_select\","
             "\"state_topic\":\"%soutput_device\","
             "\"command_topic\":\"%scmd/output_device\","
             "\"options\":[\"Speaker\",\"Bluetooth\"],"
             "\"icon\":\"mdi:speaker-bluetooth\","
             "\"availability_topic\":\"%s\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\","
             "\"device\":%s}",
             nodeId, MQTT_ROOT_TOPIC, MQTT_ROOT_TOPIC, availabilityTopic, dev);
    pubCfg("select", "output_device", cfg);
  }

  // Bluetooth sink name as a text input (combined state + control).
  {
    char cfg[700];
    snprintf(cfg, sizeof(cfg),
             "{\"name\":\"BT Sink Name\",\"unique_id\":\"%s_bt_sink_name_text\","
             "\"state_topic\":\"%sbt_sink_name\","
             "\"command_topic\":\"%scmd/bt_sink_name\","
             "\"max\":63,"
             "\"icon\":\"mdi:bluetooth-audio\","
             "\"availability_topic\":\"%s\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\","
             "\"device\":%s}",
             nodeId, MQTT_ROOT_TOPIC, MQTT_ROOT_TOPIC, availabilityTopic, dev);
    pubCfg("text", "bt_sink_name", cfg);
  }
#endif

  // --- Buttons ---
  {
    char cfg[520];
    snprintf(cfg, sizeof(cfg),
             "{\"name\":\"Sleep\",\"unique_id\":\"%s_btn_sleep\",\"command_topic\":\"%scmd/sleep\","
             "\"payload_press\":\"ON\",\"icon\":\"mdi:sleep\","
             "\"availability_topic\":\"%s\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\","
             "\"device\":%s}",
             nodeId, MQTT_ROOT_TOPIC, availabilityTopic, dev);
    pubCfg("button", "sleep", cfg);
  }
  const struct { const char* id; const char* name; const char* payload; const char* icon; } buttons[] = {
    // Use the same command names as the CommandHandler/web UI ("start"/"stop") for reliability.
    {"prev", "Previous", "prev", "mdi:skip-previous"},
    {"next", "Next", "next", "mdi:skip-next"},
    {"toggle", "Play/Pause", "toggle", "mdi:play-pause"},
    {"stop", "Stop", "stop", "mdi:stop"},
    {"start", "Play", "start", "mdi:play"},
    {"reboot", "Reboot", "reboot", "mdi:restart"},
    // Legacy: mode is now exposed as a select (combined state+command).
  };
  for (const auto& b : buttons) {
    char cfg[520];
    snprintf(cfg, sizeof(cfg),
             "{\"name\":\"%s\",\"unique_id\":\"%s_btn_%s\",\"command_topic\":\"%scommand\","
             "\"payload_press\":\"%s\",\"icon\":\"%s\","
             "\"availability_topic\":\"%s\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\","
             "\"device\":%s}",
             b.name, nodeId, b.id, MQTT_ROOT_TOPIC, b.payload, b.icon, availabilityTopic, dev);
    pubCfg("button", b.id, cfg);
  }

  // Volume as a slider (0-100). This is the only Volume entity we expose.
  {
    char cfg[650];
    snprintf(cfg, sizeof(cfg),
             "{\"name\":\"Volume\",\"unique_id\":\"%s_volume_number\","
             "\"state_topic\":\"%svolume\",\"value_template\":\"{{ value|int }}\","
             "\"command_topic\":\"%scmd/volume\","
             "\"min\":0,\"max\":100,\"step\":1,\"mode\":\"slider\","
             "\"icon\":\"mdi:volume-high\","
             "\"availability_topic\":\"%s\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\","
             "\"device\":%s}",
             nodeId, MQTT_ROOT_TOPIC, MQTT_ROOT_TOPIC, availabilityTopic, dev);
    pubCfg("number", "volume", cfg);
  }

  // Brightness as a slider (0-100).
  {
    char cfg[650];
    snprintf(cfg, sizeof(cfg),
             "{\"name\":\"Brightness\",\"unique_id\":\"%s_brightness_number\","
             "\"state_topic\":\"%sbrightness\",\"value_template\":\"{{ value|int }}\","
             "\"command_topic\":\"%scmd/brightness\","
             "\"min\":0,\"max\":100,\"step\":1,\"mode\":\"slider\","
             "\"icon\":\"mdi:brightness-6\","
             "\"availability_topic\":\"%s\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\","
             "\"device\":%s}",
             nodeId, MQTT_ROOT_TOPIC, MQTT_ROOT_TOPIC, availabilityTopic, dev);
    pubCfg("number", "brightness", cfg);
  }

  // Station number as a numeric input (1..N).
  // Keep max generous so we don't need to republish discovery when playlists change.
  {
    char cfg[650];
    snprintf(cfg, sizeof(cfg),
             "{\"name\":\"Station #\",\"unique_id\":\"%s_station_number\","
             "\"state_topic\":\"%sstation_number\",\"value_template\":\"{{ value|int }}\","
             "\"command_topic\":\"%scmd/station_number\","
             "\"min\":1,\"max\":9999,\"step\":1,\"mode\":\"box\","
             "\"icon\":\"mdi:numeric\","
             "\"availability_topic\":\"%s\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\","
             "\"device\":%s}",
             nodeId, MQTT_ROOT_TOPIC, MQTT_ROOT_TOPIC, availabilityTopic, dev);
    pubCfg("number", "station_number", cfg);
  }

  // --- Cleanup old entities (publish empty retained config to remove) ---
  {
    // Old toggle-mode button (replaced by select)
    pubCfg("button", "toggle_mode", "");
    // Old mode sensor (replaced by select)
    pubCfg("sensor", "mode", "");
    // Old volume sensor + intermediate volume_level sensor
    pubCfg("sensor", "volume", "");
    pubCfg("sensor", "volume_level", "");
    // Old display binary sensor
    pubCfg("binary_sensor", "display_on", "");
    // Old brightness sensor (replaced by number slider)
    pubCfg("sensor", "brightness", "");
    // Old volume buttons (if they were ever published)
    pubCfg("button", "vol_down", "");
    pubCfg("button", "vol_up", "");
    // Old play button id
    pubCfg("button", "play", "");
    // Old number object id
    pubCfg("number", "volume_slider", "");
  }

  s_haDiscoveryPublished = true;
}

static void mqttHADiscoveryTask(void*) {
  // Let the UI settle before blasting discovery messages.
  vTaskDelay(pdMS_TO_TICKS(2500));
  if (!s_haDiscoveryPublished && mqttClient.connected()) {
    mqttPublishHADiscovery();
  }
  mqttHADiscoveryTaskHandle = nullptr;
  vTaskDelete(nullptr);
}
} // namespace

static void mqttPublishTrackTime(bool force = false);
static void mqttPublishStationNumber(bool force = false);

void connectToMqtt() {
  #if defined(MQTT_DISABLE) && MQTT_DISABLE
    return;
  #endif
  //config.waitConnection();
  mqttClient.connect();
}

void mqttInit() {
  #if defined(MQTT_DISABLE) && MQTT_DISABLE
    return;
  #endif

  #if defined(MQTT_QUIET_LOGS) && MQTT_QUIET_LOGS
    // The AsyncMqttClient library uses the Arduino log macros with __FILE__ as tag on ESP32,
    // which results in very chatty lines like: [I][AsyncMqttClient.cpp:740] publish(): PUBLISH
    // Quiet them down to reduce Serial spam and avoid affecting timing-sensitive audio.
    esp_log_level_set("AsyncMqttClient.cpp", ESP_LOG_WARN);
    esp_log_level_set("AsyncMqttClient.c", ESP_LOG_WARN);
    esp_log_level_set("AsyncMqttClient", ESP_LOG_WARN);
  #endif

  mqttReconnectTimer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(connectToMqtt));
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onMessage(onMqttMessage);
  if(strlen(MQTT_USER)>0) mqttClient.setCredentials(MQTT_USER, MQTT_PASS);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  snprintf(availabilityTopic, sizeof(availabilityTopic), "%s%s", MQTT_ROOT_TOPIC, "availability");
  mqttClient.setWill(availabilityTopic, 0, true, "offline");
  connectToMqtt();
}

void zeroBuffer(){ memset(topic, 0, sizeof(topic)); memset(status, 0, sizeof(status)); }

void onMqttConnect(bool sessionPresent) {
  zeroBuffer();
  snprintf(topic, sizeof(topic), "%s%s", MQTT_ROOT_TOPIC, "command");
  mqttClient.subscribe(topic, 2);
  zeroBuffer();
  snprintf(topic, sizeof(topic), "%s%s", MQTT_ROOT_TOPIC, "cmd/sleep");
  mqttClient.subscribe(topic, 2);
  zeroBuffer();
  snprintf(topic, sizeof(topic), "%s%s", MQTT_ROOT_TOPIC, "cmd/volume");
  mqttClient.subscribe(topic, 2);
  zeroBuffer();
  snprintf(topic, sizeof(topic), "%s%s", MQTT_ROOT_TOPIC, "cmd/brightness");
  mqttClient.subscribe(topic, 2);
  zeroBuffer();
  snprintf(topic, sizeof(topic), "%s%s", MQTT_ROOT_TOPIC, "cmd/playback_mode");
  mqttClient.subscribe(topic, 2);
  zeroBuffer();
  snprintf(topic, sizeof(topic), "%s%s", MQTT_ROOT_TOPIC, "cmd/station_number");
  mqttClient.subscribe(topic, 2);
#if defined(BT_COMPANION_ENABLE) && (BT_COMPANION_ENABLE != 0)
  zeroBuffer();
  snprintf(topic, sizeof(topic), "%s%s", MQTT_ROOT_TOPIC, "cmd/output_device");
  mqttClient.subscribe(topic, 2);
  zeroBuffer();
  snprintf(topic, sizeof(topic), "%s%s", MQTT_ROOT_TOPIC, "cmd/bt_sink_name");
  mqttClient.subscribe(topic, 2);
#endif
  mqttPublishAvailability(true);
  if (!s_haDiscoveryPublished && mqttHADiscoveryTaskHandle == nullptr) {
    xTaskCreatePinnedToCore(mqttHADiscoveryTask, "haDisc", 4096, nullptr, 1, &mqttHADiscoveryTaskHandle, 0);
  }
  mqttPublishStatus();
  mqttPublishVolume();
  mqttPublishPlaylist();
  mqttPublishBattery();
  mqttPublishOutputDevice(true);
  mqttPublishBtSinkName(true);
  // Ensure HA gets an initial value immediately on connect.
  mqttPublishTrackTime(true);
  mqttPublishStationNumber(true);
}

void mqttPublishStatus() {
  if(mqttClient.connected()){
    zeroBuffer();
    snprintf(topic, sizeof(topic), "%s%s", MQTT_ROOT_TOPIC, "status");
    char name[BUFLEN/2];
    char title[BUFLEN/2];
    config.escapeQuotes(config.station.name, name, sizeof(name)-10);
    config.escapeQuotes(config.station.title, title, sizeof(title)-10);
    const char* mode = "Web Streaming";
#ifdef USE_SD
    if (config.getMode() == PM_SDCARD) mode = "SD Card";
#endif
#ifdef USE_DLNA
    if (config.getMode() != PM_SDCARD && config.store.playlistSource == PL_SRC_DLNA) mode = "DLNA";
#endif
    int brightness = (int)config.store.brightness;
    if (brightness < 0) brightness = 0;
    if (brightness > 100) brightness = 100;
    // Track time fields: meaningful for SD tracks (and web files if ever used).
    const uint32_t tpos = player.getAudioCurrentTime();
    const uint32_t tend = player.getAudioFileDuration();

    snprintf(status, sizeof(status),
             "{\"status\":%d,\"station\":%d,\"name\":\"%s\",\"title\":\"%s\",\"on\":%d,\"mode\":\"%s\",\"brightness\":%d,"
             "\"track_time\":%lu,\"track_duration\":%lu}",
             player.status()==PLAYING ? 1 : 0,
             config.lastStation(),
             name,
             title,
             config.store.dspon ? 1 : 0,
             mode,
             brightness,
             (unsigned long)tpos,
             (unsigned long)tend);
    mqttClient.publish(topic, 0, true, status);

    // Simple retained topics for HA sliders (avoid JSON templates).
    {
      char t2[160];
      snprintf(t2, sizeof(t2), "%s%s", MQTT_ROOT_TOPIC, "brightness");
      char bbuf[8];
      snprintf(bbuf, sizeof(bbuf), "%d", brightness);
      mqttClient.publish(t2, 0, true, bbuf);
    }
    {
      char t2[160];
      snprintf(t2, sizeof(t2), "%s%s", MQTT_ROOT_TOPIC, "mode");
      mqttClient.publish(t2, 0, true, mode);
    }
    mqttPublishStationNumber(false);
    mqttPublishOutputDevice(false);
    mqttPublishBtSinkName(false);
  }
}

static void mqttPublishTrackTime(bool force) {
  #if defined(MQTT_DISABLE) && MQTT_DISABLE
    (void)force;
    return;
  #endif
  if (!mqttClient.connected()) return;

  static uint32_t s_lastMs = 0;
  static uint32_t s_lastT = 0;
  static uint32_t s_lastD = 0;

  const uint32_t now = millis();
  if (!force && s_lastMs != 0 && (uint32_t)(now - s_lastMs) < 1000) return;
  s_lastMs = now;

  const uint32_t t = player.getAudioCurrentTime();
  const uint32_t d = player.getAudioFileDuration();
  if (!force && t == s_lastT && d == s_lastD) return;
  s_lastT = t;
  s_lastD = d;

  char t2[160];
  char buf[16];

  snprintf(t2, sizeof(t2), "%s%s", MQTT_ROOT_TOPIC, "track_time");
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)t);
  mqttClient.publish(t2, 0, true, buf);

  snprintf(t2, sizeof(t2), "%s%s", MQTT_ROOT_TOPIC, "track_duration");
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)d);
  mqttClient.publish(t2, 0, true, buf);
}

static void mqttPublishStationNumber(bool force) {
  #if defined(MQTT_DISABLE) && MQTT_DISABLE
    (void)force;
    return;
  #endif
  if (!mqttClient.connected()) return;

  static int s_last = -1;
  const int cur = (int)config.lastStation();
  if (!force && cur == s_last) return;
  s_last = cur;

  char t2[160];
  char buf[16];
  snprintf(t2, sizeof(t2), "%s%s", MQTT_ROOT_TOPIC, "station_number");
  snprintf(buf, sizeof(buf), "%d", cur);
  mqttClient.publish(t2, 0, true, buf);
}

void mqttLoop() {
  mqttPublishTrackTime(false);
  mqttPublishStationNumber(false);
  mqttPublishOutputDevice(false);
  mqttPublishBtSinkName(false);
}

void mqttPublishPlaylist() {
  if(mqttClient.connected()){
    zeroBuffer();
    snprintf(topic, sizeof(topic), "%s%s", MQTT_ROOT_TOPIC, "playlist");
    snprintf(status, sizeof(status), "http://%s%s", config.ipToStr(WiFi.localIP()), PLAYLIST_PATH);
    mqttClient.publish(topic, 0, true, status);
  }
}

void mqttPublishVolume(){
  if(mqttClient.connected()){
    zeroBuffer();
    char vol[5];
    memset(vol, 0, 5);
    snprintf(topic, sizeof(topic), "%s%s", MQTT_ROOT_TOPIC, "volume");
    int v = config.store.volume;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    snprintf(vol, sizeof(vol), "%d", v);
    mqttClient.publish(topic, 0, true, vol);
  }
}

void mqttPublishBattery() {
  if (!mqttClient.connected()) return;

  // Publish a single retained JSON payload so automations have one place to read from.
  // Keep it short to fit into the existing buffers.
  zeroBuffer();
  snprintf(topic, sizeof(topic), "%s%s", MQTT_ROOT_TOPIC, "battery");

  const bool ready = battery_is_ready();
  const bool usb = battery_usb_present();
  const float pct = ready ? battery_get_percent() : 0.0f;
  const float v = ready ? battery_get_voltage() : 0.0f;
  const float rate = ready ? battery_get_charge_rate() : 0.0f;

  const char* state = "Unknown";
  if (!ready) state = "Unavailable";
  else if (usb && rate > 0.5f) state = "Charging";
  else if (usb && pct >= 99.0f) state = "Full";
  else if (usb) state = "USB powered";
  else if (rate < -0.5f) state = "Discharging";
  else state = "Battery";

  // Example:
  // {"usb":1,"state":"charging","percent":87.2,"voltage":4.041,"rate":12.3}
  snprintf(status, sizeof(status),
           "{\"usb\":%d,\"state\":\"%s\",\"percent\":%.1f,\"voltage\":%.3f,\"rate\":%.1f}",
           usb ? 1 : 0, state, pct, v, rate);
  mqttClient.publish(topic, 0, true, status);

  // Dedicated voltage topic for HA (no JSON/template required).
  {
    char t2[160];
    snprintf(t2, sizeof(t2), "%s%s", MQTT_ROOT_TOPIC, "battery/voltage");
    char vbuf[16];
    snprintf(vbuf, sizeof(vbuf), "%.2f", v);
    mqttClient.publish(t2, 0, true, vbuf);
  }
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  if (WiFi.isConnected()) {
    xTimerStart(mqttReconnectTimer, 0);
  }
}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
  if (len == 0) return;

  // Dedicated command topic: <root>cmd/station_number (HA number)
  {
    char stTopic[160];
    snprintf(stTopic, sizeof(stTopic), "%s%s", MQTT_ROOT_TOPIC, "cmd/station_number");
    if (strcmp(topic, stTopic) == 0) {
      const size_t n = (len < 15) ? len : 15;
      char buf[16];
      strncpy(buf, payload, n);
      buf[n] = '\0';
      // Trim whitespace
      char* s = buf;
      while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
      int sb = atoi(s);
      uint16_t cs = config.playlistLength();
      if (cs == 0) return;
      if (sb < 1) sb = 1;
      if ((uint16_t)sb > cs) sb = (int)cs;

      // Persist immediately so HA reflects the selection right away.
      config.lastStation((uint16_t)sb);
      mqttPublishStationNumber(true);
      mqttPublishStatus();

      player.sendCommand({PR_PLAY, (uint16_t)sb});
      return;
    }
  }

#if defined(BT_COMPANION_ENABLE) && (BT_COMPANION_ENABLE != 0)
  // Dedicated command topic: <root>cmd/bt_sink_name (HA text)
  {
    char snTopic[160];
    snprintf(snTopic, sizeof(snTopic), "%s%s", MQTT_ROOT_TOPIC, "cmd/bt_sink_name");
    if (strcmp(topic, snTopic) == 0) {
      const size_t n = (len < 120) ? len : 120;
      char buf[121];
      strncpy(buf, payload, n);
      buf[n] = '\0';
      // Trim leading whitespace
      char* s = buf;
      while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
      if (*s == '\0') return;

      btcompanion_setSinkName(s);
      mqttPublishBtSinkName(true);

      // If BT output is enabled but not in AUDIO yet, re-issue CONNECT to apply immediately.
      if (btcompanion_enabled() && btcompanion_linkState() != BtCompanionLinkState::AUDIO) {
        btcompanion_requestConnect();
      }
      return;
    }
  }
#endif

  // Dedicated command topic: <root>cmd/output_device (HA select)
#if defined(BT_COMPANION_ENABLE) && (BT_COMPANION_ENABLE != 0)
  {
    char outTopic[160];
    snprintf(outTopic, sizeof(outTopic), "%s%s", MQTT_ROOT_TOPIC, "cmd/output_device");
    if (strcmp(topic, outTopic) == 0) {
      const size_t n = (len < 31) ? len : 31;
      char buf[32];
      strncpy(buf, payload, n);
      buf[n] = '\0';
      // Trim whitespace
      char* s = buf;
      while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;

      const bool wantBt =
        (strcasecmp(s, "bluetooth") == 0) ||
        (strcasecmp(s, "bt") == 0) ||
        (strcmp(s, "1") == 0);

      const bool wantSpk =
        (strcasecmp(s, "speaker") == 0) ||
        (strcasecmp(s, "spk") == 0) ||
        (strcmp(s, "0") == 0);

      if (!wantBt && !wantSpk) return;

      const bool wasBt = (config.store.outputDevice == 1);
      const bool nowBt = wantBt;
      if (nowBt == wasBt) {
        mqttPublishOutputDevice(true);
        return;
      }

      // Save current volume for the current output, then switch outputs and restore.
      const uint8_t curVol = config.store.volume;
      if (wasBt) config.saveValue(&config.store.volumeBt, curVol, false);
      else       config.saveValue(&config.store.volumeSpeaker, curVol, false);

      config.saveValue(&config.store.outputDevice, (uint8_t)(nowBt ? 1 : 0));

      const uint8_t newVol = nowBt ? config.store.volumeBt : config.store.volumeSpeaker;
      config.setVolume(newVol);
      player.setVol(newVol);

      if (nowBt) {
        player.setSpeakerForceMuted(true);
        btcompanion_setEnabled(true);
      } else {
        btcompanion_setEnabled(false);
        player.setSpeakerForceMuted(true);
        player.scheduleSpeakerUnmute(SPEAKER_UNMUTE_DELAY_MS);
      }

      mqttPublishOutputDevice(true);
      mqttPublishStatus();
      return;
    }
  }
#endif

  // Dedicated command topic: <root>cmd/playback_mode (HA select)
  {
    char modeTopic[160];
    snprintf(modeTopic, sizeof(modeTopic), "%s%s", MQTT_ROOT_TOPIC, "cmd/playback_mode");
    if (strcmp(topic, modeTopic) == 0) {
      const size_t n = (len < 31) ? len : 31;
      char buf[32];
      strncpy(buf, payload, n);
      buf[n] = '\0';
      // Trim whitespace
      char* s = buf;
      while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;

#ifdef USE_DLNA
      if (strcasecmp(s, "dlna") == 0) {
        player.sendCommand({PR_SWITCH_PLAYLIST, 1});
        mqttPublishStatus();
        return;
      }
#endif
      if (strcasecmp(s, "sd card") == 0 || strcasecmp(s, "sd") == 0) {
        config.changeMode(PM_SDCARD);
        mqttPublishStatus();
        return;
      }
      if (strcasecmp(s, "web streaming") == 0 || strcasecmp(s, "web") == 0) {
#ifdef USE_DLNA
        if (config.store.playlistSource == PL_SRC_DLNA) {
          player.sendCommand({PR_SWITCH_PLAYLIST, 0});
        }
#endif
        config.changeMode(PM_WEB);
        mqttPublishStatus();
        return;
      }
      return;
    }
  }

  // Dedicated command topic: <root>cmd/volume
  {
    char volTopic[160];
    snprintf(volTopic, sizeof(volTopic), "%s%s", MQTT_ROOT_TOPIC, "cmd/volume");
    if (strcmp(topic, volTopic) == 0) {
      const size_t n = (len < 15) ? len : 15;
      char buf[16];
      strncpy(buf, payload, n);
      buf[n] = '\0';
      int v = atoi(buf);
      if (v < 0) v = 0;
      if (v > 100) v = 100;
      player.setVol((uint8_t)v);
      return;
    }
  }

  // Dedicated command topic: <root>cmd/brightness
  {
    char briTopic[160];
    snprintf(briTopic, sizeof(briTopic), "%s%s", MQTT_ROOT_TOPIC, "cmd/brightness");
    if (strcmp(topic, briTopic) == 0) {
      const size_t n = (len < 15) ? len : 15;
      char buf[16];
      strncpy(buf, payload, n);
      buf[n] = '\0';
      int v = atoi(buf);
      if (v < 0) v = 0;
      if (v > 100) v = 100;
#if (BRIGHTNESS_PIN != 255)
      backlightPlugin.setUserBrightness((uint8_t)v, true);
#else
      config.store.brightness = (uint8_t)v;
      config.setBrightness(true);
      mqttPublishStatus();
#endif
      return;
    }
  }

  // Dedicated command topic: <root>cmd/sleep
  {
    char sleepTopic[160];
    snprintf(sleepTopic, sizeof(sleepTopic), "%s%s", MQTT_ROOT_TOPIC, "cmd/sleep");
    if (strcmp(topic, sleepTopic) == 0) {
      const size_t n = (len < 31) ? len : 31;
      char buf[32];
      strncpy(buf, payload, n);
      buf[n] = '\0';
      if (isTruthyPayload(buf)) mqttEnterSleep();
      return;
    }
  }

  if(len<20){
    char buf[len+1];
    strncpy(buf, payload, len);
    buf[len]='\0';
    if (strcmp(buf, "sleep") == 0 || strcmp(buf, "deepsleep") == 0) { mqttEnterSleep(); return; }
    if (strcmp(buf, "prev") == 0) { player.prev(); return; }
    if (strcmp(buf, "next") == 0) { player.next(); return; }
    if (strcmp(buf, "toggle") == 0) { player.sendCommand({PR_TOGGLE, 0}); return; }
    if (strcmp(buf, "stop") == 0) { player.sendCommand({PR_STOP, 0}); return; }
    if (strcmp(buf, "start") == 0 || strcmp(buf, "play") == 0) { player.sendCommand({PR_PLAY, config.lastStation()}); return; }
    if (strcmp(buf, "boot") == 0 || strcmp(buf, "reboot") == 0) { ESP.restart(); return; }
    if (strcmp(buf, "volm") == 0) {
      player.stepVol(false);
      return;
    }
    if (strcmp(buf, "volp") == 0) {
      player.stepVol(true);
      return;
    }
    if (strcmp(buf, "turnoff") == 0) {
      uint8_t sst = config.store.smartstart;
      config.setDspOn(0);
      player.sendCommand({PR_STOP, 0});
      delay(100);
      config.saveValue(&config.store.smartstart, sst);
      return;
    }
    if (strcmp(buf, "turnon") == 0) {
      config.setDspOn(1);
      if (config.store.smartstart == 1) player.sendCommand({PR_PLAY, config.lastStation()});
      return;
    }
    int volume;
    if ( sscanf(buf, "vol %d", &volume) == 1) {
      if (volume < 0) volume = 0;
      if (volume > 100) volume = 100;
      player.setVol(volume);
      return;
    }
    int sb;
    if (sscanf(buf, "play %d", &sb) == 1 ) {
      if (sb < 1) sb = 1;
      uint16_t cs = config.playlistLength();
      if (sb >= cs) sb = cs;
      player.sendCommand({PR_PLAY, (uint16_t)sb});
      return;
    }

#ifdef USE_SD
    // Mode switch: "mode -1" toggles WEB<->SD (matches encoder double-click behavior),
    // "mode 0" forces WEB, "mode 1" forces SD.
    int newMode;
    if (sscanf(buf, "mode %d", &newMode) == 1) {
      config.changeMode(newMode);
      return;
    }
#endif
  }else{
    if(len>MQTT_BURL_SIZE) return;
    strncpy(player.burl, payload, len);
    player.burl[len]='\0';
    player.sendCommand({PR_BURL, 0});
    return;
  }
  /*if (strstr(buf, "http")==0){
    if(len+1>sizeof(player.burl)) return;
    strlcpy(player.burl, payload, len+1);
    return;
  }*/
}

#endif // #ifdef MQTT_ROOT_TOPIC
