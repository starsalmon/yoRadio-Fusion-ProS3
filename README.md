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

### Repo layout

- **User config**: [`myoptions.h`](myoptions.h)
- **PlatformIO env**: [`platformio.ini`](platformio.ini) (`yoradio-um_pros3-ili9341`)
- **Playlist**: [`data/data/playlist.csv`](data/data/playlist.csv) (`Name<TAB>URL<TAB>0`)
- **Wi‑Fi credentials**: `data/data/wifi.csv` (gitignored) from [`data/data/wifi.example.csv`](data/data/wifi.example.csv)
- **Logos source**: [`images_src/station_logos/`](images_src/station_logos/) (tracked)
- **Generated logos (SPIFFS)**: `data/logos/*.ylg` (gitignored)

### Documentation

- **Docs index**: [`docs/README.md`](docs/README.md)
- **Hardware (PCB + schematic)**: [`docs/HARDWARE_PCB.md`](docs/HARDWARE_PCB.md)
- **Worklog / polish notes (why this fork exists)**: [`docs/WORKLOG_AND_POLISH_NOTES.md`](docs/WORKLOG_AND_POLISH_NOTES.md)
- **Upstream snapshot testing**: [`docs/UPSTREAM_TESTING.md`](docs/UPSTREAM_TESTING.md)
- **TODO / Roadmap**: [`docs/TODO_ROADMAP.md`](docs/TODO_ROADMAP.md)
- **Known issues**: [`docs/KNOWN_ISSUES.md`](docs/KNOWN_ISSUES.md)
- **Controls (buttons/encoders/IR/touch)**: [`docs/CONTROLS.md`](docs/CONTROLS.md)
- **Station logo pipeline (source images → SPIFFS `.ylg`)**: [`images_src/station_logos/README.md`](images_src/station_logos/README.md)
- **Plugin API docs**: [`src/pluginsManager/README.md`](src/pluginsManager/README.md)
- **Plugins folder notes**: [`src/plugins/README.md`](src/plugins/README.md)
- **IRremote locales** (upstream lib docs): [`src/IRremoteESP8266/locale/README.md`](src/IRremoteESP8266/locale/README.md)

## What’s custom in this fork

- **PROS3 hardware bring-up**: LDO2 (3V3_AUX) enable + optional external antenna init in `src/yoradio_user.cpp`
- **Battery gauge**: MAX17048 via I2C (can be disabled in `myoptions.h` with `BATTERY_ENABLED 0`)
  - Uses MAX17048 **ALRT** pin (ProS3: `BATTERY_INT` / GPIO10) for low-battery alert threshold (default 5%) to reduce reliance on slow % polling
  - Battery implementation lives in `src/battery/` (`battery.h/.cpp`)
- **Ambient backlight auto-dimming (BH1750, optional)**:
  - Uses a BH1750 light sensor on the same I2C bus as the MAX17048 (ProS3: GPIO8/9)
  - Auto brightness is smoothed and respects the user’s brightness slider as a **max cap**
  - **Build-time**: enable the sensor with `BH1750_ENABLE` in `myoptions.h`
  - **Runtime** (MQTT/HA): enable + tune ALS mapping without rebuilding (see MQTT section)
- **Deep sleep power management**:
  - Wake pins: `WAKE_PIN1` + optional `WAKE_PIN2` (RTC GPIO only) via ext1 wake
  - Auto deep sleep (when wake pins are configured): `AUTO_DEEPSLEEP_IDLE_MINUTES`, `AUTO_DEEPSLEEP_BATT_PCT`
- **Charging bolt (footer)**:
  - Uses **5V sense OR MAX17048 charge-rate OR full battery** to decide whether to show the bolt (more robust across different power-path wiring)
- **NeoStatus LED (NeoPixel / ring)**:
  - NeoStatus can drive either the ProS3 onboard WS2812 (GPIO18) **or** an external NeoPixel ring
  - On the PCB build, it drives an **8‑pixel ring on GPIO14** via:
    - `NEOSTATUS_PIN`
    - `NEOSTATUS_COUNT`
  - This fork currently keeps NeoStatus intentionally minimal (to avoid a tangled config surface):
    - Boot animation
    - Volume spin feedback
    - Podcast RSS indexing animation
    - Mode/Power button LED handling
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
- **Bi-amp DSP crossover (2x MAX98357, optional)**:
  - Uses **one I2S stereo stream** and routes **low band** to one channel and **high band** to the other
  - Wiring: both MAX98357 share `I2S_BCLK`/`I2S_LRC`/`I2S_DOUT`, then strap one amp to **Left** and the other to **Right** using the MAX98357 LRC strap pin
  - DSP is **compile-time optional** (`BIAMP_ENABLE=1`), and **auto-bypasses** when Bluetooth output is selected
  - Runtime tuning (MQTT/HA): enable/disable, low-on-left vs low-on-right, crossover Hz
  - Note: older “tweeter protection HP” fields are retained for EEPROM compatibility but are currently **not used** in the DSP path.
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

## MQTT + Home Assistant

MQTT is enabled/disabled via `MQTT_DISABLE` in `myoptions.h`. This fork includes Home Assistant MQTT discovery and additional state topics.

- **Key options**:
  - `MQTT_DISABLE`: set to `1` to disable MQTT entirely
  - `MQTT_QUIET_LOGS`: set to `1` to suppress noisy AsyncMqttClient INFO logs on Serial

