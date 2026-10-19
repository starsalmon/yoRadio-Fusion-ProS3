## April's Speaker (`aprils-speaker`)

Board config: [`boards/aprils_speaker.h`](../boards/aprils_speaker.h) (selected by
`-DYORADIO_BOARD_APRILS_SPEAKER=1`). Env: `aprils-speaker` in [`platformio.ini`](../platformio.ini).
Moved here from the `aprils-speaker` repo (frozen; last commit there e470f33).

### Hardware

| Part | Detail |
|------|--------|
| MCU | Waveshare ESP32-S3-Zero (4 MB flash, 2 MB QSPI PSRAM), native USB for upload/serial |
| HAT | Pimoroni Pirate Audio 3W Amp: ST7789 240x240, PCM5102A DAC + 3 W amp, buttons A/B/X/Y |
| microSD | Separate 3.3 V breakout on the display's SPI bus |
| BT companion | Node ESP32 (NodeMCU-32S), wiring **TBD**, see below |
| Battery | none (`BATTERY_ENABLED 0`) |

Flash layout: [`partitions/partition_4MB_app_spiffs.csv`](../partitions/partition_4MB_app_spiffs.csv)
(single 0x2E0000 app at 0x10000, no OTA, SPIFFS 0x110000). The current image uses 2,983,504 of
3,014,656 bytes (~31 KB free).

### Power

| Pi HAT physical pin | Signal | S3-Zero |
|---------------------|--------|---------|
| **17** | 3V3, **feeds the display logic** | 3V3 (required) |
| 1 | 3V3 | 3V3 (optional second tie) |
| 2 | 5V (amp rail) | 5V |
| 6, 9, 14, 20, 25, 30, 34, 39 | GND | GND |

Backlight on but blank panel usually means pin 17 is not tied to 3V3.

### Pins

| Function | Pi physical pin (BCM) | S3-Zero GPIO | Define |
|----------|-----------------------|--------------|--------|
| TFT MOSI | 19 (10) | 13 | `TFT_MOSI` |
| TFT SCLK | 23 (11) | 12 | `TFT_SCLK` |
| TFT CS | **26** (7, CE1; not 24) | 4 | `TFT_CS` |
| TFT D/C | 21 (9) | 8 | `TFT_DC` |
| Backlight PWM | 33 (13) | 2 | `BRIGHTNESS_PIN` |
| I2S BCK | 12 (18) | 6 | `I2S_BCLK` |
| I2S LRCK | 35 (19) | 7 | `I2S_LRC` |
| I2S DIN | 40 (21) | 5 | `I2S_DOUT` |
| Button A | 29 (5) | 3 | `BTN_LEFT` |
| Button B | 31 (6) | 9 | `BTN_RIGHT` |
| Button X | 36 (16) | 1 | `BTN_CENTER` |
| Button Y | 18 (24); older HATs 38 (20) | 10 | `BTN_MODE` |
| SD CS | breakout | **44** | `SDC_CS` |
| SD MISO (DO) | breakout | **43** | `TFT_MISO` |
| SD SCK / MOSI | breakout | 12 / 13 (shared with TFT) | |
| NeoPixel (onboard) | | 21 | `BUILTIN_NEOPIXEL_PIN` |

- The display runs on SPI2 (`DSP_HSPI 1`) at 20 MHz with the SD card on the same bus
  (`SD_SHARE_TFT_SPI 1`, SD 10 MHz). **GPIO 43/44 are the SD card's pins**; they are not available for
  the BT UART.
- Buttons are active low. The HAT has pull-ups **and** the firmware enables the internal pull-ups too
  (`BTN_INTERNALPULLUP true`).
- GPIO 0 is the BOOT strap button. Per the original wiring notes the castellated header is GPIO 1-13
  and only **GPIO 11** is unused. Which other module pads are usable is not confirmed.

### Controls

| Button | Press | Double-press | Long press |
|--------|-------|--------------|------------|
| A | Volume up (playlist: move, matched to screen) | Previous station | Fast volume up |
| B | Volume down (playlist: move) | Next station | Fast volume down |
| X | Play / stop (BT receive: mute) | Output speaker <-> BT send (no effect until the companion is wired) | Player <-> playlist |
| Y | Next mode (Web -> SD -> Podcast -> Bluetooth -> Web) | Bluetooth receive on/off (falls back to Web until wired) | nothing (`MODE_BUTTON_LONG_PRESS_SLEEP 0`) |

A/B volume direction is swapped by `BTN_VOLUME_INVERT_LR`, the playlist direction by
`BTN_STATIONS_INVERT_LR`. No wake pins are set and auto deep sleep is off
(`AUTO_DEEPSLEEP_IDLE_MINUTES 0`).

### BT companion (TBD, pending wiring)

The BT code is compiled in but **inert**: `BT_COMPANION_PINS_TODO 1` keeps it off every pin and
redirects Bluetooth mode to Web. The UART/wake values in `boards/aprils_speaker.h` (TX 15, RX 16,
wake 11) are placeholders and are **not wired**.

| Signal | S3-Zero | Node ESP32 (proposed, `nodemcu32s_bt_companion`) |
|--------|---------|-------------------------------------------------|
| UART S3 TX -> Node RX | TBD | 16 |
| UART S3 RX <- Node TX | TBD | 17 |
| Wake S3 -> Node (EXT0, active high) | TBD (GPIO 11 is the only known free pin) | 33 |
| I2S BCK | 6 (shared) | 26 |
| I2S LRCK | 7 (shared) | 25 |
| I2S DIN | 5 (shared) | 22 |
| GND | GND | GND |

Receive name: "April's Speaker". To enable: wire it, set the three S3 pins in
`boards/aprils_speaker.h`, set `BT_COMPANION_PINS_TODO 0`, set the Node pins in the companion's
`include/options.h` if they differ, then build both. Protocol: [BT_COMPANION.md](BT_COMPANION.md).

### Build / upload

```bash
pio run -e aprils-speaker                 # build (fw_guard must print both OK lines)
pio run -e aprils-speaker -t upload       # needs platformio_private.ini, see the README
pio run -e aprils-speaker -t uploadfs     # SPIFFS: data/ plus boards/aprils_speaker/data (empty now)
```

Upload is refused unless the port in `platformio_private.ini` (gitignored; copy
`platformio_private.ini.example`) has the S3-Zero's MAC, and while the "GoL" ProS3 is plugged in. The
check covers `upload` only, not `uploadfs`.

### Status

Built from this repo, not yet flashed from it. Differences from the old repo to bench-test: the
display uses yoRadio's shared-bus lock instead of April's `startWrite` mutex. Not ported (not needed
on a PSRAM S3, or would change the ProS3): the no-PSRAM paths, April's boot reorder, the cached SD
index, the clock-TTS gate and `HIDE_VU`.
