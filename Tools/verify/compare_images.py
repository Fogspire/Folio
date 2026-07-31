#!/usr/bin/env python3
"""Compares two PNGs numerically, because looking at them does not scale.

Two renderers draw every document Folio ships, and the only honest way to say they agree is to
measure it. A person can see that a shadow moved; nobody can see that it moved by two pixels, and
nobody can watch fifty documents for it.

**Alpha and colour are reported separately, and that is the point.** The preview does no sRGB
conversion where Slate linearises vertex colour and applies display gamma, so RGB will never match
and demanding that it does produces a check nobody can keep green. Coverage is a different matter:
where a glyph or a shadow or a mask puts its alpha is geometry, and geometry must match. So `--gate
alpha` is the useful cross-renderer comparison, and `--gate rgba` is for two shots from the *same*
renderer, where any difference at all is a regression.

  compare_images.py a.png b.png                        # report, gate on alpha
  compare_images.py a.png b.png --gate rgba --tolerance 0
  compare_images.py a.png b.png --allow 0.1            # tolerate 0.1% of pixels differing
  compare_images.py a.png b.png --out diff.png         # write a heat map of where

Exits non-zero when the gate fails, so it can sit in a script.
"""

from __future__ import annotations

import argparse
import pathlib
import struct
import sys
import zlib

try:
    import numpy
except ImportError:
    numpy = None


PNG_MAGIC = b"\x89PNG\r\n\x1a\n"

# Channel count by PNG colour type. Palette and 16-bit are rejected rather than half supported -
# nothing in this project writes them, and guessing at one is how a comparison silently compares
# the wrong bytes.
CHANNELS = {0: 1, 2: 3, 4: 2, 6: 4}


class FormatError(Exception):
    pass


def read_png(path: pathlib.Path) -> tuple[int, int, int, bytearray]:
    """Returns (width, height, channels, 8-bit samples), row major."""
    data = path.read_bytes()

    if data[:8] != PNG_MAGIC:
        raise FormatError(f"{path} is not a PNG")

    position = 8
    compressed = bytearray()
    width = height = depth = colour = interlace = None

    while position + 8 <= len(data):
        length = int.from_bytes(data[position:position + 4], "big")
        kind = data[position + 4:position + 8]
        chunk = data[position + 8:position + 8 + length]
        position += 12 + length

        if kind == b"IHDR":
            width, height, depth, colour, _, _, interlace = struct.unpack(">IIBBBBB", chunk)
        elif kind == b"IDAT":
            compressed += chunk
        elif kind == b"IEND":
            break

    if width is None:
        raise FormatError(f"{path} has no header")
    if depth != 8:
        raise FormatError(f"{path} is {depth}-bit; only 8 is supported")
    if interlace:
        raise FormatError(f"{path} is interlaced; only progressive is supported")
    if colour not in CHANNELS:
        raise FormatError(f"{path} has colour type {colour}, which is not supported")

    channels = CHANNELS[colour]
    return width, height, channels, unfilter(zlib.decompress(bytes(compressed)),
                                            width, height, channels, path)


def unfilter(raw: bytes, width: int, height: int, channels: int,
             path: pathlib.Path) -> bytearray:
    """Undoes the per-row filter PNG applies before compressing."""
    stride = width * channels
    out = bytearray(height * stride)
    previous = bytearray(stride)
    read = 0

    for y in range(height):
        method = raw[read]
        read += 1
        line = bytearray(raw[read:read + stride])
        read += stride

        if method == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif method == 2:
            for i in range(stride):
                line[i] = (line[i] + previous[i]) & 0xFF
        elif method == 3:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((left + previous[i]) >> 1)) & 0xFF
        elif method == 4:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                up = previous[i]
                corner = previous[i - channels] if i >= channels else 0
                estimate = left + up - corner
                dl, du, dc = abs(estimate - left), abs(estimate - up), abs(estimate - corner)
                if dl <= du and dl <= dc:
                    line[i] = (line[i] + left) & 0xFF
                elif du <= dc:
                    line[i] = (line[i] + up) & 0xFF
                else:
                    line[i] = (line[i] + corner) & 0xFF
        elif method != 0:
            raise FormatError(f"{path} row {y} uses filter {method}, which does not exist")

        out[y * stride:(y + 1) * stride] = line
        previous = line

    return out


