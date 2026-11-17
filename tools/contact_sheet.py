#!/usr/bin/env python3
"""Build a labelled contact sheet of converted sprites.

Usage:
  contact_sheet.py <base_dir> <out.png> <type1,type2,...> [--cols 6] [--zoom 4]
                   [--cell 42x50] [--step 5]
"""
import os
import sys

from PIL import Image, ImageDraw

BG = (58, 66, 58)


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 1
    base, out = sys.argv[1], sys.argv[2]
    types = sys.argv[3].split(",")
    cols, zoom, step = 6, 4, 5
    cell_w, cell_h = 42, 50
    i = 4
    while i < len(sys.argv):
        a = sys.argv[i]
        if a == "--cols" and i + 1 < len(sys.argv):
            cols = int(sys.argv[i + 1]); i += 2
        elif a == "--zoom" and i + 1 < len(sys.argv):
            zoom = int(sys.argv[i + 1]); i += 2
        elif a == "--cell" and i + 1 < len(sys.argv):
            w, h = sys.argv[i + 1].lower().split("x")
            cell_w, cell_h = int(w), int(h); i += 2
        elif a == "--step" and i + 1 < len(sys.argv):
            step = int(sys.argv[i + 1]); i += 2
        else:
            i += 1

    CW, CH = cell_w * zoom, cell_h * zoom
    label_w = 130
    sheet = Image.new("RGB", (label_w + cols * (CW + 4) + 10,
                             len(types) * (CH + 4) + 10), BG)
    dr = ImageDraw.Draw(sheet)
    missing = 0
    for ri, t in enumerate(types):
        y0 = 6 + ri * (CH + 4)
        dr.text((6, y0 + CH // 2 - 6), t, fill=(238, 238, 238))
        for k in range(cols):
            f = k * step
            p = os.path.join(base, t, "%s_%02d.png" % (t, f))
            if not os.path.exists(p):
                missing += 1
                continue
            im = Image.open(p).convert("RGBA")
            big = im.resize((im.width * zoom, im.height * zoom), Image.NEAREST)
            sheet.paste(big, (label_w + k * (CW + 4) + (CW - big.width) // 2,
                              y0 + (CH - big.height) // 2), big)
    sheet.save(out)
    print("wrote", out, len(types), "types, missing", missing)
    return 0


if __name__ == "__main__":
    sys.exit(main())
