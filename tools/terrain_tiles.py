#!/usr/bin/env python3
"""Cut RA-sized terrain tiles out of a large generated texture (crop, not downscale).

Downscaling a 512x512 texture to 24x24 collapses 100k+ colours into a handful and
produces noise - measured earlier, and the whole reason this script exists. Native
24x24 crops keep the pixel detail, so that is what we do.

Usage:
  terrain_tiles.py <in.png> <temperat.pal> <out_prefix> [--tile 24] [--count 4] [--grid 4]

Outputs:
  <prefix>_tiles.png   contact sheet: each variant, zoomed 8x
  <prefix>_tileN.png   individual 24x24 tiles (quantised, opaque)
  <prefix>_tiled.png   the best tile repeated 10x10, zoomed 2x, to judge seams
"""
import sys
from PIL import Image


def load_ra_palette(path):
    with open(path, "rb") as f:
        raw = f.read()
    return [(raw[i * 3] * 4, raw[i * 3 + 1] * 4, raw[i * 3 + 2] * 4) for i in range(256)]


def quantise(img, palette):
    px = img.load()
    w, h = img.size
    cache = {}
    for y in range(h):
        for x in range(w):
            c = px[x, y]
            q = cache.get(c)
            if q is None:
                best, bestd = palette[0], 1 << 30
                for p in palette:
                    d = (c[0] - p[0]) ** 2 + (c[1] - p[1]) ** 2 + (c[2] - p[2]) ** 2
                    if d < bestd:
                        bestd, best = d, p
                q = best
                cache[c] = q
            px[x, y] = q
    return img


def variance(img):
    """Cheap spread metric used to prefer tiles that actually have texture in them."""
    px = img.load()
    w, h = img.size
    vals = []
    for y in range(0, h, 4):
        for x in range(0, w, 4):
            r, g, b = px[x, y]
            vals.append(r + g + b)
    if not vals:
        return 0.0
    m = sum(vals) / len(vals)
    return sum((v - m) ** 2 for v in vals) / len(vals)


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 1
    src, pal_path, prefix = sys.argv[1], sys.argv[2], sys.argv[3]
    tile = 24
    count = 4
    grid = 4
    brightness = 1.0
    saturation = 1.0
    centre_only = False
    flatten = False
    i = 4
    while i < len(sys.argv):
        if sys.argv[i] == "--tile" and i + 1 < len(sys.argv):
            tile = int(sys.argv[i + 1]); i += 2
        elif sys.argv[i] == "--count" and i + 1 < len(sys.argv):
            count = int(sys.argv[i + 1]); i += 2
        elif sys.argv[i] == "--grid" and i + 1 < len(sys.argv):
            grid = int(sys.argv[i + 1]); i += 2
        elif sys.argv[i] == "--brightness" and i + 1 < len(sys.argv):
            brightness = float(sys.argv[i + 1]); i += 2
        elif sys.argv[i] == "--flatten":
            flatten = True; i += 1
        elif sys.argv[i] == "--centre-only":
            centre_only = True; i += 1
        elif sys.argv[i] == "--saturation" and i + 1 < len(sys.argv):
            saturation = float(sys.argv[i + 1]); i += 2
        else:
            i += 1

    palette = load_ra_palette(pal_path)
    img = Image.open(src).convert("RGB")
    W, H = img.size

    # AA's terrain is much darker and less saturated than typical diffusion output;
    # without this the quantised grass comes out fluorescent green.
    if brightness != 1.0 or saturation != 1.0:
        from PIL import ImageEnhance
        if saturation != 1.0:
            img = ImageEnhance.Color(img).enhance(saturation)
        if brightness != 1.0:
            img = ImageEnhance.Brightness(img).enhance(brightness)

    # Diffusion output is not actually seamless: it carries large-scale colour gradients
    # (one corner darker/greener than the other), so crops taken from different spots
    # disagree in brightness and the map shows blotches. Subtract the low-frequency
    # component and add a flat mid-grey back - keeps the high-frequency texture, kills
    # the gradient. This is the alternative to asking for "seamless tileable", which
    # reliably makes SD1.5 draw a faint grid instead.
    if flatten:
        from PIL import ImageFilter, ImageChops, ImageStat
        radius = max(8, min(W, H) // 2)
        low = img.filter(ImageFilter.GaussianBlur(radius))
        # Add back the image's own mean colour, not a fixed grey: subtracting the
        # low-frequency layer removes the gradient but would also strip the hue if we
        # added grey back, turning grass into grey mush (seen in the first attempt).
        mean = ImageStat.Stat(img).mean
        base = Image.new("RGB", (W, H), (int(mean[0]), int(mean[1]), int(mean[2])))
        img = ImageChops.add(ImageChops.subtract(img, low), base)

    # Sample a grid of native-resolution crops. Using the full image as a grid keeps
    # the variants spread out instead of coming from one corner.
    # centre_only restricts sampling to the middle band: diffusion "seamless" textures
    # tend to carry a faint grid or border artifact near the edges, and cropping from
    # the middle avoids baking that into every tile.
    margin = 0.25 if centre_only else 0.0
    x_lo = int(W * margin)
    x_hi = int(W * (1.0 - margin) - tile)
    y_lo = int(H * margin)
    y_hi = int(H * (1.0 - margin) - tile)
    if x_hi < x_lo:
        x_lo, x_hi = 0, max(0, W - tile)
    if y_hi < y_lo:
        y_lo, y_hi = 0, max(0, H - tile)
    candidates = []
    for gy in range(grid):
        for gx in range(grid):
            x0 = x_lo + int(gx * (x_hi - x_lo) / max(1, grid - 1)) if grid > 1 else x_lo
            y0 = y_lo + int(gy * (y_hi - y_lo) / max(1, grid - 1)) if grid > 1 else y_lo
            c = img.crop((x0, y0, x0 + tile, y0 + tile))
            c = quantise(c.convert("RGB"), palette)
            candidates.append((variance(c), c))

    # Highest variance first: flat crops make useless terrain.
    candidates.sort(key=lambda t: -t[0])
    picked = [c for _, c in candidates[:count]]

    # Contact sheet, each tile blown up 8x
    pad = 8
    cw = tile * 8
    sheet = Image.new("RGB", (count * (cw + pad) + pad, cw + 2 * pad), (40, 40, 44))
    for idx, t in enumerate(picked):
        sheet.paste(t.resize((cw, cw), Image.NEAREST), (pad + idx * (cw + pad), pad))
    sheet.save(prefix + "_tiles.png")

    for idx, t in enumerate(picked):
        t.save(f"{prefix}_tile{idx}.png")

    # Seam check on the first pick
    reps = 10
    tiled = Image.new("RGB", (tile * reps, tile * reps))
    for ty in range(reps):
        for tx in range(reps):
            tiled.paste(picked[0], (tx * tile, ty * tile))
    tiled.resize((tile * reps * 2, tile * reps * 2), Image.NEAREST).save(prefix + "_tiled.png")

    print(f"{count} tiles of {tile}x{tile} from {W}x{H}")
    for idx, t in enumerate(picked):
        uniq = len(t.getcolors(maxcolors=1 << 24) or [])
        print(f"  tile{idx}: {uniq} colours")
    print(f"wrote {prefix}_tiles.png / _tileN.png / _tiled.png")
    return 0


if __name__ == "__main__":
    sys.exit(main())
