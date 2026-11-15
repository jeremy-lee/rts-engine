#!/usr/bin/env python3
"""Quantise a render to RA's look: few colours, ordered dither, pure-black outline.

Measured on the real RA tank sprite (2tnk-0000.png) versus our render:
                    RA      ours
  distinct colours  13      35
  luma minimum      0       27     <- RA has a hard black outline, we do not
  neighbour change  65%     50%    <- RA is visibly busier / dithered
  luma std          68      61

"粗犷的颗粒感" is those three properties. A smooth render reduced with plain nearest-
colour quantisation is too clean and too colourful, which is why it kept reading as
plastic rather than as RA art. This tool fixes it at the conversion step:

  1. ordered (Bayer 4x4) dithering when snapping to the palette - this is what creates
     the checkerboard grain instead of flat bands
  2. a hard black silhouette outline (RA's sprites all hit luma 0)
  3. an optional colour-count cap so the result stays within a handful of tones

Usage:
  gritty_quantize.py <dir> <prefix> <temperat.pal> <out_dir>
                     [--size 24x32] [--facings 32]
                     [--dither 0.55] [--colours 12] [--outline black|dark|none]
"""
import glob
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
    return np.array([[raw[i * 3] * 4, raw[i * 3 + 1] * 4, raw[i * 3 + 2] * 4] for i in range(256)], dtype=np.float32)


def reduce_palette(pal, rgba, n):
    """Keep the n palette entries that the opaque pixels actually need most.

    Dithering against 256 entries spreads the error so thin that the grain disappears.
    Restricting to a dozen entries forces visible checkerboard decisions.
    """
    mask = rgba[:, :, 3] > 8
    flat = rgba[:, :, :3][mask].reshape(-1, 3)
    if len(flat) == 0:
        return pal
    # histogram in palette space
    d = ((flat[:, None, :] - pal[None, :, :]) ** 2).sum(axis=2)
    idx = d.argmin(axis=1)
    used, counts = np.unique(idx, return_counts=True)
    if len(used) <= n:
        return pal[used]
    order = np.argsort(-counts)[:n]
    return pal[used[order]]


def dither_quantise(rgba, pal, strength):
    """Ordered dither via per-pixel threshold offset before nearest-colour search."""
    h, w, _ = rgba.shape
    out = rgba.copy()
    mask = rgba[:, :, 3] > 8
    if not mask.any():
        return out

    # tile Bayer matrix over the image and map to a +-offset in colour space
    ys, xs = np.mgrid[0:h, 0:w]
    thr = BAYER4[ys % 4, xs % 4]
    offset = (thr * (strength * 46.0))[:, :, None]      # 46 = rough palette step

    rgb = rgba[:, :, :3].astype(np.float32)
    perturbed = np.clip(rgb + offset, 0, 255)

    flat = perturbed[mask]
    chunk = 8192
    result = np.empty_like(flat)
    for i in range(0, len(flat), chunk):
        c = flat[i:i + chunk]
        d = ((c[:, None, :] - pal[None, :, :]) ** 2).sum(axis=2)
        result[i:i + chunk] = pal[d.argmin(axis=1)]
    out[:, :, :3][mask] = result.astype(np.uint8)
    return out


