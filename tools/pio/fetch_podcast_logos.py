#!/usr/bin/env python3
"""
Fetch and cache podcast show images into images_src/podcast_logos/.
This is intended to run at filesystem-build time (buildfs/uploadfs) so:

- podcast images added via podcasts.csv after compile still get logos
- the existing SPIFFS logo generator can convert them to /logos/<hash>.ylg

Behavior:
- Reads data/data/podcasts.csv (tab/space separated: show <ws> rss_url <ws> limit?)
- Fetches each RSS feed and extracts a show image URL, preferring:
  1) <itunes:image href="...">
  2) <image><url>...</url></image>
- Downloads and converts to PNG (Pillow) and saves as:
    images_src/podcast_logos/<normalized_show_key>.png
  where normalized_show_key matches the firmware + logo generator normalization,
  so the resulting hash key matches the show name displayed in Podcast mode.

By default, existing cached images are not overwritten.
"""

from __future__ import annotations

import argparse
import io
import os
import re
import ssl
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path

from PIL import Image


REPO_ROOT = Path(__file__).resolve().parents[2]
PODCASTS_FILE = REPO_ROOT / "data" / "data" / "podcasts.csv"
OUT_DIR = REPO_ROOT / "images_src" / "podcast_logos"

# Roughly match the UA used on-device for RSS fetches.
DEFAULT_UA = (
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/124.0 Safari/537.36"
)


def _norm_for_key(name: str) -> str:
    # Must match tools/pio/gen_station_logos_from_images.py and firmware-side normalization.
    key = re.sub(r"[^a-zA-Z0-9]+", "_", name).strip("_").lower()
    return key or "logo"


def _parse_podcasts_csv_line(line: str) -> tuple[str, str] | None:
    s = line.strip()
    if not s or s.startswith("#"):
        return None

    m = re.search(r"https?://\S+", s)
    if not m:
        return None

    url = m.group(0).strip()
    show = s[: m.start()].strip()
    if not show:
        return None
    return show, url


def _extract_show_image_url(xml_text: str) -> str | None:
    # 1) itunes:image (most podcasts)
    m = re.search(r"<itunes:image[^>]*\bhref=['\"]([^'\"]+)['\"]", xml_text, flags=re.IGNORECASE)
    if m:
        return m.group(1).strip()

    # 2) RSS channel image
    m = re.search(r"<image>\s*.*?<url>\s*([^<\s]+)\s*</url>", xml_text, flags=re.IGNORECASE | re.DOTALL)
    if m:
        return m.group(1).strip()

    return None


def _http_get_text(url: str, *, timeout_s: float) -> str:
    # Match on-device behavior (setInsecure) by default; this avoids host CA issues
    # on some dev environments. Set PODCAST_LOGOS_VERIFY_SSL=1 to enforce verification.
    verify_ssl = os.environ.get("PODCAST_LOGOS_VERIFY_SSL", "0").strip() == "1"
    ctx = ssl.create_default_context() if verify_ssl else ssl._create_unverified_context()  # noqa: SLF001
    req = urllib.request.Request(
        url,
        headers={
            "User-Agent": DEFAULT_UA,
            "Accept": "application/rss+xml, application/xml, text/xml, */*",
        },
        method="GET",
    )
    with urllib.request.urlopen(req, timeout=timeout_s, context=ctx) as resp:
        data = resp.read()
    # RSS feeds are usually UTF-8, but be forgiving.
    return data.decode("utf-8", errors="replace")


def _http_get_bytes(url: str, *, timeout_s: float) -> bytes:
    verify_ssl = os.environ.get("PODCAST_LOGOS_VERIFY_SSL", "0").strip() == "1"
    ctx = ssl.create_default_context() if verify_ssl else ssl._create_unverified_context()  # noqa: SLF001
    req = urllib.request.Request(
        url,
        headers={
            "User-Agent": DEFAULT_UA,
            "Accept": "image/avif,image/webp,image/apng,image/*,*/*;q=0.8",
        },
        method="GET",
    )
    with urllib.request.urlopen(req, timeout=timeout_s, context=ctx) as resp:
        return resp.read()


