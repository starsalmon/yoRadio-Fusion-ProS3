## yoRadio Fusion – PROS3 (ESP32‑S3) build

This is a personal build of [`SimZs/yoRadio-Fusion`](https://github.com/SimZs/yoRadio-Fusion) adapted for **Unexpected Maker PROS3 (ESP32‑S3)** using **PlatformIO** and **pioarduino**.

### What’s custom in this repo

- **Board/PlatformIO target**: `um_pros3` (`platformio.ini` env: `yoradio-esp32s3n16r8-ili9431`)
- **PROS3 hardware init**: enable LDO2 (3V3_AUX) and force external antenna in `src/yoradio_user.cpp`
- **Battery gauge + footer widget**: MAX17048 via I2C + a battery percentage widget in the bottom bar
- **5V present sense**: `CHARGE_SENSE_PIN` logs ON/OFF transitions (for future charging state UX)
- **Audio high-bitrate stability (auto-patched)**: patched `liblwip.a` + `libesp_netif.a` are applied before builds (see below)
- **GFX icon glyphs (auto-patched)**: Adafruit GFX `glcdfont.c` is replaced with `fonts/glcdfont_EN.c` so custom glyphs render (IP, speaker, RSSI bars)
- **ILI9341 footer UX**: smooth DejaVu text + separate classic icon widgets; volume icon + text are positioned so 1–3 digit values don’t “drift”
- **Onboard NeoPixel handling**: treated as a NeoPixel (not PWM) and can be disabled while prototyping
- **Cursor/clangd hygiene**: `.clangd` removes toolchain-only flags for cleaner diagnostics

### Automatic patching (PlatformIO pre-build)

This repo includes a small PlatformIO pre-build script that applies two patches automatically:

- **Audio patch**: copies patched `liblwip.a` and `libesp_netif.a` into the active PlatformIO `framework-arduinoespressif32-libs` package for ESP32‑S3.
- **Font patch**: replaces the local libdeps copy of Adafruit GFX `glcdfont.c` with `fonts/glcdfont_EN.c` so classic glyph icons are available.

It runs via `platformio.ini`:

- `extra_scripts = pre:tools/pio/apply_audio_idf_mod.py`

#### Audio_IDF_MOD folder location

Recommended layout (already used in this repo):

- `tools/Audio_IDF_MOD/`
  - `CORE_3.3.6-IDF_ver5.5.2/liblwip.a`
  - `CORE_3.3.6-IDF_ver5.5.2/libesp_netif.a`

Optional override (if you don’t want to keep the archives in-repo):

- Set `AUDIO_IDF_MOD_DIR=/absolute/path/to/Audio_IDF_MOD`

### Notes on recent fixes

- **MAX17048 I2C init**: the battery gauge init pre-sets I2C pins before the Adafruit library begins the bus (avoids “bus already started” / invalid-state noise).
- **RSSI footer**: RSSI is shown as **bars only** (no numeric RSSI) on ILI9341 builds.
- **Status LED / NeoPixel**:
  - By default this firmware uses the “built-in LED” as a simple **playing indicator** (white when playing).
  - For prototyping, it’s currently forced off via `#define BUILTIN_NEOPIXEL_DISABLE 1` in `myoptions.h`.

### Secrets / local-only files

- **`data/data/` is treated as secrets/local-only**
  - It contains its own `.gitignore` that ignores everything in that folder except the `.gitignore` itself.
  - Put anything private there (tokens, local config blobs, etc).

### Build requirements

- PlatformIO Core (installed via the PlatformIO extension or CLI)
- Python 3
- Cursor/VS Code with the PlatformIO extension (and optional `clangd`)

### Build (firmware)

From the project root:

```bash
platformio run -e yoradio-esp32s3n16r8-ili9431
```

### Upload (when you have the board)

```bash
platformio run -e yoradio-esp32s3n16r8-ili9431 -t upload
```

### Upload filesystem (SPIFFS)

```bash
platformio run -e yoradio-esp32s3n16r8-ili9431 -t uploadfs
```

### Serial monitor

```bash
platformio device monitor -b 115200
```

### Where to change board-specific settings

- **Pins / display / I2S / encoder settings**: `myoptions.h`
- **PlatformIO environment / libraries / upload settings**: `platformio.ini`

### Repo

- GitHub: `https://github.com/starsalmon/yoradio-fusion-pros3`