def write_png(path: pathlib.Path, width: int, height: int, pixels: bytearray) -> None:
    """Writes 8-bit RGB, unfiltered. Only the heat map goes through here."""
    def chunk(kind: bytes, payload: bytes) -> bytes:
        return (len(payload).to_bytes(4, "big") + kind + payload
                + zlib.crc32(kind + payload).to_bytes(4, "big"))

    stride = width * 3
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw += pixels[y * stride:(y + 1) * stride]

    path.write_bytes(
        PNG_MAGIC
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(bytes(raw), 6))
        + chunk(b"IEND", b""))


def widen(samples: bytearray, channels: int) -> bytearray:
    """Anything with fewer than four channels becomes RGBA, so one comparison covers every input."""
    if channels == 4:
        return samples

    count = len(samples) // channels
    out = bytearray(count * 4)

    for i in range(count):
        source = samples[i * channels:(i + 1) * channels]
        if channels == 1:
            out[i * 4:i * 4 + 4] = bytes((source[0], source[0], source[0], 255))
        elif channels == 2:
            out[i * 4:i * 4 + 4] = bytes((source[0], source[0], source[0], source[1]))
        else:
            out[i * 4:i * 4 + 4] = bytes((source[0], source[1], source[2], 255))

    return out


class FDelta:
    """Per-pixel differences, and where the worst of them are."""

    def __init__(self, a: bytearray, b: bytearray, width: int, height: int, tolerance: int):
        self.width = width
        self.height = height
        self.tolerance = tolerance

        if numpy is not None:
            self._measure_fast(a, b)
        else:
            self._measure(a, b)

    def _measure_fast(self, a: bytearray, b: bytearray) -> None:
        left = numpy.frombuffer(bytes(a), dtype=numpy.uint8).reshape(-1, 4).astype(numpy.int16)
        right = numpy.frombuffer(bytes(b), dtype=numpy.uint8).reshape(-1, 4).astype(numpy.int16)
        difference = numpy.abs(left - right)

        self.rgb = difference[:, :3].max(axis=1)
        self.alpha = difference[:, 3]
        self.max_rgb = int(self.rgb.max())
        self.max_alpha = int(self.alpha.max())
        self.mean_rgb = float(difference[:, :3].mean())
        self.mean_alpha = float(self.alpha.mean())

    def _measure(self, a: bytearray, b: bytearray) -> None:
        count = len(a) // 4
        rgb = bytearray(count)
        alpha = bytearray(count)
        total_rgb = 0
        total_alpha = 0

        for i in range(count):
            base = i * 4
            dr = abs(a[base] - b[base])
            dg = abs(a[base + 1] - b[base + 1])
            db = abs(a[base + 2] - b[base + 2])
            da = abs(a[base + 3] - b[base + 3])

            rgb[i] = max(dr, dg, db)
            alpha[i] = da
            total_rgb += dr + dg + db
            total_alpha += da

        self.rgb = rgb
        self.alpha = alpha
        self.max_rgb = max(rgb) if count else 0
        self.max_alpha = max(alpha) if count else 0
        self.mean_rgb = total_rgb / (count * 3) if count else 0.0
        self.mean_alpha = total_alpha / count if count else 0.0

    def channel(self, gate: str):
        if gate == "alpha":
            return self.alpha
        if gate == "rgb":
            return self.rgb
        return [max(r, a) for r, a in zip(self.rgb, self.alpha)] if numpy is None \
            else numpy.maximum(self.rgb, self.alpha)

    def over(self, gate: str) -> int:
        values = self.channel(gate)
        if numpy is not None:
            return int((values > self.tolerance).sum())
        return sum(1 for v in values if v > self.tolerance)

    def bounds(self, gate: str):
        """The box containing every differing pixel. Where is more use than how many."""
        values = self.channel(gate)

        if numpy is not None:
            mask = (values > self.tolerance).reshape(self.height, self.width)
            rows = numpy.flatnonzero(mask.any(axis=1))
            columns = numpy.flatnonzero(mask.any(axis=0))
            if not len(rows):
                return None
            return int(columns[0]), int(rows[0]), int(columns[-1]), int(rows[-1])

        left = top = None
        right = bottom = -1
        for y in range(self.height):
            for x in range(self.width):
                if values[y * self.width + x] > self.tolerance:
                    left = x if left is None else min(left, x)
                    right = max(right, x)
                    top = y if top is None else top
                    bottom = y

        return None if left is None else (left, top, right, bottom)