- **State topics (retained)** (assuming `MQTT_ROOT_TOPIC` already ends with `/`):
  - `availability`: `online|offline` (LWT is `offline`)
  - `status`: JSON including playback + station + mode + brightness

- **Screen brightness**
  - `brightness`: user brightness slider (0..100)
  - `brightness_current`: effective brightness after ALS/dimming (0..100)

- **ALS / auto brightness (BH1750)**
  - `als_enable`: `ON|OFF`
  - `als_lux_current`: live lux reading (integer `lx`)
  - `als_min_pct`, `als_max_pct`: clamp range (0..100)
  - `als_update_ms`: update interval (200..10000)
  - `als_alpha_x100`: smoothing (0..100)
  - `als_gamma_x100`: response curve (10..250 → gamma 0.10..2.50)
  - `als_lux_min`, `als_lux_max`: mapping bounds (`lx`)

- **Audio controls**
  - `volume`: (0..100), `cmd/volume`
  - EQ: `bass`, `middle`, `treble`, `balance` (all -16..16) with matching `cmd/*` topics

### Bi-amp DSP controls (MQTT)

If you build with `BIAMP_ENABLE=1`, you can control the bi-amp DSP crossover at runtime over MQTT (and via Home Assistant discovery).

- **State topics (retained)**:
  - `biamp_enable`: `ON|OFF`
  - `biamp_map`: `Low->Left|Low->Right`
  - `biamp_crossover_hz`: integer (Hz)

- **Command topics**:
  - `cmd/biamp_enable`: `ON|OFF` (also accepts `1|0`, `enable|disable`, `true|false`)
  - `cmd/biamp_map`: `Low->Left|Low->Right` (also accepts `left|right`, `1|0`)
  - `cmd/biamp_crossover_hz`: integer Hz (clamped 50..20000; DSP also clamps relative to sample-rate)
 
Notes:
- These entities are only published when `BIAMP_ENABLE=1` at build time.

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

### Podcast mode (flat episodes list)

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
  - If you ever hit a `Stack canary` crash during indexing, increase `PODBUILD_STACK` in `src/core/podcasts.cpp` (RSS parsing can be stack-heavy).

Resume behavior:

- Podcasts support **best-effort resume** (last ~10 episodes) keyed by the **episode enclosure URL**.
  - Checkpoint saved roughly every ~10s while playing, and flushed on stop.
  - Resume uses HTTP **Range** requests; if a host doesn’t accept ranges, the episode will start from the beginning.
  - Resume state lives at `SPIFFS:/data/podcast_resume.bin`.


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

This fork can run a small “NeoStatus” animation engine for either:

- the ProS3 onboard WS2812 (single pixel), or
- an external NeoPixel ring (e.g. 8px behind the rotary knob).

- **Enable/disable**
  - `NEOSTATUS_ENABLE 1` (set `0` to disable)
  - `NEOSTATUS_BRIGHTNESS` (0..255) controls overall brightness

- **Select which NeoPixel device NeoStatus drives**
  - `NEOSTATUS_PIN` (GPIO)
  - `NEOSTATUS_COUNT` (number of pixels; set to `1` for a single pixel)

- **Ring behavior**
  - When `NEOSTATUS_COUNT > 1`, NeoStatus maps “pulse sequences” to **spins**:
    - 1 pulse = 1 spin
    - 2 pulses = 2 spins
    - BT SEARCHING/CONNECTED “pulses” become a continuous spin
  - Spin rate: `NEOSTATUS_SPIN_PERIOD_MS` (default 500ms per full rotation)
  - Optional: `NEOSTATUS_COLORFUL_ENABLE 1` enables a multi-color “dual comet” effect for sequences

- **Colors** (override in `myoptions.h`)
  - `NEOSTATUS_BOOT_RGB`
  - `NEOSTATUS_NET_RGB`
  - `NEOSTATUS_SD_RGB`
  - `NEOSTATUS_RADIO_START_RGB`
  - `NEOSTATUS_PODCAST_START_RGB`
  - `NEOSTATUS_SPK_SELECT_RGB`
  - `NEOSTATUS_WIFI_LOST_RGB`
  - `NEOSTATUS_SLEEP_RGB`
  - `NEOSTATUS_LOW_BATT_RGB`
  - `NEOSTATUS_BT_SEARCH_RGB`
  - `NEOSTATUS_BT_CONN_RGB`

- **Timing**
  - `NEOSTATUS_BOOT_DELAY_MS`
  - `NEOSTATUS_NET_DELAY_MS`

## Known issues

More info: [`docs/KNOWN_ISSUES.md`](docs/KNOWN_ISSUES.md)

- **SD → WEB → HLS AAC stall (real bug)**: certain HLS AAC stations may fail to start after SD playback.
- **Some AAC stations are “CPU heavy”** and can make UI responsiveness / VU refresh worse.
- **FLAC**: currently very choppy.
- **SD playback**: still has edge-case instability; album art is disabled.
- **Podcast indexing**: indexing cannot be cancelled. Should probably not index during boot.
  - Cancellation is best-effort: switching modes or starting playback should abort indexing (may take a moment).
  - Booting directly into Podcast mode intentionally triggers indexing (handy for forcing a refresh).

## TODO / Roadmap

More info: [`docs/TODO_ROADMAP.md`](docs/TODO_ROADMAP.md)