@dataclass(frozen=True)
class Result:
    show: str
    rss_url: str
    out_path: Path
    status: str  # "ok" | "skip" | "fail"
    detail: str


def fetch_one(show: str, rss_url: str, *, force: bool, timeout_s: float) -> Result:
    out_name = _norm_for_key(show) + ".png"
    out_path = OUT_DIR / out_name

    if out_path.exists() and not force:
        return Result(show, rss_url, out_path, "skip", "cached")

    try:
        xml_text = _http_get_text(rss_url, timeout_s=timeout_s)
    except (urllib.error.URLError, urllib.error.HTTPError, TimeoutError) as e:
        return Result(show, rss_url, out_path, "fail", f"rss fetch failed: {e}")

    img_url = _extract_show_image_url(xml_text)
    if not img_url:
        return Result(show, rss_url, out_path, "fail", "no show image tag found")

    try:
        img_bytes = _http_get_bytes(img_url, timeout_s=timeout_s)
    except (urllib.error.URLError, urllib.error.HTTPError, TimeoutError) as e:
        return Result(show, rss_url, out_path, "fail", f"image fetch failed: {e}")

    # Convert to PNG for the existing pipeline (only consumes .png/.jpg/.jpeg).
    try:
        OUT_DIR.mkdir(parents=True, exist_ok=True)
        with Image.open(io.BytesIO(img_bytes)) as im:
            im = im.convert("RGBA")
            im.save(out_path, format="PNG", optimize=True)
    except Exception as e:  # noqa: BLE001 - best-effort conversion
        return Result(show, rss_url, out_path, "fail", f"image decode/convert failed: {e}")

    return Result(show, rss_url, out_path, "ok", f"from {img_url}")


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--force", action="store_true", help="overwrite cached images")
    ap.add_argument("--timeout-s", type=float, default=12.0, help="per-request timeout (seconds)")
    ap.add_argument(
        "--best-effort",
        action="store_true",
        help="never fail the build if network is down (always exit 0)",
    )
    args = ap.parse_args(argv)

    if not PODCASTS_FILE.exists():
        msg = f"[podcast_logos] missing {PODCASTS_FILE}"
        print(msg, file=sys.stderr)
        return 0 if args.best_effort else 2

    lines = PODCASTS_FILE.read_text(encoding="utf-8", errors="replace").splitlines()
    entries: list[tuple[str, str]] = []
    for ln in lines:
        parsed = _parse_podcasts_csv_line(ln)
        if parsed:
            entries.append(parsed)

    if not entries:
        print("[podcast_logos] no podcast entries found (nothing to do)")
        return 0

    # Allow disabling fetch from the environment (useful when offline).
    if os.environ.get("PODCAST_LOGOS_FETCH", "1").strip() == "0":
        print("[podcast_logos] disabled via PODCAST_LOGOS_FETCH=0 (skipping)")
        return 0

    ok = skip = fail = 0
    for show, rss in entries:
        res = fetch_one(show, rss, force=args.force, timeout_s=args.timeout_s)
        if res.status == "ok":
            ok += 1
            print(f"[podcast_logos] ok   {show} -> {res.out_path.name} ({res.detail})")
        elif res.status == "skip":
            skip += 1
            print(f"[podcast_logos] skip {show} -> {res.out_path.name} ({res.detail})")
        else:
            fail += 1
            print(f"[podcast_logos] fail {show}: {res.detail}", file=sys.stderr)
        # be polite to hosts
        time.sleep(0.2)

    print(f"[podcast_logos] done: ok={ok} skip={skip} fail={fail} out={OUT_DIR}")
    if fail and not args.best_effort:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

