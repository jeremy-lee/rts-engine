#!/usr/bin/env python3
"""Build a labelled contact sheet of converted sprites."""
import os
import sys

from PIL import Image, ImageDraw

BASE = r"D:\rts-engine\artifacts\plaBig"
TYPES = [
    "type59", "type62", "type63_apc", "type70_spg",
    "type89_spg", "type63_aaa", "wz523", "type83_152",
    "j5", "j6", "j7", "q5",
    "h5", "h6", "y5", "z5",
]
OUT = r"D:\rts-engine\artifacts\pla_big.png"
Z = 4
CW, CH = 42 * Z, 50 * Z
BG = (58, 66, 58)
LABEL_W = 130
COLS = 6


def main():
    sheet = Image.new("RGB", (LABEL_W + COLS * (CW + 4) + 10, len(TYPES) * (CH + 4) + 10), BG)
    dr = ImageDraw.Draw(sheet)
    for ri, t in enumerate(TYPES):
        y0 = 6 + ri * (CH + 4)
        dr.text((6, y0 + CH // 2 - 6), t, fill=(238, 238, 238))
        for k in range(COLS):
            f = k * 5
            p = os.path.join(BASE, t, "%s_%02d.png" % (t, f))
            if not os.path.exists(p):
                continue
            im = Image.open(p).convert("RGBA")
            big = im.resize((im.width * Z, im.height * Z), Image.NEAREST)
            sheet.paste(big, (LABEL_W + k * (CW + 4) + (CW - big.width) // 2,
                              y0 + (CH - big.height) // 2), big)
    sheet.save(OUT)
    print("wrote", OUT, len(TYPES), "types")


if __name__ == "__main__":
    main()
