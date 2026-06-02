## Worklog / polish notes (why this fork exists)

This fork isn’t “just a board config change”. A lot of time went into making upstream behave well on PROS3/ESP32‑S3 + ILI9341, and into hardening a few sharp edges that only show up after real-world use (station logos at scale, HLS AAC weirdness, UI stutter, button responsiveness under heavy decode load, etc.).

This document exists so we don’t undersell the work.

### Station logo system (SPIFFS `.ylg`) — end-to-end workflow

- **Removed/retired legacy logo paths** that were confusing or unused (bulk RGB565 dumps, random default JPGs) and documented what’s actually used.
- **Tracked a proper source image library** under `images_src/station_logos/` (PNG/JPG) and kept it in-repo.
- **Added PNG transparency support** by converting alpha → RGB565 color-key (`0xF81F`) in the generator and skipping that key while drawing in firmware.
- **Fixed “logos disappear when ICY metadata updates”** by keying logo lookup off the **stable playlist name** rather than the dynamic station title.
- **Fixed a real binary format mismatch**: `.ylg` header padding differences between Python (14 bytes) and C++ (compiler padded to 16). Firmware now reads with a packed struct + static assert.
- **Pre-generated all logos**: generator emits `.ylg` not only for playlist matches but for *every* source image stem, so adding a station later “just works” if names match.
- **Made SPIFFS usage visible** at boot (sizes/usage logs) so the logo library doesn’t become a silent failure.

### SPIFFS / boot ordering and defaults

- **Fixed SPIFFS mount timing**: avoided accessing theme/SPIFFS before `SPIFFS.begin()` (was causing mount failures on full erase).
- **Fixed `showlogos` default state** so it doesn’t silently come up disabled after resets (this was a real “why are my logos gone?” trap).

### Offline SD playback + connectivity footer UX

- **Mode switching into SD playback without Wi‑Fi**: switching to `PM_SDCARD` no longer forces a reboot just because Wi‑Fi is down or the device is in SoftAP mode.
  - Net effect: you can go “offline” and still reliably enter SD playback, rather than bouncing through restarts.
- **IP/LAN widget behavior while offline**:
  - Shows **`no IP`** (instead of `0.0.0.0`) when Wi‑Fi is up/down but no usable IP is assigned.
  - Updates immediately on Wi‑Fi connect/loss events.
- **Wi‑Fi icon logic refined** (ILI9341 footer):
  - Distinguishes “not associated” vs “associated but no IP” vs signal strength (5‑step RSSI bar icons).

### Display/UI performance: make it smooth *without* killing audio

- **Text scrolling throttle during playback** (and “scroll once then stop”) to reduce redraw churn while audio decode is busy.
- **Playlist rendering optimization**: forced the more efficient “moving cursor” mode during playback to avoid expensive redraw/file I/O patterns.
- **Added 1Hz display loop diagnostics** (`[DSP] …`) to quantify queue depth, max loop times, and rough utilization instead of guessing.

### VU meter: adaptive behavior (and the trade-off)

- Implemented **adaptive VU throttling** so VU draws back off aggressively when audio buffer health drops or `Audio::loop()` time spikes.
- Added optional **VU performance logging** so you can see when/why it’s backing off.
- Net effect: keeps audio stable on “heavy” stations, but can make VU feel slower on stations that *could* handle it—this is an ongoing tuning area.

### Audio/HLS robustness (AAC reality)

Some HLS AAC streams behave differently even at similar bitrates. This fork includes targeted robustness work (documented in `docs/CHANGES_SINCE_UPSTREAM.md`) including:

- **TS/PES handling improvements** (PES length 0 semantics, segment-boundary resets to avoid losing ADTS sync after segment switches)
- **ADTS sync recovery across buffer boundaries** (tail overlap so headers can straddle blocks)
- **Decoder mutex ownership fixes** (don’t give what you didn’t take)
- **AudioBuffer accounting edge-case fixes** (avoid underflow and resBuff pointer edge cases)

### Controls responsiveness under load (buttons/encoder)

- Investigated the real-world symptom: **AAC station plays fine but UI feels “3 presses to switch mode”**.
- Prototyped a **separate controls polling task** with an event queue so button ticks can run even when the Arduino loop is starved.
- Refined to keep the system **audio-first** (task priority/core choices) and kept a safe fallback so controls still work if the task is disabled.

### MQTT + Home Assistant polish (fork-only UX)

- Improved MQTT state/command surfacing and **Home Assistant discovery** so sliders (volume/brightness) stay in sync, and battery reporting is more useful.
- Added “quiet logs” options so MQTT doesn’t drown Serial when you’re debugging audio/display.

### Date format consistency

- Fixed the `dateFormat` mapping so **web UI examples and TFT output match** (documented in `README.md` and implemented in `src/displays/widgets/widgets.cpp`).

### Clock/date visibility + user-configurable hiding

- Fixed an annoying boot/offline artifact where the clock/date could briefly show **1970-era** values before time sync.
- Added two separate build toggles in `myoptions.h`:
  - `HIDE_CLOCK` to remove the clock widget
  - `HIDE_DATE_WIDGET` to remove the date widget

