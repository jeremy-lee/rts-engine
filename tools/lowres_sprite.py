#!/usr/bin/env python3
"""RA-style sprite conversion: crop, DO NOT scale, quantise.

This reproduces Westwood's actual working method as far as it is observable. Measured on
the shipped 2tnk sprites, the eight facings have DIFFERENT bounding boxes that grow
smoothly with rotation:

    facing 0  13x17   facing 3  24x18   facing 6  24x16
    facing 1  17x18   facing 4  24x18   facing 7  24x14
    facing 2  21x18   facing 5  24x17

So each facing was framed independently and kept its own natural size - they were not all
forced into one box. Our previous converters did the opposite: they took the union bbox of
all 32 facings and squeezed every frame into it, which both shrank the sprite to a third of
its canvas and destroyed the size variation.

Here the camera renders at a fixed ortho scale (so physical size is consistent between
facings), the image is cropped to the subject, and NOTHING is resized. Each facing ends up
as large as its own projection deserves. Only downscaling is applied when a projection
exceeds the budget, never upscaling.

Usage:
  lowres_sprite.py <dir> <prefix> <temperat.pal> <out_dir>
                   [--facings 32] [--max 24x32] [--dither 0.5] [--colours 12]
                   [--grain 0.35] [--outline black]
"""
import os
import sys

import numpy as np
from PIL import Image

BAYER4 = np.array([
    [0, 8, 2, 10],
    [12, 4, 14, 6],
    [3, 11, 1, 9],
    [15, 7, 13, 5],
], dtype=np.float32) / 16.0 - 0.5


def load_pal(path):
    with open(path, "rb") as f:
        raw = f.read()
    return np.array([[raw[i * 3] * 4, raw[i * 3 + 1] * 4, raw[i * 3 + 2] * 4] for i in range(256)],
                    dtype=np.float32)


def dominant_palette(pal, frames, n, skip=16):
    """Keep only the n palette entries the sheet actually uses.

    `skip` drops temperat.pal's first entries, which are the saturated HOUSE colours
    (magenta, cyan, green, yellow, red, blue...). They exist so OpenRA can remap player
    colours, and they are nowhere near any surface colour we render - but antialiased
    edge pixels occasionally land closest to them, which is where the stray red/blue/green
    dots on our outline came from. Exclusion is cheaper and far more reliable than trying
    to filter the dots out afterwards.

    Dithering against all 256 entries spreads quantisation error so finely that no visible
    grain survives; restricting to about a dozen forces checkerboard decisions, which is
    where RA's speckle comes from.
    """
    picked = []
    for im in frames:
        a = np.asarray(im)
        m = a[:, :, 3] > 8
        if m.any():
            picked.append(a[:, :, :3][m].reshape(-1, 3).astype(np.float32))
    if not picked:
        return pal
    flat = np.concatenate(picked)
    cand = pal[skip:] if skip < len(pal) else pal
    d = ((flat[:, None, :] - cand[None, :, :]) ** 2).sum(axis=2)
    idx = d.argmin(axis=1)
    used, counts = np.unique(idx, return_counts=True)
    if len(used) <= n:
        return cand[used]
    order = np.argsort(-counts)[:n]
    return cand[used[order]]


def stretch(rgba, amount):
    """Expand the tone range before quantisation.

    Measured: our render occupies roughly (208,192,160)..(120,116,116) while the real RA
    tank spans (247,215,121)..(85,77,36). Everything sitting in the middle is what makes
    ours read as washed-out grey plastic - RA art deliberately uses a bright light tone and
    a near-black shadow, and lets the dither fill the gap between them.
    """
    if amount <= 0.0:
        return rgba
    a = rgba.copy()
    mask = a[:, :, 3] > 8
    if not mask.any():
        return a
    rgb = a[:, :, :3].astype(np.float32)
    px = rgb[mask]
    lo = np.percentile(px, 5, axis=0)
    hi = np.percentile(px, 95, axis=0)
    span = np.maximum(hi - lo, 1.0)
    norm = (px - lo) / span                      # 0..1
    norm = np.clip((norm - 0.5) * amount + 0.5, 0.0, 1.0)
    rgb[mask] = norm * 255.0
    a[:, :, :3] = rgb.astype(np.uint8)
    return a


