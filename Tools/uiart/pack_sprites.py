#!/usr/bin/env python3
"""Packs loose pixel-art frames into one atlas and the @spritesheet that names them.

Every frame lands in a cell the size of the largest frame, centred horizontally and standing on the
cell's bottom edge, so feet stay put when the frame changes. Cells sit one source pixel apart and in
from the edge, or bilinear filtering reads a neighbour's (or, wrapping, the far edge's) pixels into a
frame's border.

The art is scaled up by --scale with nearest-neighbour filtering, because the renderer filters
bilinearly and a 16px frame stretched to a HUD element would blur. The stylesheet declares the
matching resolution, so a frame's natural size is still its original pixel size in dp.

    python Tools/uiart/pack_sprites.py <frames dir> UI/art/hero.png UI/art/hero.rcss         --sheet hero --src /UI/art/hero.png --map Hero_Walk_0=hero_walk_0 --map Hero_Walk_1=hero_walk_1

Each --map names a source PNG (without .png) and the sprite it becomes, in atlas order. Needs Pillow
(`pip install pillow`).
"""

from __future__ import annotations

import argparse
import pathlib

from PIL import Image


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("frames", type=pathlib.Path)
    parser.add_argument("png", type=pathlib.Path)
    parser.add_argument("rcss", type=pathlib.Path)
    parser.add_argument("--map", action="append", required=True, help="source file stem = sprite name")
    parser.add_argument("--scale", type=int, default=8)
    parser.add_argument("--columns", type=int, default=8)
    parser.add_argument("--sheet", default="sprites", help="@spritesheet name")
    parser.add_argument("--src", required=True, help="the atlas path as the document sees it, e.g. /UI/art/mario.png")
    args = parser.parse_args()

    entries = []
    for pair in args.map:
        stem, name = pair.split("=", 1)
        entries.append((name.strip(), Image.open(args.frames / f"{stem.strip()}.png").convert("RGBA")))

    scale = args.scale
    cell_w = max(image.width for _, image in entries)
    cell_h = max(image.height for _, image in entries)
    columns = min(args.columns, len(entries))
    rows = (len(entries) + columns - 1) // columns
    pitch_w = (cell_w + 1) * scale
    pitch_h = (cell_h + 1) * scale

    atlas = Image.new("RGBA", (columns * pitch_w + scale, rows * pitch_h + scale), (0, 0, 0, 0))
    lines = []
    for index, (name, image) in enumerate(entries):
        cell_x = scale + (index % columns) * pitch_w
        cell_y = scale + (index // columns) * pitch_h
        x = cell_x + (cell_w - image.width) // 2 * scale
        y = cell_y + (cell_h - image.height) * scale
        atlas.paste(image.resize((image.width * scale, image.height * scale), Image.NEAREST), (x, y))
        lines.append(f"\t{name}: {cell_x}px {cell_y}px {cell_w * scale}px {cell_h * scale}px;")

    args.png.parent.mkdir(parents=True, exist_ok=True)
    atlas.save(args.png)
    header = f"@spritesheet {args.sheet}\n{{\n\tsrc: {args.src};\n\tresolution: {scale}x;\n\n"
    args.rcss.write_text(header + "\n".join(lines) + "\n}\n", encoding="utf-8")
    print(f"{len(entries)} frames, {cell_w}x{cell_h} cells at {scale}x -> {args.png} ({atlas.width}x{atlas.height})")


if __name__ == "__main__":
    main()
