#!/usr/bin/env python3
"""Compare two ways of getting a 24x24 RA tile out of a generated 512x512 texture.

  A) downscale the whole image to 24x24        (box filter; detail collapses to noise)
  B) crop native-resolution 24x24 patches and tile those  (keeps the pixel detail)

Usage: tile_crop_test.py <in.png> <temperat.pal> <out_prefix>
"""
import sys
from PIL import Image

TILE = 24


def load_ra_palette(path):
    with open(path, "rb") as f:
        raw = f.read()
    return [(raw[i * 3] * 4, raw[i * 3 + 1] * 4, raw[i * 3 + 2] * 4) for i in range(256)]


def quantise(img, palette):
    cache = {}
    px = img.load()
    w, h = img.size
    for y in range(h):
        for x in range(w):
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
    return img


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 1
    src, pal, out = sys.argv[1], sys.argv[2], sys.argv[3]
    palette = load_ra_palette(pal)
    img = Image.open(src).convert("RGB")
    W, H = img.size

    # A: whole-image downscale
    a = quantise(img.resize((TILE, TILE), Image.BOX), palette)
    a.resize((TILE * 12, TILE * 12), Image.NEAREST).save(out + "_A_downscale.png")

    # B: crop native patches from a grid, pick 4, tile them 4x4 each
    patches = []
    for gy in range(0, 4):
        for gx in range(0, 4):
            x0 = gx * (W // 4)
            y0 = gy * (H // 4)
            p = img.crop((x0, y0, x0 + TILE, y0 + TILE))
            patches.append(quantise(p, palette))

    sheet = Image.new("RGB", (TILE * 4, TILE * 4 * len(patches[:4])))
    for i, p in enumerate(patches[:4]):
        for ty in range(4):
            for tx in range(4):
                sheet.paste(p, (tx * TILE, i * TILE * 4 + ty * TILE))
    sheet.resize((sheet.width * 4, sheet.height * 4), Image.NEAREST).save(out + "_B_crop.png")

    # colour-count report
    print(f"A downscale: {len(set(a.getcolors(maxcolors=1 << 24) or []))} unique colours")
    for i, p in enumerate(patches[:4]):
        print(f"B patch{i}: {len(set(p.getcolors(maxcolors=1 << 24) or []))} unique colours")
    print("wrote", out + "_A_downscale.png", "and", out + "_B_crop.png")
    return 0


if __name__ == "__main__":
    sys.exit(main())