def add_grain(rgba, amount, seed=12345):
    """Deterministic per-pixel noise, applied before quantisation.

    Why: ordered dither only creates visible grain where the colour varies. Our Blender
    faces are flat fills, so there is nothing to dither and the result stays smooth. RA
    art instead has TEXTURED fills - the dither pattern is baked into the surface. Adding
    noise first gives the quantiser something to decide on, which is what turns a flat
    panel into a speckled one.
    """
    if amount <= 0.0:
        return rgba
    a = rgba.copy()
    h, w, _ = a.shape
    rng = np.random.default_rng(seed)
    n = rng.normal(0.0, amount * 40.0, size=(h, w, 1))
    mask = a[:, :, 3] > 8
    rgb = a[:, :, :3].astype(np.float32)
    rgb[mask] = np.clip(rgb[mask] + n[mask], 0, 255)
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
    size = (24, 32)
    facings = 32
    dither = 0.55
    colours = 12
    grain = 0.0
    outline = "black"
    i = 5
    while i < len(sys.argv):
        a = sys.argv[i]
        if a == "--size" and i + 1 < len(sys.argv):
            w, h = sys.argv[i + 1].lower().split("x")
            size = (int(w), int(h)); i += 2
        elif a == "--facings" and i + 1 < len(sys.argv):
            facings = int(sys.argv[i + 1]); i += 2
        elif a == "--dither" and i + 1 < len(sys.argv):
            dither = float(sys.argv[i + 1]); i += 2
        elif a == "--colours" and i + 1 < len(sys.argv):
            colours = int(sys.argv[i + 1]); i += 2
        elif a == "--outline" and i + 1 < len(sys.argv):
            outline = sys.argv[i + 1]; i += 2
        elif a == "--grain" and i + 1 < len(sys.argv):
            grain = float(sys.argv[i + 1]); i += 2
        else:
            i += 1

    pal = load_pal(pal_path)
    os.makedirs(out_dir, exist_ok=True)

    # union bbox over the whole sheet (same rule as render_sheet_to_sprites: SHP shared canvas)
    frames = []
    union = None
    for f in range(facings):
        p = os.path.join(src_dir, f"{prefix}_f{f:02d}.png")
        if not os.path.exists(p):
            print("missing", p)
            return 1
        im = Image.open(p).convert("RGBA")
        frames.append(im)
        bb = im.getbbox()
        if bb is not None:
            union = list(bb) if union is None else [
                min(union[0], bb[0]), min(union[1], bb[1]),
                max(union[2], bb[2]), max(union[3], bb[3])]

    uw, uh = union[2] - union[0], union[3] - union[1]
    tw, th = size
    sc = min(tw / uw, th / uh)
    nw, nh = max(1, int(round(uw * sc))), max(1, int(round(uh * sc)))

    # one palette for the whole sheet, derived from the first frame is too narrow;
    # union all frames then keep the dominant entries
    stacked = np.zeros((nh, nw, 4), dtype=np.uint8)
    for f, im in enumerate(frames):
        piece = im.crop((union[0], union[1], union[2], union[3]))
        if piece.size != (nw, nh):
            piece = piece.resize((nw, nh), Image.LANCZOS)
        canvas = Image.new("RGBA", (tw, th), (0, 0, 0, 0))
        canvas.paste(piece, ((tw - nw) // 2, (th - nh) // 2), piece)
        arr = np.asarray(canvas)
        if f == 0:
            stacked = arr.copy()
        else:
            m = arr[:, :, 3] > 8
            stacked[m] = arr[m]

    small = reduce_palette(pal, stacked, colours)
    print(f"palette {len(small)} entries, dither {dither}, outline {outline}")

    for f, im in enumerate(frames):
        piece = im.crop((union[0], union[1], union[2], union[3]))
        if piece.size != (nw, nh):
            piece = piece.resize((nw, nh), Image.LANCZOS)
        canvas = Image.new("RGBA", (tw, th), (0, 0, 0, 0))
        canvas.paste(piece, ((tw - nw) // 2, (th - nh) // 2), piece)
        arr = np.asarray(canvas).copy()
        arr = add_grain(arr, grain)
        arr = dither_quantise(arr, small, dither)
        if outline == "black":
            arr = hard_outline(arr, (0, 0, 0))
        elif outline == "dark":
            arr = hard_outline(arr, (28, 24, 16))
        Image.fromarray(arr, "RGBA").save(os.path.join(out_dir, f"{prefix}_{f:02d}.png"))

    print(f"wrote {facings} facings to {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
