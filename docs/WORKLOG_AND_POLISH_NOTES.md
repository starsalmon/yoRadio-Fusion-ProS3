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

### Station list management (Moode import automation)

- Added `tools/moode/export_moode_radio_to_yoradio_csv.py` to scrape Moode’s station table and merge into `data/data/playlist.csv` (URL de-dupe, don’t clobber custom names).

### Documentation cleanup (without deleting the good bits)

- Rebuilt `README.md` as a hub with links to focused docs (`docs/*.md`, `images_src/station_logos/README.md`, plugin docs).
- Moved the upstream “Controls” block into `docs/CONTROLS.md` so it stays intact and easy to find.

### Where to look for the “full” change list

- **Upstream diff summary**: `docs/CHANGES_SINCE_UPSTREAM.md` (includes repro commands)
- **Known issues**: `docs/KNOWN_ISSUES.md`
- **Controls**: `docs/CONTROLS.md`
- **Logo pipeline**: `images_src/station_logos/README.md`

