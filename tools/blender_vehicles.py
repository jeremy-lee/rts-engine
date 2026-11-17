"""Procedurally build military vehicles spanning WW1 to the Cold War, and render facings.

Why not download: there is no usable pool of freely-licensed military 3D models. OpenGameArt
has four (already used), Sketchfab/3dsearch sit behind logins or Cloudflare, and the STL
sites are 3D-print meshes with no UVs or materials. Procedural geometry is the only route
that scales, and for 24-pixel RTS sprites it is also the right level of detail - a
high-poly model loses everything on the way down.

Each vehicle is an assembly of boxes/cylinders/cones with era-appropriate proportions:
  WW1      boxy rhomboid tanks, biplanes with double wings, airships
  WW2      sloped armour, turrets with long guns, prop fighters, half-tracks, trucks
  Cold War rounded turrets, jets with swept wings, helicopters, missile launchers

Usage:
  blender --background --python blender_vehicles.py -- <out_dir> [--facings 32] [--size 512]
                                                        [--variants 2] [--only name,...]
"""
import math
import os
import sys

import bpy
from mathutils import Vector

def s2l(c):
    """sRGB 0..1 -> linear 0..1.

    Blender's Base Color socket expects LINEAR values, but palette colours are always
    quoted as sRGB. Feeding sRGB numbers straight in renders them washed out: sRGB
    (247,215,121) written as linear (0.97,0.84,0.47) comes back out of the renderer as
    roughly (250,237,183) - the blue channel nearly doubles, which is exactly why our
    tanks kept looking like pale cream instead of RA's saturated sand.
    """
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def srgb(r, g, b):
    return (s2l(r), s2l(g), s2l(b))


MATS = {}
# Colours taken from the actual RA tank sprite (2tnk-0000.png) rather than guessed.
# Measured distribution, top entries by frequency:
#   247,215,121 (highlight)  215,190,105  170,154,85 (mid)  134,113,56  85,77,36 (shadow)
# Note how BRIGHT the highlight is - my earlier palette averaged 0.62/0.52/0.28 which is
# 40% darker than the real thing, and that is why the in-game tank looked muddy.
# sRGB values, converted to linear at use time so what we ask for is what renders.
WALL_COLOURS = [
    ("ra_sand", srgb(0.97, 0.84, 0.47), srgb(0.62, 0.53, 0.26)),
    ("ra_grey", srgb(0.82, 0.82, 0.84), srgb(0.46, 0.46, 0.50)),
    ("ra_olive", srgb(0.70, 0.74, 0.38), srgb(0.42, 0.45, 0.21)),
    ("ra_red", srgb(0.85, 0.24, 0.20), srgb(0.44, 0.10, 0.08)),
    ("ra_bone", srgb(0.90, 0.86, 0.72), srgb(0.52, 0.49, 0.38)),
]
WALL_PICK = 0


def mat(key, rgb, rough=0.8, metal=0.0):
    if key in MATS:
        return MATS[key]
    m = bpy.data.materials.new(key)
    m.use_nodes = True
    b = m.node_tree.nodes.get("Principled BSDF")
    b.inputs["Base Color"].default_value = (*rgb, 1.0)
    b.inputs["Roughness"].default_value = rough
    if "Metallic" in b.inputs:
        b.inputs["Metallic"].default_value = metal
    MATS[key] = m
    return m


BODY = lambda: mat("body", WALL_COLOURS[WALL_PICK][1])
BODY_D = lambda: mat("body_d", WALL_COLOURS[WALL_PICK][2])
METAL = lambda: mat("metal", srgb(0.52, 0.52, 0.55), 0.45, 0.7)
TRACK = lambda: mat("track", srgb(0.30, 0.29, 0.27), 0.9)
GLASS = lambda: mat("glass", srgb(0.20, 0.28, 0.36), 0.25)
RUBBER = lambda: mat("rubber", srgb(0.16, 0.16, 0.16), 0.95)
ACCENT = lambda: mat("accent", srgb(0.95, 0.88, 0.45), 0.6)


def box(loc, size, material):
    bpy.ops.mesh.primitive_cube_add(size=2.0, location=loc)
    ob = bpy.context.active_object
    ob.scale = (size[0] / 2, size[1] / 2, size[2] / 2)
    ob.data.materials.append(material)
    return ob


def boxr(loc, size, material, rot):
    ob = box(loc, size, material)
    ob.rotation_euler = rot
    return ob


def cyl(loc, r, d, material, rot=(0, 0, 0), verts=12):
    bpy.ops.mesh.primitive_cylinder_add(radius=r, depth=d, location=loc, rotation=rot, vertices=verts)
    ob = bpy.context.active_object
    ob.data.materials.append(material)
    return ob


def cone(loc, r1, r2, d, material, rot=(0, 0, 0), verts=12):
    bpy.ops.mesh.primitive_cone_add(radius1=r1, radius2=r2, depth=d, location=loc, rotation=rot, vertices=verts)
    ob = bpy.context.active_object
    ob.data.materials.append(material)
    return ob


def wing(side, root_x, span, chord_root, chord_tip, z, sweep_deg, thick, material, segs=4):
    """Trapezoidal swept wing built from segmented boxes.

    A single rotated box makes a RHOMBUS, not a wing: rotating a square by the sweep angle
    leaves four equal edges and the silhouette is a diamond. A real wing tapers - wide at
    the root, narrow at the tip - and sweeps back. This builds it as `segs` chordwise
    strips whose chord shrinks linearly from chord_root to chord_tip and whose x offset
    is shifted back by tan(sweep) * distance along the span, so the outline is a proper
    tapered, swept planform. side = -1 / +1 mirrors it.
    """
    p = []
    tan_sweep = math.tan(math.radians(sweep_deg))
    for i in range(segs):
        f0 = i / float(segs)
        f1 = (i + 1) / float(segs)
        fmid = 0.5 * (f0 + f1)
        ya = side * span * f0
        yb = side * span * f1
        chord = chord_root + (chord_tip - chord_root) * fmid
        xoff = -tan_sweep * span * fmid
        p.append(box((root_x + xoff, 0.5 * (ya + yb), z),
                     (chord, abs(yb - ya) + 0.02, thick), material))
    return p


def wheels(n, x0, y, z, r=0.28, step=0.55):
    out = []
    for i in range(n):
        out.append(cyl((x0 + i * step, y, z), r, 0.18, RUBBER(), rot=(math.radians(90), 0, 0)))
    return out


def tracks(length, y, z, h=0.3):
    out = []
    for side in (-1, 1):
        out.append(box((0, side * y, z), (length, 0.28, h), TRACK()))
    return out


# ------------------------------------------------------------------ tanks
def t_ww1_rhomboid():
    """WW1 rhomboid - tracks wrap the whole lozenge, sponson guns on the sides."""
    p = []
    p.append(box((0, 0, 0.66), (3.20, 1.50, 0.78), BODY()))
    p.append(boxr((1.05, 0, 0.86), (1.10, 1.30, 0.50), BODY(), (0, math.radians(28), 0)))
    p.append(box((-1.20, 0, 0.72), (0.90, 1.20, 0.44), BODY_D()))
    p.append(box((0.30, 0, 1.06), (1.55, 1.10, 0.06), BODY()))
    for side in (-1, 1):
        p.append(box((0, side * 1.02, 0.58), (3.90, 0.34, 0.94), TRACK()))
        for i in range(7):
            p.append(cyl((-1.55 + i * 0.52, side * 1.20, 0.36), 0.16, 0.10, METAL(),
                         rot=(math.radians(90), 0, 0), verts=12))
        for i in range(4):
            p.append(box((-1.20 + i * 0.80, side * 1.06, 1.12), (0.34, 0.12, 0.14), BODY_D()))
    for i in range(3):
        p.append(box((-1.45 + i * 0.20, 0, 1.00), (0.12, 1.30, 0.05), TRACK()))
    p.append(box((0.98, 0.62, 1.02), (0.52, 0.36, 0.34), BODY_D()))
    p.append(cyl((1.28, 0.62, 1.02), 0.08, 0.72, METAL(), rot=(0, math.radians(90), 0), verts=8))
    p.append(box((-0.60, -0.62, 1.06), (0.50, 0.34, 0.30), BODY_D()))
    p.append(cyl((-0.86, -0.62, 1.06), 0.07, 0.60, METAL(), rot=(0, math.radians(90), 0), verts=8))
    p.append(cyl((1.62, 0, 0.72), 0.10, 0.24, BODY_D(), rot=(0, math.radians(90), 0), verts=10))
    return p


def t_ww2_medium():
    """WW2 medium tank - cast turret, sloped glacis, full running gear."""
    p = []
    p += running_gear(2.95, 0.70, 0.28, r=0.22, n=5)
    p.append(boxr((0.98, 0, 0.56), (0.95, 1.40, 0.44), BODY(), (0, math.radians(38), 0)))
    p.append(box((-0.28, 0, 0.52), (1.95, 1.38, 0.46), BODY()))
    p.append(box((-1.28, 0, 0.50), (0.62, 1.34, 0.38), BODY_D()))
    p.append(box((0.14, 0, 0.74), (1.45, 1.20, 0.05), BODY()))
    p += rear_grille(-1.34, 0.62, 0.72, slats=3)
    for side in (-1, 1):
        p.append(boxr((0, side * 0.74, 0.58), (2.55, 0.14, 0.22), BODY(),
                      (math.radians(16) * side, 0, 0)))
    p += turret_dome(-0.08, 0, 0.76, 0.58, layers=4, height=0.40, segs=22)
    for side in (-1, 1):
        p.append(boxr((0.34, side * 0.28, 0.94), (0.36, 0.26, 0.22), BODY_D(),
                      (0, 0, side * math.radians(34))))
    p += gun_assembly(0.48, 0.94, 0.11, 1.70)
    p.append(box((-0.46, 0.36, 1.10), (0.24, 0.20, 0.14), BODY_D()))
    p.append(cyl((-0.40, 0.36, 1.14), 0.04, 0.50, METAL(), rot=(0, math.radians(70), 0), verts=6))
    return p


