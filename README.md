## yoRadio Fusion – PROS3 (ESP32‑S3) build

This is a personal build of [`SimZs/yoRadio-Fusion`](https://github.com/SimZs/yoRadio-Fusion) adapted for **Unexpected Maker PROS3 (ESP32‑S3)** using **PlatformIO** and **pioarduino**.

### What’s custom in this repo

- **Board/PlatformIO target**: `um_pros3` (`platformio.ini` env: `yoradio-esp32s3n16r8-ili9431`)
- **PROS3 hardware init**: enable LDO2 (3V3_AUX) and force external antenna in `src/yoradio_user.cpp`
- **Battery gauge**: MAX17048 via I2C (robust init + clamped sampling)
- **MAX17048 low-power**: put the gauge into sleep mode right before ESP deep sleep (reduces gauge current during sleep)
- **Theme files cleanup**: custom theme headers live under `themes/` (instead of cluttering the repo root)
- **Deep sleep power management**:
  - **Wake pins**: uses `WAKE_PIN1` + optional `WAKE_PIN2` (RTC GPIO only: GPIO0–GPIO21) via ESP32 `ext1` wake (`ANY_LOW`) with pullups enabled
  - **Auto deep sleep** (only when at least one wake pin is configured):
    - idle timeout: `AUTO_DEEPSLEEP_IDLE_MINUTES`
    - low-battery cutoff (only when not on USB/5V): `AUTO_DEEPSLEEP_BATT_PCT`
- **5V present sense**: `CHARGE_SENSE_PIN` + charging bolt icon in footer when 5V is present
- **Audio high-bitrate stability (auto-patched)**: patched `liblwip.a` + `libesp_netif.a` are applied before builds (see below)
- **GFX icon glyphs (auto-patched)**: Adafruit GFX `glcdfont.c` is replaced with `fonts/glcdfont_EN.c` so custom glyphs render (IP, speaker, RSSI bars)
- **ILI9341 footer UX**:
  - 1‑bit bitmap icons (speaker, LAN, battery levels, charging bolt) with true transparency
  - battery icon + battery % can be **colorized by %** (toggle in `myoptions.h` via `FOOTER_BATTERY_COLORIZE`)
  - speaker icon + volume % are anchored so 1–3 digit values don’t “drift”
- **Header mode icons**: WEB / SD / DLNA are shown as icons (instead of text) in the top-right header badge
- **Date formats fixed (web UI + TFT)**: dropdown examples + TFT output match exactly (see mapping below)
- **SD auto-resume (track only)**: optional resume on mode switch/boot (toggle in `myoptions.h` via `SD_AUTORESUME_ON_MODE_SWITCH`)
- **Home Assistant MQTT discovery**: automatically creates sensors + buttons + sliders with `mdi:` icons
  - Volume is a slider and stays in sync
  - Brightness is a slider and stays in sync
  - Battery voltage is published to a dedicated topic with 2 decimals
  - Battery state + mode strings are capitalized for UI consistency
- **HA device metadata via PlatformIO board definition**: injects manufacturer/model/hw_version at build time (see `tools/pio/ha_board_meta.py` + `platformio.ini`)
- **MQTT battery state**: retained JSON payload with `usb/state/percent/voltage/rate`
- **MQTT sleep trigger**: `cmd/sleep` topic (and `command` payload) enters deep sleep without disabling SmartStart
- **Cursor/clangd hygiene**: `.clangd` removes toolchain-only flags for cleaner diagnostics
- **Audio/HLS robustness work (in progress)**:
  - Improved TS/PES handling for HLS AAC (PES length 0, segment-boundary state reset)
  - Improved AAC ADTS sync recovery across read-buffer boundaries (tail overlap + relaxed header acceptance)
  - Fixed decoder mutex ownership (avoid giving a mutex that wasn’t taken)
  - Fixed AudioBuffer accounting edge cases (resBuff underflow + read-pointer remap equality case)

### Known issues (work in progress)

- **SD → WEB → ABC HLS AAC sometimes stalls**: after switching from SD playback to WEB streaming, certain ABC HLS AAC stations may fail to start (hangs after “stream ready / buffer filled”).
  - **Workarounds**: play any “known good” MP3 station once, or switch modes and try again.
- **SD mount errors during mode switching**: you may see `sd_diskio` warnings like “token error” / “The physical drive cannot work”.
  - If SD playback still works afterwards, these are usually transient; SD bus/power timing is still being tuned.

### TODO / Roadmap

- **Test/refine low battery cutoff**: Not propperly tested, have seent this not work once
- **Theme switching**: add a way to select/switch themes (e.g. from web UI / config, and persist the chosen theme)
- **IR control UX**: set up IR receiver + add flashing “IR RX” icon on-screen
- **SD playback resume**: **track resume is implemented**; resume **position** still to be implemented
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

### Date format mapping (`dateFormat`)

`dateFormat` uses the same meaning everywhere (web UI dropdown + TFT):

- `0`: `DD/MM/YYYY`
- `1`: `DOW - DD MONTH`
- `2`: `DOW - DD/MM/YYYY`
- `3`: `DOW - MONTH DD`
- `4`: `DOW - MM/DD/YYYY`
- `5`: `MONTH DD, YYYY`

### MQTT topics (this build)

Assuming `MQTT_ROOT_TOPIC` already ends with `/`:

- **Options**:
  - `MQTT_DISABLE` (in `myoptions.h`): set to `1` to disable MQTT features entirely
  - `MQTT_QUIET_LOGS` (in `myoptions.h`): set to `1` to suppress noisy `AsyncMqttClient` INFO logs on Serial

- **State (retained)**:
  - `availability`: `online|offline` (LWT is `offline`)
  - `status`: `{"status":0|1,"station":n,"name":"...","title":"...","on":0|1,"mode":"Web Streaming|SD Card|DLNA","brightness":0..100}`
  - `mode`: `Web Streaming|SD Card|DLNA`
  - `brightness`: `0..100`
  - `volume`: `0..100`
  - `playlist`: `http://<ip>/playlist`
  - `battery`: `{"usb":0|1,"state":"...","percent":N.N,"voltage":N.NNN,"rate":N.N}`
  - `battery/voltage`: `X.XX` (e.g. `4.04`)
- **Commands**:
  - `command`: existing text commands (e.g. `prev`, `next`, `toggle`, `stop`, `start`, `reboot`, `play <n>`, `vol <n>`, etc.)
  - `cmd/sleep`: send `1`, `ON`, `true`, `sleep`, or `deepsleep` to enter deep sleep (recommended vs `command`)
  - `cmd/volume`: send `0..100` (used by the HA Volume slider)
  - `cmd/brightness`: send `0..100` (used by the HA Brightness slider)

- **Home Assistant MQTT Discovery (retained)**:
  - Published under `homeassistant/<component>/<nodeId>/<objectId>/config`
  - Uses the topics above for state/commands (`status`, `battery`, `battery/voltage`, `volume`, `brightness`, `mode`, `cmd/*`)

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

### Upload

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