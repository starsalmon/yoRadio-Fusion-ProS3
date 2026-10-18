// Companion ESP32 (Classic BT) controller for A2DP TX and A2DP RX (sink).
// Used when the main MCU is ESP32-S3 (BLE-only) but you still want Classic BT audio.
#pragma once

#include <Arduino.h>

void btcompanion_init();
void btcompanion_loop();
// Call from blocking setup/mode-change waits so RX handover UART can progress.
void btcompanion_pump();

// A2DP TX output (ProS3 I2S -> HiGrow -> external BT speaker).
void btcompanion_setEnabled(bool enable);
bool btcompanion_enabled();
void btcompanion_toggle();
void btcompanion_forceSleep();
void btcompanion_setSinkName(const char* name);
const char* btcompanion_sinkName();
void btcompanion_requestConnect();
void btcompanion_setPcmSampleRate(uint32_t hz);

enum class BtCompanionLinkState : uint8_t {
  OFF = 0,
  SEARCHING = 1,
  CONNECTED = 2,
  AUDIO = 3,
};
BtCompanionLinkState btcompanion_linkState();

// A2DP RX input (phone -> HiGrow -> shared I2S DAC). Mutually exclusive with TX enable.
bool btcompanion_rxActive();
void btcompanion_enterRxMode(const char* localName);
void btcompanion_exitRxMode();
BtCompanionLinkState btcompanion_rxLinkState();
void btcompanion_getRxMetadata(char* title, size_t titleLen, char* artist, size_t artistLen);
void btcompanion_getRxAlbum(char* album, size_t albumLen);
// Elapsed/total of the phone's current track (elapsed interpolated while playing).
// False when there is no track length (some apps don't send one) or no phone connected.
bool btcompanion_getRxProgress(uint32_t* elapsedMs, uint32_t* totalMs, bool* playing);
void btcompanion_setRxLocalName(const char* name);
const char* btcompanion_rxLocalName();
// Map yoRadio volume (0..255) to companion AVRCP volume (0..127).
void btcompanion_setRxVolume(uint8_t volume);
void btcompanion_rxAvrcpToggle();
// BT RX knob press: software mute (VOL 0 to HiGrow) / unmute (restore mapped volume).
void btcompanion_rxToggleMute();
bool btcompanion_rxMuted();
// HiGrow confirmed I2S master for this RX session and the settle delay has passed (amp may enable).
bool btcompanion_rxBusReady();
// True once after MODE TX handover finishes (consume from main loop to reclaim I2S).
bool btcompanion_rxExitComplete();