def t_ww2_heavy():
    """WW2 heavy tank - oversized turret, long 88, six wheels, heavy skirts."""
    p = []
    p += running_gear(3.60, 0.86, 0.32, r=0.24, n=6)
    p.append(boxr((1.16, 0, 0.62), (1.05, 1.70, 0.52), BODY(), (0, math.radians(40), 0)))
    p.append(box((-0.30, 0, 0.58), (2.30, 1.68, 0.52), BODY()))
    p.append(box((-1.58, 0, 0.56), (0.70, 1.64, 0.44), BODY_D()))
    p.append(box((0.20, 0, 0.84), (1.75, 1.46, 0.06), BODY()))
    p += rear_grille(-1.62, 0.76, 0.82, slats=4)
    for side in (-1, 1):
        p.append(box((-0.10, side * 1.02, 0.86), (0.85, 0.22, 0.24), BODY_D()))
        p.append(box((-1.30, side * 1.02, 0.86), (0.55, 0.22, 0.24), BODY_D()))
    p += turret_dome(-0.10, 0, 0.86, 0.72, layers=4, height=0.48, segs=24)
    for side in (-1, 1):
        p.append(boxr((0.46, side * 0.34, 1.08), (0.44, 0.30, 0.26), BODY_D(),
                      (0, 0, side * math.radians(36))))
    p += gun_assembly(0.62, 1.08, 0.12, 2.05)
    p.append(box((-0.56, 0.42, 1.26), (0.26, 0.22, 0.15), BODY_D()))
    p.append(cyl((-0.50, 0.42, 1.30), 0.045, 0.55, METAL(), rot=(0, math.radians(70), 0), verts=6))
    return p


def t_coldwar_mbt():
    """Cold-war MBT - cast dome, long 105, seven wheels, ERA blocks on the hull."""
    p = []
    p += running_gear(3.40, 0.78, 0.30, r=0.23, n=6)
    p.append(boxr((1.05, 0, 0.58), (1.00, 1.58, 0.48), BODY(), (0, math.radians(42), 0)))
    p.append(box((-0.32, 0, 0.54), (2.15, 1.54, 0.48), BODY()))
    p.append(box((-1.48, 0, 0.52), (0.66, 1.50, 0.40), BODY_D()))
    p.append(box((0.16, 0, 0.78), (1.60, 1.34, 0.05), BODY()))
    p += rear_grille(-1.52, 0.70, 0.76, slats=3)
    for side in (-1, 1):
        for i in range(4):
            p.append(box((-1.05 + i * 0.50, side * 0.82, 0.66), (0.40, 0.18, 0.20), BODY_D()))
    p += turret_dome(-0.10, 0, 0.80, 0.66, layers=5, height=0.42, segs=24)
    for side in (-1, 1):
        p.append(box((0.30, side * 0.44, 0.98), (0.42, 0.20, 0.18), BODY_D()))
    p += gun_assembly(0.52, 0.98, 0.115, 1.90)
    p.append(cyl((-0.60, 0.40, 1.16), 0.10, 0.24, BODY_D(), verts=10))
    p.append(box((-0.44, -0.40, 1.14), (0.26, 0.22, 0.16), BODY_D()))
    return p


def t_spg():
    p = []
    p.append(box((0, 0, 0.5), (3.4, 1.7, 0.5), BODY()))
    p += tracks(3.4, 0.95, 0.3)
    p.append(box((0.2, 0, 1.0), (1.6, 1.2, 0.5), BODY_D()))
    p.append(cyl((1.0, 0, 1.0), 0.09, 2.4, METAL(), rot=(0, math.radians(88), 0)))
    p.append(box((1.9, 0, 1.2), (0.5, 0.6, 0.4), METAL()))
    return p


def t_aa():
    p = []
    p.append(box((0, 0, 0.5), (2.2, 1.4, 0.5), BODY()))
    p += tracks(2.2, 0.8, 0.3)
    p.append(cyl((0, 0, 1.0), 0.45, 0.3, BODY_D(), verts=12))
    for dx in (-0.12, 0.12):
        p.append(cyl((0.5, dx, 1.14), 0.045, 1.5, METAL(), rot=(0, math.radians(65), 0)))
    return p


# ------------------------------------------------------------------ aircraft
def a_ww1_biplane():
    """WW1 biplane: stepped fuselage, glazed cockpit, twin wings with struts, gear."""
    p = []
    p += fuselage3(-1.35, 1.30, 0.10, 0.24, 0.10, 0.62, segs=12)
    p += canopy_glazed(0.10, 0.78, 0.40, 0.30, 0.22, frames=1)
    p.append(box((0.05, 0, 1.18), (1.45, 3.10, 0.055), BODY_D()))
    p.append(box((0.05, 0, 0.12), (1.45, 3.10, 0.055), BODY_D()))
    for side in (-1, 1):
        for x in (-0.55, 0.58):
            p.append(box((x, side * 1.00, 0.65), (0.14, 0.14, 1.06), METAL()))
            p.append(box((x, side * 1.42, 0.65), (0.14, 0.14, 1.06), METAL()))
    p += tail_unit(-1.42, 0.66, 1.40, 0.55, chord=0.40)
    p += propeller(1.36, 0.62, 0.78, blades=2, thickness=0.10)
    p += landing_gear(0.10, 0.55, 0.42, strut=0.30, wheel_r=0.13)
    return p


def a_ww2_fighter():
    """WW2 fighter: tapered fuselage, bubble canopy, tapered wings, four-blade prop."""
    p = []
    p += fuselage3(-1.45, 1.62, 0.12, 0.28, 0.10, 0.52, segs=14)
    p += canopy_glazed(0.42, 0.74, 0.62, 0.38, 0.26, frames=2)
    for side in (-1, 1):
        p += wing(side, 0.10, 1.75, 0.98, 0.40, 0.50, 12.0, 0.09, BODY_D())
        p += wing_root_fairing(side, 0.10, 0.30, 0.50, 0.70, 0.09)
    p += tail_unit(-1.42, 0.56, 1.30, 0.72, chord=0.44)
    p += propeller(1.70, 0.52, 0.86, blades=4)
    p += landing_gear(0.05, 0.58, 0.38, strut=0.30, wheel_r=0.12)
    for side in (-1, 1):
        p.append(cyl((0.10, side * 1.30, 0.46), 0.09, 0.55, BODY_D(),
                     rot=(0, math.radians(90), 0), verts=8))
    return p


def a_jet():
    """Early jet: pointed nose with intake, swept wings, bubble canopy, gear."""
    p = []
    p += fuselage3(-1.70, 1.85, 0.10, 0.30, 0.12, 0.52, segs=16)
    p += canopy_glazed(0.55, 0.76, 0.72, 0.42, 0.28, frames=2)
    for side in (-1, 1):
        p += wing(side, -0.20, 1.80, 1.05, 0.46, 0.48, 22.0, 0.08, BODY_D())
        p += wing_root_fairing(side, -0.20, 0.32, 0.50, 0.80, 0.08)
    p += tail_unit(-1.52, 0.54, 1.45, 0.82, chord=0.46)
    p.append(cyl((-1.82, 0, 0.52), 0.20, 0.20, TRACK(), rot=(0, math.radians(90), 0), verts=12))
    p += landing_gear(0.10, 0.60, 0.36, strut=0.30, wheel_r=0.12)
    return p


def a_heli():
    """Utility helicopter: boxy cabin, stepped boom, thick dark rotor, tail rotor."""
    p = []
    p.append(box((0.10, 0, 0.62), (2.30, 0.92, 0.72), BODY()))
    p.append(box((1.30, 0, 0.70), (0.55, 0.72, 0.58), GLASS()))
    p.append(box((1.34, 0, 1.02), (0.42, 0.66, 0.10), BODY_D()))
    p.append(cyl((1.66, 0, 0.52), 0.26, 0.28, BODY_D(),
                 rot=(0, math.radians(90), 0), verts=12))
    p.append(cone((-1.85, 0, 0.66), 0.10, 0.26, 1.30, BODY(),
                  rot=(0, math.radians(-90), 0), verts=12))
    for side in (-1, 1):
        p.append(box((-0.25, side * 0.52, 0.34), (1.40, 0.10, 0.12), METAL()))
    p.append(box((-1.20, 0, 0.94), (0.70, 0.10, 0.34), BODY_D()))
    p.append(cyl((-0.10, 0, 1.08), 0.12, 0.32, METAL(), verts=10))
    p.append(box((-0.10, 0, 1.26), (0.28, 3.60, 0.14), TRACK()))
    p.append(box((-0.10, 0, 1.26), (3.60, 0.28, 0.14), TRACK()))
    p.append(cyl((-0.10, 0, 1.26), 0.17, 0.16, METAL(), verts=10))
    p.append(box((-2.62, 0, 0.98), (0.10, 0.74, 0.74), BODY()))
    p.append(box((-2.66, 0, 0.98), (0.12, 0.12, 0.98), TRACK()))
    p += landing_gear(-0.10, 0.55, 0.34, strut=0.26, wheel_r=0.13)
    return p


def v_truck():
    p = []
    p.append(box((0.7, 0, 0.55), (1.1, 1.1, 0.7), BODY()))
    p.append(box((0.7, 0, 0.94), (0.5, 0.9, 0.16), BODY_D()))
    p.append(box((-0.5, 0, 0.66), (1.9, 1.25, 0.5), BODY_D()))
    p.append(box((-0.5, 0, 1.18), (1.95, 1.3, 0.1), BODY()))
    p.append(box((1.05, 0, 0.78), (0.42, 0.9, 0.3), GLASS()))
    p += wheels(2, -0.15, 0.72, 0.26, 0.26, 0.7)
    p += wheels(2, -0.15, -0.72, 0.26, 0.26, 0.7)
    p += wheels(2, 0.85, 0.72, 0.26, 0.26, 0.0)
    p += wheels(2, 0.85, -0.72, 0.26, 0.26, 0.0)
    return p


