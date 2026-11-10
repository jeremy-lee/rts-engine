#!/usr/bin/env python3
"""Turn an RGBA render (transparent background) into a RA-sized quantised sprite.

Different from gen_to_sprite.py: that one detects a flat BACKGROUND colour from the
corners because diffusion output is opaque. Blender renders with film_transparent, so
alpha already marks the subject - no knockout needed, and doing a colour knockout on
transparent pixels would silently wreck it.

Adds an optional dark outline because RA sprites read as shapes: without an edge the
sprite melts into the terrain at 48 px.

Usage:
  render_to_sprite.py <in.png> <temperat.pal> <out.png> [--size 48] [--outline 0.45]
"""
import os
import sys

import numpy as np
from PIL import Image


def load_pal(path):
    with open(path, "rb") as f:
        raw = f.read()
    return np.array([[raw[i * 3] * 4, raw[i * 3 + 1] * 4, raw[i * 3 + 2] * 4] for i in range(256)], dtype=np.int32)


def quantise(rgb, alpha, pal):
    flat = rgb.reshape(-1, 3).astype(np.int32)
    out = np.empty_like(flat)
    step = 8192
    for i in range(0, len(flat), step):
        chunk = flat[i:i + step]
        d = ((chunk[:, None, :] - pal[None, :, :]) ** 2).sum(axis=2)
        out[i:i + step] = pal[d.argmin(axis=1)]
    return out.reshape(rgb.shape).astype(np.uint8)


def add_outline(rgba, strength):
    """Darken opaque pixels that touch transparency."""
    a = rgba.copy()
    alpha = a[:, :, 3]
    inner = alpha > 8
    edge = np.zeros_like(inner)
    for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        edge |= inner & ~np.roll(inner, (dy, dx), axis=(0, 1))
    rgb = a[:, :, :3].astype(np.float32)
    rgb[edge] *= (1.0 - strength)
    a[:, :, :3] = np.clip(rgb, 0, 255).astype(np.uint8)
    return a


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 1
    src, pal_path, out = sys.argv[1], sys.argv[2], sys.argv[3]
    size = 48
    outline = 0.45
    i = 4
    while i < len(sys.argv):
        if sys.argv[i] == "--size" and i + 1 < len(sys.argv):
            size = int(sys.argv[i + 1]); i += 2
        elif sys.argv[i] == "--outline" and i + 1 < len(sys.argv):
            outline = float(sys.argv[i + 1]); i += 2
        else:
            i += 1

    pal = load_pal(pal_path)
    im = Image.open(src).convert("RGBA")

    # trim to the subject before scaling so it fills the target canvas
    bbox = im.getbbox()
    if bbox:
        im = im.crop(bbox)

    # fit preserving aspect, centred
    w, h = im.size
    scale = min(size / w, size / h)
    nw, nh = max(1, int(round(w * scale))), max(1, int(round(h * scale)))
    small = im.resize((nw, nh), Image.LANCZOS)
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    canvas.paste(small, ((size - nw) // 2, (size - nh) // 2), small)

    a = np.asarray(canvas).copy()
    rgb = a[:, :, :3]
    alpha = a[:, :, 3]
    # only quantise where there is something to quantise
    mask = alpha > 8
    q = quantise(rgb, alpha, pal)
    rgb[mask] = q[mask]
    a[:, :, :3] = rgb

    a = add_outline(a, outline)
    Image.fromarray(a, "RGBA").save(out)

    vis = int((a[:, :, 3] > 8).sum())
    print(f"{os.path.basename(out)} {size}x{size} visible={vis} ({100.0*vis/(size*size):.0f}%)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
