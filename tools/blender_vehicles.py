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
    ("ra_olive", srgb(0.56, 0.60, 0.30), srgb(0.30, 0.33, 0.15)),
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
    """British Mark IV: a lozenge that wraps around the tracks."""
    p = []
    p.append(box((0, 0, 0.62), (3.4, 1.5, 0.8), BODY()))
    p.append(box((0, 0, 1.06), (2.6, 1.2, 0.5), BODY_D()))
    for side in (-1, 1):
        p.append(box((0, side * 0.95, 0.55), (3.9, 0.35, 0.9), TRACK()))
    p.append(box((0.9, 0.45, 1.0), (0.5, 0.4, 0.35), BODY_D()))
    p.append(cyl((1.15, 0.45, 1.0), 0.08, 0.7, METAL(), rot=(0, math.radians(90), 0)))
    p.append(box((-0.6, -0.45, 1.05), (0.5, 0.4, 0.3), BODY_D()))
    return p


def t_ww2_medium():
    """Features are deliberately EXAGGERATED for a 24px target.

    At 24x32 a 0.07-radius gun barrel is well under one pixel and vanishes, which left a
    featureless slab. RA art is drawn at the target size with chunky shapes, so this model
    uses a thick barrel, a taller turret with a distinct ring, and dark trim that reads as
    lines once downscaled. Real proportion accuracy is the wrong goal here - readability is.
    """
    p = []
    p.append(box((0, 0, 0.5), (2.8, 1.5, 0.55), BODY()))
    p.append(box((0, 0, 0.84), (2.3, 1.3, 0.22), BODY()))
    # dark side skirt: gives a hard horizontal line along the hull
    p.append(box((0, 0, 0.30), (2.9, 1.56, 0.16), BODY_D()))
    p += tracks(2.8, 0.85, 0.32)
    for i in range(5):
        p.append(cyl((-1.0 + i * 0.5, 1.02, 0.32), 0.2, 0.1, METAL(), rot=(math.radians(90), 0, 0)))
    # turret ring (wide, dark) then turret (bright, tall) - two distinct bands
    p.append(cyl((0, 0, 0.98), 0.68, 0.18, BODY_D(), verts=16))
    p.append(cyl((0, 0, 1.18), 0.58, 0.40, BODY(), verts=16))
    p.append(cyl((0, 0, 1.40), 0.34, 0.10, BODY_D(), verts=12))      # hatch
    # THICK gun: 0.16 radius survives the downscale
    p.append(cyl((0.78, 0, 1.18), 0.16, 1.5, METAL(), rot=(0, math.radians(90), 0), verts=8))
    p.append(cyl((1.50, 0, 1.18), 0.20, 0.22, BODY_D(), rot=(0, math.radians(90), 0), verts=8))

    # ---- detail pass -------------------------------------------------------
    # Everything here is deliberately chunky: at 24 px a feature under ~2 px does not
    # survive, so these are structural elements, not surface texture.
    #1 fenders: a hard horizontal band over each track, very readable in silhouette
    for side in (-1, 1):
        p.append(box((0, side * 0.86, 0.72), (3.05, 0.30, 0.10), BODY()))
    #2 spare track links bolted to the hull side (alternating light/dark reads as a stripe)
    for side in (-1, 1):
        for i in range(3):
            p.append(box((-0.55 + i * 0.55, side * 0.78, 0.62), (0.42, 0.10, 0.26), BODY_D()))
    #3 sloped glacis plate at the front - a lighter angled face
    p.append(boxr((1.32, 0, 0.72), (0.30, 1.42, 0.52), BODY(),
                  (0, math.radians(28), 0)))
    #4 rear exhaust, sticking out so it breaks the silhouette
    p.append(cyl((-1.48, 0.42, 0.62), 0.11, 0.42, BODY_D(), rot=(0, math.radians(90), 0), verts=8))
    p.append(cyl((-1.48, -0.42, 0.62), 0.11, 0.42, BODY_D(), rot=(0, math.radians(90), 0), verts=8))
    #5 turret machine gun: small but sits on top, catches the light
    p.append(cyl((0.18, 0.34, 1.46), 0.055, 0.62, METAL(), rot=(0, math.radians(78), 0), verts=6))
    #6 stowage box on the turret side
    p.append(box((-0.34, 0.52, 1.16), (0.42, 0.22, 0.26), BODY_D()))
    #7 commander's cupola rim
    p.append(cyl((0.0, 0, 1.47), 0.20, 0.08, BODY(), verts=12))
    #8 lifting eyes / bolt heads along the hull top edge
    for side in (-1, 1):
        for i in range(4):
            p.append(box((-1.05 + i * 0.62, side * 0.62, 0.96), (0.12, 0.12, 0.08), BODY_D()))
    #9 headlight, bright so it reads as a dot
    p.append(cyl((1.42, -0.52, 0.86), 0.09, 0.10, ACCENT(), rot=(0, math.radians(90), 0), verts=6))
    return p