def v_halftrack():
    p = []
    p.append(box((0.6, 0, 0.55), (1.2, 1.2, 0.7), BODY()))
    p.append(box((0.6, 0, 0.95), (0.5, 0.95, 0.16), BODY_D()))
    p.append(box((-0.7, 0, 0.6), (2.0, 1.3, 0.45), BODY_D()))
    p.append(box((1.0, 0, 0.8), (0.4, 0.95, 0.3), GLASS()))
    for side in (-1, 1):
        p.append(box((-0.8, side * 0.8, 0.3), (2.0, 0.3, 0.42), TRACK()))
    p += wheels(2, 0.55, 0.78, 0.28, 0.27, 0.0)
    return p


def v_apc():
    p = []
    p.append(box((0, 0, 0.6), (2.8, 1.5, 0.8), BODY()))
    p.append(box((0.1, 0, 1.02), (2.2, 1.35, 0.1), BODY_D()))
    p.append(cyl((0.4, 0, 1.16), 0.3, 0.3, BODY_D(), verts=12))
    p.append(cyl((0.75, 0, 1.16), 0.045, 0.7, METAL(), rot=(0, math.radians(90), 0)))
    p += wheels(4, -1.0, 0.82, 0.3, 0.3, 0.62)
    p += wheels(4, -1.0, -0.82, 0.3, 0.3, 0.62)
    return p


def v_mlrs():
    p = []
    p.append(box((0.5, 0, 0.55), (1.4, 1.3, 0.7), BODY()))
    p.append(box((0.9, 0, 0.92), (0.5, 0.95, 0.16), BODY_D()))
    p.append(box((-0.7, 0, 0.62), (2.0, 1.4, 0.4), BODY_D()))
    p.append(boxr((-0.8, 0, 1.15), (1.7, 1.2, 0.5), METAL(), (0, math.radians(-18), 0)))
    for i in range(4):
        p.append(cyl((-1.4 + i * 0.32, 0, 1.3), 0.07, 1.0, BODY_D(), rot=(0, math.radians(72), 0)))
    p += wheels(3, -1.1, 0.78, 0.3, 0.3, 0.72)
    p += wheels(3, -1.1, -0.78, 0.3, 0.3, 0.72)
    return p


def v_jeep():
    p = []
    p.append(box((0, 0, 0.42), (1.5, 0.95, 0.3), BODY()))
    p.append(box((-0.15, 0, 0.66), (0.8, 0.85, 0.2), BODY_D()))
    p.append(box((0.5, 0, 0.6), (0.5, 0.8, 0.16), GLASS()))
    p.append(box((0.05, 0, 0.83), (0.06, 1.1, 0.06), METAL()))
    p += wheels(2, -0.5, 0.55, 0.26, 0.26, 0.0)
    p += wheels(2, -0.5, -0.55, 0.26, 0.26, 0.0)
    return p



# ------------------------------------------------------------------ expansion set
def t_light_tank():
    """Light tank - compact dome, short 76, four wheels, no skirts."""
    p = []
    p += running_gear(2.15, 0.54, 0.24, r=0.17, n=4, skirt=False)
    p.append(boxr((0.66, 0, 0.44), (0.70, 1.06, 0.32), BODY(), (0, math.radians(32), 0)))
    p.append(box((-0.28, 0, 0.42), (1.55, 1.06, 0.34), BODY()))
    p.append(box((-0.94, 0, 0.40), (0.50, 1.02, 0.28), BODY_D()))
    p.append(box((0.08, 0, 0.60), (1.15, 0.98, 0.04), BODY()))
    p += rear_grille(-1.00, 0.50, 0.58, slats=2)
    p += turret_dome(-0.06, 0, 0.60, 0.46, layers=3, height=0.30, segs=18)
    p += gun_assembly(0.34, 0.74, 0.09, 1.25)
    p.append(box((-0.36, 0.30, 0.88), (0.20, 0.16, 0.12), BODY_D()))
    return p


def t_super_heavy():
    """Super-heavy tank - huge turret, very long gun, eight wheels, twin exhausts."""
    p = []
    p += running_gear(4.30, 1.00, 0.36, r=0.27, n=7)
    p.append(boxr((1.36, 0, 0.70), (1.15, 1.95, 0.60), BODY(), (0, math.radians(42), 0)))
    p.append(box((-0.34, 0, 0.66), (2.70, 1.92, 0.60), BODY()))
    p.append(box((-1.78, 0, 0.62), (0.80, 1.86, 0.48), BODY_D()))
    p.append(box((0.26, 0, 0.96), (2.05, 1.66, 0.06), BODY()))
    for i in range(4):
        p.append(box((-1.90 + i * 0.22, 0, 0.92), (0.14, 1.40, 0.05), TRACK()))
    for dy in (-0.62, 0.62):
        p.append(cyl((-1.86, dy, 0.84), 0.11, 0.36, BODY_D(),
                     rot=(0, math.radians(90), 0), verts=10))
    p += turret_dome(-0.14, 0, 0.98, 0.86, layers=5, height=0.56, segs=26)
    for side in (-1, 1):
        p.append(boxr((0.54, side * 0.40, 1.22), (0.52, 0.34, 0.30), BODY_D(),
                      (0, 0, side * math.radians(34))))
    p += gun_assembly(0.74, 1.24, 0.14, 2.40)
    p.append(box((-0.68, 0.50, 1.44), (0.30, 0.24, 0.17), BODY_D()))
    p.append(cyl((-0.60, 0.50, 1.48), 0.05, 0.60, METAL(), rot=(0, math.radians(70), 0), verts=6))
    return p


def t_spaag():
    p = []
    p.append(box((0, 0, 0.46), (2.6, 1.5, 0.5), BODY()))
    p += tracks(2.6, 0.90, 0.30, 0.30)
    p.append(cyl((0, 0, 0.86), 0.72, 0.28, BODY_D(), verts=14))
    p.append(box((0, 0, 1.06), (1.0, 1.0, 0.42), BODY()))
    # twin autocannon angled up
    for dy in (-0.22, 0.22):
        p.append(cyl((0.72, dy, 1.30), 0.08, 1.5, METAL(),
                     rot=(0, math.radians(62), 0), verts=6))
    p.append(box((-0.5, 0, 1.28), (0.35, 0.7, 0.22), BODY_D()))   # radar
    return p


def t_recon():
    p = []
    p.append(box((0, 0, 0.44), (2.3, 1.3, 0.42), BODY()))
    p.append(boxr((0.85, 0, 0.68), (0.8, 1.1, 0.34), BODY(), (0, math.radians(22), 0)))
    p.append(box((-0.2, 0, 0.72), (0.6, 1.0, 0.24), GLASS()))
    p += wheels(2, -0.75, 0.66, 0.26, 0.26, 0.0)
    p += wheels(2, -0.75, -0.66, 0.26, 0.26, 0.0)
    p += wheels(2, 0.70, 0.66, 0.24, 0.24, 0.0)
    p += wheels(2, 0.70, -0.66, 0.24, 0.24, 0.0)
    p.append(box((-1.05, 0, 1.00), (0.06, 0.9, 0.06), METAL()))     # antenna
    p.append(cyl((1.15, 0, 0.55), 0.16, 0.2, ACCENT(), rot=(0, math.radians(90), 0), verts=6))
    return p


def t_engineer():
    """Dozer blade + hydraulic rams, sized to survive at 24px.

    First version failed because the blade was a thin plate seen nearly edge-on at most
    facings - it read as a plain box. The blade is now tall and wide with a steep rake, it
    is painted in the DARK tone so it separates from the bright hull, and two angled rams
    connect it to the body so there is structure even when the blade is foreshortened.
    """
    p = []
    p.append(box((0, 0, 0.52), (2.5, 1.4, 0.5), BODY()))
    p.append(box((0, 0, 0.30), (2.6, 1.46, 0.14), BODY_D()))
    p += tracks(2.4, 0.82, 0.28, 0.30)
    p.append(box((-0.35, 0, 0.86), (0.95, 1.05, 0.44), BODY()))
    p.append(box((-0.35, 0, 0.92), (0.72, 0.9, 0.2), GLASS()))
    p.append(box((-1.05, 0, 0.82), (0.4, 0.55, 0.3), BODY_D()))
    # blade: tall, wide, strongly raked, dark so it reads against the bright hull
    p.append(boxr((1.55, 0, 0.55), (0.26, 2.15, 1.05), BODY_D(), (0, math.radians(30), 0)))
    p.append(box((1.32, 0, 0.10), (0.45, 2.15, 0.16), METAL()))     # cutting edge
    # hydraulic rams from hull to blade - visible from every facing
    for dy in (-0.52, 0.52):
        p.append(cyl((1.02, dy, 0.72), 0.09, 0.95, METAL(),
                     rot=(0, math.radians(52), 0), verts=6))
    # exhaust stack + roof beacon so the rear is not a blank slab either
    p.append(cyl((-1.15, 0.45, 1.02), 0.09, 0.55, BODY_D(), verts=6))
    p.append(cyl((-0.35, 0, 1.14), 0.13, 0.10, ACCENT(), verts=8))
    return p


def t_sam():
    p = []
    p.append(box((0.3, 0, 0.48), (2.6, 1.35, 0.5), BODY()))
    p.append(box((1.05, 0, 0.74), (0.6, 1.0, 0.28), GLASS()))
    p += wheels(3, -0.95, 0.72, 0.28, 0.28, 0.72)
    p += wheels(3, -0.95, -0.72, 0.28, 0.28, 0.72)
    # launch rails raised at 45 deg, with two big missiles
    p.append(boxr((-0.55, 0, 1.10), (1.5, 1.1, 0.24), BODY_D(), (0, math.radians(-28), 0)))
    for dy in (-0.34, 0.34):
        p.append(cyl((-0.9, dy, 1.36), 0.14, 1.6, METAL(),
                     rot=(0, math.radians(62), 0), verts=8))
        p.append(cone((-1.60, dy, 1.78), 0.14, 0.02, 0.34, ACCENT(),
                      rot=(0, math.radians(62), 0), verts=8))
    return p


