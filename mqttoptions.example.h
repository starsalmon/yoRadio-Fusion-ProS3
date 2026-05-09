#pragma once
// Copy this file to `mqttoptions.h` and fill in your values.
// `mqttoptions.h` is gitignored to avoid committing secrets.

#define MQTT_HOST "192.168.x.x"     // e.g. your Home Assistant / broker IP
#define MQTT_PORT 1883
#define MQTT_USER "yoradio"
#define MQTT_PASS "change-me"

#define MQTT_ROOT_TOPIC  "yoradio/100/"
#define MQTT_CLIENT_ID "yoradio-pros3"

/*
Topics:
MQTT_ROOT_TOPIC/command     // Commands
MQTT_ROOT_TOPIC/status      // Player status
MQTT_ROOT_TOPIC/playlist    // Playlist URL
MQTT_ROOT_TOPIC/volume      // Current volume
MQTT_ROOT_TOPIC/battery     // Battery + power (usb/percent/voltage/state)

Commands:
prev          // prev station
next          // next station
toggle        // start/stop playing
stop          // stop playing
start, play   // start playing
boot, reboot  // reboot
vol x         // set volume
play x        // play station x
*/

