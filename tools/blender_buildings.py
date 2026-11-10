"""Generate a library of C&C-style buildings procedurally and render N facings each.

Why procedural rather than downloaded meshes: there is essentially no CC0 library of
military buildings (OpenGameArt's 3D entries are medieval houses, and the military base
models live on paid/registration sites). Procedural geometry also gives us what we
actually need for RA-style art - a consistent silhouette language, a fixed light
direction, and exact control over the footprint - none of which a random mesh provides.

Buildings are assemblies of boxes plus a few rounded pieces. Each has:
  * a distinct footprint, so the silhouette differs from the next building
  * secondary detail (vents, doors, roof gear, stacks, railings) because a bare box
    reads as nothing at 48 px
  * two-tone paint (body + trim) so the quantiser produces edge separation

Usage:
  blender --background --python blender_buildings.py -- <out_dir> [--facings 8] [--size 512] [--only name,...]
"""
import math
import os
import sys

import bpy
from mathutils import Vector

# ---------------------------------------------------------------- materials
MATS = {}


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


def box(loc, size, material):
    bpy.ops.mesh.primitive_cube_add(size=2.0, location=loc)
    ob = bpy.context.active_object
    ob.scale = (size[0] / 2, size[1] / 2, size[2] / 2)
    ob.data.materials.append(material)
    return ob


def cyl(loc, radius, depth, material, rot=(0, 0, 0), verts=12):
    bpy.ops.mesh.primitive_cylinder_add(radius=radius, depth=depth, location=loc, rotation=rot, vertices=verts)
    ob = bpy.context.active_object
    ob.data.materials.append(material)
    return ob


def cone(loc, r1, r2, depth, material, verts=12):
    bpy.ops.mesh.primitive_cone_add(radius1=r1, radius2=r2, depth=depth, location=loc, vertices=verts)
    ob = bpy.context.active_object
    ob.data.materials.append(material)
    return ob


# ---------------------------------------------------------------- palette
RED = lambda: mat("red", (0.40, 0.05, 0.05))
RED_DARK = lambda: mat("red_dark", (0.24, 0.03, 0.03))
GREY = lambda: mat("grey", (0.34, 0.34, 0.36))
GREY_DARK = lambda: mat("grey_dark", (0.16, 0.16, 0.18))
STEEL = lambda: mat("steel", (0.26, 0.26, 0.29), 0.45, 0.7)
CEMENT = lambda: mat("cement", (0.46, 0.45, 0.42))
GLASS = lambda: mat("glass", (0.10, 0.16, 0.22), 0.25)
YELLOW = lambda: mat("yellow", (0.55, 0.42, 0.06))


def pad(w, d, h=0.12):
    """Concrete foundation under everything - makes the building sit on the ground."""
    return [box((0, 0, h / 2), (w, d, h), CEMENT())]


def vents(n, x0, y, z, step=0.55):
    out = []
    for i in range(n):
        out.append(box((x0 + i * step, y, z), (0.32, 0.32, 0.14), GREY_DARK()))
    return out


def windows(n, x0, y, z, step=0.6):
    out = []
    for i in range(n):
        out.append(box((x0 + i * step, y, z), (0.3, 0.06, 0.3), GLASS()))
    return out


# ---------------------------------------------------------------- buildings
def b_barracks():
    p = pad(5.4, 4.0)
    p.append(box((0, 0, 1.15), (5.0, 3.6, 2.0), RED()))
    p.append(box((0, 0, 2.28), (5.3, 3.9, 0.24), GREY()))
    p.append(box((2.3, 1.1, 0.65), (1.9, 1.7, 1.1), RED()))
    p.append(box((2.3, 1.1, 1.28), (2.05, 1.85, 0.16), GREY()))
    p.append(box((0, -1.86, 0.75), (1.4, 0.16, 1.4), GREY_DARK()))
    p.append(box((-1.75, -1.0, 2.55), (0.5, 0.5, 1.5), STEEL()))
    p += windows(4, -1.8, -1.84, 1.35)
    p += vents(2, 1.2, -1.84, 2.45)
    return p


