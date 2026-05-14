## Station logos

This repo supports two logo sources:

- **Primary (current)**: a local **image library** (JPG/PNG) that gets converted into compact SPIFFS files (`/logos/*.ylg`) at `buildfs/uploadfs` time.
- **Legacy/manual**: a bulk RGB565 hex dump (`bulk_logos.txt`) that can be converted into a compiled header.

### Files
- **Image library → SPIFFS**
  - `large_logos/`: optional local station logo library (JPG/PNG). If missing, the generator falls back to `images/`.
  - `images/`: repo-provided logo library (JPG/PNG).
  - `default_logo.png` (or `.jpg`): optional fallback logo.
  - `../../tools/pio/gen_station_logos_from_images.py`: generator that:
    - reads `data/data/playlist.csv`
    - converts matching images into `data/logos/*.ylg`
    - writes `data/logos/index.tsv`
  - `../../tools/pio/pre_fs_generate_logos.py`: PlatformIO hook that runs the generator for `buildfs` / `uploadfs`.

- **Bulk RGB565 → header (legacy/manual)**
  - `bulk_logos.txt`: paste your logo dumps here (many logos).
  - `../../tools/pio/gen_station_logos_from_bulk.py`: generator that:
    - reads `data/data/playlist.csv`
    - filters logos to only the stations in the playlist
    - writes `src/displays/bitmaps/station_logos_playlist.hpp`

### Accepted format
Each logo section must start with a marker like:

```txt
// 'Some Station Name', 80x80px
0x1234, 0xabcd, ...
```

Notes:
- Width/height can be `80x80px`, `64x64px`, etc.
- The script expects exactly `width*height` pixels for each section (RGB565).
- The `'name'` must be the **station name** from `playlist.csv` (first column).
  Matching is name-only; playlist order/position is not used.

### Generate SPIFFS logos (recommended)

This runs automatically when you build/upload the filesystem:

```bash
platformio run -e yoradio-um_pros3-ili9341 -t buildfs
# or:
platformio run -e yoradio-um_pros3-ili9341 -t uploadfs
```

### Generate the legacy header (manual)
From the repo root:

```bash
python3 tools/pio/gen_station_logos_from_bulk.py
```

Then build/flash normally with PlatformIO.

### Note (folder move)
The canonical location for `bulk_logos.txt` is now `station_logos/bulk_logos.txt` at the repo root.