def t_transport():
    p = []
    p.append(box((1.05, 0, 0.58), (0.9, 1.15, 0.62), BODY()))
    p.append(box((1.05, 0, 0.94), (0.5, 0.95, 0.14), BODY_D()))
    p.append(box((1.20, 0, 0.78), (0.4, 0.95, 0.28), GLASS()))
    # canvas tilt over the cargo bed
    p.append(box((-0.55, 0, 0.86), (1.9, 1.30, 0.62), BODY_D()))
    p.append(box((-0.55, 0, 1.18), (1.95, 1.34, 0.10), BODY()))
    for i in range(3):
        p.append(cyl((-1.25 + i * 0.7, 0, 1.20), 0.07, 1.32, BODY(),
                     rot=(math.radians(90), 0, 0), verts=6))
    p += wheels(2, -1.15, 0.74, 0.27, 0.27, 0.72)
    p += wheels(2, -1.15, -0.74, 0.27, 0.27, 0.72)
    p += wheels(2, 0.85, 0.74, 0.27, 0.27, 0.0)
    p += wheels(2, 0.85, -0.74, 0.27, 0.27, 0.0)
    return p


def t_command_car():
    p = []
    p.append(box((0, 0, 0.50), (2.4, 1.35, 0.5), BODY()))
    p.append(box((-0.25, 0, 0.86), (1.3, 1.15, 0.44), BODY()))
    p.append(box((0.75, 0, 0.76), (0.55, 1.0, 0.30), GLASS()))
    p += wheels(2, -0.80, 0.70, 0.27, 0.27, 0.0)
    p += wheels(2, -0.80, -0.70, 0.27, 0.27, 0.0)
    p += wheels(2, 0.75, 0.70, 0.27, 0.27, 0.0)
    p += wheels(2, 0.75, -0.70, 0.27, 0.27, 0.0)
    # whip antenna + rotating dish: unmistakable command silhouette
    p.append(box((-0.95, 0, 1.35), (0.06, 0.06, 1.0), METAL()))
    p.append(cyl((-0.25, 0, 1.12), 0.30, 0.10, BODY_D(), verts=12))
    return p


# ------------------------------------------------------------------ aircraft set
def a_heavy_bomber():
    """Twin-engine bomber: long stepped fuselage, glazed nose, big podded engines."""
    p = []
    p += fuselage3(-2.30, 2.40, 0.16, 0.36, 0.12, 0.66, segs=16)
    p += canopy_glazed(1.55, 0.88, 0.72, 0.44, 0.26, frames=2)
    for side in (-1, 1):
        p += wing(side, -0.10, 2.20, 1.30, 0.55, 0.60, 30.0, 0.11, BODY_D())
        p += wing_root_fairing(side, -0.10, 0.40, 0.62, 0.95, 0.11)
        p += engine_nacelle(-0.20, side * 1.45, 0.58, 0.34, 1.05)
    p += tail_unit(-2.10, 0.70, 1.90, 0.95, chord=0.52)
    p += landing_gear(0.30, 0.72, 0.44, strut=0.36, wheel_r=0.15)
    return p


def a_recon_plane():
    """Unarmed recon: slim fuselage, long glazed cabin, high aspect-ratio wing."""
    p = []
    p += fuselage3(-1.60, 1.70, 0.10, 0.22, 0.08, 0.54, segs=14)
    p += canopy_glazed(0.42, 0.74, 0.90, 0.34, 0.22, frames=3)
    for side in (-1, 1):
        p += wing(side, 0.10, 2.10, 0.80, 0.42, 0.50, 6.0, 0.07, BODY_D())
        p += wing_root_fairing(side, 0.10, 0.26, 0.52, 0.60, 0.07)
    p += tail_unit(-1.55, 0.58, 1.35, 0.70, chord=0.42)
    p += propeller(1.76, 0.54, 0.70, blades=3)
    p += landing_gear(0.10, 0.52, 0.36, strut=0.28, wheel_r=0.11)
    return p


def a_seaplane():
    """Floatplane: stepped fuselage, glazed canopy, big outboard floats on struts."""
    p = []
    p += fuselage3(-1.60, 1.75, 0.12, 0.28, 0.10, 0.72, segs=14)
    p += canopy_glazed(0.48, 0.94, 0.66, 0.38, 0.24, frames=2)
    for side in (-1, 1):
        p += wing(side, 0.05, 1.85, 0.95, 0.44, 0.48, 10.0, 0.09, BODY_D())
        p += wing_root_fairing(side, 0.05, 0.30, 0.70, 0.70, 0.09)
        p.append(cyl((0.10, side * 1.15, 0.22), 0.36, 2.40, BODY_D(),
                     rot=(0, math.radians(90), 0), verts=12))
        p.append(cone((1.35, side * 1.15, 0.22), 0.36, 0.10, 0.50, BODY_D(),
                      rot=(0, math.radians(90), 0), verts=12))
        p.append(box((0.45, side * 1.15, 0.58), (0.20, 0.20, 0.72), METAL()))
        p.append(box((-0.55, side * 1.15, 0.58), (0.20, 0.20, 0.72), METAL()))
    p += tail_unit(-1.55, 0.78, 1.40, 0.72, chord=0.44)
    p += propeller(1.82, 0.72, 0.76, blades=3)
    return p


def a_transport_plane():
    """Transport: deep slab fuselage, high wing, twin engines, loading ramp at the tail."""
    p = []
    p.append(box((0.10, 0, 0.78), (3.60, 0.70, 0.74), BODY()))
    p.append(cone((2.05, 0, 0.78), 0.20, 0.35, 0.50, BODY_D(),
                  rot=(0, math.radians(90), 0), verts=14))
    p.append(box((2.10, 0, 1.00), (0.38, 0.44, 0.22), GLASS()))
    p.append(box((-1.55, 0, 0.68), (0.55, 0.66, 0.50), BODY_D()))
    p += canopy_glazed(1.72, 0.96, 0.34, 0.42, 0.18, frames=1)
    for side in (-1, 1):
        p += wing(side, 0.05, 2.40, 1.20, 0.50, 0.50, 4.0, 0.10, BODY_D())
        p += engine_nacelle(0.15, side * 1.45, 0.62, 0.30, 0.90)
    p += tail_unit(-1.60, 0.80, 2.10, 1.00, chord=0.50)
    p += landing_gear(0.10, 0.70, 0.50, strut=0.40, wheel_r=0.16)
    return p


def a_ground_attack():
    """Ground-attack: armoured nose, chin gun, underwing stores, straight wing."""
    p = []
    p += fuselage3(-1.60, 1.75, 0.20, 0.34, 0.12, 0.56, segs=14)
    p += canopy_glazed(0.55, 0.82, 0.66, 0.40, 0.26, frames=2)
    for side in (-1, 1):
        p += wing(side, -0.10, 1.90, 1.02, 0.48, 0.50, 12.0, 0.10, BODY_D())
        p += wing_root_fairing(side, -0.10, 0.34, 0.54, 0.85, 0.10)
        for i in range(2):
            p.append(cyl((-0.20 - i * 0.55, side * 1.05, 0.40), 0.10, 0.60, BODY_D(),
                         rot=(0, math.radians(90), 0), verts=8))
    p.append(cyl((1.92, 0, 0.40), 0.11, 0.80, METAL(), rot=(0, math.radians(90), 0), verts=8))
    p.append(box((1.55, 0, 0.34), (0.30, 0.24, 0.20), BODY_D()))
    p += tail_unit(-1.55, 0.60, 1.35, 0.80, chord=0.46)
    p += propeller(1.80, 0.56, 0.80, blades=4)
    p += landing_gear(0.10, 0.58, 0.38, strut=0.30, wheel_r=0.12)
    return p


def a_jet_bomber():
    """Swept-wing jet bomber: slim fuselage, podded engines on the wing, glazed nose."""
    p = []
    p += fuselage3(-2.10, 2.15, 0.14, 0.32, 0.12, 0.60, segs=16)
    p += canopy_glazed(1.45, 0.86, 0.60, 0.40, 0.24, frames=2)
    for side in (-1, 1):
        p += wing(side, -0.30, 2.00, 1.20, 0.52, 0.58, 32.0, 0.10, BODY_D())
        p += wing_root_fairing(side, -0.30, 0.40, 0.58, 0.95, 0.10)
        p += engine_nacelle(-0.55, side * 1.35, 0.44, 0.24, 0.85)
    p += tail_unit(-1.85, 0.64, 1.75, 0.88, chord=0.50)
    p += landing_gear(0.20, 0.66, 0.42, strut=0.34, wheel_r=0.14)
    return p


def a_vtol():
    """Tilt-rotor VTOL: boxy fuselage, two tilt nacelles with thick dark rotors."""
    p = []
    p.append(box((0.10, 0, 0.62), (2.60, 0.88, 0.62), BODY()))
    p.append(box((1.45, 0, 0.70), (0.62, 0.70, 0.50), GLASS()))
    p.append(box((0.05, 0, 0.98), (1.35, 0.34, 0.12), BODY_D()))
    p.append(cone((-1.60, 0, 0.64), 0.14, 0.26, 0.75, BODY(),
                  rot=(0, math.radians(-90), 0), verts=12))
    for side in (-1, 1):
        p.append(box((0.22, side * 0.95, 0.66), (0.55, 1.10, 0.14), BODY_D()))
        p.append(cyl((0.22, side * 1.38, 0.80), 0.13, 0.32, METAL(), verts=10))
        p.append(box((0.22, side * 1.38, 1.00), (0.16, 1.9, 0.12), TRACK()))
        p.append(box((0.22, side * 1.38, 1.00), (1.9, 0.16, 0.12), TRACK()))
    p += tail_unit(-1.60, 0.78, 1.55, 0.72, chord=0.44)
    p += landing_gear(-0.10, 0.50, 0.34, strut=0.24, wheel_r=0.12)
    return p