def heat_map(delta: FDelta, gate: str) -> bytearray:
    """Black where the two agree, rising through red to white where they do not."""
    values = delta.channel(gate)
    out = bytearray(delta.width * delta.height * 3)

    for i in range(delta.width * delta.height):
        v = int(values[i])
        out[i * 3] = min(255, v * 4)
        out[i * 3 + 1] = max(0, min(255, (v - 64) * 4))
        out[i * 3 + 2] = max(0, min(255, (v - 128) * 4))

    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("first", type=pathlib.Path)
    parser.add_argument("second", type=pathlib.Path)
    parser.add_argument("--gate", choices=["alpha", "rgb", "rgba"], default="alpha",
                        help="Which channels decide pass or fail. Default alpha, which is the only "
                             "one that means anything across renderers.")
    parser.add_argument("--tolerance", type=int, default=2,
                        help="Per-channel difference a pixel may have and still count as equal.")
    parser.add_argument("--allow", type=float, default=0.0,
                        help="Percentage of pixels allowed past the tolerance.")
    parser.add_argument("--out", type=pathlib.Path, help="Write a heat map of the differences.")
    arguments = parser.parse_args()

    try:
        width, height, channels, first = read_png(arguments.first)
        other_width, other_height, other_channels, second = read_png(arguments.second)
    except (FormatError, OSError, zlib.error) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    # Reported rather than resized. Two shots of different sizes are not a small difference, they are
    # a different question, and scaling one would invent pixels and then measure them.
    if (width, height) != (other_width, other_height):
        print(f"size differs: {arguments.first.name} is {width}x{height}, "
              f"{arguments.second.name} is {other_width}x{other_height}", file=sys.stderr)
        return 1

    delta = FDelta(widen(first, channels), widen(second, other_channels),
                   width, height, arguments.tolerance)

    total = width * height
    over = delta.over(arguments.gate)
    percent = 100.0 * over / total if total else 0.0

    print(f"{width}x{height}, gate={arguments.gate}, tolerance={arguments.tolerance}")
    print(f"  rgb    max {delta.max_rgb:3d}  mean {delta.mean_rgb:6.3f}")
    print(f"  alpha  max {delta.max_alpha:3d}  mean {delta.mean_alpha:6.3f}")
    print(f"  {over} of {total} pixels differ ({percent:.4f}%)")

    box = delta.bounds(arguments.gate)
    if box:
        print(f"  within ({box[0]}, {box[1]}) to ({box[2]}, {box[3]})")

    # A document with an opaque background has the same alpha everywhere, so gating on it compares
    # nothing and passes whatever it is given. That is worse than no check, because it reads as one.
    if arguments.gate == "alpha" and delta.max_alpha == 0 and delta.max_rgb > arguments.tolerance:
        print("warning: alpha is identical everywhere, so this gate compared nothing. The images do "
              "differ in colour. Gate on rgb or rgba, or compare shots on a transparent ground.",
              file=sys.stderr)

    if arguments.out:
        write_png(arguments.out, width, height, heat_map(delta, arguments.gate))
        print(f"  heat map -> {arguments.out}")

    if percent > arguments.allow:
        print(f"FAIL: {percent:.4f}% differ, {arguments.allow:.4f}% allowed", file=sys.stderr)
        return 1

    print("ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
