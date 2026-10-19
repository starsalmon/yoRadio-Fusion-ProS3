## TODO / Roadmap

This file is intentionally **only pending work**. Completed items are tracked in `docs/WORKLOG_AND_POLISH_NOTES.md`.

### Audio / playback

- **Fix SD → WEB → HLS AAC stall properly** (not just workarounds)
- **SD playback resume (position)**: track resume is implemented; resume *position* still WIP

### Battery / power

- **Test/refine low battery cutoff**: not properly tested

### UI / UX

- **Theme switching**: add a way to select/switch themes (web UI/config + persist chosen theme)
- **Boot screen improvement**: simple animation + build info
- **IR control UX**: set up receiver + on-screen “IR RX” indicator
- **Station logo workflow polish**: improve matching/coverage; automate maintaining the local image library
- **Load station logos from SD**: might be easier long-term; likely needs stable image decode first

### Podcast mode

- **Podcast index UX**: add sorting (pubDate) and “show → episodes” browsing (Option B)
  - Grouped playlist view (nicer UX): show headers + indented episode rows
- **Line 3 from subtitle**: if an episode title has no obvious split delimiter, persist/display a truncated `itunes:subtitle`
- **Podcast/SD playback UI**: show item length + current position while playing (e.g. `03:12 / 52:10`)
- **Track position overlay**: runtime toggle (and define behavior when weather is enabled)

### Bluetooth (companion ESP32)

- **Flash fix11 on the companion**: album + track length + play position (`META album=`, `POS`).
  Commit `6760f4b` on local branch `cursor/bt-a2dp-sink-rx-fe2d`, not on main, not flashed. The radio
  side already parses both (`87f5044`).
- **BT logo re-blit hack**: in Bluetooth mode `display.cpp` redraws the BT logo every 500 ms because
  other widget redraws clear its slot. Replace it with proper redraw ordering.
- **Play/pause in Bluetooth receive**: `AVRCP TOGGLE` exists on both sides
  (`btcompanion_rxAvrcpToggle()`), but no button or remote is mapped to it yet.
- **April's BT pins**: confirm S3-Zero pads for UART TX/RX + wake and the Node ESP32 wiring, then set
  the pins and `BT_COMPANION_PINS_TODO 0` ([APRILS_SPEAKER.md](APRILS_SPEAKER.md)).
- Speed up BT send connect (can take ~30 s depending on the speaker).
- AVRCP controls from the headphones/speaker buttons in send mode (play/pause, skip, volume).

### Hardware follow-ups (ProS3)

- **IO6 pull-up**: the LiPo SHIM LOW_BAT line on IO6 is only read by the boost-on diagnostic
  (`src/core/boostprobe.cpp`, internal pull-up). Decide whether LOW_BAT needs a real pull-up / proper
  handling.

### Experiments

- **Cover art**: album art is off (`SD_ALBUM_ART_ENABLE 0`) after decode instability. Retry as an
  isolated, opt-in experiment.

### Web UI / MQTT

- **Web UI stutter → audio stutter**: navigating the web UI can cause audible stutters; profile and reduce blocking work in web handlers (throttle status pushes, cache playlist reads, move heavy work off the websocket callback path)
- **More controls via MQTT/Web UI**:
  - expose smart start, screensaver controls
  - expose more fork-only toggles (power management, etc.)
- **Web UI improvements**
  - upload `podcasts.csv`
  - add a Podcast mode button
  - add a Bluetooth/Speaker output switch

### Maintainability

- **Reduce blocking patterns** (MQTT playlist block, other remaining busy-waits)

