#!/usr/bin/env python3
"""Prove the "render a mesh ourselves" route without installing a DCC tool.

This is a deliberately tiny software renderer: it takes a list of axis-aligned boxes,
projects them with an ORTHOGRAPHIC camera at RA's isometric-ish angle, and renders
8 evenly spaced facings plus a simple top-left light. Output is exactly the kind of
data a LoRA would train on - same object, same light, only the facing changes - which
is the thing no diffusion model could give us.

It is NOT a production renderer. It exists to show the pipeline shape and to produce
test data. A real run would swap this for Blender with proper meshes.

Usage: mesh_render.py <out_dir> [--size 64] [--facings 8]
"""
import math
import os
import sys

import numpy as np
from PIL import Image

# A blocky building: (x, y, z) centre of the box base, (sx, sy, sz) size, colour.
# All units are arbitrary "tiles"; a RA building is roughly 2x2 tiles wide.
BOXES = [
    # main hall
    dict(c=(0.0, 0.0, 0.0), s=(2.4, 1.8, 1.0), col=(150, 40, 40)),
    # roof slab
    dict(c=(0.0, 0.0, 1.0), s=(2.6, 2.0, 0.18), col=(185, 55, 55)),
    # side annex
    dict(c=(1.1, 0.6, 0.0), s=(1.0, 0.9, 0.55), col=(120, 32, 32)),
    # a stack
    dict(c=(-0.8, -0.5, 1.1), s=(0.30, 0.30, 0.75), col=(90, 90, 95)),
    # door block
    dict(c=(0.0, -0.95, 0.0), s=(0.7, 0.15, 0.45), col=(60, 60, 65)),
]

LIGHT = np.array([-0.4, -0.5, 0.75])
LIGHT = LIGHT / np.linalg.norm(LIGHT)


def box_faces(cx, cy, cz, sx, sy, sz):
    """8 vertices + 6 quads for an axis-aligned box sitting on z=cz."""
    hx, hy, hz = sx / 2, sy / 2, sz / 2
    v = np.array([
        [cx - hx, cy - hy, cz], [cx + hx, cy - hy, cz],
        [cx + hx, cy + hy, cz], [cx - hx, cy + hy, cz],
        [cx - hx, cy - hy, cz + sz], [cx + hx, cy - hy, cz + sz],
        [cx + hx, cy + hy, cz + sz], [cx - hx, cy + hy, cz + sz],
    ])
    quads = [(0, 1, 2, 3), (4, 5, 6, 7), (0, 1, 5, 4),
             (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    return v, quads


def rot_z(pts, ang):
    c, s = math.cos(ang), math.sin(ang)
    m = np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])
    return pts @ m.T


def project(pts, tilt_deg, scale, cx, cy, size):
    """Orthographic projection with a fixed camera tilt; yaw is applied before this."""
    t = math.radians(tilt_deg)
    # camera looks down at `tilt` from the +y side: squash y by cos(t), keep z vertical
    sx = pts[:, 0] * scale
    sy = (pts[:, 1] * math.cos(t) - pts[:, 2] * math.sin(t)) * scale
    return np.stack([sx + cx, sy + cy], axis=1)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    size = 64
    facings = 8
    for i, a in enumerate(sys.argv):
        if a == "--size" and i + 1 < len(sys.argv):
            size = int(sys.argv[i + 1])
        if a == "--facings" and i + 1 < len(sys.argv):
            facings = int(sys.argv[i + 1])
    os.makedirs(out, exist_ok=True)

    tilt = 35.0
    scale = size / 5.0
    cx = cy = size / 2

    for f in range(facings):
        yaw = 2 * math.pi * f / facings
        img = np.zeros((size, size, 3), dtype=np.float32)
        zbuf = np.full((size, size), -1e9, dtype=np.float32)

        for b in BOXES:
            v, quads = box_faces(b["c"][0], b["c"][1], b["c"][2], *b["s"])
            v = rot_z(v, yaw)
            base_col = np.array(b["col"], dtype=np.float32)
            for q in quads:
                tri = [v[q[0]], v[q[1]], v[q[2]]]
                n = np.cross(tri[1] - tri[0], tri[2] - tri[0])
                ln = np.linalg.norm(n)
                if ln < 1e-9:
                    continue
                n = n / ln
                if n[2] < 0:                    # back-face cull (z up)
                    n = -n
                shade = 0.45 + 0.55 * max(0.0, float(np.dot(n, LIGHT)))
                col = np.clip(base_col * shade, 0, 255)

                pts = project(v[list(q)], tilt, scale, cx, cy, size)
                # rasterise the quad as two triangles with a depth buffer
                for t_idx in ((0, 1, 2), (0, 2, 3)):
                    p0, p1, p2 = (pts[i] for i in t_idx)
                    minx = max(0, int(math.floor(min(p0[0], p1[0], p2[0]))))
                    maxx = min(size - 1, int(math.ceil(max(p0[0], p1[0], p2[0]))))
                    miny = max(0, int(math.floor(min(p0[1], p1[1], p2[1]))))
                    maxy = min(size - 1, int(math.ceil(max(p0[1], p1[1], p2[1]))))
                    if minx > maxx or miny > maxy:
                        continue
                    d = (p1[0] - p0[0]) * (p2[1] - p0[1]) - (p2[0] - p0[0]) * (p1[1] - p0[1])
                    if abs(d) < 1e-9:
                        continue
                    for y in range(miny, maxy + 1):
                        for x in range(minx, maxx + 1):
                            px, py = x + 0.5, y + 0.5
                            w0 = ((p1[0] - px) * (p2[1] - py) - (p2[0] - px) * (p1[1] - py)) / d
                            w1 = ((p2[0] - px) * (p0[1] - py) - (p0[0] - px) * (p2[1] - py)) / d
                            w2 = 1.0 - w0 - w1
                            if w0 < 0 or w1 < 0 or w2 < 0:
                                continue
                            z = w0 * v[q[t_idx[0]]][2] + w1 * v[q[t_idx[1]]][2] + w2 * v[q[t_idx[2]]][2]
                            if z <= zbuf[y, x]:
                                continue
                            zbuf[y, x] = z
                            img[y, x] = col

        rgb = np.clip(img, 0, 255).astype(np.uint8)
        Image.fromarray(rgb, "RGB").save(os.path.join(out, f"facing_{f}.png"))

    print(f"rendered {facings} facings at {size}x{size} -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
