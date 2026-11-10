#!/usr/bin/env python3
"""Turn a generated image into a RA-ready sprite PNG.

Pipeline (the whole point of this file):
  1. detect the flat background from the corner pixels and knock it out to alpha 0
  2. crop to the subject's bounding box
  3. scale down to fit the target canvas, preserving aspect, centred
  4. quantise to the real RA 6-bit palette (temperat.pal)
  5. write an RGBA PNG that rts_import can consume

Usage:
  gen_to_sprite.py <in.png> <temperat.pal> <out.png> [--size 48x48] [--tol 40] [--brightness 1.0]

--tol         background colour distance tolerance (RGB euclidean)
--brightness  multiply before quantising; RA art is darker than typical diffusion output
"""
import sys
from PIL import Image, ImageEnhance


def parse_args(argv):
    src, pal, out = argv[1], argv[2], argv[3]
    size = (48, 48)
    tol = 40
    brightness = 1.0
    i = 4
    while i < len(argv):
        if argv[i] == "--size" and i + 1 < len(argv):
            w, h = argv[i + 1].lower().split("x")
            size = (int(w), int(h))
            i += 2
        elif argv[i] == "--tol" and i + 1 < len(argv):
            tol = int(argv[i + 1])
            i += 2
        elif argv[i] == "--brightness" and i + 1 < len(argv):
            brightness = float(argv[i + 1])
            i += 2
        else:
            i += 1
    return src, pal, out, size, tol, brightness


def load_ra_palette(path):
    with open(path, "rb") as f:
        raw = f.read()
    return [(raw[i * 3] * 4, raw[i * 3 + 1] * 4, raw[i * 3 + 2] * 4) for i in range(256)]


def detect_bg(img):
    """Sample the four corners; the flat background is their median colour."""
    w, h = img.size
    px = img.load()
    corners = [px[0, 0], px[w - 1, 0], px[0, h - 1], px[w - 1, h - 1]]
    rs = sorted(c[0] for c in corners)
    gs = sorted(c[1] for c in corners)
    bs = sorted(c[2] for c in corners)
    return (rs[1], gs[1], bs[1])


def knock_out(img, bg, tol):
    """Make pixels close to bg transparent. Soft edge: partial alpha in the last 25%."""
    img = img.convert("RGBA")
    px = img.load()
    w, h = img.size
    inner = tol * 0.75
    for y in range(h):
        for x in range(w):
            r, g, b, _ = px[x, y]
            d = ((r - bg[0]) ** 2 + (g - bg[1]) ** 2 + (b - bg[2]) ** 2) ** 0.5
            if d <= inner:
                px[x, y] = (r, g, b, 0)
            elif d < tol:
                a = int(255 * (d - inner) / max(1e-6, tol - inner))
                px[x, y] = (r, g, b, a)
    return img


def crop_to_content(img):
    bbox = img.getbbox()
    if bbox is None:
        return img
    return img.crop(bbox)


def fit_into(img, size):
    """Scale preserving aspect ratio so it fits, then centre on a transparent canvas."""
    w, h = img.size
    tw, th = size
    scale = min(tw / w, th / h)
    nw, nh = max(1, int(round(w * scale))), max(1, int(round(h * scale)))
    small = img.resize((nw, nh), Image.LANCZOS)
    canvas = Image.new("RGBA", (tw, th), (0, 0, 0, 0))
    canvas.paste(small, ((tw - nw) // 2, (th - nh) // 2), small)
    return canvas


def quantise_rgba(img, palette):
    """Nearest palette colour for visible pixels; keeps alpha."""
    px = img.load()
    w, h = img.size
    cache = {}
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            key = (r, g, b)
            q = cache.get(key)
            if q is None:
                best, bestd = palette[0], 1 << 30
                for p in palette:
                    d = (r - p[0]) ** 2 + (g - p[1]) ** 2 + (b - p[2]) ** 2
                    if d < bestd:
                        bestd, best = d, p
                q = best
                cache[key] = q
            px[x, y] = (q[0], q[1], q[2], a)
    return img


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 1
    src, pal_path, out, size, tol, brightness = parse_args(sys.argv)
    palette = load_ra_palette(pal_path)

    img = Image.open(src).convert("RGB")
    bg = detect_bg(img)
    print(f"background detected: rgb{bg}")

    img = knock_out(img, bg, tol)
    img = crop_to_content(img)
    print(f"subject bbox after knockout: {img.size}")

    if brightness != 1.0:
        img = ImageEnhance.Brightness(img).enhance(brightness)

    img = fit_into(img, size)
    img = quantise_rgba(img, palette)

    img.save(out)
    visible = sum(1 for p in (img.getcolors(maxcolors=1 << 24) or []) if p[1][3] > 0)
    print(f"wrote {out}  {size[0]}x{size[1]}  visible px={visible}  ({100.0*visible/(size[0]*size[1]):.0f}%)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
