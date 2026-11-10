#!/usr/bin/env python3
"""Build a LoRA training set from exported RA sprites.

RA buildings are only 48-72 px. Upscaling them to 512 would just produce mush, so
instead each sprite is centred at NATIVE size on a 512x512 neutral canvas. The model
then learns "small isometric building on a flat background" - which is exactly the
composition we want the LoRA to reproduce.

Writes <dst>/<name>.png plus <dst>/<name>.txt captions (sd-scripts reads a same-named
.txt next to each image when the folder is configured for captions).
"""
import collections
import glob
import os
import sys

from PIL import Image

CAPTION = "pixel art isometric {name}, Command and Conquer Red Alert style building, " \
          "RTS game asset, limited colour palette, sharp pixels, plain background"


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else r"D:\rts-engine\artifacts\shp_export"
    dst = sys.argv[2] if len(sys.argv) > 2 else r"D:\rts-engine\artifacts\lora_ds"
    size = int(sys.argv[3]) if len(sys.argv) > 3 else 512
    os.makedirs(dst, exist_ok=True)
    for f in glob.glob(os.path.join(dst, "*.png")) + glob.glob(os.path.join(dst, "*.txt")):
        os.remove(f)

    bybase = collections.defaultdict(list)
    for f in glob.glob(os.path.join(src, "*.png")):
        try:
            im = Image.open(f)
            w, h = im.size
            if w < 48 or h < 48:
                continue
            if im.getbbox() is None:
                continue
            base = os.path.basename(f).split("-")[0]
            # skip UI icons and dead husks - they are not buildings and would teach the
            # model the wrong thing
            low = base.lower()
            if low.endswith("icon") or "husk" in low or low.endswith("dead"):
                continue
            bybase[base].append(f)
        except Exception:
            continue

    # Take up to MAX_PER_SPRITE frames per sprite, evenly spaced. One frame per sprite
    # gave only 18 images and the LoRA memorised them; multiple facings/states of the
    # same structure add real variety while keeping the style consistent.
    MAX_PER_SPRITE = 8
    count = 0
    for base, files in sorted(bybase.items()):
        files.sort()
        take = min(MAX_PER_SPRITE, len(files))
        step = max(1, len(files) // take)
        picked = files[::step][:take]
        for idx, f in enumerate(picked):
            im = Image.open(f).convert("RGBA")
            if im.width > size or im.height > size:
                continue
            if im.getbbox() is None:
                continue
            canvas = Image.new("RGBA", (size, size), (40, 40, 40, 255))
            canvas.paste(im, ((size - im.width) // 2, (size - im.height) // 2), im)
            name = f"{base}_{idx:02d}"
            canvas.convert("RGB").save(os.path.join(dst, name + ".png"))
            with open(os.path.join(dst, name + ".txt"), "w", encoding="utf-8") as fh:
                fh.write(CAPTION.format(name=base) + "\n")
            count += 1

    print(f"dataset images: {count}")
    print("sprites:", ", ".join(sorted(bybase.keys())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