def t_ww2_heavy():
    p = []
    p.append(box((0, 0, 0.6), (3.6, 1.9, 0.7), BODY()))
    p.append(box((0, 0, 1.0), (3.0, 1.7, 0.25), BODY()))
    p += tracks(3.6, 1.05, 0.36, 0.36)
    p.append(cyl((0, 0, 1.3), 0.72, 0.5, BODY_D(), verts=14))
    p.append(cyl((0.78, 0, 1.3), 0.08, 1.5, METAL(), rot=(0, math.radians(90), 0)))
    p.append(box((0.62, 0, 1.3), (0.3, 0.26, 0.26), METAL()))
    return p


def t_coldwar_mbt():
    p = []
    p.append(box((0, 0, 0.55), (3.2, 1.8, 0.5), BODY()))
    p.append(box((0, 0, 0.88), (2.8, 1.6, 0.3), BODY()))
    p += tracks(3.2, 1.0, 0.34, 0.34)
    p.append(cyl((0, 0, 1.16), 0.62, 0.34, BODY_D(), verts=16))
    p.append(box((0.3, 0, 1.16), (1.0, 0.4, 0.28), BODY_D()))
    p.append(cyl((0.95, 0, 1.16), 0.07, 1.6, METAL(), rot=(0, math.radians(90), 0)))
    p.append(box((-0.6, 0, 1.32), (0.3, 0.3, 0.2), METAL()))
    p.append(cyl((-0.9, 0.5, 1.3), 0.12, 0.3, ACCENT()))
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
    """Wings thinned to 0.055 and pushed apart to a 1.06 gap.

    At 0.1 thickness in a 128px render the two wings were roughly one pixel apart after
    downscaling and merged into a single slab, which is why the biplane read as a pale box.
    Struts are 0.14 so they survive, and outboard as well as inboard so the gap is visible
    from the side.
    """
    p = []
    p.append(box((0, 0, 0.62), (2.5, 0.42, 0.44), BODY()))
    p.append(box((0, 0, 1.18), (1.45, 3.1, 0.055), BODY_D()))
    p.append(box((0, 0, 0.12), (1.45, 3.1, 0.055), BODY_D()))
    for side in (-1, 1):
        for x in (-0.55, 0.58):
            p.append(box((x, side * 1.00, 0.65), (0.14, 0.14, 1.06), METAL()))
            p.append(box((x, side * 1.42, 0.65), (0.14, 0.14, 1.06), METAL()))
    p.append(box((-1.40, 0, 0.66), (0.55, 1.40, 0.07), BODY_D()))
    p.append(box((-1.62, 0, 1.02), (0.40, 0.09, 0.62), BODY_D()))
    p.append(cyl((1.32, 0, 0.62), 0.13, 0.26, BODY_D(), rot=(0, math.radians(90), 0), verts=10))
    p.append(box((1.24, 0, 0.62), (0.05, 1.6, 0.18), BODY_D()))
    return p


