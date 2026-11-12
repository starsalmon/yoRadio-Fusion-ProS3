# ProS3 ILI9341 animated screensavers

Three styles are selectable via EEPROM / web socket / HTTP commands (same path as other screensaver settings):

| `screensaverStyle` | Name | Description |
|-------------------|------|-------------|
| 0 | Clock | Default bouncing clock (unchanged) |
| 1 | Fluid | 2D FLIP/PIC water (fluid2d core); gravity from MPU6050 orient bus |
| 2 | GoL | 80×60 toroidal Life, 4×4 pixel cells → 320×240 |

## Enable on hardware

1. Flash firmware from this branch (not done by the agent).
2. Turn on screensaver as usual (`screensaverenabled`, timeout, optional `screensaverplayingenabled`).
3. Pick style: `/set?screensaverstyle=1` (fluid) or `2` (GoL). Web UI: websocket keys `scrsty` / `scrgs`.
4. GoL palette: `screensavergolstyle` 0–6 (white, rainbow, heatmap, neon, plasma, fire) or **7** = random style on each auto-reseed.

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
