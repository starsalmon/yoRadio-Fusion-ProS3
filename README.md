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
- **Cursor/clangd hygiene**: `.clangd` removes toolchain-only flags for cleaner diagnostics

### TODO / Roadmap

- **Battery icon color**: colorize by state (charging / low / normal)
- **IR control UX**: set up IR receiver + add flashing “IR RX” icon on-screen
- **MQTT sleep**: allow sending a sleep command via MQTT
- **Home Assistant**: MQTT discovery (auto-create sensors + buttons)
- **SD playback resume**: resume last track + position (not restart at first file)
- **Album art + station logos**: display on screen (likely larger change)

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

## Controls

<img src="https://github.com/e2002/yoradio/blob/main/images/joystick.jpg" width="830" height="440"><br />

---
- [Buttons](#buttons)
- [Encoders](#encoders)
- [IR receiver](#ir-receiver)
- [Joystick](#joystick)
- [Touchscreen](#touchscreen)

---
### Buttons
Up to 5 buttons can be connected to the device. Three buttons are enough to control it.

Button actions:
- BTN_LEFT\
 click: volume down\
 dblclick: previous station\
 longpress: quick volume down
- BTN_CENTER\
 click: start/stop playing\
 dblclick: switch SD/WEB mode\
 longpress: toggle between PLAYER/PLAYLIST mode
- BTN_RIGHT\
 click: volume up\
 dblclick: next station\
 longpress: quick volume up
- BTN_UP\
 click: without display - next station, with display - move up\
 dblclick: doing nothing\
 longpress: with display - quick move up
- BTN_DOWN\
 click: without display - prev station, with display - move down\
 dblclick: doing nothing\
 longpress: with display - quick move down
- BTN_MODE\
 click: switch SD/WEB mode\
 longpress: go to sleep
 
---
### Encoders
You can connect one or two encoders to replace/complete the buttons. One encoder (without buttons) is enough to control the device.

- ENCODER1\
 rotate left: (ENC_BTNL) in PLAYER mode - volume down, in PLAYLIST mode - move up\
 rotate right: (ENC_BTNR) in PLAYER mode - volume up, in PLAYLIST mode - move down\
 click, dblclick, longpress: (ENC_BTNB) same as BTN_CENTER
- ENCODER2\
 rotate left: (ENC2_BTNL) if not pressed - switch to PLAYLIST mode and move up, if pressed - volume down\
 rotate right: (ENC2_BTNR) if not pressed - switch to PLAYLIST mode and move down, if pressed - volume up\
 click, dblclick: (ENC2_BTNB) toggle between PLAYER/VOLUME mode

---
### IR receiver
Starting from version 0.6.450, adding an IR remote control has been moved to the web interface. Can be added for up to three remotes.
1. go to Settings >> controls >> IR Recorder (fig.1)
2. press the button you need on the left to record the IR code (fig.2)

<img src="https://github.com/e2002/yoradio/blob/main/images/irRecorder01.png" width="830" height="490"><br>

3. select the slot on the right and press the button on the physical IR remote (fig.3). Avoid the inscription "UNKNOWN" (fig.4)

<img src="https://github.com/e2002/yoradio/blob/main/images/irRecorder02.png" width="830" height="490"><br>

4. repeat steps 2 and 3 for other buttons
5. select BACK, select DONE

**Button assignment:**
- &#9199; - start/stop playing
- &#9194; - previous station
- &#9193; - next station
- &#9650; - volume up, longpress - quick volume up
- &#9660; - volume down, longpress - quick volume down
- &nbsp;**\#**  &nbsp;- toggle between PLAYER/PLAYLIST mode
- **0-9** - Start entering the station number. To finish input and start playback, press the play button. To cancel, press hash.

---
### Joystick
You can use a joystick [like this](https://aliexpress.com/item/4000681560472.html) instead of connecting five buttons

<img src="https://github.com/e2002/yoradio/blob/main/images/joystick.jpg" width="300" height="300"><br />

---
### Touchscreen
- Swipe horizontally: volume control
- Swipe vertically: station selection
- Tap: in PLAYER mode - start/stop playback, in PLAYLIST mode - select
- Long tap: in PLAYLIST mode - cancel

---