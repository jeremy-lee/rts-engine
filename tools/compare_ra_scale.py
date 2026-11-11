#!/usr/bin/env python3
"""Compare our renders against the real RA sprites at RA's ACTUAL pixel sizes.

Measured from the shipped assets (trimmed bbox):
  medium tank  13x17     light tank 11x15     infantry 14x17
  harvester    26x25     barracks   48x39     factory  72x24

The point of this file is to answer one question with evidence: at 13x17, does our
procedural geometry still say "tank"? If it does, procedural is the right path and
high-poly models are pointless. If it turns into mush, the geometry is too detailed
and needs simplifying for that scale.

Usage: compare_ra_scale.py <render.png> <ra_sprite.png> <out.png> [--size 14x18]
"""
import sys

import numpy as np
from PIL import Image, ImageDraw


def fit(img, size):
    """Fit into `size` preserving aspect, centred, transparent background."""
    img = img.convert("RGBA")
    bb = img.getbbox()
    if bb:
        img = img.crop(bb)
    w, h = img.size
    tw, th = size
    sc = min(tw / w, th / h)
    nw, nh = max(1, int(round(w * sc))), max(1, int(round(h * sc)))
    small = img.resize((nw, nh), Image.LANCZOS)
    canvas = Image.new("RGBA", (tw, th), (0, 0, 0, 0))
    canvas.paste(small, ((tw - nw) // 2, (th - nh) // 2), small)
    return canvas


def knockout(img, tol=38):
    a = np.asarray(img.convert("RGB"), dtype=np.int32)
    h, w, _ = a.shape
    corners = np.array([a[0, 0], a[0, w - 1], a[h - 1, 0], a[h - 1, w - 1]])
    bg = np.median(corners, axis=0)
    d = np.sqrt(((a - bg) ** 2).sum(axis=2))
    alpha = np.where(d <= tol * 0.75, 0, 255).astype(np.uint8)
    return Image.fromarray(np.dstack([np.asarray(img.convert("RGB")), alpha]), "RGBA")


def posterise(img, levels=5):
    """Snap each channel to N levels - kills the smooth shading gradient that makes our
    renders turn to mush at 14px. This is what 'hand-drawn pixel art' really means at
    this scale: a handful of flat tones, not a continuous ramp."""
    a = np.asarray(img).astype(np.float32)
    rgb = a[:, :, :3]
    q = 255.0 / (levels - 1)
    a[:, :, :3] = np.clip(np.round(rgb / q) * q, 0, 255)
    return Image.fromarray(a.astype(np.uint8), "RGBA")


def add_outline(img, strength=0.85):
    """1px dark edge on the silhouette. RA sprites have it; without it a 14px shape
    dissolves into whatever is behind it."""
    a = np.asarray(img).copy()
    alpha = a[:, :, 3]
    inner = alpha > 8
    edge = np.zeros_like(inner)
    for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        edge |= inner & ~np.roll(inner, (dy, dx), axis=(0, 1))
    rgb = a[:, :, :3].astype(np.float32)
    rgb[edge] *= (1.0 - strength)
    a[:, :, :3] = np.clip(rgb, 0, 255).astype(np.uint8)
    return Image.fromarray(a, "RGBA")


def boost_contrast(img, factor=1.45):
    a = np.asarray(img).astype(np.float32)
    mask = a[:, :, 3] > 8
    rgb = a[:, :, :3]
    mean = rgb[mask].mean() if mask.any() else 128.0
    rgb[mask] = np.clip((rgb[mask] - mean) * factor + mean, 0, 255)
    a[:, :, :3] = rgb
    return Image.fromarray(a.astype(np.uint8), "RGBA")


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 1
    ours_path, ra_path, out = sys.argv[1], sys.argv[2], sys.argv[3]
    size = (14, 18)
    levels = 5
    for i, a in enumerate(sys.argv):
        if a == "--size" and i + 1 < len(sys.argv):
            w, h = sys.argv[i + 1].lower().split("x")
            size = (int(w), int(h))
        if a == "--levels" and i + 1 < len(sys.argv):
            levels = int(sys.argv[i + 1])

    ours = Image.open(ours_path)
    if ours.getpixel((0, 0))[3] > 200 and ours.convert("RGB").getpixel((0, 0)) != (0, 0, 0):
        ours = knockout(ours)                 # opaque render -> transparent
    ours_small = fit(ours, size)
    ours_small = boost_contrast(ours_small, 1.45)
    ours_small = posterise(ours_small, levels)
    ours_small = add_outline(ours_small, 0.85)

    ra = Image.open(ra_path).convert("RGBA")
    bb = ra.getbbox()
    if bb:
        ra = ra.crop(bb)

    zoom = 22
    pad = 16
    cw, ch = size[0] * zoom, size[1] * zoom
    rw, rh = ra.width * zoom, ra.height * zoom
    W = pad + cw + pad + rw + pad
    H = pad + max(ch, rh) + pad + 40

    canvas = Image.new("RGB", (W, H), (48, 52, 48))
    d = ImageDraw.Draw(canvas)

    def place(im, x, y):
        # checker so transparency is visible
        for cy in range(y, y + im.height, 8):
            for cx in range(x, x + im.width, 8):
                col = (60, 64, 60) if ((cx - x) // 8 + (cy - y) // 8) % 2 == 0 else (72, 76, 72)
                d.rectangle([cx, cy, cx + 7, cy + 7], fill=col)
        canvas.paste(im.convert("RGB"), (x, y), im)

    place(ours_small.resize((cw, ch), Image.NEAREST), pad, pad + 24)
    place(ra.resize((rw, rh), Image.NEAREST), pad + cw + pad, pad + 24)

    d.text((pad, 6), f"ours {size[0]}x{size[1]}", fill=(230, 230, 230))
    d.text((pad + cw + pad, 6), f"RA original {ra.width}x{ra.height}", fill=(230, 230, 230))
    canvas.save(out)

    uniq = len(set(ours_small.getcolors(maxcolors=1 << 24) or []))
    print(f"ours {size[0]}x{size[1]}  distinct colours={uniq}")
    print(f"RA   {ra.width}x{ra.height}  distinct colours={len(set(ra.getcolors(maxcolors=1 << 24) or []))}")
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
