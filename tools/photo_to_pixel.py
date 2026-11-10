#!/usr/bin/env python3
"""Try to turn a photorealistic render into RA-style pixel art, and show honestly how
far downscaling + palette quantisation alone gets you.

Three variants, increasing in effort:
  A  plain  box-downscale + palette quantisation      (the naive approach)
  B  + median filter before scaling                   (kills sensor/detail noise first)
  C  + outline pass                                    (RA sprites have hard dark edges)

Usage: photo_to_pixel.py <in.png> <temperat.pal> <out_prefix> [--size 48]
"""
import sys

import numpy as np
from PIL import Image, ImageFilter, ImageStat


def load_pal(path):
    with open(path, "rb") as f:
        raw = f.read()
    return np.array([[raw[i * 3] * 4, raw[i * 3 + 1] * 4, raw[i * 3 + 2] * 4] for i in range(256)], dtype=np.int32)


def quantise(img, pal):
    a = np.asarray(img.convert("RGB"), dtype=np.int32)
    flat = a.reshape(-1, 3)
    # chunked nearest-colour so we do not allocate a 200k x 256 distance matrix at once
    out = np.empty_like(flat)
    step = 8192
    for i in range(0, len(flat), step):
        chunk = flat[i:i + step]
        d = ((chunk[:, None, :] - pal[None, :, :]) ** 2).sum(axis=2)
        out[i:i + step] = pal[d.argmin(axis=1)]
    return Image.fromarray(out.reshape(a.shape).astype(np.uint8), "RGB")


def knockout_bg(img, tol=38):
    """Flat background from the corners."""
    a = np.asarray(img.convert("RGB"), dtype=np.int32)
    h, w, _ = a.shape
    corners = np.array([a[0, 0], a[0, w - 1], a[h - 1, 0], a[h - 1, w - 1]])
    bg = np.median(corners, axis=0)
    d = np.sqrt(((a - bg) ** 2).sum(axis=2))
    alpha = np.where(d <= tol * 0.75, 0, np.where(d < tol, (255 * (d - tol * 0.75) / (tol * 0.25)), 255)).astype(np.uint8)
    rgba = np.dstack([np.asarray(img.convert("RGB")), alpha])
    return Image.fromarray(rgba, "RGBA")


def outline(img, strength=110):
    """Darken pixels on the silhouette edge - RA sprites read as shapes because of it."""
    a = np.asarray(img.convert("RGBA")).copy()
    alpha = a[:, :, 3]
    edge = np.zeros_like(alpha, dtype=bool)
    # a pixel is an edge if it is opaque but any 4-neighbour is transparent
    inner = alpha > 0
    for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        shifted = np.roll(inner, (dy, dx), axis=(0, 1))
        edge |= inner & ~shifted
    rgb = a[:, :, :3].astype(np.int16)
    rgb[edge] = np.clip(rgb[edge] - strength, 0, 255)
    a[:, :, :3] = rgb.astype(np.uint8)
    return Image.fromarray(a, "RGBA")


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 1
    src, pal_path, prefix = sys.argv[1], sys.argv[2], sys.argv[3]
    size = 48
    for i, v in enumerate(sys.argv):
        if v == "--size" and i + 1 < len(sys.argv):
            size = int(sys.argv[i + 1])
    pal = load_pal(pal_path)

    base = knockout_bg(Image.open(src))

    a = quantise(base.resize((size, size), Image.BOX), pal)
    a.save(f"{prefix}_A_naive.png")

    med = base.filter(ImageFilter.MedianFilter(5))
    b = quantise(med.resize((size, size), Image.BOX), pal)
    b.save(f"{prefix}_B_median.png")

    c = outline(quantise(med.resize((size, size), Image.BOX), pal))
    c.save(f"{prefix}_C_outline.png")

    # upscale all three for inspection
    for tag, im in (("A_naive", a), ("B_median", b), ("C_outline", c)):
        im.resize((size * 8, size * 8), Image.NEAREST).convert("RGB").save(f"{prefix}_{tag}_zoom.png")

    print(f"size {size}x{size}")
    for tag, im in (("A naive", a), ("B median", b), ("C outline", c)):
        print(f"  {tag}: {len(im.getcolors(maxcolors=1 << 24) or [])} colours")
    return 0


if __name__ == "__main__":
    sys.exit(main())
