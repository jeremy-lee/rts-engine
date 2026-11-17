#!/usr/bin/env python3
"""Show the same model converted at three sprite budgets, side by side."""
import os

from PIL import Image, ImageDraw

BG = (58, 66, 58)
Z = 4
CELL_W, CELL_H = 44 * Z, 52 * Z
LABEL_W = 100
ITEMS = [
    ("24x32", r"D:\rts-engine\artifacts\sz24_32"),
    ("32x40", r"D:\rts-engine\artifacts\sz32_40"),
    ("40x48", r"D:\rts-engine\artifacts\sz40_48"),
]
FACINGS = [0, 8, 16, 24]
OUT = r"D:\rts-engine\artifacts\size_preview.png"


def main():
    sheet = Image.new("RGB", (LABEL_W + len(FACINGS) * (CELL_W + 8) + 8,
                             len(ITEMS) * (CELL_H + 8) + 8), BG)
    dr = ImageDraw.Draw(sheet)
    for ri, (tag, d) in enumerate(ITEMS):
        y0 = 8 + ri * (CELL_H + 8)
        dr.text((8, y0 + CELL_H // 2 - 6), tag, fill=(240, 240, 240))
        for k, f in enumerate(FACINGS):
            p = os.path.join(d, "type59_%02d.png" % f)
            if not os.path.exists(p):
                continue
            im = Image.open(p).convert("RGBA")
            big = im.resize((im.width * Z, im.height * Z), Image.NEAREST)
            x = LABEL_W + k * (CELL_W + 8) + (CELL_W - big.width) // 2
            y = y0 + (CELL_H - big.height) // 2
            sheet.paste(big, (x, y), big)
    sheet.save(OUT)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
