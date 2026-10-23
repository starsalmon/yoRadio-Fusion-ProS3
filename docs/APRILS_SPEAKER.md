## April's Speaker (`aprils-speaker`)

Board config: [`boards/aprils_speaker.h`](../boards/aprils_speaker.h) (selected by
`-DYORADIO_BOARD_APRILS_SPEAKER=1`). Env: `aprils-speaker` in [`platformio.ini`](../platformio.ini).
Moved here from the `aprils-speaker` repo (frozen; last commit there e470f33, which targeted the
Waveshare ESP32-S3-Zero). The speaker now uses an **Unexpected Maker FeatherS3**. The main PCB is
not final, so **every pin below is PROPOSED** until it is wired and bench-tested.

### Hardware

| Part | Detail |
|------|--------|
| MCU | Unexpected Maker FeatherS3: ESP32-S3, 16 MB QSPI flash, 8 MB QSPI PSRAM, native USB (PlatformIO board `um_feathers3`) |
| HAT | Pimoroni Pirate Audio 3W Amp: ST7789 240x240, 2x MAX98357A I2S DAC+amp (no MCLK; amp enable BCM25 pin 22, driven by FeatherS3 IO44 `AMP_EN` - verify pin 22 before any 5 V), buttons A/B/X/Y |
| microSD | Separate 3.3 V breakout on the display's SPI bus (SD park design, as on the ProS3) |
| BT companion | Node ESP32 (NodeMCU-32S), pins assigned below (not yet bench-tested) |
| 5 V | Pololu S13V10F5 (#4083) 5 V 1 A buck-boost from FeatherS3 BAT, feeds HAT pins 2/4 (and, proposed, the Node 5V) |
| Battery | LiPo on the FeatherS3 JST; VBAT sense IO2, VBUS sense IO34 (`src/battery/battery_adc.cpp`) |

FeatherS3 references (Unexpected Maker):
- Product page: <https://esp32s3.com/feathers3.html>
- Pinout card + schematic: <https://github.com/UnexpectedMaker/esp32s3> (`Pinout Cards/FeatherS3_Pin_Reference.png`, `schematics/`)
- Arduino variant: [`variants/um_feathers3/pins_arduino.h`](https://github.com/espressif/arduino-esp32/blob/master/variants/um_feathers3/pins_arduino.h)
- UM helper library (VBAT/VBUS/LDO2): <https://github.com/UnexpectedMaker/esp32s3-arduino-helper> (`src/UMS3.h`)

Fixed on the board (not free): IO2 VBAT sense (442k/160k divider, x3.7625), IO34 VBUS sense, IO4
ambient light sensor, IO13 blue LED, IO39 LDO2 enable, IO40 RGB LED data (RGB powered from LDO2),
IO19/20 USB D-/D+ (not on the header), IO0 BOOT button. Strapping pins on the header: 0 and 3.

Flash layout: [`partitions/partition_16MB_ota_spiffs_bigspiffs.csv`](../partitions/partition_16MB_ota_spiffs_bigspiffs.csv),
the same as the ProS3 (app0 at 0x10000, 5 MB per OTA slot). The current image uses 2,997,695 of
5,242,880 bytes (57 %). PSRAM: `board_build.psram = qspi`, `BOARD_HAS_PSRAM`.

### Pins (PROPOSED)

| Function | Pi HAT pin (BCM) | FeatherS3 GPIO | Define | Why |
|----------|------------------|----------------|--------|-----|
| TFT + SD SCLK | 23 (11) | **36** (SCK) | default `SPI` | Feather HW SPI default (FSPI IOMUX); PSRAM is quad so 35-37 are free |
| TFT + SD MOSI | 19 (10) | **35** (MOSI) | default `SPI` | as above |
| SD MISO | breakout | **37** (MISO) | default `SPI` | as above; the HAT doesn't use MISO |
| TFT CS | 26 (7, CE1) | **38** | `TFT_CS` | non-RTC pin, keeps RTC pins for buttons |
| TFT D/C | 21 (9) | **33** | `TFT_DC` | non-RTC pin |
| TFT reset | | none | `TFT_RST -1` | the HAT has no reset line |
| Backlight PWM | 33 (13) | **10** | `BRIGHTNESS_PIN` | any LEDC pin |
| SD CS | breakout | **1** | `SDC_CS` | not strapping; pulled HIGH at boot (SD park) |
| I2S BCK | 12 (18) | **17** | `I2S_BCLK` | plain GPIO, next to LRCK on the header |
| I2S LRCK | 35 (19) | **18** | `I2S_LRC` | |
| I2S DIN | 40 (21) | **14** | `I2S_DOUT` | |
| Button A | 29 (5) | **5** | `BTN_LEFT` | RTC GPIO |
| Button B | 31 (6) | **6** | `BTN_RIGHT` | RTC GPIO |
| Button X | 36 (16) | **12** | `BTN_CENTER` | RTC GPIO |
| Button Y | 18 (24); older HATs 38 (20) | **11** | `BTN_MODE` | RTC GPIO; deep-sleep wake (`WAKE_PIN1`) |
| NeoPixel (onboard) | | 40 | `BUILTIN_NEOPIXEL_PIN` | fixed; needs LDO2 on |
| Battery | | 2 (VBAT), 34 (VBUS) | `BATTERY_ADC_PIN`, `CHARGE_SENSE_PIN` | fixed |
| BT UART1 TX -> Node RX2 (16) | | **15** | `BT_COMPANION_UART_TX` | vertical STEMMA QT (LDO2 pads; logic only, do not power the Node from it) |
| BT UART1 RX <- Node TX2 (17) | | **16** | `BT_COMPANION_UART_RX` | also the Node-flash bridge |
| BT wake -> Node 33 | | **7** | `BT_COMPANION_WAKE_PIN` | free GPIO |
| Node EN (reset) | | **9** | `BT_COMPANION_NODE_EN_PIN` | open-drain, low = reset |
| Node IO0 (boot) | | **8** | `BT_COMPANION_NODE_BOOT_PIN` | open-drain, low only to enter the ROM loader |
| Amp enable, HAT pin 22 (BCM25) | 22 (25) | **44** | `AMP_EN_PIN` = `MUTE_PIN` (`MUTE_VAL LOW`) | low when stopped / asleep, on during BT receive; MOSFET optional |

Avoided: 0 and 3 (strapping), 19/20 (USB), 39/40 (LDO2/RGB), 4 (light sensor), 13 (LED). Spare: 43
(carries the ROM boot log at reset, so it is not used for anything the Node listens to).

- TFT and SD share the default SPI bus at 20 MHz / 10 MHz (`SD_SHARE_TFT_SPI 1`, no `DSP_HSPI`), the
  same code path as the ProS3; `yoradio_user.cpp` enables LDO2 and drives `SDC_CS`/`TFT_CS` HIGH early.
- Buttons are active low. The HAT has pull-ups **and** the firmware enables the internal pull-ups.
- The companion link is on UART1 (15/16), so the S3 ROM boot log on IO43 no longer reaches the Node.

### Power

| Pi HAT pin | Signal | FeatherS3 |
|------------|--------|-----------|
| **17** | 3V3, **feeds the display logic** | 3V3 (LDO1) |
| 1 | 3V3 | 3V3 (LDO1) |
| 2 / 4 | 5V (amp rail) | Pololu S13V10F5 VOUT (VIN = FeatherS3 BAT) |
| 6, 9, 14, 20, 25, 30, 34, 39 | GND | GND |

- Feed the HAT's 3V3 from **LDO1** (always on). LDO2 turns off in deep sleep, so HAT pull-ups on LDO2
  would float the buttons and could cause false wakes.
- The SD breakout can go on **LDO2** (3V3_2), like the ProS3, which keeps the card's inrush off the
  MCU rail.
- Pololu S13V10F5: 1 A max (S13V15F5/S13V20F5 are pin-compatible upgrades); no enable pin, 10-20 mA idle
  drain from the battery even in deep sleep - add a switch if needed.
- Pin 22: Pimoroni documents it as the 3.3 V amp ENABLE input. Verify before putting 5 V on it; if it
  is really a power pin, switch the 5 V with a MOSFET from IO44.
- Series resistors (~100 R) on the SD lines, as on the ProS3 PCB, are recommended.
- Backlight on but blank panel usually means pin 17 is not tied to 3V3.

### Battery

`BATTERY_ADC_PIN 2`, `BATTERY_ADC_SCALE 3.7625` (UM divider), empty 3.30 V / full 4.20 V. Median of 8
reads every 30 s, outliers rejected; `CHARGE_SENSE_PIN 34` (VBUS) reports USB present. Published over
MQTT; the on-screen battery footer is hidden (`HIDE_BAT 1`, no 240x240 layout yet). At 5 % on battery
(no USB) it deep-sleeps; Y wakes it. Idle auto sleep stays off.

### Controls

| Button | Press | Double-press | Long press |
|--------|-------|--------------|------------|
| A | Volume up (playlist: move, matched to screen) | Previous station | Fast volume up |
| B | Volume down (playlist: move) | Next station | Fast volume down |
| X | Play / stop (BT receive: mute) | Output speaker <-> BT send (ignored until the companion is wired) | Player <-> playlist |
| Y | Next mode (Web -> SD -> Podcast -> Bluetooth -> Web) | Bluetooth receive on/off (falls back to Web until wired) | nothing (`MODE_BUTTON_LONG_PRESS_SLEEP 0`) |

A/B volume direction is swapped by `BTN_VOLUME_INVERT_LR`, the playlist direction by
`BTN_STATIONS_INVERT_LR`.

### BT companion

| Signal | FeatherS3 | Node ESP32 (`nodemcu32s_bt_companion`) |
|--------|-----------|----------------------------------------|
| UART1 S3 TX -> Node RX2 | 15 | 16 |
| UART1 S3 RX <- Node TX2 | 16 | 17 |
| Wake S3 -> Node (EXT0, active high) | 7 | 33 |
| Reset (open-drain) | 9 | EN |
| Boot select (open-drain) | 8 | IO0 |
| I2S BCK | 17 (shared with DAC) | 26 |
| I2S LRCK | 18 (shared) | 25 |
| I2S data | 14 (shared) | 22 |
| GND | GND | GND |

Receive name: "April's Speaker". Only one side drives the shared I2S lines; the Node sets them to
high-impedance inputs first thing in `setup()` and whenever it is not the active source.
Protocol: [BT_COMPANION.md](BT_COMPANION.md). Wiring PDF: [aprils-speaker-wiring.pdf](aprils-speaker-wiring.pdf).

### Flashing the Node through the FeatherS3

The S3 can act as a USB <-> UART bridge to the Node's ROM loader (`src/core/node_flash.cpp`):

```bash
curl "http://<speaker-ip>/set?nodeflash=1"      # or type: nodeflash 1  on the USB console
# S3 reboots into the bridge (no WiFi/audio), holds Node IO0 low and pulses EN
esptool --chip esp32 --port /dev/cu.usbmodemXXXX --before no-reset --after no-reset -b 460800 \
  write-flash 0x10000 .pio/build/nodemcu32s_bt_companion/firmware.bin
```

- `--before no-reset` is required: esptool's DTR/RTS reset would reset the FeatherS3, not the Node.
  (esptool 4.x spelling: `--before no_reset --after no_reset write_flash`.)
- Baud changes are followed automatically (the bridge watches esptool's CHANGE_BAUDRATE command).
- 30 s after the last traffic (5 min if nothing connects) the S3 resets the Node into its app and
  restarts normally. If the S3 is reset mid-session it releases the Node into its app on the next boot.

### Build / upload

```bash
pio run -e aprils-speaker                 # build (fw_guard must print both OK lines)
pio run -e aprils-speaker -t upload       # needs platformio_private.ini, see below
pio run -e aprils-speaker -t uploadfs     # SPIFFS: data/ plus boards/aprils_speaker/data (empty now)
```

`tools/pio/verify_aprils_speaker_upload.py` runs before `upload` **and** `uploadfs`. It reads USB
descriptors only (no port is opened) and refuses unless:
1. `upload_port` is set in `platformio_private.ini` (gitignored; copy `platformio_private.ini.example`)
   and is connected;
2. it is the **only** Espressif (VID 0x303A) device plugged in;
3. it is not a ProS3 (product "ProS3", or a MAC in `custom_refuse_macs`, default the GoL ProS3);
4. it is a FeatherS3: PID 0x80D0/0x80D6 or product "FeatherS3" (UM/TinyUSB firmware), or, on the
   generic USB-Serial/JTAG PID 0x1001 (yoRadio itself, ROM download mode, identical on every S3), its
   serial number (the MAC) equals `custom_upload_mac`. Get the MAC from `pio device list` with only
   the FeatherS3 plugged in.

### Status

Builds from this repo; never flashed from it, and never run on a FeatherS3. Bench-test: the pin map,
the shared-bus display lock (instead of April's old `startWrite` mutex), battery readings. Not ported
(not needed on a PSRAM S3, or would change the ProS3): the no-PSRAM paths, April's boot reorder, the
cached SD index, the clock-TTS gate and `HIDE_VU`.

Wiring map (PDF): [aprils-speaker-wiring.pdf](aprils-speaker-wiring.pdf)