def a_ww2_fighter():
    p = []
    p.append(cyl((0, 0, 0.5), 0.26, 2.6, BODY(), rot=(0, math.radians(90), 0), verts=12))
    p.append(cone((1.4, 0, 0.5), 0.26, 0.05, 0.5, BODY_D(), rot=(0, math.radians(90), 0), verts=12))
    p.append(box((0.1, 0, 0.55), (0.9, 3.4, 0.1), BODY_D()))
    p.append(box((-1.15, 0, 0.55), (0.5, 1.2, 0.08), BODY_D()))
    p.append(box((-1.35, 0, 0.85), (0.35, 0.08, 0.6), BODY_D()))
    p.append(box((0.9, 0, 0.78), (0.55, 0.5, 0.28), GLASS()))
    p.append(cyl((1.55, 0, 0.5), 0.06, 0.1, METAL(), rot=(0, math.radians(90), 0)))
    return p


def a_jet():
    p = []
    p.append(cyl((0, 0, 0.5), 0.3, 3.2, BODY(), rot=(0, math.radians(90), 0), verts=12))
    p.append(cone((1.75, 0, 0.5), 0.3, 0.06, 0.6, BODY_D(), rot=(0, math.radians(90), 0), verts=12))
    # swept wings
    for side in (-1, 1):
        p += wing(side, -0.25, 1.75, 1.05, 0.48, 0.48, 22.0, 0.08, BODY_D())
    p.append(box((-1.4, 0, 0.5), (0.45, 1.4, 0.08), BODY_D()))
    p.append(box((-1.55, 0, 0.85), (0.3, 0.07, 0.7), BODY_D()))
    p.append(box((1.0, 0, 0.78), (0.7, 0.42, 0.26), GLASS()))
    p.append(cyl((-1.65, 0, 0.5), 0.2, 0.16, METAL(), rot=(0, math.radians(90), 0)))
    return p


def a_heli():
    p = []
    p.append(box((0, 0, 0.62), (2.4, 0.85, 0.75), BODY()))
    p.append(box((1.25, 0, 0.66), (0.7, 0.7, 0.6), GLASS()))
    p.append(box((-1.9, 0, 0.66), (1.6, 0.22, 0.22), BODY()))
    p.append(cyl((0, 0, 1.08), 0.11, 0.34, METAL()))
    # rotor blades: 0.26 wide and DARK. At 0.16 x METAL they were a one-pixel line that
    # read as a scratch rather than a rotor.
    p.append(box((0, 0, 1.26), (0.26, 3.6, 0.14), TRACK()))
    p.append(box((0, 0, 1.26), (3.6, 0.26, 0.14), TRACK()))
    p.append(cyl((0, 0, 1.26), 0.16, 0.16, METAL(), verts=10))
    # tail rotor, also thickened, mounted on a fin so it is not floating
    p.append(box((-2.62, 0, 0.98), (0.10, 0.72, 0.72), BODY()))
    p.append(box((-2.66, 0, 0.98), (0.12, 0.12, 0.95), TRACK()))
    for side in (-1, 1):
        p.append(box((-0.3, side * 0.5, 0.35), (1.4, 0.1, 0.12), METAL()))
        p.append(cyl((0.2, side * 0.5, 0.3), 0.14, 0.1, RUBBER(), rot=(math.radians(90), 0, 0)))
    return p


# ------------------------------------------------------------------ soft / utility
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
    p = []
    p.append(box((0, 0, 0.42), (2.1, 1.2, 0.42), BODY()))
    p.append(box((0, 0, 0.66), (1.8, 1.05, 0.14), BODY()))
    p += tracks(2.1, 0.68, 0.26, 0.26)
    p.append(cyl((0, 0, 0.86), 0.42, 0.32, BODY(), verts=12))
    p.append(cyl((0, 0, 0.74), 0.50, 0.12, BODY_D(), verts=12))
    p.append(cyl((0.58, 0, 0.86), 0.12, 1.05, METAL(), rot=(0, math.radians(90), 0), verts=8))
    p.append(box((-0.32, 0, 1.06), (0.3, 0.26, 0.14), BODY_D()))
    p.append(cyl((0.95, -0.42, 0.6), 0.07, 0.2, ACCENT(), rot=(0, math.radians(90), 0), verts=6))
    return p


