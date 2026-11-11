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

MATS = {}
WALL_COLOURS = [
    ("olive", (0.24, 0.27, 0.12), (0.14, 0.16, 0.07)),
    ("sand", (0.44, 0.35, 0.15), (0.26, 0.20, 0.08)),
    ("grey", (0.26, 0.27, 0.29), (0.15, 0.16, 0.17)),
    ("rust", (0.32, 0.17, 0.08), (0.19, 0.10, 0.05)),
    ("desert", (0.50, 0.42, 0.24), (0.30, 0.25, 0.14)),
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
METAL = lambda: mat("metal", (0.24, 0.24, 0.26), 0.45, 0.7)
TRACK = lambda: mat("track", (0.10, 0.10, 0.11), 0.9)
GLASS = lambda: mat("glass", (0.09, 0.14, 0.19), 0.25)
RUBBER = lambda: mat("rubber", (0.07, 0.07, 0.07), 0.95)
ACCENT = lambda: mat("accent", (0.5, 0.42, 0.1), 0.6)


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
    p = []
    p.append(box((0, 0, 0.5), (2.8, 1.5, 0.55), BODY()))
    p.append(box((0, 0, 0.84), (2.3, 1.3, 0.22), BODY()))
    p += tracks(2.8, 0.85, 0.32)
    for i in range(5):
        p.append(cyl((-1.0 + i * 0.5, 1.02, 0.32), 0.2, 0.1, METAL(), rot=(math.radians(90), 0, 0)))
    p.append(cyl((0, 0, 1.06), 0.55, 0.44, BODY_D(), verts=14))
    p.append(cyl((0.62, 0, 1.06), 0.07, 1.1, METAL(), rot=(0, math.radians(90), 0)))
    p.append(box((-0.5, 0, 1.16), (0.35, 0.3, 0.16), METAL()))
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


def setup_lights():
    key = bpy.data.lights.new("key", type="SUN")
    key.energy = 3.2
    key.angle = math.radians(10)
    ko = bpy.data.objects.new("key", key)
    bpy.context.collection.objects.link(ko)
    ko.rotation_euler = (math.radians(55), 0, math.radians(-40))
    fill = bpy.data.lights.new("fill", type="SUN")
    fill.energy = 1.2
    fo = bpy.data.objects.new("fill", fill)
    bpy.context.collection.objects.link(fo)
    fo.rotation_euler = (math.radians(70), 0, math.radians(150))
    w = bpy.data.worlds.new("w")
    w.use_nodes = True
    w.node_tree.nodes["Background"].inputs[0].default_value = (0.2, 0.2, 0.22, 1)
    w.node_tree.nodes["Background"].inputs[1].default_value = 0.6
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
