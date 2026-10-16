#!/usr/bin/env python3
"""Preview / validate the ProS3 BT RX station-logo raster (matches display.cpp).

Slot sizes: 64x64 (vuLayout 0) or 80x80. Badge: filled circle, #0082FC (RGB565 0x041F),
white rune from bilinear-upsampled 17x16 Material BT mask. 4x4 supersampling per pixel.

Usage: python3 tools/gen_bt_rx_station_logo.py [64|80] [out.png]
"""

from __future__ import annotations

import math
import struct
import sys
from pathlib import Path

# Footer icon: ICON_BT_17x16 from footer_icons_16.h (MSB-first, 3 bytes/row)
ICON_BT_17x16 = bytes([
    0x01, 0x80, 0x00, 0x01, 0xc0, 0x00, 0x01, 0xe0, 0x00, 0x19, 0x30, 0x00, 0x0d, 0x30, 0x00, 0x07,
    0x30, 0x00, 0x03, 0xe0, 0x00, 0x01, 0xc0, 0x00, 0x01, 0xc0, 0x00, 0x03, 0xe0, 0x00, 0x07, 0x30,
    0x00, 0x0d, 0x30, 0x00, 0x19, 0x30, 0x00, 0x11, 0xe0, 0x00, 0x01, 0xc0, 0x00, 0x01, 0x80, 0x00,
])

BT_BLUE = (0x00, 0x82, 0xFC)
MAGENTA_KEY = (0xF8, 0x00, 0xF8)


def read_bt_pixel(bmp: bytes, x: int, y: int) -> float:
    if x < 0 or x >= 17 or y < 0 or y >= 16:
        return 0.0
    byte_idx = y * 3 + x // 8
    bit = 7 - (x % 8)
    return 1.0 if (bmp[byte_idx] >> bit) & 1 else 0.0


def sample_mask(bmp: bytes, u: float, v: float) -> float:
    fx, fy = u * 16.0, v * 15.0
    x0, y0 = int(fx), int(fy)
    x1 = min(x0 + 1, 16)
    y1 = min(y0 + 1, 15)
    tx, ty = fx - x0, fy - y0
    p00 = read_bt_pixel(bmp, x0, y0)
    p10 = read_bt_pixel(bmp, x1, y0)
    p01 = read_bt_pixel(bmp, x0, y1)
    p11 = read_bt_pixel(bmp, x1, y1)
    a = p00 + (p10 - p00) * tx
    b = p01 + (p11 - p01) * tx
    return a + (b - a) * ty


def render(size: int) -> list[tuple[int, int, int]]:
    cx = cy = size * 0.5
    r = size * 0.47
    rune_scale = r * 1.35
    out: list[tuple[int, int, int]] = []
    for y in range(size):
        for x in range(size):
            badge_a = rune_a = 0.0
            for sy in range(4):
                for sx in range(4):
                    px = x + (sx + 0.5) / 4.0
                    py = y + (sy + 0.5) / 4.0
                    d = math.hypot(px - cx, py - cy)
                    badge_a += max(0.0, min(1.0, (r - d) + 0.85))
                    dx = (px - cx) / rune_scale
                    dy = (py - cy) / rune_scale
                    u = dx * (17.0 / 16.0) + 0.5
                    v = dy + 0.5
                    if 0.0 <= u <= 1.0 and 0.0 <= v <= 1.0:
                        rune_a += sample_mask(ICON_BT_17x16, u, v)
            badge_a /= 16.0
            rune_a /= 16.0
            if badge_a < 0.04:
                out.append(MAGENTA_KEY)
            elif rune_a > 0.45:
                out.append((255, 255, 255))
            else:
                out.append(BT_BLUE)
    return out


def write_png(path: Path, size: int, rgb: list[tuple[int, int, int]]) -> None:
    import zlib

    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    raw = bytearray()
    for y in range(size):
        raw.append(0)
        for x in range(size):
            raw.extend(rgb[y * size + x])
    compressed = zlib.compress(bytes(raw), 9)
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", compressed)
    png += chunk(b"IEND", b"")
    path.write_bytes(png)


def main() -> None:
    size = int(sys.argv[1]) if len(sys.argv) > 1 else 64
    if size not in (64, 80):
        raise SystemExit("size must be 64 or 80")
    rgb = render(size)
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(f"/tmp/bt_rx_logo_{size}.png")
    write_png(out, size, rgb)
    print(f"Wrote {out} ({size}x{size}, badge #0082FC, 4x SS)")


if __name__ == "__main__":
    main()
