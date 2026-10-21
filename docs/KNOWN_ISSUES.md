## Known issues / rough edges

Honest status. Applies to both devices unless marked **ProS3** or **April**. Some items are upstream
issues, some are ESP32-S3 specific, and some come from this fork.

### Streaming / audio

- **AAC streams vary wildly in CPU cost** even at the same bitrate; some 128k AAC stations can peg the audio core and make UI interactions sluggish.
- **AAC HLS stability**: some HLS AAC stations may stutter occasionally; this appears stream-dependent.
- **FLAC playback**: currently very choppy.

### Mode switching / stalls

- **SD → WEB → HLS AAC stall (real bug)**: after switching from SD playback back to WEB streaming, certain HLS AAC stations (notably some ABC streams) can fail to start / stall.
  - **Workarounds**: play any “known good” MP3 station once, or switch modes and try again.

### SD card

- **SD hot-insert: brief white flash (accepted, intentionally not fixed)**: inserting a card while
  running can flash the display white for a moment. The TFT and SD share one SPI bus (and on the
  ProS3 the card's power-up inrush is on the shared LDO2 rail); until the card gets CMD0 it can drive
  MOSI. The SD park probe (every 1.5 s) parks the card and re-inits the panel, so it recovers by
  itself. Changing SD-insert behaviour risks breaking the working fix, so leave it.
- **SD track ends / restarts and `sd_diskio` "crc error" / "token error"**: the multi-block CMD12
  bug behind these is fixed by `YORADIO_SD_DISKIO_FIX` (framework patch, checked by `fw_guard`).
  If they come back, first check the build log for the two `[fw-guard] ... (OK)` lines.
  Re-test the older "restart after ~30 s / reboot on track change" reports on current firmware before
  removing them from this list.
- **SD album art**: disabled (`SD_ALBUM_ART_ENABLE 0`); JPEG decode was unstable on this target.
- **macOS SD metadata**: fixed (indexing skips `._*`, `.DS_Store` and other dotfiles).

### Build / framework

- The pioarduino framework under `~/.platformio` is shared and gets reinstalled when another project
  or a different `custom_sdkconfig` builds, which drops the lwip and `sd_diskio` patches. The
  pre-scripts re-apply them and `fw_guard.py` rejects any image without them. Build one env at a
  time and never flash a `firmware.bin.REJECTED`.

### Bluetooth

- **April**: the BT companion is inert until wired (`BT_COMPANION_PINS_TODO`). Bluetooth mode
  falls back to Web, and X double-press / MQTT `output_device=bt` are ignored (fixed in 861ca0f).
- **April**: moved to the UM FeatherS3; the pin map, power wiring and battery reading are proposed
  and not bench-tested.
- BT send connect can take ~30 s depending on the speaker.

### UI / display

- **April**: the ST7789 driver uses yoRadio's shared-bus lock instead of April's old `startWrite` mutex.
  Not yet bench-tested on April's Speaker.
- **VU Label font**: L and R labels are slightly too large for the grey boxes in the Studio VU style; in the default VU style they sit slightly too high.
- **Scrolling text during playback** is intentionally throttled in this fork to protect audio (some stations make it feel “too slow”).

### Podcast mode

- **Podcast indexing cancellation**: a MODE button press cancels a running refresh (worst case ~4 s while a connection/redirect times out); the previous list is kept and the mode change then runs. Mode changes from the web UI, MQTT or telnet can't cancel it: they're handled by the main loop, which waits for the refresh (by design, to avoid a playlist-swap race).
- **Boot-time indexing**: booting into Podcast mode uses the existing episode list and defers the refresh to the next entry into Podcast mode. Only a first boot with no list builds one at boot (not cancellable, controls aren't running yet).
- **ABC feeds** don't send `ETag`/`Last-Modified`, so they're downloaded in full on every refresh (BBC and risky.biz feeds answer `304` when unchanged).