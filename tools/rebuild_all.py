#!/usr/bin/env python3
"""Rebuild every vehicle/aircraft sprite at the agreed settings.

Renders each type from the current geometry, converts at 40x48 with contrast 0.75, and
reports per-type success so a single failure cannot hide behind a summary line.

Settings are the ones established by measurement in docs/pixel-military-asset-workflow.md
and the lighting sweep:
  size 40x48   - 24x32 (768 px) cannot hold the detail, 40x48 (1920 px) can
  colours 12   - matches RA's 13
  dither 0.5 + grain 0.35 - the grain must come before the dither or there is nothing to
                            dither on flat Blender faces
  outline black - RA's sprites all hit luma 0
  contrast 0.75 - fills the shadows; 1.0 leaves them 45.7 in quadrant spread vs RA's 29.6,
                  and 0.48 matches RA numerically but flattens the form

Usage: rebuild_all.py [--contrast 0.75] [--size 40x48] [--facings 32] [--only a,b,c]
"""
import os
import shutil
import subprocess
import sys

ROOT = r"D:\rts-engine"
BLENDER = os.path.join(ROOT, "artifacts", "blender",
                       "blender-4.2.16-windows-x64", "blender.exe")
PY = r"D:\Comfy-Desktop\ComfyUI-Installs\Minimax\ComfyUI\.venv\Scripts\python.exe"
VEHICLES_PY = os.path.join(ROOT, "tools", "blender_vehicles.py")
LOWRES = os.path.join(ROOT, "tools", "lowres_sprite.py")
PAL = os.path.join(ROOT, "assets_raw", "ra", "temperat.pal")
ART = os.path.join(ROOT, "artifacts")

GENERIC = [
    "ww1_rhomboid", "ww2_medium", "ww2_heavy", "coldwar_mbt", "spg", "aa_track",
    "ww1_biplane", "ww2_fighter", "jet", "helicopter", "truck", "halftrack",
    "apc", "mlrs", "jeep", "light_tank", "super_heavy", "spaag", "recon",
    "engineer", "sam", "transport", "command_car", "heavy_bomber", "recon_plane",
    "seaplane", "transport_plane", "ground_attack", "jet_bomber", "vtol", "airship",
]
PLA = [
    "type59", "type62", "type63_apc", "type70_spg", "type89_spg", "type63_aaa",
    "wz523", "type83_152", "j5", "j6", "j7", "q5", "h5", "h6", "y5", "z5",
]


def run(cmd):
    return subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode


def main():
    contrast, size, facings = "0.75", "40x48", "32"
    only = None
    i = 1
    while i < len(sys.argv):
        if sys.argv[i] == "--contrast" and i + 1 < len(sys.argv):
            contrast = sys.argv[i + 1]; i += 2
        elif sys.argv[i] == "--size" and i + 1 < len(sys.argv):
            size = sys.argv[i + 1]; i += 2
        elif sys.argv[i] == "--facings" and i + 1 < len(sys.argv):
            facings = sys.argv[i + 1]; i += 2
        elif sys.argv[i] == "--only" and i + 1 < len(sys.argv):
            only = set(sys.argv[i + 1].split(",")); i += 2
        else:
            i += 1

    groups = [("generic", GENERIC, "ra_sand"), ("pla", PLA, "ra_olive")]
    ok, bad = [], []
    for gname, types, paint in groups:
        types = [t for t in types if only is None or t in only]
        if not types:
            continue
        rdir = os.path.join(ART, "all_r_" + gname)
        shutil.rmtree(rdir, ignore_errors=True)
        print("rendering %d %s types ..." % (len(types), gname), flush=True)
        rc = run([BLENDER, "--background", "--python", VEHICLES_PY, "--",
                  rdir, "--facings", facings, "--size", "128", "--variants", "1",
                  "--paint", paint, "--only", ",".join(types)])
        if rc != 0:
            print("  blender failed rc=%d" % rc)
        for t in types:
            gdir = os.path.join(ART, "all_g", t)
            shutil.rmtree(gdir, ignore_errors=True)
            src = rdir
            rc = run([PY, LOWRES, src, t, PAL, gdir, "--facings", facings,
                      "--max", size, "--dither", "0.5", "--grain", "0.35",
                      "--colours", "12", "--outline", "black",
                      "--contrast", contrast])
            produced = 0
            if os.path.isdir(gdir):
                produced = len([f for f in os.listdir(gdir) if f.endswith(".png")])
            if rc == 0 and produced >= int(facings):
                ok.append(t)
            else:
                bad.append((t, rc, produced))

    print("\nconverted OK: %d" % len(ok))
    if bad:
        print("FAILED:")
        for t, rc, n in bad:
            print("  %-18s rc=%d produced=%d" % (t, rc, n))
    return 0


if __name__ == "__main__":
    sys.exit(main())