def t_super_heavy():
    p = []
    p.append(box((0, 0, 0.72), (4.2, 2.2, 0.8), BODY()))
    p.append(box((0, 0, 1.20), (3.8, 2.0, 0.22), BODY()))
    p.append(box((0, 0, 0.36), (4.3, 2.26, 0.22), BODY_D()))
    p += tracks(4.2, 1.20, 0.42, 0.42)
    for side in (-1, 1):
        for i in range(6):
            p.append(cyl((-1.5 + i * 0.6, side * 1.40, 0.42), 0.22, 0.1, METAL(), rot=(math.radians(90), 0, 0)))
    p.append(cyl((0, 0, 1.42), 0.88, 0.22, BODY_D(), verts=16))
    p.append(cyl((0, 0, 1.66), 0.76, 0.42, BODY(), verts=16))
    p.append(cyl((0, 0, 1.90), 0.42, 0.10, BODY_D(), verts=12))
    p.append(cyl((0.95, 0, 1.66), 0.19, 2.0, METAL(), rot=(0, math.radians(90), 0), verts=8))
    p.append(cyl((1.92, 0, 1.66), 0.24, 0.26, BODY_D(), rot=(0, math.radians(90), 0), verts=8))
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
    p = []
    p.append(cyl((0, 0, 0.62), 0.34, 4.6, BODY(), rot=(0, math.radians(90), 0), verts=12))
    p.append(cone((2.45, 0, 0.62), 0.34, 0.06, 0.7, BODY_D(), rot=(0, math.radians(90), 0), verts=12))
    p.append(box((0.1, 0, 0.66), (1.3, 5.6, 0.12), BODY_D()))
    p.append(box((-1.75, 0, 0.66), (0.7, 2.0, 0.10), BODY_D()))
    p.append(box((-2.05, 0, 1.05), (0.4, 0.10, 0.85), BODY_D()))
    p.append(box((1.05, 0, 0.92), (0.8, 0.46, 0.28), GLASS()))
    # engine nacelles: 0.36 radius, 1.0 long. At 0.20 they were lost under the wing and the
    # bomber was indistinguishable from the transport.
    for side in (-1, 1):
        p.append(cyl((-0.20, side * 1.45, 0.46), 0.36, 1.05, BODY_D(),
                     rot=(0, math.radians(90), 0), verts=12))
        p.append(cyl((0.36, side * 1.45, 0.46), 0.26, 0.18, TRACK(),
                     rot=(0, math.radians(90), 0), verts=10))
        p.append(box((-0.20, side * 1.45, 0.78), (0.60, 0.60, 0.12), BODY()))
    return p


def a_recon_plane():
    p = []
    p.append(cyl((0, 0, 0.5), 0.22, 3.0, BODY(), rot=(0, math.radians(90), 0), verts=12))
    p.append(cone((1.65, 0, 0.5), 0.22, 0.05, 0.5, BODY_D(), rot=(0, math.radians(90), 0), verts=12))
    p.append(box((0.05, 0, 0.54), (0.75, 4.2, 0.08), BODY_D()))
    p.append(box((-1.35, 0, 0.54), (0.45, 1.3, 0.07), BODY_D()))
    p.append(box((-1.55, 0, 0.82), (0.32, 0.07, 0.55), BODY_D()))
    p.append(box((0.75, 0, 0.76), (0.7, 0.4, 0.22), GLASS()))
    p.append(cyl((1.75, 0, 0.5), 0.05, 0.08, METAL(), rot=(0, math.radians(90), 0), verts=6))
    return p