def a_zeppelin_air():
    """Airship: banded envelope, large cruciform tail, podded engines, glazed gondola."""
    p = []
    p.append(cyl((0, 0, 0.95), 0.56, 4.40, BODY(), rot=(0, math.radians(90), 0), verts=20))
    p.append(cone((2.60, 0, 0.95), 0.12, 0.56, 0.85, BODY_D(),
                  rot=(0, math.radians(90), 0), verts=20))
    p.append(cone((-2.60, 0, 0.95), 0.10, 0.56, 0.85, BODY(),
                  rot=(0, math.radians(-90), 0), verts=20))
    for x in (-1.75, -0.85, 0.05, 0.95, 1.85):
        p.append(cyl((x, 0, 0.95), 0.59, 0.12, BODY_D(),
                     rot=(0, math.radians(90), 0), verts=20))
    p.append(box((-2.05, 0, 0.95), (1.00, 3.40, 0.12), BODY_D()))
    p.append(box((-2.05, 0, 0.95), (1.00, 0.12, 2.60), BODY_D()))
    p.append(box((-2.55, 0, 0.95), (0.30, 1.50, 0.10), ACCENT()))
    for dy in (-0.75, 0.75):
        p.append(box((-0.30, dy, 0.42), (0.85, 0.34, 0.30), BODY_D()))
        p.append(cyl((0.20, dy, 0.42), 0.15, 0.24, METAL(),
                     rot=(0, math.radians(90), 0), verts=8))
    p.append(box((0.55, 0, 0.30), (1.35, 0.60, 0.36), BODY_D()))
    p.append(box((0.95, 0, 0.34), (0.42, 0.52, 0.22), GLASS()))
    p.append(box((0.55, 0, 0.10), (1.00, 0.45, 0.12), METAL()))
    return p


def p_type59():
    """Type 59 with a full detail pass: layered dome turret, sloped armour, grilles.

    The earlier version was a handful of plain boxes, so at 32x36 it carried little
    structure - the user asked for more faces and more structural information. Nothing
    here is small enough to vanish: every added element is at least ~0.06 thick and pairs
    a light and a dark tone so it survives the quantiser. Detail is layered rather than
    scattered: turret is built from four stacked cylinders of decreasing radius (a real
    dome curve instead of one ring), the hull is split into glacis / fighting compartment /
    engine deck at different heights, and the rear gets an engine grille.
    """
    p = []
    # ---- running gear: layered tracks, road wheels, drive sprockets, return rollers
    p += tracks(2.95, 0.88, 0.30, 0.36)
    for side in (-1, 1):
        for i in range(5):
            p.append(cyl((-1.08 + i * 0.52, side * 1.04, 0.30), 0.22, 0.12, METAL(),
                         rot=(math.radians(90), 0, 0), verts=16))
            p.append(cyl((-1.08 + i * 0.52, side * 1.04, 0.30), 0.09, 0.14, BODY_D(),
                         rot=(math.radians(90), 0, 0), verts=10))
        # drive sprocket and idler, larger and toothed via a ring
        p.append(cyl((1.42, side * 1.04, 0.34), 0.26, 0.14, METAL(),
                     rot=(math.radians(90), 0, 0), verts=14))
        p.append(cyl((-1.62, side * 1.04, 0.34), 0.24, 0.14, METAL(),
                     rot=(math.radians(90), 0, 0), verts=14))
        # return rollers
        for i in range(3):
            p.append(cyl((-0.75 + i * 0.75, side * 1.02, 0.52), 0.10, 0.10, BODY_D(),
                         rot=(math.radians(90), 0, 0), verts=10))
        # side skirt: sloped plate over the top run, reads as a hard diagonal
        p.append(boxr((0, side * 0.94, 0.60), (2.75, 0.16, 0.26), BODY(),
                      (math.radians(18) * side, 0, 0)))
        # stowage boxes on the sponsons
        for i in range(2):
            p.append(box((-1.05 + i * 0.72, side * 0.92, 0.78), (0.55, 0.20, 0.22), BODY_D()))

    # ---- hull: three distinct blocks at different heights
    p.append(box((-0.30, 0, 0.56), (2.05, 1.46, 0.50), BODY()))          # fighting compartment
    p.append(box((-1.28, 0, 0.54), (0.60, 1.42, 0.44), BODY_D()))        # engine deck, lower
    p.append(boxr((0.95, 0, 0.58), (1.05, 1.46, 0.48), BODY(),
                  (0, math.radians(36), 0)))                             # glacis
    p.append(box((0.30, 0, 0.80), (1.20, 1.30, 0.06), BODY()))           # deck plate lip
    # engine grille: three dark slats, the classic rear-deck read
    for i in range(3):
        p.append(box((-1.45 + i * 0.22, 0, 0.77), (0.13, 1.24, 0.05), TRACK()))
    # exhausts poking out the back
    for dy in (-0.46, 0.46):
        p.append(cyl((-1.62, dy, 0.52), 0.10, 0.34, BODY_D(),
                     rot=(0, math.radians(90), 0), verts=10))
    # front fenders over the tracks
    for side in (-1, 1):
        p.append(box((1.10, side * 0.92, 0.62), (0.55, 0.26, 0.08), BODY_D()))

    # ---- turret: four stacked cylinders = a real dome curve
    p.append(cyl((0, 0, 0.90), 0.76, 0.12, BODY_D(), verts=24))          # ring
    p.append(cyl((0, 0, 1.02), 0.68, 0.16, BODY(), verts=24))
    p.append(cyl((0, 0, 1.16), 0.58, 0.16, BODY(), verts=24))
    p.append(cyl((0, 0, 1.30), 0.42, 0.14, BODY(), verts=20))
    p.append(cyl((0, 0, 1.40), 0.24, 0.08, BODY_D(), verts=14))          # hatch
    # turret cheek armour: two angled plates give the front a faceted look
    for side in (-1, 1):
        p.append(boxr((0.42, side * 0.30, 1.10), (0.42, 0.30, 0.26), BODY_D(),
                      (0, 0, side * math.radians(32))))
    # 100mm: barrel, bore evacuator, muzzle brake, mantlet
    p.append(cyl((0.72, 0, 1.12), 0.15, 0.34, BODY_D(), rot=(0, math.radians(90), 0), verts=16))
    p.append(cyl((0.95, 0, 1.12), 0.12, 1.45, METAL(), rot=(0, math.radians(90), 0), verts=12))
    p.append(cyl((0.98, 0, 1.12), 0.17, 0.22, BODY_D(), rot=(0, math.radians(90), 0), verts=12))
    p.append(cyl((1.62, 0, 1.12), 0.16, 0.24, BODY_D(), rot=(0, math.radians(90), 0), verts=12))
    # AA machine gun on the roof
    p.append(box((-0.52, 0.42, 1.32), (0.26, 0.22, 0.16), BODY_D()))
    p.append(cyl((-0.40, 0.42, 1.36), 0.045, 0.60, METAL(),
                 rot=(0, math.radians(70), 0), verts=6))
    # lifting eyes / bolt heads along the turret ring
    for i in range(8):
        a = math.radians(45 * i)
        p.append(box((0.74 * math.cos(a), 0.74 * math.sin(a), 1.00),
                     (0.09, 0.09, 0.06), BODY_D()))
    return p


def p_type62():
    """Type 62 light tank - dome turret, three-block hull, full running gear."""
    p = []
    p += running_gear(2.30, 0.60, 0.26, r=0.19, n=4)
    p.append(boxr((0.72, 0, 0.50), (0.80, 1.18, 0.38), BODY(), (0, math.radians(34), 0)))
    p.append(box((-0.30, 0, 0.48), (1.70, 1.18, 0.40), BODY()))
    p.append(box((-1.02, 0, 0.46), (0.55, 1.14, 0.34), BODY_D()))
    p.append(box((0.10, 0, 0.68), (1.30, 1.10, 0.05), BODY()))
    p += rear_grille(-1.10, 0.56, 0.66, slats=3)
    for side in (-1, 1):
        p.append(box((0.92, side * 0.62, 0.52), (0.42, 0.20, 0.07), BODY_D()))
    p += turret_dome(-0.05, 0, 0.70, 0.52, layers=4, height=0.34, segs=20)
    for side in (-1, 1):
        p.append(boxr((0.28, side * 0.24, 0.86), (0.32, 0.24, 0.20), BODY_D(),
                      (0, 0, side * math.radians(30))))
    p += gun_assembly(0.42, 0.86, 0.10, 1.55)
    p.append(box((-0.42, 0.34, 1.02), (0.22, 0.18, 0.13), BODY_D()))
    return p


def p_type63_apc():
    """Type 63 APC: boxy tracked carrier, 12.7mm on an open ring."""
    p = []
    p.append(box((0, 0, 0.62), (2.9, 1.5, 0.62), BODY()))
    p.append(box((0.9, 0, 0.94), (0.8, 1.1, 0.30), BODY()))
    p.append(box((1.30, 0, 0.90), (0.3, 0.8, 0.20), GLASS()))
    p.append(box((0, 0, 0.30), (3.0, 1.56, 0.16), BODY_D()))
    p += tracks(2.9, 0.86, 0.28, 0.30)
    # troop compartment roof hatches
    for i in range(3):
        p.append(box((-0.85 + i * 0.55, 0, 0.96), (0.40, 0.9, 0.10), BODY_D()))
    p.append(cyl((-0.55, 0, 1.06), 0.26, 0.12, BODY_D(), verts=12))
    p.append(cyl((-0.55, 0, 1.18), 0.06, 0.55, METAL(), verts=6))
    return p


def p_type70_spg():
    """Type 70 122mm SPH: Type 63 chassis with an open-top howitzer."""
    p = []
    p.append(box((0, 0, 0.58), (2.9, 1.5, 0.55), BODY()))
    p.append(box((0, 0, 0.30), (3.0, 1.56, 0.16), BODY_D()))
    p += tracks(2.9, 0.86, 0.28, 0.30)
    # fighting compartment: raised, open at the back
    p.append(box((-0.35, 0, 1.00), (1.9, 1.35, 0.42), BODY_D()))
    p.append(box((-1.25, 0, 1.05), (0.22, 1.35, 0.50), BODY()))
    p.append(box((0.55, 0, 1.05), (0.22, 1.35, 0.50), BODY()))
    p.append(cyl((0.85, 0, 1.16), 0.14, 2.0, METAL(), rot=(0, math.radians(80), 0), verts=8))
    p.append(cyl((1.75, 0, 1.32), 0.17, 0.20, BODY_D(), rot=(0, math.radians(80), 0), verts=8))
    return p


