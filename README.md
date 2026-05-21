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

- **User config**: [`myoptions.h`](myoptions.h)
- **PlatformIO env**: [`platformio.ini`](platformio.ini) (`yoradio-um_pros3-ili9341`)
- **Playlist**: [`data/data/playlist.csv`](data/data/playlist.csv) (`Name<TAB>URL<TAB>0`)
- **Wi‑Fi credentials**: `data/data/wifi.csv` (gitignored) from [`data/data/wifi.example.csv`](data/data/wifi.example.csv)
- **Logos source**: [`images_src/station_logos/`](images_src/station_logos/) (tracked)
- **Generated logos (SPIFFS)**: `data/logos/*.ylg` (gitignored)

### Documentation

- **Worklog / polish notes (why this fork exists)**: [`docs/WORKLOG_AND_POLISH_NOTES.md`](docs/WORKLOG_AND_POLISH_NOTES.md)
- **Changes vs upstream (repro commands + high-signal summary)**: [`docs/CHANGES_SINCE_UPSTREAM.md`](docs/CHANGES_SINCE_UPSTREAM.md)
- **Suggested fixes**: [`docs/SUGGESTED_FIXES.md`](docs/SUGGESTED_FIXES.md)
- **TODO / Roadmap**: [`docs/TODO_ROADMAP.md`](docs/TODO_ROADMAP.md)
- **Known issues**: [`docs/KNOWN_ISSUES.md`](docs/KNOWN_ISSUES.md)
- **Controls (buttons/encoders/IR/touch)**: [`docs/CONTROLS.md`](docs/CONTROLS.md)
- **Station logo pipeline (source images → SPIFFS `.ylg`)**: [`images_src/station_logos/README.md`](images_src/station_logos/README.md)
- **Plugin API docs**: [`src/pluginsManager/README.md`](src/pluginsManager/README.md)
- **Plugins folder notes**: [`src/plugins/README.md`](src/plugins/README.md)
- **IRremote locales** (upstream lib docs): [`src/IRremoteESP8266/locale/README.md`](src/IRremoteESP8266/locale/README.md)

## What’s custom in this fork (high signal)

- **PROS3 hardware bring-up**: LDO2 (3V3_AUX) enable + optional external antenna init in `src/yoradio_user.cpp`
- **Battery gauge**: MAX17048 via I2C (can be disabled in `myoptions.h` with `BATTERY_ENABLED 0`)
  - Uses MAX17048 **ALRT** pin (ProS3: `BATTERY_INT` / GPIO10) for low-battery alert threshold (default 5%) to reduce reliance on slow % polling
  - Battery implementation lives in `src/battery/` (`battery.h/.cpp`)
- **Deep sleep power management**:
  - Wake pins: `WAKE_PIN1` + optional `WAKE_PIN2` (RTC GPIO only) via ext1 wake
  - Auto deep sleep (when wake pins are configured): `AUTO_DEEPSLEEP_IDLE_MINUTES`, `AUTO_DEEPSLEEP_BATT_PCT`
- **Charging bolt (footer)**:
  - Uses **5V sense OR MAX17048 charge-rate OR full battery** to decide whether to show the bolt (more robust across different power-path wiring)
- **Built-in NeoPixel status LED (ProS3 WS2812)**:
  - Enabled when `BUILTIN_NEOPIXEL_PIN` is set (ProS3: GPIO18) and `NEO_STATUS_ENABLE 1`
  - All colors/timing/pulse counts are easy to fine-tune from `myoptions.h` (see “NeoPixel status tuning knobs” below)
- **Offline SD playback + footer connectivity UX**:
  - Switch into SD mode without needing Wi‑Fi (avoid unnecessary reboots)
  - Footer IP shows `no IP` when disconnected instead of `0.0.0.0`
  - Wi‑Fi icon logic improved (5-step RSSI + explicit “not connected” icons)
- **Display tuning**:
  - `TFT_SPI_FREQ` is used by the ILI9341 driver and logged at boot as `##[BOOT]# TFT_SPI_FREQ <value>`
