#!/usr/bin/env python3
"""
Generate station logo files for SPIFFS from an image folder (JPG/PNG).

- Source folder: station_logos/large_logos/ (preferred) or station_logos/images/ (fallback)
- Playlist:       data/data/playlist.csv
- Output SPIFFS:  data/logos/*.ylg

Output:
- For each station in playlist.csv, pick the best matching image filename.
- Resize to 80x80, convert to RGB565.
- Encode as either raw RGB565 or simple RLE (count,color uint16 pairs),
  choosing whichever is smaller per-logo.
- Also generate a default logo from station_logos/default_logo.png as /logos/default.ylg
"""

from __future__ import annotations

import re
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

from PIL import Image


REPO_ROOT = Path(__file__).resolve().parents[2]
_SRC_PRIMARY = REPO_ROOT / "station_logos" / "large_logos"
_SRC_FALLBACK = REPO_ROOT / "station_logos" / "images"
SRC_DIR = _SRC_PRIMARY if _SRC_PRIMARY.exists() else _SRC_FALLBACK
PLAYLIST_FILE = REPO_ROOT / "data" / "data" / "playlist.csv"
OUT_SPIFFS_DIR = REPO_ROOT / "data" / "logos"
DEFAULT_LOGO_PNG = REPO_ROOT / "station_logos" / "default_logo.png"
DEFAULT_LOGO_JPG = REPO_ROOT / "station_logos" / "default_logo.jpg"

TARGET_W = 80
TARGET_H = 80


def norm_name(s: str) -> str:
    return "".join(ch.lower() for ch in s if ch.isalnum())


def c_escape(s: str) -> str:
    return (
        s.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\n", "\\n")
        .replace("\r", "")
        .replace("\t", "\\t")
    )


def sym_from_label(label: str, suffix: str) -> str:
    base = re.sub(r"[^a-zA-Z0-9]+", "_", label).strip("_")
    if not base:
        base = "LOGO"
    if base[0].isdigit():
        base = "L" + base
    # Use a distinct prefix to avoid symbol collisions with other generators
    # (e.g. station_logos_playlist.hpp from bulk hex).
    return f"LOGOEX_{base}_{TARGET_W}x{TARGET_H}{suffix}"


def _norm_for_key(station: str) -> str:
    # Keep in sync with firmware-side normalization.
    key = re.sub(r"[^a-zA-Z0-9]+", "_", station).strip("_").lower()
    return key or "logo"


def _fnv1a32(data: bytes) -> int:
    h = 0x811C9DC5
    for b in data:
        h ^= b
        h = (h * 0x01000193) & 0xFFFFFFFF
    return h


def spiffs_key_from_station(station: str) -> str:
    # SPIFFS has a small per-object name limit; use short deterministic keys.
    # Key is FNV-1a 32-bit hash of the normalized station name, as 8 hex chars.
    norm = _norm_for_key(station)
    return f"{_fnv1a32(norm.encode('utf-8')):08x}"


def write_spiffs_logo_file(path: Path, *, fmt: int, w: int, h: int, words: list[int]) -> None:
    # Binary format (little-endian):
    # - magic: 4 bytes = b"YLG0"
    # - version: u8   = 1
    # - format:  u8   = 0 raw, 1 rle (count,color words)
    # - w:       u16
    # - h:       u16
    # - words:   u32  (# of uint16 words that follow)
    # - payload: words * u16
    header = struct.pack("<4sBBHHI", b"YLG0", 1, fmt, w, h, len(words))
    payload = bytearray()
    payload_extend = payload.extend
    for ww in words:
        payload_extend(struct.pack("<H", ww & 0xFFFF))
    path.write_bytes(header + payload)


def read_playlist_names(path: Path) -> list[str]:
    if not path.exists():
        raise FileNotFoundError(f"playlist not found: {path}")
    names: list[str] = []
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        raw = raw.strip()
        if not raw:
            continue
        parts = raw.split("\t")
        if not parts:
            continue
        name = parts[0].strip()
        if name:
            names.append(name)
    return names


@dataclass(frozen=True)
class ImageCandidate:
    path: Path
    label: str  # filename stem
    norm: str


def list_candidates(src_dir: Path) -> list[ImageCandidate]:
    if not src_dir.exists():
        return []
    out: list[ImageCandidate] = []
    for p in sorted(src_dir.iterdir()):
        if not p.is_file():
            continue
        if p.name == ".DS_Store":
            continue
        if p.suffix.lower() not in (".jpg", ".jpeg", ".png"):
            continue
        out.append(ImageCandidate(path=p, label=p.stem, norm=norm_name(p.stem)))
    return out


