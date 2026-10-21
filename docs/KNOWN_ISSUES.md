## Known issues / rough edges

Honest status. Applies to both devices unless marked **ProS3** or **April**. Some items are upstream
issues, some are ESP32-S3 specific, and some come from this fork.

### Streaming / audio

- **AAC streams vary wildly in CPU cost** even at the same bitrate; some 128k AAC stations can peg the audio core and make UI interactions sluggish.
- **AAC HLS stability**: some HLS AAC stations may stutter occasionally; this appears stream-dependent.
- **FLAC playback**: decode is now ~16 ms/frame (was 27 ms), but **high-bitrate FLAC still underruns** on long-RTT servers.
  - Radio Paradise Ogg FLAC (44.1 kHz/16-bit, ~100–108 KB/s needed, ~256 ms RTT from here): the stock tuned lwip's **32 KB TCP window** caps throughput at ~125 KB/s in theory; measured ~90–98 KB/s, so the buffer drains on busy/loud passages (build 810df1e).
  - Tried tuned lwip rebuilds with a **48 KB (643aa6c, RECVMBOX 32)** and **64 KB (367a0ce, RECVMBOX 64)** window: both **kill Wi-Fi within 2–10 s** of starting RP (errno 113, ping and web UI dead, app keeps running). Internal heap stayed ~49 KB, so not heap exhaustion.
  - Suspected cause (unconfirmed): Wi-Fi driver RX buffer count (dynamic/static RX buffers, AMPDU RX window) too small for a >32 KB advertised window.
  - Goal: a general fix for any high-bitrate stream, not an RP-specific workaround. Rolled back to 810df1e.

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