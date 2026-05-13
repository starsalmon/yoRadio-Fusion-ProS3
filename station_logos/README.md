## Station logo bulk import (RGB565)

This folder is for **bulk station logos** (e.g. from Moode Audio) that you paste in as raw RGB565 hex.

### Files
- `bulk_logos.txt`: paste your logo dumps here (many logos).
- `../../gen_station_logos_from_bulk.py`: generator script that:
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

### Generate the header
From the repo root:

```bash
python3 tools/pio/gen_station_logos_from_bulk.py
```

Then build/flash normally with PlatformIO.

### Note (folder move)
The canonical location for `bulk_logos.txt` is now `station_logos/bulk_logos.txt` at the repo root.

