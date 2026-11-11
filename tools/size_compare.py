#!/usr/bin/env python3
"""Render the same tank at four target sizes to show where procedural 3D stops working.

The question this answers: RA sprites are 13x17, but a photoreal-ish render reduced to
that size becomes noise. At what size does our pipeline still read as a tank?
"""
import numpy as np
from PIL import Image, ImageDraw

SRC = r"D:\rts-engine\artifacts\veh\ww2_medium_v0_f12.png"
OUT = r"D:\rts-engine\artifacts\size_compare.png"
SIZES = [(14, 18), (24, 32), (32, 40), (48, 48)]
ZOOM = 10


def prep(img, size):
    w, h = img.size
    sc = min(size[0] / w, size[1] / h)
    nw, nh = max(1, int(round(w * sc))), max(1, int(round(h * sc)))
    s = img.resize((nw, nh), Image.LANCZOS)

    a = np.asarray(s).astype(np.float32)
    mask = a[:, :, 3] > 8
    if mask.any():
        mean = a[:, :, :3][mask].mean()
        a[:, :, :3][mask] = np.clip((a[:, :, :3][mask] - mean) * 1.5 + mean, 0, 255)
    q = 255.0 / 4
    a[:, :, :3] = np.clip(np.round(a[:, :, :3] / q) * q, 0, 255)

    alpha = a[:, :, 3]
    inner = alpha > 8
    edge = np.zeros_like(inner)
    for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        edge |= inner & ~np.roll(inner, (dy, dx), axis=(0, 1))
    a[:, :, :3][edge] *= 0.15
    return Image.fromarray(a.astype(np.uint8), "RGBA")


def main():
    im = Image.open(SRC).convert("RGBA")
    bb = im.getbbox()
    if bb:
        im = im.crop(bb)

    cell = 48 * ZOOM
    sheet = Image.new("RGB", (len(SIZES) * cell, cell + 30), (45, 50, 45))
    d = ImageDraw.Draw(sheet)
    for i, sz in enumerate(SIZES):
        t = prep(im, sz)
        canvas = Image.new("RGBA", sz, (0, 0, 0, 0))
        canvas.paste(t, ((sz[0] - t.width) // 2, (sz[1] - t.height) // 2), t)
        sheet.paste(canvas.convert("RGB").resize((cell, cell), Image.NEAREST), (i * cell, 30))
        d.text((i * cell + 4, 6), "%dx%d" % (sz[0], sz[1]), fill=(230, 230, 230))
    sheet.save(OUT)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
