#!/usr/bin/env python3
"""Before/after for the diamond-wing fix, at a readable scale."""
import os

from PIL import Image, ImageDraw

BG = (58, 66, 58)
Z = 3
CELL_W, CELL_H = 46 * Z, 54 * Z
LABEL_W = 90
NAMES = ["j5", "j6", "j7", "q5", "h5", "h6", "y5", "z5"]
FACINGS = [4, 12]
ROWS = [
    ("OLD", r"D:\rts-engine\artifacts\plaBig"),
    ("NEW", r"D:\rts-engine\artifacts\planeFixGrit"),
]
OUT = r"D:\rts-engine\artifacts\wing_fix.png"


def main():
    cols = len(FACINGS)
    sheet = Image.new("RGB", (LABEL_W + cols * (CELL_W + 6) + 6,
                             len(ROWS) * len(NAMES) * (CELL_H + 4) + 6), BG)
    dr = ImageDraw.Draw(sheet)
    row_i = 0
    for tag, d in ROWS:
        for nm in NAMES:
            y0 = 6 + row_i * (CELL_H + 4)
            dr.text((6, y0 + CELL_H // 2 - 6), "%s %s" % (nm, tag), fill=(240, 240, 240))
            for k, f in enumerate(FACINGS):
                p = os.path.join(d, nm, "%s_%02d.png" % (nm, f))
                if not os.path.exists(p):
                    continue
                im = Image.open(p).convert("RGBA")
                big = im.resize((im.width * Z, im.height * Z), Image.NEAREST)
                sheet.paste(big, (LABEL_W + k * (CELL_W + 6) + (CELL_W - big.width) // 2,
                                  y0 + (CELL_H - big.height) // 2), big)
            row_i += 1
    sheet.save(OUT)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
