#!/usr/bin/env python3
"""Downscale a generated texture to a RA tile and quantise it to the real 6-bit palette.

Proves the last mile of the asset pipeline: AI image -> 24x24 indexed-colour tile
that the engine/OpenRA can actually consume.

Usage:
  tile_quantize.py <in.png> <temperat.pal> <out_prefix> [size]

Outputs:
  <prefix>_tile.png     the tile at target size (24x24 by default)
  <prefix>_zoom.png     nearest-neighbour 16x blow-up for eyeballing
  <prefix>_tiled.png    the tile repeated 8x8 to expose seams
  <prefix>_pal.png      palette swatch actually used
"""
import sys
import os
from PIL import Image


def load_ra_palette(path):
    """RA .pal = 768 bytes, 256 x RGB, each channel 0..63 (6-bit, multiply by 4)."""
    with open(path, "rb") as f:
        raw = f.read()
    if len(raw) < 768:
        raise ValueError(f"palette too small: {len(raw)}")
    return [(raw[i * 3] * 4, raw[i * 3 + 1] * 4, raw[i * 3 + 2] * 4) for i in range(256)]


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 1
    src_path, pal_path, out_prefix = sys.argv[1], sys.argv[2], sys.argv[3]
    size = int(sys.argv[4]) if len(sys.argv) > 4 else 24

    palette = load_ra_palette(pal_path)
    img = Image.open(src_path).convert("RGB")

    # Box filter averages detail down, which is what a tile wants; NEAREST would
    # alias badly at 24px.
    small = img.resize((size, size), Image.BOX)

    # Quantise against the real palette: nearest colour in RGB.
    # (A CIELAB distance would be better; RGB is what the 6-bit palette makes cheap.)
    cache = {}
    px = small.load()
    for y in range(size):
        for x in range(size):
            c = px[x, y]
            if c in cache:
                px[x, y] = cache[c]
                continue
            best, bestd = palette[0], 1 << 30
            for p in palette:
                d = (c[0] - p[0]) ** 2 + (c[1] - p[1]) ** 2 + (c[2] - p[2]) ** 2
                if d < bestd:
                    bestd, best = d, p
            cache[c] = best
            px[x, y] = best

    base = os.path.splitext(out_prefix)[0]
    small.save(base + "_tile.png")

    zoom = small.resize((size * 16, size * 16), Image.NEAREST)
    zoom.save(base + "_zoom.png")

    tiled = Image.new("RGB", (size * 8, size * 8))
    for ty in range(8):
        for tx in range(8):
            tiled.paste(small, (tx * size, ty * size))
    tiled.resize((size * 8 * 4, size * 8 * 4), Image.NEAREST).save(base + "_tiled.png")

    used = sorted(set(cache.values()))
    swatch = Image.new("RGB", (len(used) * 24, 24))
    for i, c in enumerate(used):
        for yy in range(24):
            for xx in range(24):
                swatch.putpixel((i * 24 + xx, yy), c)
    swatch.save(base + "_pal.png")

    print(f"tile {size}x{size} -> {base}_tile.png")
    print(f"unique colours after quantise: {len(used)}")
    print(f"input colours before: {len(img.getcolors(maxcolors=1 << 24) or [])}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
