## ProS3 radio (`yoradio-um_pros3-ili9341`)

Board config: [`boards/pros3.h`](../boards/pros3.h) (the default; `myoptions.h` selects it when no
board flag is set). Env: `yoradio-um_pros3-ili9341` in [`platformio.ini`](../platformio.ini).

### Hardware

| Part | Detail |
|------|--------|
| MCU | Unexpected Maker ProS3 (ESP32-S3, 16 MB flash, QSPI PSRAM) |
| Display | ILI9341 on the ProS3 default SPI: CS 4, DC 5, RST 2, backlight PWM 7, `TFT_SPI_FREQ` 40 MHz |
| microSD | CS 1, **same SPI bus as the TFT** (`SD_SHARE_TFT_SPI 1`), `SDSPISPEED` 10 MHz |
| Audio | I2S DOUT 40, BCLK 41, LRC 42; amp/boost enable on `MUTE_PIN` 39 (LOW = off) |
| Encoder | A/B 16/15, push 13 (internal pull-ups) |
| Mode button | GPIO 12, button LED on GPIO 34 |
| IR receiver | GPIO 21 |
| NeoPixel ring | 8 px on GPIO 14 (onboard WS2812 on 18 is only cleared at boot) |
| Battery gauge | MAX17048 on I2C SDA 8 / SCL 9, ALRT on 10 |
| Light sensor | BH1750 on the same I2C bus (0x23) |
| Power | LDO2 (3V3_AUX) enable 17, 5 V sense 33, RF switch 11 (external antenna at boot) |
| BT companion | LilyGo HiGrow (classic ESP32), see [BT_COMPANION.md](BT_COMPANION.md) |

### PCB

First PCB revision (2026-06-03):

- EasyEDA Pro project: [`hardware/easyeda/ProPrj_yoRadio-Fusion-ProS3_2026-06-03`](hardware/easyeda/ProPrj_yoRadio-Fusion-ProS3_2026-06-03)
  (a ZIP container with `*.esch`, `*.epcb` and libraries; copy it to `*.zip` to extract)
- Schematic PDF: [`yoRadio-Fusion-ProS3_Schematic1_2026-06-03.pdf`](yoRadio-Fusion-ProS3_Schematic1_2026-06-03.pdf)
- Gerbers / drill / fab files: [`hardware/pcb/yoRadio-Fusion-ProS3_PCB1_2026-06-03.zip`](hardware/pcb/yoRadio-Fusion-ProS3_PCB1_2026-06-03.zip)

Firmware changes must work on this PCB as built; no hardware changes are assumed.

### Controls

| Input | Action |
|-------|--------|
| Knob turn | Volume (player screen) / move (playlist) |
| Knob press | Play / stop. **In Bluetooth receive mode: mute / unmute** (sends `VOL 0`, restores on unmute) |
| Knob double-press | Output: internal speaker <-> BT headphones (A2DP send). Ignored in Bluetooth receive mode |
| Knob long press | Player <-> playlist |
| Mode press | Next mode: Web -> SD -> Podcast -> Bluetooth receive -> Web |
| Mode double-press | Enter Bluetooth receive / return to the previous mode |
| Mode long press | Deep sleep (`MODE_BUTTON_LONG_PRESS_SLEEP`, default on) |
| Wake from sleep | Mode button, knob press or IR (`WAKE_PIN1..3`) |
| IR remote | Upstream IR recorder (web UI: Settings > controls > IR Recorder) |

### Power / sleep

- Auto deep sleep after `AUTO_DEEPSLEEP_IDLE_MINUTES` (15) with nothing playing, or immediately when
  the battery is at `AUTO_DEEPSLEEP_BATT_PCT` (5 %) or the gauge alert fires and there is no 5 V.
- **Bluetooth receive only counts as playing while the companion reports A2DP audio** (`STATUS ...
  audio=` streaming), sticky for 60 s after the last streaming status. Waiting, paused or
  disconnected BT still auto-sleeps.
- The amp/boost (`MUTE_PIN`) stays on once ProS3 playback has started; it is switched off only for
  BT handover, BT output, sleep and restart (each toggle used to glitch the shared SPI bus).

### SD on the shared SPI bus

The TFT and SD card share one SPI bus. What makes that work:

- **CMD12 fix** (`-DYORADIO_SD_DISKIO_FIX=1`): upstream `sd_diskio.cpp` read a data byte of the
  in-flight block as CMD12's R1 after multi-block reads ("crc error" / "token error [12]", tracks
  ending after ~8 s). [`tools/pio/patch_sd_shared_spi.py`](../tools/pio/patch_sd_shared_spi.py)
  patches the framework copy at build time (pristine backup `sd_diskio.cpp.yoradio-orig`) and also
  adds the shared-bus lock hooks.
- **SD park** (`src/core/sdpark.cpp`): a CMD0 probe at boot (before display/SD) and every 1.5 s in
  the display loop, so a hot-inserted card is put into SPI mode and stops driving MOSI; the panel then
  gets one hardware re-init. See [KNOWN_ISSUES.md](KNOWN_ISSUES.md) for the brief white flash.
- SD code never drives `TFT_CS`; mount and I/O run under the recursive shared-bus lock.

### Tuned lwip + post-build guard

- [`tools/pio/apply_audio_idf_mod.py`](../tools/pio/apply_audio_idf_mod.py) swaps in the patched
  `liblwip.a` / `libesp_netif.a` from `tools/Audio_IDF_MOD` before the link. Stock 3.3.6 lwip gives
  ~1.5 s web-radio stalls.
- The shared framework under `~/.platformio` is **reinstalled** when another project (for example the
  BT companion) or an env with a different `custom_sdkconfig` builds. That restores stock lwip and
  stock `sd_diskio.cpp`; the pre-scripts re-apply both on the next build.
- [`tools/pio/fw_guard.py`](../tools/pio/fw_guard.py) checks every image after the build: lwip
  function sizes (`netconn_alloc` 0x78, `tcp_recved` 0x72, `poll_tcp` 0x69, `tcp_rst_common` 0x5f)
  and, with the SD fix enabled, the CMD12 strings. A bad image is renamed `firmware.bin.REJECTED` and
  the build fails. Expect these lines:

```
[fw-guard] lwip: Audio_IDF_MOD build linked (OK)
[fw-guard] sd_diskio: CMD12 fix linked (OK)
```

### Flashing

See [Safe flashing](../README.md#safe-flashing) in the top-level README (app only at 0x10000, never
older than `milestone-2026-10-18-bt-rx-stable`).
