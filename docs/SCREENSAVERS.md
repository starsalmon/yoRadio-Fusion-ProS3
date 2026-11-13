# ProS3 ILI9341 animated screensavers

Three styles are selectable via EEPROM / web socket / HTTP commands (same path as other screensaver settings):

| `screensaverStyle` | Name | Description |
|-------------------|------|-------------|
| 0 | Clock | Default bouncing clock (unchanged) |
| 1 | Fluid | 2D FLIP/PIC water (fluid2d core); gravity from MPU6050 orient bus |
| 2 | GoL | 80×60 toroidal Life, 4×4 pixel cells → 320×240 |

## Enable on hardware

1. Turn the screensaver on. The idle saver (`screensaverenabled`, timeout in s) only runs when **not playing**;
   while a stream plays you need `screensaverplayingenabled=1` (timeout in min). The matching `...blank=1`
   option shows a blank screen instead of any style.
2. Pick style: `/set?screensaverstyle=1` (fluid) or `2` (Life); `/?screensaverstyle=1` works too. Web UI
   websocket keys `scrsty` / `scrgs`. The style is read when the saver starts; changing it never wakes a blank screen.
3. Life palette: `screensavergolstyle` 0 white, 1 rainbow, 2 heatmap, 3 neon trails, 4 plasma, 5 fire,
   **7** random (re-picked on each auto-reseed; 6 is an alias). Applies live.

## MQTT / Home Assistant

| Command topic | State topic (retained) | Payload | HA entity |
|---|---|---|---|
| `<root>cmd/screensaverstyle` | `<root>screensaverstyle` | `Clock`/`Fluid`/`Game of Life` (or 0/1/2, `life`) | select "Screensaver Style" |
| `<root>cmd/screensavergolstyle` | `<root>screensavergolstyle` | `White`..`Fire`/`Random` (or 0..5/7) | select "Life Colour Style" |
| `<root>cmd/screensaver` | `<root>screensaver` | `ON`/`OFF` | switch "Screensaver (Idle)" |
| `<root>cmd/screensavertimeout` | `<root>screensavertimeout` | seconds (5..) | number "Screensaver Idle Timeout" |
| `<root>cmd/screensaverplaying` | `<root>screensaverplaying` | `ON`/`OFF` | switch "Screensaver While Playing" |
| `<root>cmd/screensaverplayingtimeout` | `<root>screensaverplayingtimeout` | minutes (1..1080) | number "Screensaver Playing Timeout" |

Enable/timeout changes use the existing setters, which return the screen to the player (same as the web UI).

## RAM / PSRAM

- Animated buffers allocate only when **entering** `SCREENSAVER` (not blank-off). Freed on wake to player UI.
- Fluid: FLIP workspace (~tens of KB) + 320×240 RGB565 framebuffer in **PSRAM**.
- GoL: bit grids + age + history + framebuffer in **PSRAM** (~160 KB while active).
- One style at a time; leaving screensaver calls `screensaver_leave()`.

## Gravity (fluid)

Uses `orient_gravityScreen()` — same MPU sample and H/V axis map as auto-rotate (`boards/pros3.h`: H=X, V=Z). No extra I2C traffic.

## Testing notes

- Tilt while fluid runs; pool should slosh. Audio keeps playing (simulation runs on DspTask only).
- Wake with encoder/button as today; animated saver must not block player redraw.
- If PSRAM init fails, style falls back to clock.
- SPI: ~20 Hz (fluid) / ~12 Hz (GoL) full-frame blits; shared SD/TFT bus uses existing `startWrite` mutex.