def p_type89_spg():
    """Type 89 120mm tank destroyer: long gun over a rear fighting compartment."""
    p = []
    p.append(box((0, 0, 0.55), (3.0, 1.5, 0.50), BODY()))
    p.append(box((0, 0, 0.30), (3.1, 1.56, 0.16), BODY_D()))
    p += tracks(3.0, 0.86, 0.28, 0.30)
    p.append(box((-0.45, 0, 0.92), (1.6, 1.25, 0.42), BODY()))
    p.append(box((-0.45, 0, 1.16), (1.2, 1.05, 0.14), BODY_D()))
    p.append(cyl((0.9, 0, 0.95), 0.12, 2.6, METAL(), rot=(0, math.radians(90), 0), verts=8))
    p.append(cyl((2.25, 0, 0.95), 0.15, 0.24, BODY_D(), rot=(0, math.radians(90), 0), verts=8))
    return p


def p_type63_aaa():
    """Type 63 twin 37mm AA: open ring mount on a tracked chassis."""
    p = []
    p.append(box((0, 0, 0.55), (2.6, 1.45, 0.50), BODY()))
    p.append(box((0, 0, 0.30), (2.7, 1.5, 0.14), BODY_D()))
    p += tracks(2.6, 0.82, 0.26, 0.28)
    p.append(cyl((0, 0, 0.86), 0.62, 0.16, BODY_D(), verts=16))
    p.append(box((0, 0, 1.02), (0.85, 0.85, 0.28), BODY()))
    for dy in (-0.20, 0.20):
        p.append(cyl((0.62, dy, 1.22), 0.065, 1.35, METAL(),
                     rot=(0, math.radians(58), 0), verts=6))
    p.append(box((-0.48, 0, 1.20), (0.30, 0.55, 0.20), BODY_D()))
    return p


def p_wz523():
    """WZ-523 wheeled APC.

    Wheels were the problem: at 0.27 radius tucked inside a 1.42-wide body they were hidden
    under the hull from an isometric camera and the vehicle read as a featureless box. The
    wheels are now bigger, pushed OUTBOARD so they break the silhouette on both sides, and
    each gets a bright hub cap so there is a light/dark pair. Chassis is lifted to give them
    room, and the turret and gun are enlarged.
    """
    p = []
    p.append(boxr((1.18, 0, 0.76), (0.5, 1.32, 0.5), BODY(), (0, math.radians(30), 0)))
    p.append(box((0, 0, 0.78), (2.7, 1.30, 0.58), BODY()))
    p.append(box((0, 0, 0.36), (2.9, 1.36, 0.20), BODY_D()))
    # big outboard wheels: radius 0.34, protruding past a 1.30-wide hull
    for x in (-1.02, 0.02, 0.98):
        for side in (-1, 1):
            p.append(cyl((x, side * 0.78, 0.36), 0.34, 0.26, RUBBER(),
                         rot=(math.radians(90), 0, 0), verts=12))
            p.append(cyl((x, side * 0.92, 0.36), 0.15, 0.10, BODY_D(),
                         rot=(math.radians(90), 0, 0), verts=8))
    # fender line above the wheels - another hard horizontal
    for side in (-1, 1):
        p.append(box((0, side * 0.80, 0.66), (2.75, 0.36, 0.10), BODY_D()))
    p.append(cyl((-0.15, 0, 1.16), 0.42, 0.30, BODY(), verts=14))
    p.append(cyl((-0.15, 0, 1.02), 0.50, 0.12, BODY_D(), verts=14))
    p.append(cyl((0.30, 0, 1.16), 0.075, 0.85, METAL(), rot=(0, math.radians(90), 0), verts=6))
    p.append(box((1.32, 0, 0.94), (0.3, 0.72, 0.20), GLASS()))
    return p


def p_type83_152():
    """Type 83 152mm SPH: big rear turret, very long gun."""
    p = []
    p.append(box((0, 0, 0.58), (3.4, 1.6, 0.55), BODY()))
    p.append(box((0, 0, 0.32), (3.5, 1.66, 0.18), BODY_D()))
    p += tracks(3.4, 0.90, 0.30, 0.32)
    for side in (-1, 1):
        for i in range(5):
            p.append(cyl((-1.25 + i * 0.55, side * 1.06, 0.30), 0.20, 0.10, METAL(),
                         rot=(math.radians(90), 0, 0)))
    p.append(box((-0.70, 0, 1.05), (1.9, 1.45, 0.52), BODY()))
    p.append(box((-0.70, 0, 1.34), (1.6, 1.2, 0.10), BODY_D()))
    p.append(cyl((0.95, 0, 1.05), 0.14, 2.9, METAL(), rot=(0, math.radians(90), 0), verts=8))
    p.append(cyl((2.42, 0, 1.05), 0.18, 0.26, BODY_D(), rot=(0, math.radians(90), 0), verts=8))
    return p


# ---------------------------------------------------------------- PLA aircraft
def p_j5():
    """J-5 (MiG-17): nose intake, swept wing, small tail fin."""
    p = []
    p.append(cyl((0, 0, 0.52), 0.30, 2.9, BODY(), rot=(0, math.radians(90), 0), verts=14))
    p.append(cyl((1.46, 0, 0.52), 0.26, 0.22, BODY_D(), rot=(0, math.radians(90), 0), verts=14))
    p.append(cyl((1.58, 0, 0.52), 0.10, 0.12, METAL(), rot=(0, math.radians(90), 0), verts=8))
    for side in (-1, 1):
        p += wing(side, 0.05, 1.60, 0.95, 0.42, 0.50, 35.0, 0.09, BODY_D())
    p.append(box((-1.30, 0, 0.52), (0.55, 1.35, 0.08), BODY_D()))
    p.append(box((-1.55, 0, 0.92), (0.40, 0.09, 0.80), BODY_D()))
    p.append(box((0.55, 0, 0.76), (0.75, 0.44, 0.26), GLASS()))
    return p


def p_j6():
    """J-6 (MiG-19): twin engines side by side, nose intake, big swept wing."""
    p = []
    for dy in (-0.19, 0.19):
        p.append(cyl((0, dy, 0.52), 0.23, 3.1, BODY(), rot=(0, math.radians(90), 0), verts=12))
    p.append(cyl((1.58, 0, 0.52), 0.33, 0.20, BODY_D(), rot=(0, math.radians(90), 0), verts=16))
    for side in (-1, 1):
        p += wing(side, 0.0, 1.85, 1.05, 0.48, 0.50, 38.0, 0.09, BODY_D())
    p.append(box((-1.45, 0, 0.52), (0.65, 1.55, 0.08), BODY_D()))
    p.append(box((-1.75, 0, 0.98), (0.42, 0.09, 0.92), BODY_D()))
    p.append(box((0.50, 0, 0.78), (0.80, 0.44, 0.26), GLASS()))
    return p


def p_j7():
    """J-7 (MiG-21): delta wing and a nose shock cone - the give-away features."""
    p = []
    p.append(cyl((0, 0, 0.52), 0.28, 3.2, BODY(), rot=(0, math.radians(90), 0), verts=14))
    p.append(cone((1.72, 0, 0.52), 0.28, 0.02, 0.75, BODY_D(),
                  rot=(0, math.radians(90), 0), verts=14))       # shock cone
    for side in (-1, 1):
        p += wing(side, -0.35, 1.55, 1.85, 0.28, 0.50, 58.0, 0.08, BODY_D())   # delta
    p.append(box((-1.45, 0, 0.52), (0.55, 1.05, 0.08), BODY_D()))
    p.append(box((-1.70, 0, 0.88), (0.36, 0.08, 0.72), BODY_D()))
    p.append(box((0.60, 0, 0.78), (0.85, 0.40, 0.24), GLASS()))
    return p


def p_q5():
    """Q-5: pointed nose with SIDE intakes, swept wing - clearly not a J-5/J-6."""
    p = []
    p.append(cyl((0, 0, 0.52), 0.30, 3.1, BODY(), rot=(0, math.radians(90), 0), verts=14))
    p.append(cone((1.68, 0, 0.52), 0.30, 0.04, 0.70, BODY_D(),
                  rot=(0, math.radians(90), 0), verts=14))
    for side in (-1, 1):
        p.append(box((0.95, side * 0.52, 0.52), (1.0, 0.30, 0.30), BODY_D()))
        p += wing(side, 0.0, 1.45, 0.95, 0.40, 0.50, 36.0, 0.09, BODY_D())
    p.append(box((-1.40, 0, 0.52), (0.55, 1.30, 0.08), BODY_D()))
    p.append(box((-1.62, 0, 0.90), (0.36, 0.08, 0.78), BODY_D()))
    p.append(box((0.52, 0, 0.76), (0.70, 0.42, 0.24), GLASS()))
    return p


def p_h5():
    """H-5 (Il-28): twin podded engines on a straight wing, glazed nose."""
    p = []
    p.append(cyl((0, 0, 0.58), 0.33, 3.6, BODY(), rot=(0, math.radians(90), 0), verts=14))
    p.append(cone((1.95, 0, 0.58), 0.33, 0.10, 0.55, GLASS(),
                  rot=(0, math.radians(90), 0), verts=14))
    p.append(box((0.0, 0, 0.56), (1.2, 4.4, 0.10), BODY_D()))
    p.append(box((-1.55, 0, 0.56), (0.6, 1.7, 0.10), BODY_D()))
    p.append(box((-1.80, 0, 0.95), (0.36, 0.09, 0.82), BODY_D()))
    for side in (-1, 1):
        p.append(cyl((-0.15, side * 1.30, 0.44), 0.21, 0.75, BODY_D(),
                     rot=(0, math.radians(90), 0), verts=10))
    return p


def p_h6():
    """H-6 (Tu-16): big swept-wing bomber, engines buried in the wing roots."""
    p = []
    p.append(cyl((0, 0, 0.62), 0.38, 4.6, BODY(), rot=(0, math.radians(90), 0), verts=16))
    p.append(cone((2.35, 0, 0.62), 0.38, 0.12, 0.65, GLASS(),
                  rot=(0, math.radians(90), 0), verts=16))
    for side in (-1, 1):
        p += wing(side, -0.10, 2.20, 1.30, 0.55, 0.60, 30.0, 0.11, BODY_D())
        p.append(cyl((0.05, side * 0.62, 0.52), 0.19, 0.9, BODY_D(),
                     rot=(0, math.radians(90), 0), verts=10))
    p.append(box((-1.95, 0, 0.62), (0.7, 1.9, 0.10), BODY_D()))
    p.append(box((-2.25, 0, 1.05), (0.42, 0.10, 0.90), BODY_D()))
    return p