def a_seaplane():
    p = []
    p.append(cyl((0, 0, 0.72), 0.26, 3.0, BODY(), rot=(0, math.radians(90), 0), verts=12))
    p.append(cone((1.68, 0, 0.72), 0.26, 0.05, 0.5, BODY_D(), rot=(0, math.radians(90), 0), verts=12))
    p.append(box((0.05, 0, 0.76), (0.9, 3.8, 0.09), BODY_D()))
    p.append(box((-1.35, 0, 0.76), (0.45, 1.2, 0.08), BODY_D()))
    p.append(box((-1.55, 0, 1.02), (0.32, 0.07, 0.55), BODY_D()))
    p.append(box((0.8, 0, 0.96), (0.7, 0.42, 0.24), GLASS()))
    # floats: 0.36 radius and pushed outboard to 1.15. At 0.18 radius tucked under the
    # fuselage they were completely invisible once downscaled, so the seaplane looked like
    # every other monoplane.
    for side in (-1, 1):
        p.append(cyl((0.1, side * 1.15, 0.22), 0.36, 2.4, BODY_D(),
                     rot=(0, math.radians(90), 0), verts=12))
        p.append(cone((1.35, side * 1.15, 0.22), 0.36, 0.10, 0.5, BODY_D(),
                      rot=(0, math.radians(90), 0), verts=12))
        p.append(box((0.45, side * 1.15, 0.58), (0.20, 0.20, 0.72), METAL()))
        p.append(box((-0.55, side * 1.15, 0.58), (0.20, 0.20, 0.72), METAL()))
    return p


def a_transport_plane():
    p = []
    p.append(box((0, 0, 0.75), (3.6, 0.6, 0.7), BODY()))
    p.append(box((1.85, 0, 0.75), (0.5, 0.5, 0.55), BODY_D()))
    p.append(box((1.9, 0, 0.95), (0.4, 0.4, 0.2), GLASS()))
    p.append(box((0.05, 0, 1.10), (1.2, 5.0, 0.10), BODY_D()))
    p.append(box((-1.6, 0, 1.15), (0.5, 1.9, 0.09), BODY_D()))
    p.append(box((-1.85, 0, 1.55), (0.35, 0.09, 0.8), BODY_D()))
    for side in (-1, 1):
        p.append(cyl((0.25, side * 1.45, 0.62), 0.18, 0.5, BODY_D(),
                     rot=(0, math.radians(90), 0), verts=10))
    return p


def a_ground_attack():
    p = []
    p.append(cyl((0, 0, 0.58), 0.34, 3.0, BODY(), rot=(0, math.radians(90), 0), verts=12))
    p.append(cone((1.7, 0, 0.58), 0.34, 0.08, 0.6, BODY_D(), rot=(0, math.radians(90), 0), verts=12))
    p.append(box((0.0, 0, 0.56), (1.0, 3.6, 0.12), BODY_D()))
    p.append(box((-1.45, 0, 0.58), (0.5, 1.3, 0.09), BODY_D()))
    p.append(box((-1.62, 0, 0.92), (0.34, 0.08, 0.7), BODY_D()))
    p.append(box((1.0, 0, 0.88), (0.8, 0.44, 0.26), GLASS()))
    # chin gun + underwing stores
    p.append(cyl((1.9, 0, 0.42), 0.09, 0.9, METAL(), rot=(0, math.radians(90), 0), verts=6))
    for side in (-1, 1):
        for i in range(2):
            p.append(cyl((0.1 - i * 0.55, side * 0.95, 0.40), 0.09, 0.6, BODY_D(),
                         rot=(0, math.radians(90), 0), verts=6))
    return p


def a_jet_bomber():
    p = []
    p.append(cyl((0, 0, 0.58), 0.32, 3.4, BODY(), rot=(0, math.radians(90), 0), verts=12))
    p.append(cone((1.85, 0, 0.58), 0.32, 0.06, 0.6, BODY_D(), rot=(0, math.radians(90), 0), verts=12))
    for side in (-1, 1):
        p += wing(side, -0.35, 1.85, 1.15, 0.50, 0.56, 30.0, 0.09, BODY_D())
        p.append(cyl((-0.55, side * 1.35, 0.44), 0.14, 0.7, METAL(),
                     rot=(0, math.radians(90), 0), verts=8))
    p.append(box((-1.45, 0, 0.58), (0.5, 1.6, 0.09), BODY_D()))
    p.append(box((-1.62, 0, 0.95), (0.32, 0.08, 0.7), BODY_D()))
    p.append(box((1.1, 0, 0.88), (0.7, 0.42, 0.24), GLASS()))
    return p


