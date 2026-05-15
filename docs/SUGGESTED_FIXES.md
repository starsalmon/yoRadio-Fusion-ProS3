## Suggested fixes (from the read-only sweep)

This is a punch-list of issues spotted during a read-only scan. Nothing here has been applied automatically.

### Critical / High

#### `setup()` early return skips important initialization

If boot starts in a “not connected yet” state, `setup()` returns before:
- `Audio::audio_info_callback = my_audio_info`
- `pm.on_end_setup()`
- `telnet.begin()`

That can leave audio-info hooks unwired for the whole run.

- **Where**: `src/main.cpp` (early return path around the “network not connected” branch; the callback is only set later on the happy path)
- **Next step**: refactor `setup()` so global hooks and “end-of-setup” plugin events run on both paths

#### Busy-wait loops if queue creation fails (starvation / watchdog risk)

There are infinite loops like `while(displayQueue==NULL){;}` and `while(nsQueue==NULL){;}` with no delay/yield.

- **Where**: `src/core/display.cpp`, `src/core/netserver.cpp`
- **Next step**: add a delay + retry limit + error log (or restart) to avoid deadlock under heap pressure

#### Web handler can wait forever on `mqttplaylistblock`

If that flag never clears, the handler loops indefinitely.

- **Where**: `src/core/netserver.cpp`
- **Next step**: add a timeout/deadline or make it event-driven

### Medium (cleanup / correctness / clarity)

#### `Config::waitConnection()` looks broken/misleading

It waits for `player.connproc`, but in this tree `connproc` is set `true` and never cleared, so the wait is meaningless and still sleeps 500ms.

- **Where**: `src/core/config.cpp`, `src/core/player.{h,cpp}`
- **Next step**: either implement proper `connproc` semantics or remove the wait

#### `clock_tts_setup()` is called twice

It’s guarded against double task creation, but it’s confusing/duplicative.

- **Where**: `src/main.cpp`
- **Next step**: call it once in a clearly “finalized config is loaded” spot

#### Unreachable preprocessor branch in `dspcore.h` (`DSP_ST7789_76`)

There’s an `#elif` that can’t ever be hit due to earlier conditions.

- **Where**: `src/displays/dspcore.h`
- **Next step**: merge/remove the dead branch to reduce drift risk

### Low (tidy / future maintainability)

- Lots of serial noise in some paths (IR/control)
- Mixed-language logs/comments in a few hot paths
- Duplicated debounce helper (`checklpdelay` vs touchscreen)
- A couple of brittle `sprintf` patterns

### Suggested next step (high ROI)

Fix `setup()` early-return parity + remove busy-waits + add timeouts. That will make the system much more predictable under load.

