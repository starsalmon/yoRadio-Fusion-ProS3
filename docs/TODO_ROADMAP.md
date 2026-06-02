## TODO / Roadmap

This file is intentionally **only pending work**. Completed items are tracked in `docs/WORKLOG_AND_POLISH_NOTES.md`.

### Audio / playback

- **Fix SD → WEB → HLS AAC stall properly** (not just workarounds)
- **SD playback resume (position)**: track resume is implemented; resume *position* still WIP
- **Album art**: revisit later (needs stable decoder/task model)

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

### Bluetooth output (companion ESP32)

- Speed up BT connect (currently can take ~30s depending on speaker)
- Consider a cleaner “connected/disconnected” status message from the companion (optional; ProS3 currently polls `STATUS`)
- Implement **BT AVRCP controls** (if supported): play/pause, skip, volume via the speaker/headphones buttons

### Web UI / MQTT

- **More controls via MQTT/Web UI**:
  - expose tone/equalizer, smart start, screensaver controls
  - expose more fork-only toggles (power management, etc.)
- **Web UI improvements**
  - upload `podcasts.csv`
  - add a Podcast mode button
  - add a Bluetooth/Speaker output switch

### Maintainability

- **Reduce blocking patterns** (MQTT playlist block, other remaining busy-waits)