def a_vtol():
    p = []
    p.append(box((0, 0, 0.62), (2.8, 0.9, 0.6), BODY()))
    p.append(box((1.45, 0, 0.68), (0.7, 0.7, 0.5), GLASS()))
    p.append(box((0.1, 0, 0.98), (1.4, 0.32, 0.12), BODY_D()))
    # two tilt rotors on stub wings
    for side in (-1, 1):
        p.append(box((0.25, side * 0.95, 0.66), (0.55, 1.1, 0.14), BODY_D()))
        p.append(cyl((0.25, side * 1.35, 0.80), 0.10, 0.30, METAL(), verts=8))
        p.append(box((0.25, side * 1.35, 0.98), (0.10, 1.5, 0.05), METAL()))
    p.append(box((-1.35, 0, 0.72), (0.35, 1.5, 0.08), BODY_D()))
    p.append(box((-1.5, 0, 1.0), (0.28, 0.06, 0.5), BODY_D()))
    return p


def a_zeppelin_air():
    """Airship with a readable structure.

    The first version was a bare cigar: hull plus tail fins that vanished at 24px. Now the
    hull is banded with dark rings (so the cylinder reads as segmented rather than smooth),
    the tail carries a large cruciform of four fins, two engine nacelles hang under the
    centre where they break the silhouette, and the gondola is a proper glazed cabin.
    """
    p = []
    p.append(cyl((0, 0, 0.95), 0.56, 4.4, BODY(), rot=(0, math.radians(90), 0), verts=18))
    p.append(cone((2.60, 0, 0.95), 0.56, 0.12, 0.85, BODY_D(), rot=(0, math.radians(90), 0), verts=18))
    p.append(cone((-2.60, 0, 0.95), 0.56, 0.10, 0.85, BODY(), rot=(0, math.radians(-90), 0), verts=18))
    # dark bands: make the envelope read as segmented, not as a smooth tube
    for x in (-1.75, -0.85, 0.05, 0.95, 1.85):
        p.append(cyl((x, 0, 0.95), 0.585, 0.12, BODY_D(), rot=(0, math.radians(90), 0), verts=18))
    # cruciform tail: four big fins, unmistakable from any facing
    p.append(box((-2.05, 0, 0.95), (1.0, 3.4, 0.12), BODY_D()))
    p.append(box((-2.05, 0, 0.95), (1.0, 0.12, 2.6), BODY_D()))
    p.append(box((-2.55, 0, 0.95), (0.30, 1.5, 0.10), ACCENT()))
    # engine nacelles under the hull
    for dy in (-0.75, 0.75):
        p.append(box((-0.30, dy, 0.42), (0.85, 0.34, 0.30), BODY_D()))
        p.append(cyl((0.20, dy, 0.42), 0.15, 0.24, METAL(),
                     rot=(0, math.radians(90), 0), verts=8))
    # gondola: glazed cabin with a nose
    p.append(box((0.55, 0, 0.30), (1.35, 0.60, 0.36), BODY_D()))
    p.append(box((0.95, 0, 0.34), (0.42, 0.52, 0.22), GLASS()))
    p.append(box((0.55, 0, 0.10), (1.0, 0.45, 0.12), METAL()))
    return p

# ================================================================== PLA (1949-1991)
# Cold-War People's Liberation Army inventory. Silhouette cues are what matter at 24px:
# Type 59's dome turret + long 100mm gun, J-7's delta wing and nose shock cone, Q-5's
# side intakes and pointed nose. Painted in olive (--paint ra_olive).
PLA_GREEN = "ra_olive"


