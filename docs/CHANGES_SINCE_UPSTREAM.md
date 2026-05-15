## Changes vs SimZs upstream (local diff)

This is a *reproducible* comparison against your local upstream copy:

- **Upstream reference**: `/Users/cain.davidson/Documents/cursor-esp32/yoRadio-fusion-pros3-upstream-test/yoRadio`
- **This fork**: `/Users/cain.davidson/Documents/cursor-esp32/yoRadio-fusion-pros3`

### How to reproduce the comparison

From anywhere:

```bash
git diff --no-index --stat \
  "/Users/cain.davidson/Documents/cursor-esp32/yoRadio-fusion-pros3-upstream-test/yoRadio/src" \
  "/Users/cain.davidson/Documents/cursor-esp32/yoRadio-fusion-pros3/src"

git diff --no-index --name-status \
  "/Users/cain.davidson/Documents/cursor-esp32/yoRadio-fusion-pros3-upstream-test/yoRadio/src" \
  "/Users/cain.davidson/Documents/cursor-esp32/yoRadio-fusion-pros3/src"
```

Notes:
- Your upstream-test snapshot does **not** include a top-level `README.md` (so README comparison isn’t possible from that folder alone).
- It also doesn’t include the extra `tools/` scripts that exist in this fork (those are fork-only additions).

### High-signal summary (what actually changed)

The local diff shows large, real changes in `src/` (dozens of files; thousands of lines). Highlights:

- **Display/UI (`src/core/display.cpp`, `src/displays/widgets/*`)**
  - Station logo pipeline (SPIFFS `.ylg`), packed header handling, stable logo keying by playlist name
  - Performance throttles/changes: scroll pacing, “scroll once then stop”, playlist rendering mode switches
  - Optional 1Hz loop timing diagnostics (`[DSP] …`)
  - Footer/header icon work (new bitmaps)

- **Audio/player (`src/core/player.*`, `src/audioI2S/*`)**
  - Optional 1Hz audio diagnostics (`[AUD] …`) including core + buffer and `Audio::loop()` timing
  - **Audio/HLS robustness work (in progress)** (stream-dependent), with changes in:
    - `src/audioI2S/Audio.cpp` (TS/PES demux + segment boundary handling + buffer/mutex fixes)
    - `src/audioI2S/aac_decoder/aac_decoder.cpp` (ADTS sync scanning/validation tweaks)
    - Improved TS/PES handling for HLS AAC:
      - PES length `0` treated as “unspecified” (continue until next PES start)
      - Segment-boundary state reset (don’t carry mid-PES continuation across HLS `.ts` segments)
    - Improved AAC ADTS sync recovery across read-buffer boundaries:
      - Keep a small tail overlap when searching for sync so a 7‑byte ADTS header can straddle blocks
      - Relaxed header acceptance/validation in the ADTS scanner to handle partial frames
    - Fixed decoder mutex ownership:
      - Don’t `give` a mutex that wasn’t successfully `take`n (see `mutex_audioTaskIsDecoding`)
    - Fixed AudioBuffer accounting edge cases:
      - Guard against unsigned underflow when `readPtr` lives in the `resBuff` region
      - Remap `readPtr` from `resBuff` back into the main buffer using `>=` to handle the equality case

- **Controls (`src/core/controls.*`, `src/main.cpp`)**
  - Optional queued controls-event model to avoid running button handlers from a polling task context
  - Default behavior remains “poll in the Arduino loop” unless the task is enabled

- **Config/network/MQTT (`src/core/config.*`, `src/core/mqtt.*`, `src/core/netserver.cpp`)**
  - PROS3 hardware bring-up sequencing (SPIFFS ordering, rail enables, etc.)
  - Additional MQTT/Home Assistant discovery behaviors in this fork
  - Web config surface updates (e.g. `dateFormat`)

- **New modules**
  - `src/battery.*` (MAX17048)
  - `src/idf_component.yml` (IDF component metadata)

### “Date format fix” (documented + in code)

This fork standardizes `dateFormat` meanings (0..5) in the display formatting code. Source of truth is in `src/displays/widgets/widgets.cpp`:

- `0`: `DD/MM/YYYY`
- `1`: `DOW - DD MONTH`
- `2`: `DOW - DD/MM/YYYY`
- `3`: `DOW - MONTH DD`
- `4`: `DOW - MM/DD/YYYY`
- `5`: `MONTH DD, YYYY`

### “Translating comments”

There are still some non-English (Hungarian) comments in performance-sensitive sections (notably around the VU curve code in `src/displays/widgets/widgets.cpp`). If you want, we can do a focused “comment language cleanup” pass and keep it mechanical (translate only, no logic changes).