### Bluetooth audio output (A2DP Source) via companion ESP32

ESP32‑S3 is BLE-only, so this fork added an optional **companion Classic-BT ESP32** path to transmit A2DP audio (SBC) while the ProS3 continues to do UI/network/stream decode.

- Implemented a ProS3-side control module (`src/core/bt_companion.{h,cpp}`):
  - UART control protocol (`PING/STATUS/CONNECT/DISCONNECT/SLEEP`)
  - Wake pin pulse to bring the companion out of deep sleep
  - Output toggle on **MODE double-click** (internal speaker vs BT), with internal amp mute while keeping I2S audio flowing
- Output UX polish on the ProS3 footer (ILI9341):
  - Added a **separate BT icon widget** to the left of the speaker icon (so speaker volume icon stays a speaker).
  - BT icon shows **OFF / SEARCHING / CONNECTED / AUDIO**:
    - SEARCHING blinks about ~1Hz
    - CONNECTED (link up, waiting for audio) vs AUDIO (audio started) are distinct icons
  - Switching from BT → speaker delays speaker unmute by ~0.5s after volume restore to avoid a brief loud transient.
- Made reconnect behavior more robust (especially after reboot mid-BT):
  - On enable, ProS3 asks the companion for `STATUS` first; if it’s already connected, it won’t tear down a stable link.
  - Gentle “kick” retry while SEARCHING (first at ~45s, then every 60s indefinitely) instead of aggressive spamming.
- Added runtime control of the **target speaker name**:
  - Home Assistant publishes a `text` entity (MQTT discovery) to set the BT sink name at runtime.
  - ProS3 sends `CONNECT <name>` to the companion using the new target (without disrupting active audio).
- Companion firmware work (separate PlatformIO project) hardened for real use:
  - Deep sleep made reliable (task shutdown ordering)
  - Logging made optional/quiet by default
  - Corrected BT target matching so `CONNECT <name>` works deterministically
- Added built-in **NeoPixel status LED** animations (ProS3 onboard WS2812):
  - Boot: purple **3-pulse** sequence (optionally delayed via `NEO_BOOT_DELAY_MS`)
  - Network connect: green **3-pulse** sequence
  - Wi‑Fi lost / reconnecting: amber **2‑pulse** sequence on CONNECTED → not connected
  - SD playback start: yellow **2-pulse** sequence
  - Radio (web) start: white **2-pulse** sequence
  - Speaker select: greener aqua **2‑pulse** sequence (red reserved for low battery)
  - Deep sleep entering: purple **2‑pulse** sequence, then off
  - Low battery warning: red **3‑pulse** sequence (rate-limited to ~once/min when near cutoff; also shown immediately before forced sleep)
  - BT SEARCHING: slow **pure blue** “breathing” pulse (smooth pulse, not a flash)
  - BT CONNECTED (waiting for audio): **fast pure blue** pulse (continuous) until AUDIO arrives
  - BT AUDIO: quick pure blue pulse (event), no constant pulse
- Current user-observed behavior:
  - BT audio is stable once connected
  - Switching to BT can take ~30s (ongoing UX improvement area: status LED + connection-state feedback + faster connect path)

### Bi-amp DSP crossover (2x MAX98357) + tweeter protection

This fork adds an optional **bi-amp** mode using two MAX98357 I2S DAC/amps while keeping the wiring simple (single I2S bus):

- **DSP crossover**: Mixes stereo to mono, then splits into **low** and **high** bands using a 4th‑order Linkwitz‑Riley crossover.
- **Routing**: Low band goes to one I2S channel, high band to the other (mapping is runtime-selectable).
- **Bluetooth safety**: DSP is automatically bypassed when BT output is selected (BT path receives full-range audio).
- **Runtime control**: Bi-amp enable/map/crossover are controllable via MQTT + Home Assistant discovery (and persisted in EEPROM).
- **Tweeter protection**: Optional extra high-pass on the tweeter (high) band only (12 dB/oct or 24 dB/oct) to protect small tweeters from LF content.

### Charging indicator (footer bolt) robust across power paths

- On this hardware/power-path, the PROS3 **5V sense** pin can be unreliable (or unused). The footer **charging bolt** now uses a combined heuristic:
  - Bolt is shown when **any** is true:
    - 5V sense indicates external power is present, **or**
    - MAX17048 \(rate \ge -1\%\!/\!h\) (charging or near-full drift), **or**
    - Battery is effectively full (≥ ~100%)
  - Bolt is hidden only when we’re clearly discharging (no 5V sense, \(rate < -1\%\!/\!h\), not full)

### Battery + low-battery behavior (MAX17048 alert pin)

- Reworked low-battery handling to use the MAX17048 **ALRT** pin (ProS3: GPIO10 / `BATTERY_INT`) with a programmable SOC-low threshold, rather than relying solely on periodic % sampling.
- Moved battery implementation into a dedicated module folder:
  - `src/battery/battery.cpp`
  - `src/battery/battery.h`

