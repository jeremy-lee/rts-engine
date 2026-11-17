#!/usr/bin/env python3
"""Preview a set of converted sprites at a readable scale, old vs new."""
import os

from PIL import Image, ImageDraw

BG = (58, 66, 58)
Z = 12
CW, CH = 24 * Z, 32 * Z
LABEL_W = 150

# (label, directory, name, facings to show)
ROWS = [
    ("type62 OLD", r"D:\rts-engine\artifacts\plagrit\type62", "type62", [0, 8, 16]),
    ("type62 NEW", r"D:\rts-engine\artifacts\plafixgrit\type62", "type62", [0, 8, 16]),
    ("wz523 OLD", r"D:\rts-engine\artifacts\plagrit\wz523", "wz523", [0, 8, 16]),
    ("wz523 NEW", r"D:\rts-engine\artifacts\plafixgrit\wz523", "wz523", [0, 8, 16]),
    ("y5 OLD", r"D:\rts-engine\artifacts\plagrit\y5", "y5", [4, 8, 12]),
    ("y5 NEW", r"D:\rts-engine\artifacts\plafixgrit\y5", "y5", [4, 8, 12]),
]
OUT = r"D:\rts-engine\artifacts\pla_fix_preview.png"


def main():
    cols = max(len(r[3]) for r in ROWS)
    sheet = Image.new("RGB", (LABEL_W + cols * (CW + 8) + 12,
                             len(ROWS) * (CH + 8) + 12), BG)
    dr = ImageDraw.Draw(sheet)
    for ri, (tag, d, nm, facings) in enumerate(ROWS):
        y0 = 8 + ri * (CH + 8)
        dr.text((8, y0 + CH // 2 - 6), tag, fill=(240, 240, 240))
        for k, f in enumerate(facings):
            p = os.path.join(d, "%s_%02d.png" % (nm, f))
            if not os.path.exists(p):
                continue
            im = Image.open(p).convert("RGBA")
            big = im.resize((im.width * Z, im.height * Z), Image.NEAREST)
            sheet.paste(big, (LABEL_W + k * (CW + 8) + (CW - big.width) // 2,
                              y0 + (CH - big.height) // 2), big)
    sheet.save(OUT)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