def b_factory():
    p = pad(7.6, 4.8)
    p.append(box((0, 0, 1.5), (7.0, 4.2, 2.8), RED()))
    p.append(box((0, 0, 3.02), (7.3, 4.5, 0.24), GREY()))
    p.append(box((0, -2.16, 1.15), (3.2, 0.16, 2.3), GREY_DARK()))
    p.append(box((-2.9, 1.7, 3.5), (1.0, 1.0, 2.0), RED()))
    p.append(box((-2.9, 1.7, 4.55), (1.1, 1.1, 0.16), GREY()))
    p += vents(3, 0.6, -2.16, 3.15)
    p.append(box((2.6, -2.16, 0.9), (0.5, 0.16, 0.7), YELLOW()))
    return p


def b_power():
    p = pad(4.6, 3.4)
    p.append(box((0, 0, 0.7), (4.2, 3.0, 1.2), RED()))
    p.append(box((-1.1, 0, 2.6), (0.95, 0.95, 3.6), STEEL()))
    p.append(box((1.1, 0, 2.2), (0.95, 0.95, 2.8), STEEL()))
    p.append(cyl((-1.1, 0, 4.5), 0.55, 0.25, GREY_DARK()))
    p.append(cyl((1.1, 0, 3.7), 0.55, 0.25, GREY_DARK()))
    p.append(box((0, 1.1, 1.05), (3.4, 0.26, 0.26), GREY_DARK()))
    return p


def b_radar():
    p = pad(3.6, 3.6)
    p.append(box((0, 0, 0.8), (3.2, 3.2, 1.5), RED()))
    p.append(cyl((0, 0, 1.9), 0.35, 0.9, GREY_DARK()))
    p.append(cyl((0, 0, 2.5), 1.35, 0.12, GREY(), rot=(math.radians(28), 0, 0), verts=16))
    p.append(box((0, 0, 2.62), (0.12, 0.12, 0.5), YELLOW()))
    p.append(box((1.4, -1.4, 0.35), (0.5, 0.5, 0.7), GREY()))
    return p


def b_refinery():
    p = pad(7.4, 5.0)
    p.append(box((-0.8, 0, 1.3), (5.0, 4.2, 2.4), RED()))
    p.append(box((-0.8, 0, 2.62), (5.3, 4.5, 0.24), GREY()))
    p.append(cyl((2.6, 0.9, 1.4), 0.9, 2.8, GREY(), verts=16))
    p.append(cyl((2.6, 0.9, 2.9), 0.95, 0.16, GREY_DARK(), verts=16))
    p.append(cyl((2.6, -0.9, 1.2), 0.7, 2.4, STEEL(), verts=16))
    p.append(box((1.0, 0, 0.5), (2.4, 0.5, 0.5), GREY_DARK()))
    p += vents(2, -2.4, -2.14, 2.85)
    return p


def b_conyard():
    p = pad(7.4, 5.2)
    p.append(box((0, 0, 1.5), (6.4, 4.4, 2.8), RED()))
    p.append(box((0, 0, 3.0), (6.7, 4.7, 0.26), GREY()))
    # crane
    p.append(box((-2.4, 1.4, 4.4), (0.4, 0.4, 3.2), YELLOW()))
    p.append(box((-1.2, 1.4, 5.9), (3.0, 0.35, 0.35), YELLOW()))
    p.append(box((0.4, 1.4, 5.4), (0.1, 0.1, 0.9), GREY_DARK()))
    p += windows(3, -1.6, -2.24, 1.6)
    p.append(box((2.2, -2.24, 1.0), (1.2, 0.16, 1.8), GREY_DARK()))
    return p


