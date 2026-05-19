## Suggested fixes (from the read-only sweep)

This is a punch-list of issues spotted during a read-only scan. Nothing here has been applied automatically.

### Critical / High

#### `setup()` early return skips important initialization

**Status: FIXED in this fork.** `setup()` no longer returns early on “not connected yet” paths, so the initialization sequence now runs consistently (including audio-info hooks and end-of-setup plugin events).

Historical note (what was wrong):

- If boot started in a “not connected yet” state, `setup()` returned before:
  - `Audio::audio_info_callback = my_audio_info`
  - `pm.on_end_setup()`
  - `telnet.begin()`

That could leave audio-info hooks unwired for the whole run.

- **Where (was)**: `src/main.cpp` (early return path around the “network not connected” branch)

#### Busy-wait loops if queue creation fails (starvation / watchdog risk)

**Status: FIXED in this fork.** Display and NetServer queues now use **static FreeRTOS queues** (`xQueueCreateStatic`) so queue creation can’t fail due to heap pressure, and there are no busy-wait loops.

- **Where**: `src/core/display.cpp`, `src/core/netserver.cpp`
- **Notes**: This also removes a potential watchdog starvation path during boot.

#### Web handler can wait forever on `mqttplaylistblock`

**Status: FIXED in this fork.** The playlist GET handler now uses a **bounded wait** (2s max with yield) and then serves the file anyway, so a stuck flag can’t hang the web request forever.

- **Where**: `src/core/netserver.cpp`

### Medium (cleanup / correctness / clarity)

#### `Config::waitConnection()` looks broken/misleading

**Status: FIXED in this fork.** `player.connproc` now reflects “connect attempt in progress”, and `waitConnection()` only waits/settles when a connect is actually happening (plus a timeout to avoid hanging forever).

- **Where**: `src/core/config.cpp`, `src/core/player.{h,cpp}`

#### `clock_tts_setup()` is called twice

**Status: FIXED in this fork.** `clock_tts_setup()` is now called once from `setup()`.

- **Where**: `src/main.cpp`

#### Unreachable preprocessor branch in `dspcore.h` (`DSP_ST7789_76`)

**Status: FIXED in this fork.** The `DSP_ST7789_76` case is no longer shadowed by the broader `DSP_ST7789` branch.

- **Where**: `src/displays/dspcore.h`

### Low (tidy / future maintainability)

**Status: FIXED in this fork.**

- **Reduced serial noise** in IR/control hot paths by guarding diagnostic logs behind option macros (e.g. `IR_WAKE_DIAG_LOG`, `IR_RECORD_DIAG_LOG`, `BT2_DIAG_LOG`).
- **Translated mixed-language logs/comments** in hot paths to keep maintenance readable.
- **Deduplicated debounce helpers** by using the shared timing utility (`yoEveryMs`) instead of local `checklpdelay` variants.
- **Hardened brittle `sprintf` patterns** by switching to `snprintf` and sizing buffers defensively.

