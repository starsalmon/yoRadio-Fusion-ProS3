#pragma once
// Flash the BT companion (classic ESP32) through the host S3's USB port.
// Needs BT_COMPANION_NODE_EN_PIN + BT_COMPANION_NODE_BOOT_PIN (board header); otherwise all no-ops.
// Usage: docs/APRILS_SPEAKER.md "Updating the Node through the FeatherS3".
void nodeflash_boot();      // call first thing in setup(): releases EN/IO0; runs the bridge if requested (never returns then)
void nodeflash_request();   // web/serial "nodeflash": set the RTC flag and reboot into the bridge
bool nodeflash_supported();