def b_repair():
    p = pad(5.2, 4.0)
    p.append(box((0, 0, 1.2), (4.8, 3.4, 2.2), RED()))
    p.append(box((0, 0, 2.42), (5.1, 3.7, 0.24), GREY()))
    p.append(box((0, -1.72, 0.85), (2.6, 0.16, 1.7), GREY_DARK()))
    p.append(box((0, -1.72, 1.75), (2.8, 0.2, 0.2), YELLOW()))
    p += vents(2, -1.9, -1.72, 2.55)
    return p


def b_helipad():
    p = pad(5.0, 5.0)
    p.append(box((0, 0, 0.28), (4.6, 4.6, 0.3), GREY()))
    p.append(box((0, -2.0, 0.3), (4.6, 0.3, 0.34), RED()))
    p.append(box((0, 2.0, 0.3), (4.6, 0.3, 0.34), RED()))
    p.append(box((-2.0, 0, 0.3), (0.3, 4.6, 0.34), RED()))
    p.append(box((2.0, 0, 0.3), (0.3, 4.6, 0.34), RED()))
    # H marking
    p.append(box((-0.55, 0, 0.46), (0.22, 1.6, 0.02), YELLOW()))
    p.append(box((0.55, 0, 0.46), (0.22, 1.6, 0.02), YELLOW()))
    p.append(box((0, 0, 0.46), (1.1, 0.22, 0.02), YELLOW()))
    return p


def b_silo():
    p = pad(3.0, 3.0)
    p.append(cyl((0, 0, 1.5), 1.15, 3.0, RED(), verts=16))
    p.append(cyl((0, 0, 3.05), 1.2, 0.2, GREY(), verts=16))
    p.append(cyl((0, 0, 3.35), 0.5, 0.45, STEEL(), verts=12))
    for i in range(4):
        a = math.radians(90 * i + 45)
        p.append(box((1.3 * math.cos(a), 1.3 * math.sin(a), 0.9), (0.12, 0.12, 1.8), GREY_DARK()))
    return p


def b_turret_tower():
    p = pad(2.6, 2.6)
    p.append(box((0, 0, 0.55), (2.2, 2.2, 1.0), RED_DARK()))
    p.append(cyl((0, 0, 1.55), 0.72, 0.9, RED(), verts=14))
    p.append(box((0, -0.85, 1.55), (0.16, 1.1, 0.16), STEEL()))
    p.append(box((0.42, -0.8, 1.55), (0.16, 1.0, 0.16), STEEL()))
    p.append(cyl((0, 0, 2.1), 0.5, 0.2, GREY(), verts=14))
    return p


def b_wall():
    p = pad(3.2, 1.0, 0.1)
    p.append(box((0, 0, 0.65), (3.0, 0.6, 1.1), CEMENT()))
    p.append(box((0, 0, 1.24), (3.1, 0.7, 0.12), GREY()))
    for i in range(3):
        p.append(box((-1.0 + i, 0, 1.4), (0.24, 0.7, 0.2), CEMENT()))
    return p


def b_airfield():
    p = pad(7.0, 5.0, 0.1)
    p.append(box((0, 0, 0.16), (6.6, 4.6, 0.12), GREY_DARK()))
    p.append(box((0, 2.2, 0.35), (6.6, 0.3, 0.2), YELLOW()))
    for i in range(4):
        p.append(box((-2.4 + i * 1.6, 0, 0.2), (0.9, 0.14, 0.02), YELLOW()))
    p.append(box((-2.2, -1.6, 1.1), (1.2, 1.2, 1.8), RED()))
    p.append(box((-2.2, -1.6, 2.06), (1.35, 1.35, 0.16), GREY()))
    return p


def b_oil():
    p = pad(4.2, 3.0)
    for i, x in enumerate((-1.3, 0.0, 1.3)):
        p.append(cyl((x, 0, 1.35), 0.55, 2.2, GREY(), verts=14))
        p.append(cyl((x, 0, 2.5), 0.58, 0.12, GREY_DARK(), verts=14))
        p.append(box((x, 0.9, 0.5), (0.16, 1.4, 0.16), STEEL()))
    return p


