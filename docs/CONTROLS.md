## Controls

What each button does depends on the board header, so the tables live with each device:

- ProS3 (rotary knob + mode button + IR): [PROS3.md#controls](PROS3.md#controls)
- April's Speaker (Pirate Audio A/B/X/Y): [APRILS_SPEAKER.md#controls](APRILS_SPEAKER.md#controls)

Fork-specific behaviour, shared by both (`src/core/controls.cpp`):

- Center/knob press in **Bluetooth receive** mutes/unmutes instead of play/stop.
- Center/knob double-press toggles the output (speaker <-> BT send); it is ignored in Bluetooth receive.
- Mode press cycles Web -> SD -> Podcast -> Bluetooth receive -> Web (SD only when `SDC_CS` is set,
  Bluetooth only with `BT_COMPANION_ENABLE`). Mode double-press enters/leaves Bluetooth receive.
- Mode long press = deep sleep, unless `MODE_BUTTON_LONG_PRESS_SLEEP 0`.
- `BTN_VOLUME_INVERT_LR` / `BTN_STATIONS_INVERT_LR` swap left/right for volume / playlist.

### IR remote

Recorded in the web UI (from upstream 0.6.450): Settings > controls > IR Recorder, pick a function on
the left, a slot on the right, press the remote button (avoid codes shown as "UNKNOWN"), then BACK /
DONE. Up to three remotes. Play/stop, prev/next station, volume up/down (long press = fast),
`#` = player/playlist, digits = station number (play to confirm, `#` to cancel).

Upstream reference for joystick, touchscreen and generic button defaults:
[e2002/yoradio](https://github.com/e2002/yoradio).
