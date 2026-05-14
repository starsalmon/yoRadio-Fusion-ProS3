#!/usr/bin/env python3
"""
Generate station logo header from a bulk RGB565 dump.

- Input bulk file: images_src/station_logos/bulk_logos.txt
- Input playlist:  data/data/playlist.csv
- Output header:   src/displays/bitmaps/station_logos_playlist.hpp

The output contains only the logos that match stations in playlist.csv.
"""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
BULK_FILE = REPO_ROOT / "images_src" / "station_logos" / "bulk_logos.txt"
PLAYLIST_FILE = REPO_ROOT / "data" / "data" / "playlist.csv"
OUT_HEADER = REPO_ROOT / "src" / "displays" / "bitmaps" / "station_logos_playlist.hpp"


@dataclass(frozen=True)
class Logo:
    label: str
    w: int
    h: int
    pixels: list[int]


def norm_name(s: str) -> str:
    # Lowercase, keep alnum only. ("Sub.FM" -> "subfm")
    return "".join(ch.lower() for ch in s if ch.isalnum())

def cleanup_bulk_label(label: str) -> str:
    """
    Convert common imagetocpp-style names into playlist-style station names.

    Examples:
      - "Triple J_round_sm" -> "Triple J"
      - "ABC Jazz_sm"       -> "ABC Jazz"
      - "Soma FM - Secret Agent_sm" -> "Soma FM - Secret Agent"
    """
    s = label.replace("_", " ").strip()
    # Drop common trailing tags (iteratively).
    drop = {
        "sm",
        "round",
        "square",
        "icon",
        "logo",
        "small",
        "large",
    }
    parts = [p for p in re.split(r"\s+", s) if p]
    while parts and parts[-1].lower() in drop:
        parts.pop()
    return " ".join(parts).strip()


def c_escape(s: str) -> str:
    return (
        s.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\n", "\\n")
        .replace("\r", "")
        .replace("\t", "\\t")
    )


def sym_from_label(label: str, w: int, h: int) -> str:
    base = re.sub(r"[^a-zA-Z0-9]+", "_", label).strip("_")
    if not base:
        base = "LOGO"
    if base[0].isdigit():
        base = "L" + base
    return f"LOGO_{base}_{w}x{h}"


def read_playlist_names(path: Path) -> list[str]:
    if not path.exists():
        raise FileNotFoundError(f"playlist not found: {path}")
    names: list[str] = []
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        raw = raw.strip()
        if not raw:
            continue
        # playlist.csv is tab-separated: NAME \t URL \t 0
        parts = raw.split("\t")
        if not parts:
            continue
        name = parts[0].strip()
        if name:
            names.append(name)
    return names


def parse_bulk_logos(text: str) -> list[Logo]:
    # Marker examples:
    #   // 'Double J', 80x80px
    #   // '1.FM - Blues Radio_sm', 80x80px
    marker = re.compile(r"^\s*//\s*'([^']+)'\s*,\s*(\d+)\s*x\s*(\d+)\s*px\s*$", re.IGNORECASE | re.MULTILINE)
    matches = list(marker.finditer(text))
    logos: list[Logo] = []

    if not matches:
        return logos

    for i, m in enumerate(matches):
        label = m.group(1).strip()
        w = int(m.group(2))
        h = int(m.group(3))
        body_start = m.end()
        body_end = matches[i + 1].start() if i + 1 < len(matches) else len(text)
        body = text[body_start:body_end]
        pixels = [int(x, 16) for x in re.findall(r"0x[0-9a-fA-F]+", body)]
        logos.append(Logo(label=label, w=w, h=h, pixels=pixels))

    return logos