def b_command():
    p = pad(5.6, 4.4)
    p.append(box((0, 0, 1.3), (5.2, 4.0, 2.4), RED()))
    p.append(box((0, 0, 2.6), (5.5, 4.3, 0.26), GREY()))
    p.append(cyl((0, 0, 3.5), 1.05, 1.5, GREY(), verts=16))
    p.append(cone((0, 0, 4.5), 1.1, 0.15, 0.9, GREY_DARK(), verts=16))
    p.append(box((0, 0, 4.0), (0.1, 0.1, 0.8), STEEL()))
    p += windows(4, -1.9, -2.04, 1.5)
    p.append(box((0, -2.04, 0.7), (1.2, 0.14, 1.4), GREY_DARK()))
    return p


BUILDINGS = {
    "barracks": b_barracks,
    "factory": b_factory,
    "power": b_power,
    "radar": b_radar,
    "refinery": b_refinery,
    "conyard": b_conyard,
    "repair": b_repair,
    "helipad": b_helipad,
    "silo": b_silo,
    "tower": b_turret_tower,
    "wall": b_wall,
    "airfield": b_airfield,
    "oilstore": b_oil,
    "command": b_command,
}


# ---------------------------------------------------------------- rig
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


def mutate(seed):
    """Deterministic per-variant jitter: overall scale + colour temperature.

    Structure stays the same (we are not going to write 56 distinct buildings by hand),
    but scale and hue variation stops the adapter from memorising one exact silhouette
    and colour for each class. Seeded so a rebuild reproduces the same set.
    """
    import random
    rng = random.Random(seed)
    s = rng.uniform(0.88, 1.14)
    warm = rng.uniform(0.9, 1.12)
    for ob in bpy.data.objects:
        if ob.type != "MESH":
            continue
        ob.scale = (ob.scale.x * s, ob.scale.y * s, ob.scale.z * s)
        ob.location = (ob.location.x * s, ob.location.y * s, ob.location.z * s)
        for m in ob.data.materials:
            if not m or not m.use_nodes:
                continue
            b = m.node_tree.nodes.get("Principled BSDF")
            if not b:
                continue
            col = list(b.inputs["Base Color"].default_value)
            col[0] = min(1.0, col[0] * warm)
            col[2] = min(1.0, col[2] * (2.0 - warm) * 0.5 + col[2] * 0.5)
            b.inputs["Base Color"].default_value = (col[0], col[1], col[2], 1.0)


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    out = argv[0] if argv else "."
    facings, size = 8, 512
    only = None
    variants = 1
    i = 1
    while i < len(argv):
        if argv[i] == "--facings" and i + 1 < len(argv):
            facings = int(argv[i + 1]); i += 2
        elif argv[i] == "--size" and i + 1 < len(argv):
            size = int(argv[i + 1]); i += 2
        elif argv[i] == "--only" and i + 1 < len(argv):
            only = set(argv[i + 1].split(",")); i += 2
        elif argv[i] == "--variants" and i + 1 < len(argv):
            variants = int(argv[i + 1]); i += 2
        else:
            i += 1
    os.makedirs(out, exist_ok=True)

    names = [n for n in BUILDINGS if (only is None or n in only)]
    for name in names:
      for v in range(variants):
        tag = name if variants == 1 else f"{name}_v{v}"
        MATS.clear()
        bpy.ops.wm.read_factory_settings(use_empty=True)
        BUILDINGS[name]()
        if variants > 1:
            mutate(hash(name) % 100000 + v * 7919)
        lo, hi = mesh_bounds()
        centre = (lo + hi) * 0.5
        extent = max(hi.x - lo.x, hi.y - lo.y, (hi.z - lo.z) * 1.4)

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
        print(f"[built] {tag}: {facings} facings", flush=True)

    print(f"[done] {len(names)} buildings -> {out}")


if __name__ == "__main__":
    main()