def p_y5():
    """Y-5 (An-2) biplane: the two wings must be far enough apart to read as TWO wings.

    Previously the gap was 0.58 in a 128px render that becomes about 1px at sprite size, so
    it looked like a single thick wing with clutter. The gap is nearly doubled, the struts
    are made thick enough to show, and the interplane struts are moved outboard where they
    are not hidden behind the fuselage.
    """
    p = []
    p.append(box((0, 0, 0.62), (2.9, 0.62, 0.58), BODY()))
    p.append(cyl((1.58, 0, 0.62), 0.34, 0.40, BODY_D(),
                 rot=(0, math.radians(90), 0), verts=14))          # radial cowl
    p.append(cyl((1.82, 0, 0.62), 0.16, 0.12, METAL(),
                 rot=(0, math.radians(90), 0), verts=10))          # prop hub
    # upper and lower wings, gap 1.05 (was 0.58), both thick enough to show
    p.append(box((0.05, 0, 1.10), (1.50, 4.2, 0.13), BODY_D()))
    p.append(box((0.05, 0, 0.22), (1.50, 4.2, 0.13), BODY_D()))
    # inboard AND outboard interplane struts, thick
    for side in (-1, 1):
        for x in (-0.60, 0.62):
            p.append(box((x, side * 1.05, 0.66), (0.13, 0.13, 1.0), METAL()))
            p.append(box((x, side * 1.95, 0.66), (0.13, 0.13, 1.0), METAL()))
    p.append(box((-1.55, 0, 0.66), (0.60, 1.85, 0.12), BODY_D()))
    p.append(box((-1.80, 0, 1.10), (0.42, 0.10, 0.95), BODY_D()))
    p.append(box((1.05, 0, 0.88), (0.60, 0.50, 0.24), GLASS()))
    p.append(box((0.15, 0, 0.42), (0.9, 0.5, 0.14), BODY_D()))    # belly fuel tank
    return p


def p_z5():
    """Z-5 (Mi-4): piston helicopter, boxy cabin, four-blade main rotor."""
    p = []
    p.append(box((0, 0, 0.62), (2.6, 0.95, 0.80), BODY()))
    p.append(box((1.35, 0, 0.68), (0.75, 0.75, 0.62), GLASS()))
    p.append(cyl((1.72, 0, 0.50), 0.24, 0.26, BODY_D(),
                 rot=(0, math.radians(90), 0), verts=12))       # radial cowl
    p.append(box((-2.05, 0, 0.66), (1.7, 0.24, 0.24), BODY()))
    p.append(cyl((0, 0, 1.08), 0.10, 0.26, METAL()))
    p.append(box((0, 0, 1.22), (0.18, 3.6, 0.06), METAL()))
    p.append(box((0.9, 0, 1.22), (1.9, 0.12, 0.05), METAL()))
    p.append(box((-2.85, 0, 0.95), (0.06, 0.65, 0.65), METAL()))
    for side in (-1, 1):
        p.append(box((-0.30, side * 0.52, 0.33), (1.5, 0.10, 0.12), METAL()))
        p.append(cyl((0.15, side * 0.52, 0.28), 0.13, 0.10, RUBBER(),
                     rot=(math.radians(90), 0, 0)))
    return p

# ------------------------------------------------------------------ structure helpers
# Reusable sub-assemblies for the detail pass. The lesson from the Type 59 rework is that
# structure has to be LAYERED, not scattered: small parts vanish at 32x36, but a stack of
# decreasing radii, a hull split into blocks at different heights, and a hard dark grille
# survive. These helpers encode that so every tank gets the same treatment.
def turret_dome(cx, cy, z0, r_base, layers=4, height=0.44, segs=24):
    """Stacked cylinders of decreasing radius - reads as a curved dome, not a ring."""
    p = []
    p.append(cyl((cx, cy, z0), r_base * 1.12, 0.12, BODY_D(), verts=segs))
    step = height / layers
    for i in range(layers):
        r = r_base * (1.0 - 0.24 * i)
        p.append(cyl((cx, cy, z0 + 0.12 + step * (i + 0.5)), r, step * 1.02, BODY(), verts=segs))
    p.append(cyl((cx, cy, z0 + 0.12 + height + 0.04), r_base * 0.34, 0.08, BODY_D(), verts=14))
    return p


def running_gear(length, half_w, z, r=0.22, n=5, skirt=True):
    """Road wheels with hubs, drive sprocket, idler, return rollers, skirt, stowage."""
    p = []
    p += tracks(length, half_w, z, r * 1.55)
    span = length - 2.0 * r
    for side in (-1, 1):
        for i in range(n):
            x = -span / 2 + span * i / float(n - 1)
            p.append(cyl((x, side * (half_w + 0.16), z), r, 0.12, METAL(),
                         rot=(math.radians(90), 0, 0), verts=16))
            p.append(cyl((x, side * (half_w + 0.16), z), r * 0.40, 0.14, BODY_D(),
                         rot=(math.radians(90), 0, 0), verts=10))
        p.append(cyl((length / 2 - 0.05, side * (half_w + 0.16), z + 0.03), r * 1.15, 0.14,
                     METAL(), rot=(math.radians(90), 0, 0), verts=14))
        p.append(cyl((-length / 2 + 0.05, side * (half_w + 0.16), z + 0.03), r * 1.05, 0.14,
                     METAL(), rot=(math.radians(90), 0, 0), verts=14))
        for i in range(3):
            p.append(cyl((-span / 2 + span * i / 2.0, side * (half_w + 0.14), z + r * 0.95),
                         r * 0.42, 0.10, BODY_D(), rot=(math.radians(90), 0, 0), verts=10))
        if skirt:
            p.append(boxr((0, side * (half_w + 0.06), z + r * 1.15), (length * 0.95, 0.14, 0.24),
                          BODY(), (math.radians(16) * side, 0, 0)))
            for i in range(2):
                p.append(box((-length * 0.28 + i * length * 0.30, side * (half_w + 0.04),
                              z + r * 1.5), (length * 0.22, 0.18, 0.20), BODY_D()))
    return p


def rear_grille(x, half_w, z, slats=3):
    """Dark engine grille plus exhausts - the classic rear-deck read."""
    p = []
    for i in range(slats):
        p.append(box((x + i * 0.20, 0, z), (0.12, half_w * 1.75, 0.05), TRACK()))
    for dy in (-half_w * 0.62, half_w * 0.62):
        p.append(cyl((x - 0.16, dy, z - 0.14), 0.09, 0.30, BODY_D(),
                     rot=(0, math.radians(90), 0), verts=10))
    return p


def gun_assembly(x, z, r_tube, length, muzzle=True, evacuator=True):
    """Mantlet, tube, bore evacuator, muzzle brake."""
    p = []
    p.append(cyl((x + 0.16, 0, z), r_tube * 1.5, 0.32, BODY_D(),
                 rot=(0, math.radians(90), 0), verts=16))
    p.append(cyl((x + 0.16 + length * 0.5, 0, z), r_tube, length, METAL(),
                 rot=(0, math.radians(90), 0), verts=12))
    if evacuator:
        p.append(cyl((x + 0.10 + length * 0.42, 0, z), r_tube * 1.45, length * 0.16, BODY_D(),
                     rot=(0, math.radians(90), 0), verts=12))
    if muzzle:
        p.append(cyl((x + 0.16 + length * 0.98, 0, z), r_tube * 1.35, 0.22, BODY_D(),
                     rot=(0, math.radians(90), 0), verts=12))
    return p

# ------------------------------------------------------------------ aircraft structure
# Aircraft need a different structural grammar from tanks. The layers that survive a
# reduction to 32x36 are: a stepped nose/centre/tail fuselage, a contrasting glazed
# canopy, a wing-root fairing, a proper tail unit, podded engines and landing gear. A
# single long cylinder reads as a pipe, and a thin prop disc disappears completely.
def fuselage3(x0, x1, r_nose, r_mid, r_tail, z, segs=14):
    """Tapered nose + cylindrical centre + tapered tail, plus a dorsal spine."""
    p = []
    length = x1 - x0
    ln, lm, lt = length * 0.26, length * 0.44, length * 0.30
    p.append(cone((x1 - ln / 2, 0, z), r_nose, r_mid, ln, BODY(),
                  rot=(0, math.radians(90), 0), verts=segs))
    p.append(cyl((x1 - ln - lm / 2, 0, z), r_mid, lm, BODY(),
                 rot=(0, math.radians(90), 0), verts=segs))
    p.append(cone((x0 + lt / 2, 0, z), r_tail, r_mid, lt, BODY(),
                  rot=(0, math.radians(-90), 0), verts=segs))
    p.append(box((x0 + length * 0.40, 0, z + r_mid * 0.92),
                 (length * 0.54, r_mid * 0.55, r_mid * 0.24), BODY_D()))
    return p


def canopy_glazed(x, z, length, width, height, frames=2):
    """Glazed cockpit with dark framing bands, raised above the spine."""
    p = []
    p.append(box((x, 0, z + height * 0.5), (length, width, height * 0.55), GLASS()))
    p.append(box((x, 0, z + height * 0.94), (length * 0.9, width * 0.86, height * 0.30),
                 BODY_D()))
    for i in range(frames):
        p.append(box((x - length * 0.3 + i * length * 0.6, 0, z + height * 0.45),
                     (length * 0.10, width * 1.03, height * 0.55), BODY_D()))
    return p


def tail_unit(x, z, span, fin_h, chord=0.42):
    """Horizontal stabiliser plus vertical fin with a contrasting rudder."""
    p = []
    p.append(box((x, 0, z), (chord, span, 0.07), BODY_D()))
    p.append(box((x - chord * 0.35, 0, z + fin_h * 0.5), (chord * 0.85, 0.08, fin_h), BODY_D()))
    p.append(box((x - chord * 0.62, 0, z + fin_h * 0.55), (chord * 0.16, 0.09, fin_h * 0.8),
                 BODY()))
    return p


