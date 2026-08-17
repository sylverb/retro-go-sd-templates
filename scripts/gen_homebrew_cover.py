#!/usr/bin/env python3
"""Generate a compact placeholder JPEG cover for a GWHB homebrew.

The firmware HW JPEG scratch caps covers at 186x100 / 10 KiB. This script
defaults to a much smaller canvas so the coverflow thumbnail does not dominate
the screen, and scales the title text to fit.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

# Hard max from gui.c — never exceed these.
COVER_MAX_WIDTH = 186
COVER_MAX_HEIGHT = 100
COVER_SIZE_MAX = 10 * 1024

# Default placeholder size: compact coverflow tile, not the HW maximum.
DEFAULT_WIDTH = 100
DEFAULT_HEIGHT = 64


def load_font(size: int) -> ImageFont.ImageFont:
    for name in (
        "DejaVuSans-Bold.ttf",
        "DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
    ):
        try:
            return ImageFont.truetype(name, size=size)
        except OSError:
            continue
    return ImageFont.load_default()


def fit_font(draw: ImageDraw.ImageDraw, text: str, max_width: int, max_height: int) -> ImageFont.ImageFont:
    # Prefer a readable size, then shrink until the title fits the box.
    for size in range(22, 7, -1):
        font = load_font(size)
        left, top, right, bottom = draw.textbbox((0, 0), text, font=font)
        if (right - left) <= max_width and (bottom - top) <= max_height:
            return font
    return load_font(8)


def generate(path: Path, title: str, width: int, height: int) -> None:
    if width > COVER_MAX_WIDTH or height > COVER_MAX_HEIGHT:
        raise SystemExit(
            f"cover size {width}x{height} exceeds max "
            f"{COVER_MAX_WIDTH}x{COVER_MAX_HEIGHT}"
        )

    img = Image.new("RGB", (width, height), (32, 48, 96))
    draw = ImageDraw.Draw(img)
    margin = 4
    draw.rectangle(
        (margin, margin, width - margin - 1, height - margin - 1),
        outline=(220, 220, 255),
        width=1,
    )

    text = title.strip() or "Homebrew"
    pad = 8
    font = fit_font(draw, text, width - 2 * pad, height - 2 * pad)
    left, top, right, bottom = draw.textbbox((0, 0), text, font=font)
    tw, th = right - left, bottom - top
    x = (width - tw) // 2 - left
    y = (height - th) // 2 - top
    draw.text((x, y), text, fill=(255, 255, 255), font=font)

    path.parent.mkdir(parents=True, exist_ok=True)
    img.save(path, "JPEG", quality=85, optimize=True)
    size = path.stat().st_size
    if size > COVER_SIZE_MAX:
        raise SystemExit(f"cover too big: {size} bytes (max {COVER_SIZE_MAX})")
    print(f"cover: {path} ({width}x{height}, {size} bytes)")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", type=Path, required=True, help="output JPEG path")
    ap.add_argument("--title", default="Homebrew", help="label drawn on the cover")
    ap.add_argument("--width", type=int, default=DEFAULT_WIDTH)
    ap.add_argument("--height", type=int, default=DEFAULT_HEIGHT)
    args = ap.parse_args()
    generate(args.out, args.title, args.width, args.height)


if __name__ == "__main__":
    main()