def p_type59():
    """Type 59: T-54 derivative. Dome turret, 100mm, five road wheels, rounded glacis."""
    p = []
    p.append(boxr((0.85, 0, 0.62), (0.9, 1.5, 0.46), BODY(), (0, math.radians(38), 0)))
    p.append(box((-0.2, 0, 0.55), (2.6, 1.5, 0.52), BODY()))
    p.append(box((0, 0, 0.32), (2.9, 1.56, 0.18), BODY_D()))
    p += tracks(2.9, 0.86, 0.30, 0.34)
    for side in (-1, 1):
        for i in range(5):
            p.append(cyl((-1.05 + i * 0.52, side * 1.02, 0.30), 0.21, 0.10, METAL(),
                         rot=(math.radians(90), 0, 0)))
    # dome turret: the signature shape
    p.append(cyl((0, 0, 0.94), 0.70, 0.16, BODY_D(), verts=18))
    p.append(box((0, 0, 1.14), (1.06, 1.06, 0.30), BODY()))
    p.append(cyl((0, 0, 1.32), 0.50, 0.14, BODY(), verts=16))
    p.append(cyl((0, 0, 1.42), 0.22, 0.08, BODY_D(), verts=12))       # hatch
    # long 100mm with a bore evacuator
    p.append(cyl((0.80, 0, 1.14), 0.13, 1.55, METAL(), rot=(0, math.radians(90), 0), verts=8))
    p.append(cyl((0.55, 0, 1.14), 0.19, 0.26, BODY_D(), rot=(0, math.radians(90), 0), verts=8))
    p.append(cyl((1.58, 0, 1.14), 0.15, 0.22, BODY_D(), rot=(0, math.radians(90), 0), verts=8))
    p.append(box((-0.55, 0.45, 1.30), (0.28, 0.24, 0.18), BODY_D()))  # AA MG
    return p


def p_type62():
    """Type 62: light tank.

    The first pass was a bland slab because at 24px a 0.92-wide turret and a thin 1.15m gun
    both vanish. It now borrows the Type 59's readable recipe - a raised turret with a
    visible ring, a longer and fatter 85mm with a muzzle brake, fenders and spare track
    links - so it still reads as light (shorter hull, fewer wheels) while having enough
    structure to survive the downscale.
    """
    p = []
    p.append(boxr((0.68, 0, 0.50), (0.75, 1.20, 0.34), BODY(), (0, math.radians(34), 0)))
    p.append(box((-0.15, 0, 0.46), (2.0, 1.20, 0.42), BODY()))
    p.append(box((0, 0, 0.26), (2.2, 1.26, 0.16), BODY_D()))
    p += tracks(2.2, 0.70, 0.26, 0.30)
    # fenders: hard horizontal line over the tracks
    for side in (-1, 1):
        p.append(box((0, side * 0.71, 0.60), (2.35, 0.24, 0.09), BODY()))
    # spare track links on the hull side
    for side in (-1, 1):
        for i in range(2):
            p.append(box((-0.35 + i * 0.55, side * 0.64, 0.50), (0.38, 0.09, 0.22), BODY_D()))
    # raised turret: ring, body, cupola
    p.append(cyl((0, 0, 0.70), 0.62, 0.14, BODY_D(), verts=16))
    p.append(box((0, 0, 0.88), (1.02, 1.02, 0.34), BODY()))
    p.append(cyl((0, 0, 1.06), 0.26, 0.10, BODY_D(), verts=12))
    # 85mm: longer and fatter, with a muzzle brake
    p.append(cyl((0.78, 0, 0.88), 0.115, 1.75, METAL(), rot=(0, math.radians(90), 0), verts=8))
    p.append(cyl((1.62, 0, 0.88), 0.155, 0.26, BODY_D(), rot=(0, math.radians(90), 0), verts=8))
    p.append(box((-0.48, 0.42, 1.02), (0.26, 0.22, 0.16), BODY_D()))
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
