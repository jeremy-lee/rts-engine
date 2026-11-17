#!/usr/bin/env python3
"""Splice the detailed tank rewrites into blender_vehicles.py.

Each replacement is located by its `def <name>():` line and runs to the next top-level
`def ` at column 0, so the script does not depend on exact whitespace of the old body.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tank_details as td  # noqa: E402

SRC = r"D:\rts-engine\tools\blender_vehicles.py"

REPLACEMENTS = {
    "p_type62": td.TYPE62,
    "t_ww2_medium": td.WW2_MEDIUM,
    "t_ww2_heavy": td.WW2_HEAVY,
    "t_coldwar_mbt": td.COLDWAR_MBT,
    "t_light_tank": td.LIGHT_TANK,
    "t_super_heavy": td.SUPER_HEAVY,
    "t_ww1_rhomboid": td.WW1_RHOMBOID,
}


def main():
    with open(SRC, encoding="utf-8") as fh:
        text = fh.read()

    ok, bad = [], []
    for name, new_src in REPLACEMENTS.items():
        # find "def name(" at the start of a line, capture through to the next top-level def
        pat = re.compile(r"^def %s\(\):.*?(?=^def |\Z)" % re.escape(name), re.S | re.M)
        m = pat.search(text)
        if m is None:
            bad.append(name)
            continue
        text = text[:m.start()] + new_src.strip() + "\n\n\n" + text[m.end():]
        ok.append(name)

    with open(SRC, "w", encoding="utf-8", newline="") as fh:
        fh.write(text)

    print("replaced: %d" % len(ok))
    if bad:
        print("NOT FOUND: %s" % ", ".join(bad))
    return 0


if __name__ == "__main__":
    sys.exit(main())
