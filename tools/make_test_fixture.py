#!/usr/bin/env python3
"""Regenerate the raw-pixel fixture used by core/tests/test_ocr_e2e.

naina ships no image decoder on purpose, so the C++ end-to-end test consumes
raw pixels rather than a PNG. The format is deliberately trivial:

    int32 LE width, int32 LE height, then width*height*3 RGB8 bytes

Usage:
    python tools/make_test_fixture.py

Requires Pillow (``pip install pillow``). Font lookup covers macOS, the common
Linux distributions and Windows; the script exits with a clear message rather
than silently producing an unreadable bitmap-font fixture.
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    sys.exit("Pillow is required: pip install pillow")

OUT = Path(__file__).resolve().parent.parent / "core/tests/fixtures/hello_world.rgb"
WIDTH, HEIGHT = 480, 140
LINES = ("HELLO WORLD", "naina 2026")

# Scalable fonts by platform. Pillow's bitmap fallback renders far too small
# for a text detector to find, so a real TrueType font is required.
FONT_CANDIDATES = (
    # macOS
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "/System/Library/Fonts/Helvetica.ttc",
    # Debian / Ubuntu
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    # Fedora / RHEL / Arch / Alpine
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
    # Windows
    "C:/Windows/Fonts/arial.ttf",
    "C:/Windows/Fonts/segoeui.ttf",
)


def find_font(size: int):
    for path in FONT_CANDIDATES:
        try:
            return ImageFont.truetype(path, size)
        except OSError:
            continue
    for name in ("DejaVuSans.ttf", "LiberationSans-Regular.ttf", "Arial.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return None


def main() -> int:
    font = find_font(40)
    if font is None:
        sys.exit(
            "No scalable TrueType font found. Install one (e.g. "
            "fonts-dejavu-core on Debian/Ubuntu) and retry."
        )

    img = Image.new("RGB", (WIDTH, HEIGHT), (255, 255, 255))
    draw = ImageDraw.Draw(img)
    for i, text in enumerate(LINES):
        draw.text((24, 18 + i * 58), text, fill=(0, 0, 0), font=font)

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("wb") as f:
        f.write(struct.pack("<ii", WIDTH, HEIGHT))
        f.write(img.tobytes())

    print(f"wrote {OUT} ({OUT.stat().st_size} bytes, {WIDTH}x{HEIGHT})")
    print("note: the exact glyph shapes depend on the font found on this")
    print("      platform, so regenerating may change recognition confidences")
    print("      slightly. The assertions in test_ocr_e2e are text-based, not")
    print("      pixel- or score-exact, so that is fine.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
