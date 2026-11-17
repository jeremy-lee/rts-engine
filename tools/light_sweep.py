#!/usr/bin/env python3
"""Single-variable sweep of the fill-light energy, measuring lighting contrast each step.

Why this exists: I previously tried to soften the lighting by moving THREE knobs at once
(key 4.6->2.6, fill 0.55->1.9, ambient 0.22->0.55) and the measured quadrant spread got
WORSE, not better - with three variables changing there was no way to tell which one did
it. This script changes exactly one: `fill.energy`. Everything else stays at the committed
values.

Target: mean quadrant spread ~30, which is what the shipped RA tank measures.

For each value it rewrites the constant, renders type59 at 128px, converts at 40x48 with
the documented recipe, then measures the mean spread across 8 facings. The source file is
restored on exit no matter what.

Usage: light_sweep.py [--values 0.55,1.2,2.0,3.0] [--model type59] [--paint ra_olive]
"""
import os
import re
import shutil
import subprocess
import sys

import numpy as np
from PIL import Image

ROOT = r"D:\rts-engine"
SRC = os.path.join(ROOT, "tools", "blender_vehicles.py")
BLENDER = os.path.join(ROOT, "artifacts", "blender",
                       "blender-4.2.16-windows-x64", "blender.exe")
PY = r"D:\Comfy-Desktop\ComfyUI-Installs\Minimax\ComfyUI\.venv\Scripts\python.exe"
LOWRES = os.path.join(ROOT, "tools", "lowres_sprite.py")
PAL = os.path.join(ROOT, "assets_raw", "ra", "temperat.pal")
ART = os.path.join(ROOT, "artifacts")
FACINGS = [0, 4, 8, 12, 16, 20, 24, 28]


def quadrant_spread(path):
    a = np.asarray(Image.open(path).convert("RGBA"))
    m = a[:, :, 3] > 100
    if not m.any():
        return None
    lum = 0.299 * a[:, :, 0] + 0.587 * a[:, :, 1] + 0.114 * a[:, :, 2]
    ys, xs = np.nonzero(m)
    cx, cy = xs.mean(), ys.mean()
    X = np.arange(a.shape[1])[None, :]
    Y = np.arange(a.shape[0])[:, None]
    q = [lum[m & (X < cx) & (Y < cy)].mean(), lum[m & (X >= cx) & (Y < cy)].mean(),
         lum[m & (X < cx) & (Y >= cy)].mean(), lum[m & (X >= cx) & (Y >= cy)].mean()]
    return float(max(q) - min(q))


def measure(d, model):
    vals = []
    for f in FACINGS:
        p = os.path.join(d, model, "%s_%02d.png" % (model, f))
        if os.path.exists(p):
            s = quadrant_spread(p)
            if s is not None:
                vals.append(s)
    return float(np.mean(vals)) if vals else None


def set_fill(value):
    with open(SRC, encoding="utf-8") as fh:
        text = fh.read()
    new, n = re.subn(r"fill\.energy = [0-9.]+", "fill.energy = %s" % value, text)
    if n != 1:
        raise RuntimeError("expected exactly one fill.energy assignment, got %d" % n)
    with open(SRC, "w", encoding="utf-8", newline="") as fh:
        fh.write(new)


def main():
    values = ["0.55", "1.2", "2.0", "3.0"]
    model, paint = "type59", "ra_olive"
    i = 1
    while i < len(sys.argv):
        if sys.argv[i] == "--values" and i + 1 < len(sys.argv):
            values = sys.argv[i + 1].split(","); i += 2
        elif sys.argv[i] == "--model" and i + 1 < len(sys.argv):
            model = sys.argv[i + 1]; i += 2
        elif sys.argv[i] == "--paint" and i + 1 < len(sys.argv):
            paint = sys.argv[i + 1]; i += 2
        else:
            i += 1

    backup = SRC + ".sweepbak"
    shutil.copy2(SRC, backup)
    print("baseline (committed) fill.energy, measuring reference ...")
    results = []
    try:
        for v in values:
            set_fill(v)
            rdir = os.path.join(ART, "sweep_r_%s" % v.replace(".", ""))
            gdir = os.path.join(ART, "sweep_g_%s" % v.replace(".", ""))
            for d in (rdir, gdir):
                shutil.rmtree(d, ignore_errors=True)
            subprocess.run([BLENDER, "--background", "--python", SRC, "--",
                            rdir, "--facings", "32", "--size", "128",
                            "--variants", "1", "--paint", paint,
                            "--only", model],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            subprocess.run([PY, LOWRES, rdir, model, PAL, gdir,
                            "--facings", "32", "--max", "40x48", "--dither", "0.5",
                            "--grain", "0.35", "--colours", "12",
                            "--outline", "black", "--contrast", "1.0"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            m = measure(gdir, model)
            results.append((float(v), m))
            print("  fill=%-5s mean quadrant spread = %s" % (v, "n/a" if m is None else "%.1f" % m))
    finally:
        shutil.copy2(backup, SRC)
        os.remove(backup)
        print("source restored")

    print("\n--- sweep summary (target 29.6) ---")
    best = None
    for v, m in results:
        if m is None:
            continue
        print("  fill %-5s -> %5.1f" % (v, m))
        if best is None or abs(m - 29.6) < abs(best[1] - 29.6):
            best = (v, m)
    if best:
        print("  closest to RA: fill %s (%.1f)" % (best[0], best[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