### SD/Podcast “track position” overlay (and weather interaction)

- Added an optional `mm:ss / mm:ss` overlay shown while playing **SD** or **Podcast** items.
- It’s centered to “belong with track info” rather than the clock.
- On the ILI9341 layout this overlaps the weather text region, so it’s controllable via build-time toggles:
  - `TRACKPOS_ENABLE`
  - `TRACKPOS_REPLACE_WEATHER_WHILE_PLAYING`

### Ambient backlight auto-dimming (BH1750 on I2C)

- Added optional **BH1750** ambient light integration to the backlight plugin (`src/plugins/backlight/*`).
- Uses the same I2C pins as the MAX17048 fuel gauge (ProS3: GPIO8/9).
- Auto brightness is smoothed and applied via hardware-only PWM writes (no EEPROM spam), while the user brightness slider acts as a **cap**.

### Stability hardening (FreeRTOS + web handler)

- Removed boot-time **busy-wait starvation** risks by switching `displayQueue` and `nsQueue` to **static FreeRTOS queues** (`xQueueCreateStatic`) instead of heap allocation loops.
- Prevented a web request hang: the playlist GET path no longer waits forever on `mqttplaylistblock` (bounded 2s wait + yields, then serves anyway).
- Made `Config::waitConnection()` meaningful again by fixing `player.connproc` semantics (true = idle / not connecting; false = connect in progress) and adding a timeout.
- Removed duplicate `clock_tts_setup()` call (now called once in `setup()`).
- Fixed a `dspcore.h` preprocessor shadowing issue so the `DSP_ST7789_76` branch is reachable again.

### Station list management (Moode import automation)

- Added `tools/moode/export_moode_radio_to_yoradio_csv.py` to scrape Moode’s station table and merge into `data/data/playlist.csv` (URL de-dupe, don’t clobber custom names).

### Documentation cleanup (without deleting the good bits)

- Rebuilt `README.md` as a hub with links to focused docs (`docs/*.md`, `images_src/station_logos/README.md`, plugin docs).
- Moved the upstream “Controls” block into `docs/CONTROLS.md` so it stays intact and easy to find.

### Podcast playback (Option A: flat episodes list)

- Added a new playback mode `PM_PODCAST` ("Podcast") that generates a flat episode list from RSS feeds and reuses the existing station browser/player.
- Input list: `data/data/podcasts.csv` (`show<TAB>rss_url<TAB>episodes_to_list`).
- On entering Podcast mode, the firmware fetches RSS feeds, generates `SPIFFS:/data/podcast_episodes.csv`, and indexes it so station navigation works like normal playlists.
- Display split: in Podcast mode, the **show name** is the station name (top line) and the **episode title** is shown on the second line.
- Persistence: the last selected podcast episode is stored separately from radio/SD last-station so rebooting/switching modes doesn’t cross-contaminate selections.
- Build robustness: episode generation runs in a background task with guards (Wi‑Fi connected, display ready, and no active playback), aborts if mode changes or playback starts, and uses atomic file swaps to avoid “missing file” windows during refresh.
- Mode entry robustness:
  - Entering Podcast mode can run a full rebuild + index with visible progress before the list is shown (reduces “missing shows” and scroll-time races).
  - To keep mode switching snappy during normal use, the rebuild is throttled to ~**once per 3 hours**; within the interval it reuses the cached episode list.
- BBC compatibility:
  - RSS feeds may publish `open.live.bbc.co.uk/mediaselector/.../audio-nondrm-download-rss/...` enclosure URLs which 403 on-device; those URLs are rewritten to `audio-nondrm-download` (the variant that works in a browser).
  - Long redirect `Location:` headers are supported (prevents truncated redirect URLs which can also cause 403s).
- UI polish:
  - The player view restores the “episode title” after showing volume/dialog overlays.
  - ILI9341 custom config no longer forces uppercase for the top lines (show/episode aren’t ALL CAPS).
  - Playlist UX: in moving-cursor mode, the **selected** playlist row uses a marquee scroll for long items (helps long podcast “Show - Episode” labels).
- BT output robustness:
  - Some podcast MP3s are **48kHz**; ProS3 now reports PCM sample-rate changes to the companion (`SR 44100|48000`) and the companion resamples 48k → 44.1k so A2DP output stays pitch-correct without clicks.
- MQTT / Home Assistant "Mode" select now includes `Podcast`.
- Resume:
  - Best-effort podcast resume keyed by **episode enclosure URL**, keeping only the last ~10 entries.
  - Checkpoint saved while playing and flushed on stop; resume uses HTTP **Range** requests when supported by the host.

### Where to look for the “full” change list

- **Upstream diff summary**: `docs/CHANGES_SINCE_UPSTREAM.md` (includes repro commands)
- **Known issues**: `docs/KNOWN_ISSUES.md`
- **Controls**: `docs/CONTROLS.md`
- **Logo pipeline**: `images_src/station_logos/README.md`

