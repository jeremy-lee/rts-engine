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
    p = []
    p.append(box((0, 0, 0.5), (2.4, 0.4, 0.4), BODY()))
    p.append(box((0, 0, 0.92), (1.5, 3.0, 0.1), BODY_D()))
    p.append(box((0, 0, 0.32), (1.5, 3.0, 0.1), BODY_D()))
    for side in (-1, 1):
        for x in (-0.6, 0.6):
            p.append(box((x, side * 1.35, 0.62), (0.08, 0.08, 0.68), METAL()))
    p.append(box((-1.35, 0, 0.6), (0.5, 1.3, 0.08), BODY_D()))
    p.append(box((-1.5, 0, 0.94), (0.35, 0.1, 0.5), BODY_D()))
    p.append(cyl((1.28, 0, 0.5), 0.1, 0.2, METAL(), rot=(0, math.radians(90), 0)))
    p.append(box((1.22, 0, 0.5), (0.05, 1.5, 0.16), BODY_D()))
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
        p.append(boxr((-0.25, side * 0.75, 0.48), (1.5, 1.5, 0.08), BODY_D(), (0, 0, side * math.radians(22))))
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
    p.append(cyl((0, 0, 1.08), 0.09, 0.3, METAL()))
    p.append(box((0, 0, 1.24), (0.16, 3.4, 0.06), METAL()))
    p.append(box((-2.65, 0, 0.95), (0.06, 0.6, 0.6), METAL()))
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
    p = []
    p.append(box((0, 0, 0.52), (2.5, 1.4, 0.5), BODY()))
    # dozer blade up front
    p.append(boxr((1.45, 0, 0.34), (0.22, 1.75, 0.62), BODY_D(), (0, math.radians(14), 0)))
    p.append(box((1.20, 0, 0.62), (0.3, 0.3, 0.3), METAL()))
    p += tracks(2.4, 0.82, 0.28, 0.28)
    p.append(box((-0.35, 0, 0.86), (0.9, 1.05, 0.42), BODY()))
    p.append(box((-0.35, 0, 0.9), (0.7, 0.9, 0.2), GLASS()))
    p.append(box((-1.05, 0, 0.80), (0.35, 0.5, 0.28), BODY_D()))
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
    for side in (-1, 1):
        p.append(cyl((-0.25, side * 1.55, 0.50), 0.20, 0.55, BODY_D(),
                     rot=(0, math.radians(90), 0), verts=10))
        p.append(box((-0.25, side * 1.55, 0.50), (0.06, 0.9, 0.16), BODY_D()))
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
    # floats
    for side in (-1, 1):
        p.append(cyl((0.1, side * 0.85, 0.26), 0.18, 2.0, BODY_D(),
                     rot=(0, math.radians(90), 0), verts=10))
        p.append(box((0.5, side * 0.85, 0.5), (0.08, 0.08, 0.4), METAL()))
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
        p.append(boxr((-0.35, side * 0.95, 0.56), (1.5, 1.7, 0.09), BODY_D(),
                      (0, 0, side * math.radians(30))))
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
    p = []
    p.append(cyl((0, 0, 0.9), 0.55, 4.6, BODY(), rot=(0, math.radians(90), 0), verts=16))
    p.append(cone((2.55, 0, 0.9), 0.55, 0.10, 0.8, BODY_D(), rot=(0, math.radians(90), 0), verts=16))
    p.append(cone((-2.3, 0, 0.9), 0.55, 0.28, 0.7, BODY_D(), rot=(0, math.radians(-90), 0), verts=16))
    p.append(box((-1.9, 0, 0.9), (0.8, 2.6, 0.10), BODY_D()))
    p.append(box((-1.9, 0, 1.35), (0.7, 0.10, 0.9), BODY_D()))
    # gondola underneath
    p.append(box((0.2, 0, 0.34), (1.2, 0.5, 0.28), BODY_D()))
    for side in (-1, 1):
        p.append(cyl((0.2, side * 0.2, 0.20), 0.10, 0.2, METAL(), verts=6))
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
        else:
            i += 1
    os.makedirs(out, exist_ok=True)

    names = [n for n in VEHICLES if (only is None or n in only)]
    for name in names:
        for v in range(variants):
            global WALL_PICK
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
