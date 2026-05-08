## yoRadio Fusion – PROS3 (ESP32‑S3) build

This is a personal build of [`SimZs/yoRadio-Fusion`](https://github.com/SimZs/yoRadio-Fusion) adapted for **Unexpected Maker PROS3 (ESP32‑S3)** using **PlatformIO** and **pioarduino**.

### What’s custom in this repo

- **Board/PlatformIO target**: `um_pros3` (`platformio.ini` env: `yoradio-esp32s3n16r8-ili9431`)
- **PROS3 hardware init**: enable LDO2 (3V3_AUX) and force external antenna in `src/yoradio_user.cpp`
- **Battery gauge + footer widget**: MAX17048 via I2C + a battery percentage widget in the bottom bar
- **5V present sense**: `CHARGE_SENSE_PIN` logs ON/OFF transitions (for future charging state UX)
- **Cursor/clangd hygiene**: `.clangd` removes toolchain-only flags for cleaner diagnostics

### Secrets / local-only files

- **`data/data/` is treated as secrets/local-only**
  - It contains its own `.gitignore` that ignores everything in that folder except the `.gitignore` itself.
  - Put anything private there (tokens, local config blobs, etc).

> Note: `myoptions.h` is currently committed. If you later add Wi‑Fi credentials or other secrets there, consider switching to a `myoptions.example.h` pattern and keeping your private `myoptions.h` untracked.

### Build requirements

- PlatformIO Core (installed via the PlatformIO extension or CLI)
- Python 3
- Cursor/VS Code with the PlatformIO extension (and optional `clangd`)

### Build (firmware)

From the project root:

```bash
platformio run -e yoradio-esp32s3n16r8-ili9431
```

### Upload (when you have the board)

```bash
platformio run -e yoradio-esp32s3n16r8-ili9431 -t upload
```

### Upload filesystem (SPIFFS)

```bash
platformio run -e yoradio-esp32s3n16r8-ili9431 -t uploadfs
```

### Serial monitor

```bash
platformio device monitor -b 115200
```

### Where to change board-specific settings

- **Pins / display / I2S / encoder settings**: `myoptions.h`
- **PlatformIO environment / libraries / upload settings**: `platformio.ini`

### Repo

- GitHub: `https://github.com/starsalmon/yoradio-fusion-pros3`

