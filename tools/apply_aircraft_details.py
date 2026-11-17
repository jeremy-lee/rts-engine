#!/usr/bin/env python3
"""Insert the aircraft structure helpers and splice in the 12 detailed aircraft.

Two operations:
  1. insert air_helpers.HELPERS once, immediately before the VEHICLES dict, so the helper
     functions sit alongside the tank ones and can use the same primitives.
  2. replace each aircraft function body, located by its `def name():` line and running to
     the next top-level `def `, so the patch does not depend on old whitespace.

Idempotent for step 1: the helpers are skipped if `def fuselage3(` already exists.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import air_helpers  # noqa: E402
import aircraft_details as ad  # noqa: E402

SRC = r"D:\rts-engine\tools\blender_vehicles.py"

REPLACEMENTS = {
    "a_ww1_biplane": ad.WW1_BIPLANE,
    "a_ww2_fighter": ad.WW2_FIGHTER,
    "a_jet": ad.JET,
    "a_heli": ad.HELICOPTER,
    "a_heavy_bomber": ad.HEAVY_BOMBER,
    "a_recon_plane": ad.RECON_PLANE,
    "a_seaplane": ad.SEAPLANE,
    "a_transport_plane": ad.TRANSPORT_PLANE,
    "a_ground_attack": ad.GROUND_ATTACK,
    "a_jet_bomber": ad.JET_BOMBER,
    "a_vtol": ad.VTOL,
    "a_zeppelin_air": ad.AIRSHIP,
}


def main():
    with open(SRC, encoding="utf-8") as fh:
        text = fh.read()

    if "def fuselage3(" not in text:
        marker = "\nVEHICLES = {"
        idx = text.find(marker)
        if idx < 0:
            print("FAILED: VEHICLES dict not found")
            return 1
        text = text[:idx] + "\n" + air_helpers.HELPERS + text[idx:]
        print("helpers inserted")
    else:
        print("helpers already present, skipped")

    ok, bad = [], []
    for name, new_src in REPLACEMENTS.items():
        pat = re.compile(r"^def %s\(\):.*?(?=^def |\Z)" % re.escape(name), re.S | re.M)
        m = pat.search(text)
        if m is None:
            bad.append(name)
            continue
        text = text[:m.start()] + new_src.strip() + "\n\n\n" + text[m.end():]
        ok.append(name)

    with open(SRC, "w", encoding="utf-8", newline="") as fh:
        fh.write(text)

    print("aircraft replaced: %d" % len(ok))
    if bad:
        print("NOT FOUND: %s" % ", ".join(bad))
    return 0


if __name__ == "__main__":
    sys.exit(main())
