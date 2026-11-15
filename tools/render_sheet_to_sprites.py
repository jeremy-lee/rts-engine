#!/usr/bin/env python3
"""Convert a whole facing sheet to sprites using ONE shared crop and scale.

Why this exists: render_to_sprite.py handles a single image, cropping it to its own bbox
and scaling it to fill the target box. Applied per-frame that is wrong, because an
isometric render projects to a DIFFERENT bounding box at every facing - so each facing
gets a different scale and the unit visibly changes size as it turns (user report:
"坦克在各个方向 有的大有的小").

OpenRA's SHP semantics are one shared canvas for all frames (ShpTDLoader: a single
u16 width/height header applies to every frame; TrimmedFrame derives one bbox and one
offset for the file). We reproduce that properly:
  1. union the opaque bounds of all facings
  2. derive ONE scale from that union
  3. place every facing with that same scale, preserving its position in the union box

Usage:
  render_sheet_to_sprites.py <dir> <prefix> <temperat.pal> <out_dir>
                             [--size 24x32] [--facings 32] [--outline 0.45]
"""
import glob
import os
import sys

import numpy as np
from PIL import Image


def load_pal(path):
    with open(path, "rb") as f:
        raw = f.read()
    return np.array([[raw[i * 3] * 4, raw[i * 3 + 1] * 4, raw[i * 3 + 2] * 4] for i in range(256)], dtype=np.int32)


def quantise(rgba, pal):
    a = np.asarray(rgba).copy()
    rgb = a[:, :, :3].astype(np.int32)
    mask = a[:, :, 3] > 8
    if not mask.any():
        return Image.fromarray(a, "RGBA")
    flat = rgb[mask]
    uniq, inverse = np.unique(flat.reshape(-1, 3), axis=0, return_inverse=True)
    d = ((uniq[:, None, :] - pal[None, :, :]) ** 2).sum(axis=2)
    rgb[mask] = pal[d.argmin(axis=1)][inverse.reshape(-1)]
    a[:, :, :3] = rgb.astype(np.uint8)
    return Image.fromarray(a, "RGBA")


def add_outline(rgba, strength):
    a = np.asarray(rgba).copy()
    alpha = a[:, :, 3]
    inner = alpha > 8
    edge = np.zeros_like(inner)
    for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        edge |= inner & ~np.roll(inner, (dy, dx), axis=(0, 1))
    rgb = a[:, :, :3].astype(np.float32)
    rgb[edge] *= (1.0 - strength)
    a[:, :, :3] = np.clip(rgb, 0, 255).astype(np.uint8)
    return Image.fromarray(a, "RGBA")


def main():
    if len(sys.argv) < 5:
        print(__doc__)
        return 1
    src_dir, prefix, pal_path, out_dir = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
    size = (24, 32)
    facings = 32
    outline = 0.45
    i = 5
    while i < len(sys.argv):
        if sys.argv[i] == "--size" and i + 1 < len(sys.argv):
            a, b = sys.argv[i + 1].lower().split("x")
            size = (int(a), int(b)); i += 2
        elif sys.argv[i] == "--facings" and i + 1 < len(sys.argv):
            facings = int(sys.argv[i + 1]); i += 2
        elif sys.argv[i] == "--outline" and i + 1 < len(sys.argv):
            outline = float(sys.argv[i + 1]); i += 2
        else:
            i += 1

    pal = load_pal(pal_path)
    os.makedirs(out_dir, exist_ok=True)

    frames = []
    for f in range(facings):
        p = os.path.join(src_dir, f"{prefix}_f{f:02d}.png")
        if not os.path.exists(p):
            print("missing", p)
            return 1
        frames.append(Image.open(p).convert("RGBA"))

    # ---- one union bbox for the whole sheet (this is the SHP shared canvas) ----
    union = None
    for im in frames:
        bb = im.getbbox()
        if bb is None:
            continue
        if union is None:
            union = list(bb)
        else:
            union[0] = min(union[0], bb[0])
            union[1] = min(union[1], bb[1])
            union[2] = max(union[2], bb[2])
            union[3] = max(union[3], bb[3])
    if union is None:
        print("no opaque pixels")
        return 1

    uw = union[2] - union[0]
    uh = union[3] - union[1]
    tw, th = size
    # ONE scale for every facing
    scale = min(tw / uw, th / uh)
    nw, nh = max(1, int(round(uw * scale))), max(1, int(round(uh * scale)))

    print(f"union bbox {uw}x{uh} -> {nw}x{nh} in {tw}x{th} (scale {scale:.4f})")

    wrote = 0
    for f, im in enumerate(frames):
        # crop every frame with the SAME union box, so relative size and position survive
        piece = im.crop((union[0], union[1], union[2], union[3]))
        if piece.size != (nw, nh):
            piece = piece.resize((nw, nh), Image.LANCZOS)
        canvas = Image.new("RGBA", (tw, th), (0, 0, 0, 0))
        canvas.paste(piece, ((tw - nw) // 2, (th - nh) // 2), piece)
        canvas = quantise(canvas, pal)
        canvas = add_outline(canvas, outline)
        out = os.path.join(out_dir, f"{prefix}_{f:02d}.png")
        canvas.save(out)
        wrote += 1

    print(f"wrote {wrote} facings to {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
