## Known issues / rough edges (PROS3 fork)

This is intentionally “honest status”. Some of these are upstream issues, some are PROS3/ESP32‑S3 specific, and some are introduced by the fork.

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

- **SD mount errors during mode switching**: you may see `sd_diskio` warnings like “token error” / “The physical drive cannot work”.
  - If SD playback still works afterwards, these are usually transient; SD bus/power timing is still being tuned.
- **SD playback instability (~30s)**: some tracks restart from the beginning after ~30 seconds, may stop early before the song finishes, and selecting another track can sometimes reboot the device.
- **SD album art**: currently disabled (JPEG decode was unstable / caused crashes on this target).
- **macOS SD metadata**: fixed in this fork (indexing filters common junk like `._*`, `.DS_Store`, and other dotfiles so they can’t be selected).

### UI / display

- **VU Label font**: L and R labels are slightly too large for the grey boxes in the Studio VU style; in the default VU style they sit slightly too high.
- **Scrolling text during playback** is intentionally throttled in this fork to protect audio (some stations make it feel “too slow”).

### Podcast mode

- **Podcast indexing cancellation**: a MODE button press cancels a running refresh (worst case ~4 s while a connection/redirect times out); the previous list is kept and the mode change then runs. Mode changes from the web UI, MQTT or telnet can't cancel it: they're handled by the main loop, which waits for the refresh (by design, to avoid a playlist-swap race).
- **Boot-time indexing**: booting into Podcast mode uses the existing episode list and defers the refresh to the next entry into Podcast mode. Only a first boot with no list builds one at boot (not cancellable, controls aren't running yet).
- **ABC feeds** don't send `ETag`/`Last-Modified`, so they're downloaded in full on every refresh (BBC and risky.biz feeds answer `304` when unchanged).