- **Unicode SSIDs on boot screen**:
  - The classic boot font can’t render arbitrary emoji; non‑ASCII SSID characters are shown as `U+XXXX` to avoid garbled glyphs.
  - Special case: 👾 (U+1F47E) is rendered as a built-in custom glyph, so `"Connecting to 👾"` looks right.
- **Date format mapping fixed (web UI + TFT)**: `dateFormat` is now consistent (see below)
- **Station logos (SPIFFS `.ylg`)**:
  - Logos are generated from `images_src/station_logos/` into `data/logos/*.ylg`
  - PNG alpha is supported (alpha → RGB565 color-key `0xF81F`; firmware treats that key as transparent)
  - Lookup uses the **stable playlist name** so logos don’t disappear when ICY metadata changes the displayed station title
  - `.ylg` header is packed (14 bytes) in both generator and firmware
- **Bluetooth audio output (A2DP Source) via companion ESP32 (optional)**:
  - ESP32‑S3 is BLE-only, so Classic-BT A2DP TX is handled by a second ESP32 over UART + I2S
  - Output can be toggled (speaker vs BT) and the footer shows BT **off/searching/connected/audio** state (separate BT icon left of the speaker)
  - Podcasts can be 48kHz: the ProS3 now informs the companion of PCM sample-rate changes (`SR 44100|48000`) so BT output stays pitch-correct (companion resamples to A2DP’s fixed 44.1kHz)
- **SD/Podcast playback UI**:
  - Optional **track position overlay** (`mm:ss / mm:ss`) shown while playing SD/Podcast
  - Controlled via `myoptions.h`:
    - `TRACKPOS_ENABLE` (0/1)
    - `TRACKPOS_REPLACE_WEATHER_WHILE_PLAYING` (0/1) — if enabled, temporarily hides weather while the counter is visible
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
  - `station_number`: `1..N` (current station number for the active playlist/source)
  - `mode`: `Web Streaming|SD Card|Podcast|DLNA`
  - `output_device`: `Speaker|Bluetooth` (only meaningful when `BT_COMPANION_ENABLE != 0`)
  - `bt_sink_name`: current BT target speaker name (only when `BT_COMPANION_ENABLE != 0`)
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
  - `cmd/station_number`: `1..N` (start playing station number)
  - `cmd/playback_mode`: `Web Streaming|SD Card|Podcast|DLNA` (Home Assistant “select”)
  - `cmd/output_device`: `Speaker|Bluetooth` (Home Assistant “select”, only when `BT_COMPANION_ENABLE != 0`)
  - `cmd/bt_sink_name`: set BT target speaker name (Home Assistant “text”, only when `BT_COMPANION_ENABLE != 0`)

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

### Podcast mode (Option A / flat episodes list)

Podcast sources live in `data/data/podcasts.csv` (tab-delimited):

```
Show Name<TAB>https://example/show.rss<TAB>5
```

When you switch to **Podcast** mode, the firmware fetches each RSS feed, extracts the most recent N episodes, and writes a generated episode playlist to:

- `SPIFFS:/data/podcast_episodes.csv` (tab-delimited, same format as `playlist.csv`)

Notes:

- The generated playlist is designed to reuse the existing station browser:
  - **Top line** (station name) shows the **podcast/show name**
  - **Second line** (title) shows the **episode title**
- Some BBC feeds use `open.live.bbc.co.uk/mediaselector/.../audio-nondrm-download-rss/...` links in RSS; the firmware rewrites those to `audio-nondrm-download` so the episode URLs match what works in a browser.
- Episode list generation runs in a **background task** and is guarded to reduce watchdog/network contention:
  - It refreshes on entering Podcast mode (throttled, see below), and can refresh again after playback stops while still in Podcast mode (also throttled).
  - It avoids rebuilding while audio is actively playing, and aborts early if you leave Podcast mode mid-refresh.
- Uploading a new `podcasts.csv` via the web UI will remove the generated playlist/index so it rebuilds next time you enter Podcast mode.

Indexing behavior:

- **Entering Podcast mode** will perform a full RSS fetch + index build at most **once every ~3 hours** and shows **progress** on the player screen.
  - Switching into Podcast mode again within that window reuses the cached `podcast_episodes.csv` list (no refresh).
  - Booting straight into Podcast mode is a convenient way to force a refresh when you want it.


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

