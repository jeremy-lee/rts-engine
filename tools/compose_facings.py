#!/usr/bin/env python3
"""Compose a sheet from <prefix>_fNN.png renders."""
import os
import sys

from PIL import Image, ImageDraw

BG = (52, 58, 52)


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else r"D:\rts-engine\artifacts\t59mesh"
    out = sys.argv[2] if len(sys.argv) > 2 else r"D:\rts-engine\artifacts\t59mesh_sheet.png"
    prefix = sys.argv[3] if len(sys.argv) > 3 else "t59mesh"
    zoom = int(sys.argv[4]) if len(sys.argv) > 4 else 2
    cols = int(sys.argv[5]) if len(sys.argv) > 5 else 4

    files = []
    i = 0
    while True:
        p = os.path.join(d, "%s_f%02d.png" % (prefix, i))
        if not os.path.exists(p):
            break
        files.append(p)
        i += 1
    if not files:
        print("no files")
        return 1

    proto = Image.open(files[0])
    cw, ch = proto.width * zoom, proto.height * zoom
    rows = (len(files) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (cw + 6) + 6, rows * (ch + 20) + 6), BG)
    dr = ImageDraw.Draw(sheet)
    for k, p in enumerate(files):
        r, c = divmod(k, cols)
        im = Image.open(p).convert("RGBA")
        big = im.resize((im.width * zoom, im.height * zoom), Image.LANCZOS)
        x = 6 + c * (cw + 6)
        y = 6 + r * (ch + 20) + 16
        dr.text((x, y - 14), os.path.basename(p)[:-4], fill=(235, 235, 235))
        sheet.paste(big, (x, y), big)
    sheet.save(out)
    print("wrote", out, len(files), "frames", sheet.size)
    return 0


if __name__ == "__main__":
    sys.exit(main())