def main() -> int:
    playlist_names = read_playlist_names(PLAYLIST_FILE)
    if not playlist_names:
        print(f"ERROR: no stations found in {PLAYLIST_FILE}", file=sys.stderr)
        return 2

    playlist_norm = {norm_name(n): n for n in playlist_names}
    playlist_norm_list = list(playlist_norm.keys())

    bulk_text = BULK_FILE.read_text(encoding="utf-8", errors="replace") if BULK_FILE.exists() else ""
    logos = parse_bulk_logos(bulk_text)

    selected: list[tuple[str, Logo]] = []  # (station_name, logo)
    errors: list[str] = []
    warnings: list[str] = []

    for logo in logos:
        # Name-only matching. Do not allow numeric "index" labels.
        if logo.label.isdigit():
            warnings.append(f"Skipping numeric label '{logo.label}' ({logo.w}x{logo.h}): use the station name from playlist.csv instead")
            continue

        station_name = cleanup_bulk_label(logo.label)

        nn = norm_name(station_name)
        # 1) Exact normalized match.
        if nn in playlist_norm:
            station_name = playlist_norm[nn]
        else:
            # 2) Fuzzy mapping: allow a bulk label to match a playlist name if:
            #    - the normalized forms overlap via substring in either direction, AND
            #    - the overlap is unique across the playlist.
            #
            # This handles cases like:
            #   "SUB_FM - Where Bass Matters_sm" -> "Sub.FM"
            #   "abc local_sm" -> "ABC Local Radio 774 Melbourne" (when unique)
            candidates = []
            for pn in playlist_norm_list:
                if pn in nn or nn in pn:
                    candidates.append(pn)
            if len(candidates) == 1:
                station_name = playlist_norm[candidates[0]]
            elif len(candidates) > 1:
                warnings.append(
                    f"Ambiguous label '{logo.label}' -> cleaned '{station_name}': matches multiple playlist stations ({', '.join(playlist_norm[c] for c in candidates)})"
                )
                continue
            else:
                continue  # not in playlist

        expected = logo.w * logo.h
        if len(logo.pixels) != expected:
            errors.append(f"'{logo.label}' {logo.w}x{logo.h}: pixels={len(logo.pixels)} expected={expected}")
            continue

        selected.append((station_name, logo))

    # De-dup by normalized station name (keep first)
    dedup: dict[str, tuple[str, Logo]] = {}
    for station_name, logo in selected:
        k = norm_name(station_name)
        dedup.setdefault(k, (station_name, logo))
    selected = list(dedup.values())

    # Write header
    out: list[str] = []
    out.append("#pragma once")
    out.append("")
    out.append("#include <stdint.h>")
    out.append("#include <pgmspace.h>")
    out.append("")
    out.append("// AUTO-GENERATED by tools/pio/gen_station_logos_from_bulk.py")
    out.append(f"// Source: {BULK_FILE.relative_to(REPO_ROOT)}")
    out.append(f"// Filter: {PLAYLIST_FILE.relative_to(REPO_ROOT)}")
    out.append("")

    # Always define a safe empty table.
    if not selected:
        out.append("static const StationLogoEntry STATION_LOGOS_PLAYLIST[] = { { nullptr, nullptr, 0, 0 } };")
        out.append("static constexpr size_t STATION_LOGOS_PLAYLIST_COUNT = 0;")
        out.append("")
        OUT_HEADER.write_text("\n".join(out) + "\n", encoding="utf-8")
        print(f"Wrote {OUT_HEADER} (no matching logos found)")
        for w in warnings:
            print("WARN: " + w, file=sys.stderr)
        if errors:
            print("WARN: some logos had invalid pixel counts:", file=sys.stderr)
            for e in errors:
                print(" - " + e, file=sys.stderr)
        return 0

    for station_name, logo in selected:
        sym = sym_from_label(station_name, logo.w, logo.h)
        out.append(f"static const uint16_t {sym}[{logo.w} * {logo.h}] PROGMEM = {{")
        out.append(f"  // '{c_escape(station_name)}', {logo.w}x{logo.h}px")
        # 16 pixels per line
        for i in range(0, len(logo.pixels), 16):
            chunk = logo.pixels[i : i + 16]
            line = ", ".join(f"0x{v:04x}" for v in chunk)
            if i + 16 < len(logo.pixels):
                line += ","
            out.append("  " + line)
        out.append("};")
        out.append("")

    out.append("static const StationLogoEntry STATION_LOGOS_PLAYLIST[] = {")
    for station_name, logo in selected:
        sym = sym_from_label(station_name, logo.w, logo.h)
        out.append(f'  {{ "{c_escape(station_name)}", {sym}, {logo.w}, {logo.h} }},')
    out.append("};")
    out.append(f"static constexpr size_t STATION_LOGOS_PLAYLIST_COUNT = {len(selected)};")
    out.append("")

    OUT_HEADER.write_text("\n".join(out) + "\n", encoding="utf-8")
    print(f"Wrote {OUT_HEADER} with {len(selected)} logos")
    for w in warnings:
        print("WARN: " + w, file=sys.stderr)
    if errors:
        print("WARN: some logos were skipped due to invalid pixel counts:", file=sys.stderr)
        for e in errors:
            print(" - " + e, file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

