## yoRadio Fusion – PROS3 (ESP32‑S3) build

This is a PROS3-focused fork of [`SimZs/yoRadio-Fusion`](https://github.com/SimZs/yoRadio-Fusion) (which itself builds on e2002’s ёRadio). It’s set up for **Unexpected Maker PROS3 (ESP32‑S3)** using **PlatformIO** and tuned around **ILI9341 + PSRAM**.

### Quick start

- **Build firmware**:

```bash
platformio run -e yoradio-um_pros3-ili9341
```

- **Upload firmware**:

```bash
platformio run -e yoradio-um_pros3-ili9341 -t upload
```

- **Build/upload filesystem (SPIFFS)**:

```bash
platformio run -e yoradio-um_pros3-ili9341 -t uploadfs
```

- **Serial monitor**:

```bash
platformio device monitor -b 115200
```

### Repo layout (the parts you’ll actually touch)

- **User config**: `myoptions.h`
- **PlatformIO env**: `platformio.ini` (`yoradio-um_pros3-ili9341`)
- **Playlist**: `data/data/playlist.csv` (`Name<TAB>URL<TAB>0`)
- **Wi‑Fi credentials**: `data/data/wifi.csv` (gitignored) from `data/data/wifi.example.csv`
- **Logos source**: `images_src/station_logos/` (tracked)
- **Generated logos (SPIFFS)**: `data/logos/*.ylg` (gitignored)

### Docs index

- **What changed vs upstream**: `docs/CHANGES_SINCE_UPSTREAM.md`
- **Worklog / polish notes (why this fork exists)**: `docs/WORKLOG_AND_POLISH_NOTES.md`
- **Controls (buttons/encoders/IR/touch)**: `docs/CONTROLS.md`
- **Known issues / rough edges**: `docs/KNOWN_ISSUES.md`
- **Station logo pipeline (source images → SPIFFS `.ylg`)**: `images_src/station_logos/README.md`
- **Plugin API docs**: `src/pluginsManager/README.md`
- **Plugins folder notes**: `src/plugins/README.md`
- **IRremote locales** (upstream lib docs): `src/IRremoteESP8266/locale/README.md`

## What’s custom in this fork (high signal)

- **PROS3 hardware bring-up**: LDO2 (3V3_AUX) enable + optional external antenna init in `src/yoradio_user.cpp`
- **Battery gauge**: MAX17048 via I2C (can be disabled in `myoptions.h` with `BATTERY_ENABLED 0`)
- **Deep sleep power management**:
  - Wake pins: `WAKE_PIN1` + optional `WAKE_PIN2` (RTC GPIO only) via ext1 wake
  - Auto deep sleep (when wake pins are configured): `AUTO_DEEPSLEEP_IDLE_MINUTES`, `AUTO_DEEPSLEEP_BATT_PCT`
- **5V present sense**: `CHARGE_SENSE_PIN` + charging icon in footer when 5V is present
- **Display tuning**:
  - `TFT_SPI_FREQ` is used by the ILI9341 driver and logged at boot as `##[BOOT]# TFT_SPI_FREQ <value>`
- **Date format mapping fixed (web UI + TFT)**: `dateFormat` is now consistent (see below)
- **Station logos (SPIFFS `.ylg`)**:
  - Logos are generated from `images_src/station_logos/` into `data/logos/*.ylg`
  - PNG alpha is supported (alpha → RGB565 color-key `0xF81F`; firmware treats that key as transparent)
  - Lookup uses the **stable playlist name** so logos don’t disappear when ICY metadata changes the displayed station title
  - `.ylg` header is packed (14 bytes) in both generator and firmware
- **Audio/UI performance work** (opt-in diagnostics + throttling):
  - 1Hz audio diagnostics (`[AUD] …`) and display loop diagnostics (`[DSP] …`)
  - Adaptive VU throttling based on buffer health and audio loop time
  - Text scrolling throttles during playback to avoid starving audio
- **Moode playlist import helper**:
  - `tools/moode/export_moode_radio_to_yoradio_csv.py` can pull stations from `http://moode.local` and merge into `data/data/playlist.csv`
- **Filesystem ordering fixes**: SPIFFS mount timing fixed (theme load only after `SPIFFS.begin()`)

## Date format mapping (`dateFormat`)

Source of truth is `src/displays/widgets/widgets.cpp`:

- `0`: `DD/MM/YYYY`
- `1`: `DOW - DD MONTH`
- `2`: `DOW - DD/MM/YYYY`
- `3`: `DOW - MONTH DD`
- `4`: `DOW - MM/DD/YYYY`
- `5`: `MONTH DD, YYYY`

## MQTT + Home Assistant (this fork)

MQTT is enabled/disabled via `MQTT_DISABLE` in `myoptions.h`. This fork includes Home Assistant MQTT discovery and additional state topics.

- **Key options**:
  - `MQTT_DISABLE`: set to `1` to disable MQTT entirely
  - `MQTT_QUIET_LOGS`: set to `1` to suppress noisy AsyncMqttClient INFO logs on Serial

- **State topics (retained)** (assuming `MQTT_ROOT_TOPIC` already ends with `/`):
  - `availability`: `online|offline` (LWT is `offline`)
  - `status`: JSON including playback + station + mode + brightness
  - `mode`: `Web Streaming|SD Card|DLNA`
  - `volume`: `0..100`
  - `brightness`: `0..100`
  - `playlist`: `http://<ip>/playlist`
  - `battery`: JSON (`usb/state/percent/voltage/rate`)
  - `battery/voltage`: `X.XX`

- **Command topics**:
  - `command`: legacy text commands (prev/next/toggle/play n/vol n/…)
  - `cmd/sleep`: enter deep sleep (recommended vs `command`)
  - `cmd/volume`: `0..100`
  - `cmd/brightness`: `0..100`

- **Home Assistant discovery (retained)**:
  - Published under `homeassistant/<component>/<nodeId>/<objectId>/config`
  - Exposes sensors + buttons + sliders (volume/brightness stay in sync)

## PlatformIO “pre” scripts (important)

Your `platformio.ini` runs these:

- `pre:tools/pio/apply_audio_idf_mod.py`
  - Applies patched `liblwip.a` + `libesp_netif.a` (high-bitrate/audio stability work)
  - Applies the Adafruit GFX `glcdfont` glyph set patch (icon glyphs)
- `pre:tools/pio/ha_board_meta.py`
  - Injects HA device metadata defines at build time
- `pre:tools/pio/pre_fs_generate_logos.py`
  - Runs the logo generator during `buildfs/uploadfs` so SPIFFS always matches your source images/playlist

## Playlists

`data/data/playlist.csv` is tab-delimited:

```
Station Name<TAB>https://example/stream<TAB>0
```

### Import stations from Moode (fast)

If Moode is reachable from your dev machine:

```bash
python3 tools/moode/export_moode_radio_to_yoradio_csv.py \
  --base-url "http://moode.local" \
  --merge \
  --playlist "data/data/playlist.csv"
```

This pulls Moode’s `cfg_radio` from `command/cfg-table.php?cmd=get_cfg_tables` and appends new stations by unique URL.

## Station logos

See `images_src/station_logos/README.md` for the full pipeline. Summary:

- **Source**: `images_src/station_logos/*.png|*.jpg` (tracked)
- **Output**: `data/logos/*.ylg` + `data/logos/index.tsv` (gitignored; uploaded to SPIFFS)
- **Default logo**: `images_src/station_logos/default_logo.png` → `/logos/default.ylg`
- **Generation** runs automatically on filesystem builds:

```bash
platformio run -e yoradio-um_pros3-ili9341 -t uploadfs
```

## Controls

The full upstream-style controls reference (buttons/encoders/IR/touch) lives here:

- `docs/CONTROLS.md`

## Diagnostics / debug toggles

All the usual “turn on logging” knobs live in `myoptions.h` under `/* DIAGNOSTICS */` (append-only section).

- **Display**:
  - `DSP_DIAG_LOG`: prints `DspTask.core` and `[DSP] …` loop timing
  - `VU_PERF_LOG`: prints VU draw timing stats
- **Audio**:
  - `AUDIO_DIAG_LOG`: prints `[AUD] …` once per second (codec, SR/ch/br, buffer %, loop times, task HWM)
  - `AUDIO_DIAG_LOG_INTERVAL_MS`: cadence
- **Task/core pinning (advanced)**:
  - `AUDIO_TASK_CORE_ID`, `DSP_TASK_CORE_ID`
- **Controls polling task (optional)**:
  - Defaults to “poll on every Arduino loop”
  - You can experiment with a separate polling task via `CONTROLS_TASK_ENABLE`

## Known issues (honest status)

Short version (full list in `docs/KNOWN_ISSUES.md`):

- **SD → WEB → HLS AAC stall (real bug)**: certain HLS AAC stations may fail to start after SD playback.
- **Some AAC stations are “CPU heavy”** and can make UI responsiveness / VU refresh worse.
- **FLAC**: currently very choppy.
- **SD playback**: still has edge-case instability; album art is disabled.

## TODO / Roadmap

- **Fix up invalid pin warnings at boot**: find what is generating `__pinMode(): Invalid IO 255 selected`
- **Fix SD → WEB → HLS AAC stall properly** (not just workarounds)
- **More controls via MQTT**: add other controls that only exist in the web UI (tone, smart start, auto dimming, screensaver), and include some fork-only features too
- **Test/refine low battery cutoff**: not properly tested
- **Theme switching**: add a way to select/switch themes (web UI/config + persist chosen theme)
- **Boot screen improvement**: simple animation + build info; unicode SSID handling
- **IR control UX**: set up receiver + on-screen “IR RX” indicator
- **Station logo workflow polish**: improve matching/coverage; automate maintaining the local image library
- **SD playback resume**: track resume is implemented; resume *position* still WIP
- **Album art**: revisit later (needs stable decoder/task model)
- **Output via bluetooth**: may not be possible without a dedicated BT module
- **Podcast mode**: dedicated mode to stream recent episodes from podcast feeds
- **Load station logos from SD**: might be easier long-term; likely needs stable image decode first
- **Configure new features from web**: allow setting options like `BATTERY_ENABLED`, `AUTO_DEEPSLEEP_IDLE_MINUTES`, `AUTO_DEEPSLEEP_BATT_PCT` from the web interface
- **Reduce blocking patterns and busy-waits** (queue allocation loops, MQTT playlist block)