## BT companion (classic-BT ESP32) and UART protocol

The ESP32-S3 has no Classic Bluetooth, so A2DP runs on a second ESP32 with the
[BT-A2DP-Source-HiGrow](https://github.com/starsalmon/BT-A2DP-Source-HiGrow) firmware. The radio and
the companion share the I2S bus and talk over a line-based UART.

- **Send (TX, "BT output")**: the radio plays as usual; the companion is I2S slave, reads the PCM and
  sends it to BT headphones/speaker (A2DP source, 44.1 kHz; 48 kHz input is resampled).
- **Receive (RX, "Bluetooth" mode)**: a phone connects to the companion (A2DP sink); the radio
  releases the I2S pins and the companion becomes I2S master into the radio's DAC/amp.

Radio side: `src/core/bt_companion.cpp` (`BT_COMPANION_*` in the board header). Companion side:
`src/main.cpp`, pins in `include/options.h`.

### Wiring

| Signal | ProS3 | HiGrow (`higrow_a2dp_source`) | April's FeatherS3 (proposed) | Node ESP32 (`nodemcu32s_bt_companion`) |
|--------|-------|-------------------------------|------------------------------|---------------------------------------|
| UART radio TX -> companion RX | 43 | 39 (~1k series) | 43 | 16 (proposed) |
| UART radio RX <- companion TX | 44 | 13 (~1k series) | 44 | 17 (proposed) |
| Wake (EXT0, active high) | 38 | 38 | 7 | 33 (proposed) |
| I2S BCK | 41 | 17 | 17 | 26 (proposed) |
| I2S LRCK / WS | 42 | 23 | 18 | 25 (proposed) |
| I2S data (radio DOUT / companion DIN, also its DOUT in RX) | 40 | 19 | 14 | 22 (proposed) |

UART 115200 8N1. April's pins are proposed (PCB not final) and not wired; until then `BT_COMPANION_PINS_TODO 1` keeps the
radio side inert ([APRILS_SPEAKER.md](APRILS_SPEAKER.md)).

### Commands (radio -> companion)

One command per line, `\n` terminated.

| Command | Reply | Notes |
|---------|-------|-------|
| `PING` | `PONG` | |
| `STATUS` | `STATUS ...` (below) | Polled; in RX it also replays cached `META` |
| `CONNECT <name>` | `OK connect '<name>'` | TX only: scan and connect to the sink by name |
| `DISCONNECT` | `OK disconnect` | |
| `SR 44100\|48000` | `OK sr=<hz>` | PCM rate on the I2S lines (TX) |
| `SLEEP` | `OK sleep` | Disconnect, deep sleep until the wake pin goes high |
| `HANDOVER PROS3_RELEASED` | `OK HIGROW_ARMED` | Radio has tri-stated its I2S pins |
| `MODE RX <name>` | `OK MODE_RX`, `BUS_HIGROW_MASTER` | A2DP sink named `<name>`, I2S master |
| `MODE TX` | `OK MODE_TX`, `BUS_HIGROW_RELEASED` | Back to I2S slave / A2DP source |
| `VOL <0-127>` | `OK vol=<v>` | RX volume (`VOL 0` = knob mute) |
| `AVRCP TOGGLE\|PLAY\|PAUSE` | `OK avrcp_play\|avrcp_pause` | RX only. Not mapped to any control yet |
| `BT_ON`, `BT_OFF`, `HELP` | `OK ...` | Companion-side tools; the radio doesn't send them |

Errors come back as `ERR <reason>`.

### Messages (companion -> radio)

| Line | Meaning |
|------|---------|
| `STATUS mode=tx conn=<c> audio=<a> bt=<0\|1> sr=<hz> ring=... ` | TX state + buffer stats |
| `STATUS mode=rx conn=<c> audio=<a> bt=<0\|1> vol=<v>` | RX state; `bt=1` = sink running (companion is I2S master) |
| `META title=<text>` / `META artist=<text>` | Track metadata from the phone (RX) |
| `META album=<text>` | **fix11, pending**: on change |
| `POS <elapsed_ms> <total_ms> <playing>` | **fix11, pending**: at most 1/s, only when changed |

`conn=2` is connected; `audio=1` or `2` counts as streaming. The radio already parses `META album=`
and `POS` (album on the second title line, progress bar); the companion only sends them from fix11
(`6760f4b`, local branch `cursor/bt-a2dp-sink-rx-fe2d`, not on main and not flashed).

### Receive handover (radio enters Bluetooth mode)

1. Wake pulse (600 ms high), then `PING` every 500 ms until `PONG` (up to ~5 s).
2. `STATUS`. If the companion is already `mode=rx bt=1` (e.g. the radio rebooted), keep the phone
   connected and just resync volume.
3. Otherwise, if the companion was not freshly booted, reset it first (`SLEEP`, wait, wake). Only a
   fresh companion is given `MODE RX`, because a second sink start in one awake session fails.
4. `HANDOVER PROS3_RELEASED` -> `OK HIGROW_ARMED` (2 s timeout) -> `MODE RX <name>` ->
   `BUS_HIGROW_MASTER` (5 s timeout). The amp is enabled only after that plus a short settle delay.
5. While in RX: `STATUS` polling, `VOL` follows the knob. Two consecutive non-RX statuses mean the
   companion restarted, and the radio re-runs the handover.

Leaving: `MODE TX` -> `BUS_HIGROW_RELEASED` (4 s timeout) -> `SLEEP`, then the radio reclaims I2S.

### Sleep

- The companion deep-sleeps after 30 s in TX with BT not started and no UART activity
  (`AUTO_SLEEP_IDLE_MS`), and on `SLEEP`.
- The radio's own idle sleep counts Bluetooth receive as playing only while the companion reports
  streaming (see [PROS3.md](PROS3.md#power--sleep)).