def pick_image_for_station(station_name: str, candidates: list[ImageCandidate]) -> ImageCandidate | None:
    nn = norm_name(station_name)
    # 1) exact normalized match
    for c in candidates:
        if c.norm == nn:
            return c
    # 2) substring fuzzy (unique best by longest overlap)
    best: ImageCandidate | None = None
    best_score = 0
    for c in candidates:
        if not c.norm:
            continue
        if c.norm in nn or nn in c.norm:
            score = min(len(c.norm), len(nn))
            if score > best_score:
                best = c
                best_score = score
    return best


def rgb888_to_rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def image_to_rgb565_80x80(path: Path) -> list[int]:
    im = Image.open(path)
    im = im.convert("RGB")
    if im.size != (TARGET_W, TARGET_H):
        im = im.resize((TARGET_W, TARGET_H), resample=Image.LANCZOS)
    # getdata() returns tuples (r,g,b)
    px = list(im.getdata())
    return [rgb888_to_rgb565(r, g, b) for (r, g, b) in px]


def rle_encode_rgb565(pixels: list[int]) -> list[int]:
    # Output words: count,color,count,color...
    out: list[int] = []
    i = 0
    n = len(pixels)
    while i < n:
        c = pixels[i]
        j = i + 1
        while j < n and pixels[j] == c and (j - i) < 0xFFFF:
            j += 1
        out.append(j - i)  # count
        out.append(c)      # color
        i = j
    return out


def emit_words_as_c_array(words: list[int], words_per_line: int = 16) -> list[str]:
    lines: list[str] = []
    hexw = [f"0x{w:04x}" for w in words]
    for i in range(0, len(hexw), words_per_line):
        chunk = hexw[i : i + words_per_line]
        line = ", ".join(chunk)
        if i + words_per_line < len(hexw):
            line += ","
        lines.append("  " + line)
    return lines


def main() -> int:
    playlist_names = read_playlist_names(PLAYLIST_FILE)
    if not playlist_names:
        print(f"ERROR: no stations found in {PLAYLIST_FILE}", file=sys.stderr)
        return 2

    candidates = list_candidates(SRC_DIR)
    if not candidates:
        print(f"ERROR: no images found in {SRC_DIR}", file=sys.stderr)
        return 2

    selected: list[tuple[str, ImageCandidate]] = []
    missing: list[str] = []
    for station in playlist_names:
        c = pick_image_for_station(station, candidates)
        if not c:
            missing.append(station)
            continue
        selected.append((station, c))

    OUT_SPIFFS_DIR.mkdir(parents=True, exist_ok=True)
    spiffs_index: list[tuple[str, str]] = []

    # Clean stale outputs so old filenames can't break SPIFFS builds.
    for p in OUT_SPIFFS_DIR.glob("*.ylg"):
        try:
            p.unlink()
        except OSError:
            pass

    for station, cand in selected:
        pixels = image_to_rgb565_80x80(cand.path)
        raw_words = pixels
        rle_words = rle_encode_rgb565(pixels)

        raw_bytes = len(raw_words) * 2
        rle_bytes = len(rle_words) * 2

        key = spiffs_key_from_station(station)
        spiffs_name = f"{key}.ylg"

        if rle_bytes < raw_bytes:
            write_spiffs_logo_file(OUT_SPIFFS_DIR / spiffs_name, fmt=1, w=TARGET_W, h=TARGET_H, words=rle_words)
        else:
            write_spiffs_logo_file(OUT_SPIFFS_DIR / spiffs_name, fmt=0, w=TARGET_W, h=TARGET_H, words=raw_words)

        spiffs_index.append((station, spiffs_name))

    # Default logo (used when a station doesn't have a matching SPIFFS logo file).
    default_src = DEFAULT_LOGO_PNG if DEFAULT_LOGO_PNG.exists() else DEFAULT_LOGO_JPG
    if default_src.exists():
        dpixels = image_to_rgb565_80x80(default_src)
        draw_words = dpixels
        drle_words = rle_encode_rgb565(dpixels)
        if len(drle_words) * 2 < len(draw_words) * 2:
            write_spiffs_logo_file(OUT_SPIFFS_DIR / "default.ylg", fmt=1, w=TARGET_W, h=TARGET_H, words=drle_words)
        else:
            write_spiffs_logo_file(OUT_SPIFFS_DIR / "default.ylg", fmt=0, w=TARGET_W, h=TARGET_H, words=draw_words)
        spiffs_index.append(("_DEFAULT_", "default.ylg"))
    else:
        print(f"WARN: default logo not found at {DEFAULT_LOGO_PNG} (or .jpg)", file=sys.stderr)

    (OUT_SPIFFS_DIR / "index.tsv").write_text(
        "\n".join(f"{s}\t{fn}" for (s, fn) in spiffs_index) + ("\n" if spiffs_index else ""),
        encoding="utf-8",
    )
    print(f"Wrote {OUT_SPIFFS_DIR / 'index.tsv'} with {len(spiffs_index)} entries")
    if missing:
        print("WARN: missing images for:", file=sys.stderr)
        for m in missing:
            print(" - " + m, file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