def wing_root_fairing(side, x, y, z, chord, thick):
    """Fairing at the wing/fuselage junction so the join is not a bare box corner."""
    return [box((x, side * y, z), (chord, 0.34, thick * 1.5), BODY())]


def landing_gear(x, y, z, strut=0.34, wheel_r=0.13, nose=True):
    """Two main legs plus a nose leg; small dark stubs that still register at 32x36."""
    p = []
    for side in (-1, 1):
        p.append(box((x, side * y, z - strut * 0.5), (0.07, 0.07, strut), METAL()))
        p.append(cyl((x, side * y, z - strut), wheel_r, 0.10, TRACK(),
                     rot=(math.radians(90), 0, 0), verts=10))
    if nose:
        p.append(box((x + 0.55, 0, z - strut * 0.4), (0.07, 0.07, strut * 0.85), METAL()))
        p.append(cyl((x + 0.55, 0, z - strut * 0.85), wheel_r * 0.9, 0.10, TRACK(),
                     rot=(math.radians(90), 0, 0), verts=10))
    return p


def engine_nacelle(x, y, z, r, length, intake=True):
    """Podded engine with a dark intake ring and an exhaust stub."""
    p = []
    p.append(cyl((x, y, z), r, length, BODY_D(), rot=(0, math.radians(90), 0), verts=12))
    if intake:
        p.append(cyl((x + length * 0.5, y, z), r * 0.72, 0.10, TRACK(),
                     rot=(0, math.radians(90), 0), verts=10))
    p.append(cyl((x - length * 0.55, y, z), r * 0.62, 0.16, METAL(),
                 rot=(0, math.radians(90), 0), verts=10))
    return p


def propeller(x, z, r, blades=4, thickness=0.07):
    """Prop as thick dark blades; a thin disc disappears when downscaled."""
    p = []
    p.append(cyl((x, 0, z), r * 0.18, 0.16, BODY_D(), rot=(0, math.radians(90), 0), verts=8))
    for i in range(blades):
        a = math.pi * i / blades
        p.append(box((x, 0, z), (0.07, r * 2.0 * abs(math.cos(a)) + thickness,
                                 r * 2.0 * abs(math.sin(a)) + thickness), TRACK()))
    return p


VEHICLES = {
    "ww1_rhomboid": t_ww1_rhomboid,
    "ww2_medium": t_ww2_medium,
    "ww2_heavy": t_ww2_heavy,
    "coldwar_mbt": t_coldwar_mbt,
    "spg": t_spg,
    "aa_track": t_aa,
    "ww1_biplane": a_ww1_biplane,
    "ww2_fighter": a_ww2_fighter,
    "jet": a_jet,
    "helicopter": a_heli,
    "truck": v_truck,
    "halftrack": v_halftrack,
    "apc": v_apc,
    "mlrs": v_mlrs,
    "jeep": v_jeep,
    # expansion: armoured set
    "light_tank": t_light_tank,
    "super_heavy": t_super_heavy,
    "spaag": t_spaag,
    "recon": t_recon,
    "engineer": t_engineer,
    "sam": t_sam,
    "transport": t_transport,
    "command_car": t_command_car,
    # expansion: air set
    "heavy_bomber": a_heavy_bomber,
    "recon_plane": a_recon_plane,
    "seaplane": a_seaplane,
    "transport_plane": a_transport_plane,
    "ground_attack": a_ground_attack,
    "jet_bomber": a_jet_bomber,
    "vtol": a_vtol,
    "airship": a_zeppelin_air,
    # PLA cold-war set
    "type59": p_type59,
    "type62": p_type62,
    "type63_apc": p_type63_apc,
    "type70_spg": p_type70_spg,
    "type89_spg": p_type89_spg,
    "type63_aaa": p_type63_aaa,
    "wz523": p_wz523,
    "type83_152": p_type83_152,
    "j5": p_j5,
    "j6": p_j6,
    "j7": p_j7,
    "q5": p_q5,
    "h5": p_h5,
    "h6": p_h6,
    "y5": p_y5,
    "z5": p_z5,
}


# ------------------------------------------------------------------ rig
def setup_render(size):
    sc = bpy.context.scene
    for engine in ("BLENDER_EEVEE_NEXT", "BLENDER_EEVEE", "CYCLES", "BLENDER_WORKBENCH"):
        try:
            sc.render.engine = engine
            break
        except TypeError:
            continue
    sc.render.resolution_x = size
    sc.render.resolution_y = size
    sc.render.film_transparent = True
    sc.render.image_settings.file_format = "PNG"
    sc.render.image_settings.color_mode = "RGBA"
    # CRITICAL: Blender defaults to AgX/Filmic tone mapping, which desaturates and lifts
    # everything. A sand-yellow material (0.97,0.84,0.47) was rendering as (180,172,151)
    # grey-white, which is why units kept coming out muddy no matter what palette I set.
    # Standard means "material colour goes straight to the PNG".
    try:
        sc.view_settings.view_transform = "Standard"
        sc.view_settings.look = "None"
        sc.view_settings.exposure = 0.0
        sc.view_settings.gamma = 1.0
    except Exception as e:
        print("[warn] view transform:", e)


def setup_lights():
    # Grey ambient washing out saturation was the reason the render measured (208,192,160)
    # for a base colour of (0.97,0.84,0.47): the world light lifted the blue channel from
    # 0.47 to 0.63 and the result read as cream instead of RA's saturated (247,215,121).
    # Ambient is now weak and slightly warm, and the key light carries the exposure.
    key = bpy.data.lights.new("key", type="SUN")
    key.energy = 4.6
    key.angle = math.radians(10)
    ko = bpy.data.objects.new("key", key)
    bpy.context.collection.objects.link(ko)
    ko.rotation_euler = (math.radians(55), 0, math.radians(-40))
    fill = bpy.data.lights.new("fill", type="SUN")
    fill.energy = 0.55
    fo = bpy.data.objects.new("fill", fill)
    bpy.context.collection.objects.link(fo)
    fo.rotation_euler = (math.radians(70), 0, math.radians(150))
    w = bpy.data.worlds.new("w")
    w.use_nodes = True
    w.node_tree.nodes["Background"].inputs[0].default_value = (0.24, 0.20, 0.14, 1)
    w.node_tree.nodes["Background"].inputs[1].default_value = 0.22
    bpy.context.scene.world = w


def mesh_bounds():
    lo = Vector((1e9, 1e9, 1e9))
    hi = Vector((-1e9, -1e9, -1e9))
    for ob in bpy.data.objects:
        if ob.type != "MESH":
            continue
        for c in ob.bound_box:
            w = ob.matrix_world @ Vector(c)
            lo = Vector((min(lo.x, w.x), min(lo.y, w.y), min(lo.z, w.z)))
            hi = Vector((max(hi.x, w.x), max(hi.y, w.y), max(hi.z, w.z)))
    return lo, hi


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    out = argv[0] if argv else "."
    facings, size, variants = 32, 512, 1
    only = None
    paint = None
    i = 1
    while i < len(argv):
        if argv[i] == "--facings" and i + 1 < len(argv):
            facings = int(argv[i + 1]); i += 2
        elif argv[i] == "--size" and i + 1 < len(argv):
            size = int(argv[i + 1]); i += 2
        elif argv[i] == "--variants" and i + 1 < len(argv):
            variants = int(argv[i + 1]); i += 2
        elif argv[i] == "--only" and i + 1 < len(argv):
            only = set(argv[i + 1].split(",")); i += 2
        elif argv[i] == "--paint" and i + 1 < len(argv):
            paint = argv[i + 1]; i += 2
        else:
            i += 1
    os.makedirs(out, exist_ok=True)

    names = [n for n in VEHICLES if (only is None or n in only)]
    for name in names:
        for v in range(variants):
            global WALL_PICK
            if paint is not None:
                names = [n for n, _, _ in WALL_COLOURS]
                if paint in names:
                    WALL_PICK = names.index(paint)
                else:
                    print("[warn] unknown paint", paint, "have", names)
            else:
                WALL_PICK = (v * 2) % len(WALL_COLOURS)
            tag = name if variants == 1 else f"{name}_v{v}"
            MATS.clear()
            bpy.ops.wm.read_factory_settings(use_empty=True)
            VEHICLES[name]()
            lo, hi = mesh_bounds()
            centre = (lo + hi) * 0.5
            extent = max(hi.x - lo.x, hi.y - lo.y, (hi.z - lo.z) * 1.3)

            setup_render(size)
            setup_lights()
            cam_data = bpy.data.cameras.new("cam")
            cam_data.type = "ORTHO"
            cam_data.ortho_scale = extent * 1.35
            cam = bpy.data.objects.new("cam", cam_data)
            bpy.context.collection.objects.link(cam)
            bpy.context.scene.camera = cam
            aim = bpy.data.objects.new("aim", None)
            bpy.context.collection.objects.link(aim)
            aim.location = centre
            tc = cam.constraints.new(type="TRACK_TO")
            tc.target = aim
            tc.track_axis = "TRACK_NEGATIVE_Z"
            tc.up_axis = "UP_Y"

            pitch = math.radians(60.0)
            dist = extent * 4.0 + 12.0
            sc = bpy.context.scene
            for f in range(facings):
                yaw = math.radians(45.0 + 360.0 * f / facings)
                cam.location = (
                    centre.x + dist * math.sin(pitch) * math.cos(yaw),
                    centre.y - dist * math.sin(pitch) * math.sin(yaw),
                    centre.z + dist * math.cos(pitch),
                )
                bpy.context.view_layer.update()
                sc.render.filepath = os.path.join(out, f"{tag}_f{f:02d}.png")
                bpy.ops.render.render(write_still=True)
            print(f"[built] {tag}: {facings} facings ({WALL_COLOURS[WALL_PICK][0]})", flush=True)

    print(f"[done] {len(names)} vehicle types -> {out}")


if __name__ == "__main__":
    main()
