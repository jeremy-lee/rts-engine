#!/usr/bin/env python3
"""Build a mixed LoRA dataset from three sources, normalised to one pixel-art look.

Sources and why each is handled differently:
  * Blender renders (512x512, anti-aliased) - must be reduced to a RA-sized sprite and
    then blown back up with NEAREST, otherwise the LoRA learns smooth gradients instead
    of pixel edges.
  * RA original sprites (24-72 px) - pasted at NATIVE size on a 512 canvas, never scaled
    up, because upscaling them just makes mush.
  * Either way the result is a crisp pixel-art image at 512x512 with a caption.

Usage: build_mixed_dataset.py <out_dir> [--ra-dir ...] [--render-dirs ...]
"""
import glob
import os
import sys

import numpy as np
from PIL import Image

SPRITE = 48          # RA-ish sprite size before blowing back up
CANVAS = 512         # training resolution
BG = (40, 40, 40)


def load_pal(path):
    with open(path, "rb") as f:
        raw = f.read()
    return np.array([[raw[i * 3] * 4, raw[i * 3 + 1] * 4, raw[i * 3 + 2] * 4] for i in range(256)], dtype=np.int32)


def quantise(rgba, pal):
    a = np.asarray(rgba).copy()
    rgb = a[:, :, :3].astype(np.int32)
    mask = a[:, :, 3] > 8
    if not mask.any():
        return rgba
    flat = rgb[mask]
    out = np.empty_like(flat)
    step = 4096
    for i in range(0, len(flat), step):
        chunk = flat[i:i + step]
        d = ((chunk[:, None, :] - pal[None, :, :]) ** 2).sum(axis=2)
        out[i:i + step] = pal[d.argmin(axis=1)]
    rgb[mask] = out
    a[:, :, :3] = rgb.astype(np.uint8)
    return Image.fromarray(a, "RGBA")


def add_outline(rgba, strength=0.45):
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


def render_to_pixel(path, pal, sprite=SPRITE):
    """Blender render -> quantised sprite -> blown back up with NEAREST."""
    im = Image.open(path).convert("RGBA")
    bbox = im.getbbox()
    if bbox:
        im = im.crop(bbox)
    w, h = im.size
    sc = min(sprite / w, sprite / h)
    nw, nh = max(1, int(round(w * sc))), max(1, int(round(h * sc)))
    small = im.resize((nw, nh), Image.LANCZOS)
    canvas = Image.new("RGBA", (sprite, sprite), (0, 0, 0, 0))
    canvas.paste(small, ((sprite - nw) // 2, (sprite - nh) // 2), small)
    canvas = add_outline(quantise(canvas, pal))
    return canvas.resize((CANVAS, CANVAS), Image.NEAREST)


def ra_sprite_to_canvas(path, pal):
    """RA original -> native size centred on the canvas (no upscaling)."""
    im = Image.open(path).convert("RGBA")
    if im.width > CANVAS or im.height > CANVAS:
        return None
    base = Image.new("RGBA", (CANVAS, CANVAS), (*BG, 255))
    base.paste(im, ((CANVAS - im.width) // 2, (CANVAS - im.height) // 2), im)
    return base.convert("RGB")


CAPTION = ("pixel art isometric {name}, Command and Conquer Red Alert style, "
           "RTS game asset, limited colour palette, sharp pixels, {kind}")


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else r"D:\rts-engine\artifacts\ds_mixed"
    os.makedirs(out, exist_ok=True)
    for f in glob.glob(os.path.join(out, "*.png")) + glob.glob(os.path.join(out, "*.txt")):
        os.remove(f)

    pal = load_pal(r"D:\rts-engine\assets_raw\ra\temperat.pal")

    render_sources = [
        ("building", r"D:\rts-engine\artifacts\blender_out4"),
        ("vehicle", r"D:\rts-engine\artifacts\model_out2"),
        ("vehicle", r"D:\rts-engine\artifacts\model_out_plane"),
        ("vehicle", r"D:\rts-engine\artifacts\model_out_turretcar"),
        ("vehicle", r"D:\rts-engine\artifacts\model_out_zeppelin"),
    ]

    n_render = 0
    for kind, d in render_sources:
        if not os.path.isdir(d):
            continue
        for f in sorted(glob.glob(os.path.join(d, "*.png"))):
            base = os.path.splitext(os.path.basename(f))[0]
            if base in ("preview", "all_facings", "all_sprites"):
                continue
            try:
                img = render_to_pixel(f, pal)
            except Exception as e:
                print("skip", f, e)
                continue
            name = f"{kind}_{base}"
            img.convert("RGB").save(os.path.join(out, name + ".png"))
            with open(os.path.join(out, name + ".txt"), "w", encoding="utf-8") as fh:
                fh.write(CAPTION.format(name=base.replace("_", " "), kind=kind) + "\n")
            n_render += 1

    n_ra = 0
    ra = sorted(glob.glob(r"D:\rts-engine\assets_raw\ra\*.png"))
    for f in ra[::max(1, len(ra) // 60)]:
        try:
            img = ra_sprite_to_canvas(f, pal)
        except Exception:
            continue
        if img is None:
            continue
        base = os.path.splitext(os.path.basename(f))[0]
        name = f"ra_{base}"
        img.save(os.path.join(out, name + ".png"))
        with open(os.path.join(out, name + ".txt"), "w", encoding="utf-8") as fh:
            fh.write(CAPTION.format(name=base.split("-")[0], kind="sprite") + "\n")
        n_ra += 1

    print(f"renders: {n_render}  ra: {n_ra}  total: {n_render + n_ra}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
