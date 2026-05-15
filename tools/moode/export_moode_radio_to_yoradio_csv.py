#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import sys
import urllib.request
from dataclasses import dataclass
from pathlib import Path


CFG_TABLES_PATH = "/command/cfg-table.php?cmd=get_cfg_tables"


@dataclass(frozen=True)
class PlaylistRow:
    name: str
    url: str
    flag: str = "0"

    def to_line(self) -> str:
        return f"{self.name}\t{self.url}\t{self.flag}"


def _read_existing_playlist(path: Path) -> list[PlaylistRow]:
    if not path.exists():
        return []
    rows: list[PlaylistRow] = []
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.strip("\n")
        if not line.strip():
            continue
        parts = line.split("\t")
        if len(parts) >= 2:
            name = parts[0].strip()
            url = parts[1].strip()
            flag = parts[2].strip() if len(parts) >= 3 and parts[2].strip() else "0"
            if name and url:
                rows.append(PlaylistRow(name=name, url=url, flag=flag))
    return rows


def _fetch_json(url: str, timeout_s: float) -> dict:
    req = urllib.request.Request(url, headers={"Accept": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout_s) as resp:
        data = resp.read()
    return json.loads(data.decode("utf-8", errors="replace"))


def _normalize_base_url(base_url: str) -> str:
    b = base_url.strip()
    if not b:
        raise ValueError("base_url is empty")
    if not b.startswith("http://") and not b.startswith("https://"):
        b = "http://" + b
    return b.rstrip("/")


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Export Moode radio stations to yoRadio playlist.csv format")
    ap.add_argument("--base-url", default="http://moode.local", help="Moode base URL (default: http://moode.local)")
    ap.add_argument("--timeout", type=float, default=8.0, help="HTTP timeout seconds (default: 8)")
    ap.add_argument("--playlist", default="data/data/playlist.csv", help="Target yoRadio playlist.csv path")
    ap.add_argument(
        "--types",
        default="all",
        help='Which Moode station "type" values to export: e.g. "f" or "r" or "f,r" or "all" (default: all)',
    )
    ap.add_argument(
        "--merge",
        action="store_true",
        help="Merge into existing playlist (preserve existing order, append new unique URLs).",
    )
    args = ap.parse_args(argv)

    base = _normalize_base_url(args.base_url)
    cfg_url = base + CFG_TABLES_PATH

    try:
        obj = _fetch_json(cfg_url, timeout_s=args.timeout)
    except Exception as e:
        print(f"ERROR: failed to fetch {cfg_url}: {e}", file=sys.stderr)
        return 2

    radio = obj.get("cfg_radio")
    if not isinstance(radio, dict):
        print('ERROR: response missing "cfg_radio" dict', file=sys.stderr)
        return 3

    allow_types: set[str] | None
    if str(args.types).strip().lower() == "all":
        allow_types = None
    else:
        allow_types = {t.strip() for t in str(args.types).split(",") if t.strip()}
        if not allow_types:
            print("ERROR: --types parsed to empty set", file=sys.stderr)
            return 4

    moode_rows: list[PlaylistRow] = []
    for url, meta in radio.items():
        if not isinstance(meta, dict):
            continue
        stype = str(meta.get("type", "")).strip()
        if allow_types is not None and stype not in allow_types:
            continue
        name = str(meta.get("name", "")).strip()
        u = str(url).strip()
        if not name or not u:
            continue
        moode_rows.append(PlaylistRow(name=name, url=u, flag="0"))

    # Stable output
    moode_rows.sort(key=lambda r: (r.name.lower(), r.url))

    playlist_path = Path(args.playlist)
    existing = _read_existing_playlist(playlist_path) if args.merge else []
    existing_by_url = {r.url: r for r in existing}

    merged: list[PlaylistRow] = list(existing)
    added = 0
    for r in moode_rows:
        if r.url in existing_by_url:
            continue
        merged.append(r)
        added += 1

    out_rows = merged if args.merge else moode_rows

    playlist_path.parent.mkdir(parents=True, exist_ok=True)
    playlist_path.write_text("\n".join(r.to_line() for r in out_rows) + "\n", encoding="utf-8")

    print(f"Fetched: {len(moode_rows)} station(s) from Moode ({cfg_url})", file=sys.stderr)
    if args.merge:
        print(f"Merged: {len(existing)} existing + {added} added => {len(out_rows)} total", file=sys.stderr)
    else:
        print(f"Wrote: {len(out_rows)} total", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