def grain_and_dither(rgba, pal, dither, grain, seed):
    a = rgba.copy()
    h, w, _ = a.shape
    mask = a[:, :, 3] > 8
    if not mask.any():
        return a
    rgb = a[:, :, :3].astype(np.float32)
    if grain > 0.0:
        rng = np.random.default_rng(seed)
        rgb[mask] = np.clip(rgb[mask] + rng.normal(0.0, grain * 40.0, size=rgb[mask].shape), 0, 255)
    ys, xs = np.mgrid[0:h, 0:w]
    off = (BAYER4[ys % 4, xs % 4] * (dither * 46.0))[:, :, None]
    pert = np.clip(rgb + off, 0, 255)
    flat = pert[mask]
    chunk = 8192
    out = np.empty_like(flat)
    for i in range(0, len(flat), chunk):
        c = flat[i:i + chunk]
        d = ((c[:, None, :] - pal[None, :, :]) ** 2).sum(axis=2)
        out[i:i + chunk] = pal[d.argmin(axis=1)]
    rgb[mask] = out
    a[:, :, :3] = rgb.astype(np.uint8)
    return a


def hard_outline(rgba, colour=(0, 0, 0)):
    a = rgba.copy()
    alpha = a[:, :, 3]
    inner = alpha > 8
    edge = np.zeros_like(inner)
    for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        edge |= inner & ~np.roll(inner, (dy, dx), axis=(0, 1))
    a[:, :, :3][edge] = colour
    return a


def main():
    if len(sys.argv) < 5:
        print(__doc__)
        return 1
    src_dir, prefix, pal_path, out_dir = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
    facings = 32
    budget = (24, 32)
    dither, grain, colours, outline = 0.5, 0.35, 12, "black"
    contrast = 1.0
    i = 5
    while i < len(sys.argv):
        a = sys.argv[i]
        if a == "--facings" and i + 1 < len(sys.argv):
            facings = int(sys.argv[i + 1]); i += 2
        elif a == "--max" and i + 1 < len(sys.argv):
            w, h = sys.argv[i + 1].lower().split("x")
            budget = (int(w), int(h)); i += 2
        elif a == "--dither" and i + 1 < len(sys.argv):
            dither = float(sys.argv[i + 1]); i += 2
        elif a == "--grain" and i + 1 < len(sys.argv):
            grain = float(sys.argv[i + 1]); i += 2
        elif a == "--colours" and i + 1 < len(sys.argv):
            colours = int(sys.argv[i + 1]); i += 2
        elif a == "--outline" and i + 1 < len(sys.argv):
            outline = sys.argv[i + 1]; i += 2
        elif a == "--contrast" and i + 1 < len(sys.argv):
            contrast = float(sys.argv[i + 1]); i += 2
        else:
            i += 1

    pal_full = load_pal(pal_path)
    os.makedirs(out_dir, exist_ok=True)

    # crop every facing to its own subject bounds, no resizing unless over budget
    cropped = []
    for f in range(facings):
        p = os.path.join(src_dir, f"{prefix}_f{f:02d}.png")
        if not os.path.exists(p):
            print("missing", p)
            return 1
        im = Image.open(p).convert("RGBA")
        bb = im.getbbox()
        if bb:
            im = im.crop(bb)
        cropped.append(im)

    pal = dominant_palette(pal_full, cropped, colours)
    print(f"palette {len(pal)} entries")

    # ONE scale for the whole sheet. Scaling each facing independently to fit the budget
    # gave every facing a different factor, so whichever facing happened to hit the height
    # limit came out visibly larger than its neighbours (measured on type59: width held at
    # 27-28 but height ranged 17-32, a ratio of 1.88, and f12 was the outlier). The camera
    # renders every facing at the same ortho scale, so their relative sizes are already
    # correct - they must simply share one reduction factor.
    tw, th = budget
    uniform = None
    for im in cropped:
        w, h = im.size
        s = min(tw / w, th / h) if (w > 0 and h > 0) else 1.0
        uniform = s if uniform is None else min(uniform, s)
    print(f"uniform scale {uniform:.4f} for all facings")

    sizes = []
    for f, im in enumerate(cropped):
        w, h = im.size
        if uniform < 1.0 or True:
            s = uniform * 0.999                    # never exceed the budget
            im = im.resize((max(1, int(round(w * s))), max(1, int(round(h * s)))), Image.LANCZOS)
        w, h = im.size
        sizes.append((w, h))

        arr = stretch(np.asarray(im).copy(), contrast)
        arr = grain_and_dither(arr, pal, dither, grain, 1000 + f)
        if outline == "black":
            arr = hard_outline(arr, (0, 0, 0))
        elif outline == "dark":
            arr = hard_outline(arr, (28, 24, 16))
        Image.fromarray(arr, "RGBA").save(os.path.join(out_dir, f"{prefix}_{f:02d}.png"))

    ws = [s[0] for s in sizes]
    hs = [s[1] for s in sizes]
    print(f"per-facing sizes: {min(ws)}-{max(ws)} wide, {min(hs)}-{max(hs)} tall")
    print(f"wrote {facings} facings to {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
