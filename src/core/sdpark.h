#pragma once
// SD card "parking" for the shared TFT/SD SPI bus (UM ProS3 board).
//
// An SD card powers up in SD-native mode, where its CMD pin is our MOSI. On this PCB the SD socket
// and the TFT sit on the same side of the 100R series resistors, so a native-mode card that decodes
// a command out of TFT traffic answers by driving MOSI and overpowers the ESP32 right at the panel
// input -> corrupted panel command (white screen / garbage). Sending CMD0 with SD_CS low puts the
// card into SPI mode, where it ignores MOSI and releases DO whenever SD_CS is high.
//
// sdpark_boot(): once in setup(), after LDO2-on and before display.init().
// sdpark_poll(): display task, between draws; parks a hot-inserted card and reports transitions.
// Both are no-ops while the card is mounted (sdman.ready) or being indexed; SD.begin() re-runs the
// full init (clocks, CMD0, CMD8, ACMD41) later regardless, so mounting is unaffected.
#include "options.h"

#if defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI && (SDC_CS != 255)
#define SDPARK_ENABLED 1
void sdpark_boot();
// Returns +1 when a card appeared (absent -> present), -1 when it disappeared, 0 otherwise
// (no change, or skipped because the card is mounted/indexing).
int sdpark_poll();
#endif