More info: [`images_src/station_logos/README.md`](images_src/station_logos/README.md)

Summary:

- **Source**: `images_src/station_logos/*.png|*.jpg` (tracked)
- **Podcast show logos (cached)**: `images_src/podcast_logos/*.png` (tracked; fetched from RSS on `buildfs/uploadfs`)
- **Output**: `data/logos/*.ylg` + `data/logos/index.tsv` (gitignored; uploaded to SPIFFS)
- **Default logo**: `images_src/station_logos/default_logo.png` → `/logos/default.ylg`
- **Default podcast logo**: `images_src/podcast_logos/default_podcast.png` → `/logos/podcast_default.ylg`
- **Generation** runs automatically on filesystem builds:

```bash
platformio run -e yoradio-um_pros3-ili9341 -t uploadfs
```

Notes:

- Podcast show logo fetching is **best-effort** during filesystem builds. To disable it (offline builds), set `PODCAST_LOGOS_FETCH=0`.

## Controls

The full upstream-style controls reference (buttons/encoders/IR/touch) lives here:

- [`docs/CONTROLS.md`](docs/CONTROLS.md)

## Diagnostics / debug toggles

All the usual “turn on logging” knobs live in `myoptions.h` under `/* DIAGNOSTICS */` (append-only section).

- **IR / controls (reduce Serial noise)**:
  - `IR_WAKE_DIAG_LOG`: set to `1` to log the IR wake “waiting for POWER code” flow
  - `IR_RECORD_DIAG_LOG`: set to `1` for verbose IR record/learn logs
- **BT companion UART debug**:
  - `BT2_DIAG_LOG`: set to `1` to log BT companion UART commands/responses + state transitions

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

## NeoPixel status tuning knobs (ProS3)

If you have `BUILTIN_NEOPIXEL_PIN` defined (ProS3: GPIO18), this fork can run a small “NeoStatus” animation engine for a single WS2812 pixel.

- **Enable/disable**
  - `NEO_STATUS_ENABLE 1` (set `0` to disable)
  - `BUILTIN_NEOPIXEL_STATUS_BRIGHTNESS` (0..255) controls overall brightness (defaults to `BUILTIN_NEOPIXEL_BOOT_BRIGHTNESS` if present)

- **Colors** (override in `myoptions.h`)
  - Preferred “easy to find” names:
    - `BUILTIN_NEOPIXEL_BOOT_RGB`
    - `BUILTIN_NEOPIXEL_NET_RGB`
    - `BUILTIN_NEOPIXEL_SD_START_RGB` (preferred; legacy `BUILTIN_NEOPIXEL_SD_RGB` still supported)
    - `BUILTIN_NEOPIXEL_RADIO_START_RGB`
    - `BUILTIN_NEOPIXEL_PODCAST_START_RGB`
    - `BUILTIN_NEOPIXEL_SPK_SELECT_RGB`
    - `BUILTIN_NEOPIXEL_WIFI_LOST_RGB`
    - `BUILTIN_NEOPIXEL_SLEEP_RGB`
    - `BUILTIN_NEOPIXEL_LOW_BATT_RGB`
    - `BUILTIN_NEOPIXEL_BT_SEARCH_RGB`
    - `BUILTIN_NEOPIXEL_BT_CONN_RGB`
  - Internal equivalent names (also supported): `NEO_*_RGB`

- **Timing**
  - Preferred: `BUILTIN_NEOPIXEL_BOOT_DELAY_MS` (alias of `NEO_BOOT_DELAY_MS`)

## Known issues

More info: [`docs/KNOWN_ISSUES.md`](docs/KNOWN_ISSUES.md)

- **SD → WEB → HLS AAC stall (real bug)**: certain HLS AAC stations may fail to start after SD playback.
- **Some AAC stations are “CPU heavy”** and can make UI responsiveness / VU refresh worse.
- **FLAC**: currently very choppy.
- **SD playback**: still has edge-case instability; album art is disabled.

## TODO / Roadmap

More info: [`docs/TODO_ROADMAP.md`](docs/TODO_ROADMAP.md)