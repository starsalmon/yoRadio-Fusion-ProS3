## Known issues / rough edges (PROS3 fork)

This is intentionally “honest status”. Some of these are upstream issues, some are PROS3/ESP32‑S3 specific, and some are introduced by the fork’s performance/diagnostics work.

### Streaming / audio

- **AAC streams vary wildly in CPU cost** even at the same bitrate; some 128k AAC stations can peg the audio core and make UI interactions sluggish.
- **AAC HLS stability**: some HLS AAC stations may stutter occasionally; this appears stream-dependent.
- **FLAC playback**: currently very choppy.

### Mode switching / stalls

- **SD → WEB → HLS AAC stall (real bug)**: after switching from SD playback back to WEB streaming, certain HLS AAC stations (notably some ABC streams) can fail to start / stall.
  - **Workarounds**: play any “known good” MP3 station once, or switch modes and try again.

### SD card

- **SD mount errors during mode switching**: you may see `sd_diskio` warnings like “token error” / “The physical drive cannot work”.
  - If SD playback still works afterwards, these are usually transient; SD bus/power timing is still being tuned.
- **SD playback instability (~30s)**: some tracks restart from the beginning after ~30 seconds, may stop early before the song finishes, and selecting another track can sometimes reboot the device.
- **SD album art**: currently disabled (JPEG decode was unstable / caused crashes on this target).
- **macOS SD metadata**: fixed in this fork (indexing filters common junk like `._*`, `.DS_Store`, and other dotfiles so they can’t be selected).

### UI / display

- **VU Label font**: L and R labels are slightly too large for the grey boxes in the Studio VU style; in the default VU style they sit slightly too high.
- **Scrolling text during playback** is intentionally throttled in this fork to protect audio (some stations make it feel “too slow”).

