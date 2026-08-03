#!/usr/bin/env python3
"""Bake the three mote tiles that `/folio/lib/motes.rcss` draws.

Each tile is a field of soft dots on transparent white, seamless top to bottom and left to
right, so two stacked copies scrolling past the viewport never show a join. White so
`image-color` can tint it from the theme.

    python Tools/uiart/make_motes.py

Writes rcss/art/motes_{far,mid,near}.png. Deterministic: same seed, same bytes, so a re-bake
is a no-op in the diff unless a parameter changed.
"""

from __future__ import annotations

import argparse
import math
import random
import struct
import zlib
from pathlib import Path

# 1920x1200dp is the box the decorator stretches one tile over, so the tile carries that ratio
# and a mote stays round on a 1080p screen. A square tile would draw every one as an ellipse.
WIDTH = 960
HEIGHT = 600

# count, radius range, peak alpha range, falloff exponent.
# Depth reads as size and softness together: the near layer is largest, dimmest at the core and
# softest, which is what makes it sit in front of the others rather than just above them.
LAYERS = {
    "far": dict(count=110, radius=(1.6, 3.4), alpha=(0.20, 0.40), falloff=1.5, seed=17),
    "mid": dict(count=52, radius=(3.0, 6.5), alpha=(0.28, 0.55), falloff=2.0, seed=101),
    "near": dict(count=18, radius=(7.0, 15.0), alpha=(0.22, 0.45), falloff=3.0, seed=8081),
}


def write_png(path: Path, width: int, height: int, alpha: bytearray) -> None:
    """White RGBA with the given straight-alpha coverage."""
    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter: none
        row = alpha[y * width : (y + 1) * width]
        for a in row:
            raw += b"\xff\xff\xff"
            raw.append(a)

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (
            struct.pack(">I", len(data))
            + tag
            + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        )

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    path.write_bytes(png)


def bake(count: int, radius: tuple, alpha: tuple, falloff: float, seed: int) -> bytearray:
    rng = random.Random(seed)
    field = [0.0] * (WIDTH * HEIGHT)

    for _ in range(count):
        cx = rng.uniform(0.0, WIDTH)
        cy = rng.uniform(0.0, HEIGHT)
        r = rng.uniform(*radius)
        peak = rng.uniform(*alpha)

        # Every dot is stamped at its eight neighbouring wraps as well, so one straddling an
        # edge arrives whole on the other side. Cheap at these radii and the only thing that
        # makes the tile seamless.
        for ox in (-WIDTH, 0, WIDTH):
            for oy in (-HEIGHT, 0, HEIGHT):
                stamp(field, cx + ox, cy + oy, r, peak, falloff)

    return bytearray(min(255, int(round(v * 255.0))) for v in field)


def stamp(field: list, cx: float, cy: float, r: float, peak: float, falloff: float) -> None:
    x0 = max(0, int(math.floor(cx - r)))
    x1 = min(WIDTH - 1, int(math.ceil(cx + r)))
    y0 = max(0, int(math.floor(cy - r)))
    y1 = min(HEIGHT - 1, int(math.ceil(cy + r)))
    if x0 > x1 or y0 > y1:
        return

    for y in range(y0, y1 + 1):
        dy = (y + 0.5) - cy
        base = y * WIDTH
        for x in range(x0, x1 + 1):
            dx = (x + 0.5) - cx
            t = math.hypot(dx, dy) / r
            if t >= 1.0:
                continue
            v = peak * ((1.0 - t * t) ** falloff)
            field[base + x] = min(1.0, field[base + x] + v)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--out",
        type=Path,
        default=Path(__file__).resolve().parents[2] / "rcss" / "art",
        help="directory to write the tiles into",
    )
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    for name, params in LAYERS.items():
        alpha = bake(**params)
        path = args.out / f"motes_{name}.png"
        write_png(path, WIDTH, HEIGHT, alpha)
        print(f"{path}  {WIDTH}x{HEIGHT}  {params['count']} motes")


if __name__ == "__main__":
    main()
