"""Render N facings of an existing .blend model with a consistent isometric camera.

Unlike blender_render.py (which builds its own boxes), this opens someone else's .blend
and has to cope with whatever is inside it: unknown units, unknown origin, possibly
extra cameras/lights/empties. So it:
  * keeps only mesh objects (removes cameras/lights from the original file)
  * frames the whole mesh bounding box automatically
  * installs its own orthographic camera and upper-left light rig

Run headless:
  blender --background --python blender_model_render.py -- <in.blend> <out_dir>
          [--facings 32] [--size 512] [--name tank]
"""
import math
import os
import sys

import bpy
from mathutils import Vector


def load_and_clean(path):
    bpy.ops.wm.open_mainfile(filepath=path)
    # drop anything that is not geometry - the source files ship their own cameras
    for ob in list(bpy.data.objects):
        if ob.type != "MESH":
            bpy.data.objects.remove(ob, do_unlink=True)
    meshes = [o for o in bpy.data.objects if o.type == "MESH"]
    return meshes


def bounds(meshes):
    lo = Vector((1e9, 1e9, 1e9))
    hi = Vector((-1e9, -1e9, -1e9))
    for ob in meshes:
        for corner in ob.bound_box:
            w = ob.matrix_world @ Vector(corner)
            lo = Vector((min(lo.x, w.x), min(lo.y, w.y), min(lo.z, w.z)))
            hi = Vector((max(hi.x, w.x), max(hi.y, w.y), max(hi.z, w.z)))
    return lo, hi


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
    fill.energy = 1.3
    fo = bpy.data.objects.new("fill", fill)
    bpy.context.collection.objects.link(fo)
    fo.rotation_euler = (math.radians(70), 0, math.radians(150))

    w = bpy.data.worlds.new("w")
    w.use_nodes = True
    w.node_tree.nodes["Background"].inputs[0].default_value = (0.2, 0.2, 0.22, 1)
    w.node_tree.nodes["Background"].inputs[1].default_value = 0.6
    bpy.context.scene.world = w


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if len(argv) < 2:
        print("usage: blender_model_render.py -- <in.blend> <out_dir> [--facings N] [--size N] [--name x]")
        return
    src, out = argv[0], argv[1]
    facings, size, name = 32, 512, os.path.splitext(os.path.basename(src))[0]
    i = 2
    while i < len(argv):
        if argv[i] == "--facings" and i + 1 < len(argv):
            facings = int(argv[i + 1]); i += 2
        elif argv[i] == "--size" and i + 1 < len(argv):
            size = int(argv[i + 1]); i += 2
        elif argv[i] == "--name" and i + 1 < len(argv):
            name = argv[i + 1]; i += 2
        else:
            i += 1
    os.makedirs(out, exist_ok=True)

    meshes = load_and_clean(src)
    if not meshes:
        print("[error] no mesh objects in file")
        return

    # The CC0 source models ship with default grey materials (or none at all), which
    # quantises to flat grey on the RA palette. Give every mesh a small military palette
    # so the render has the light/dark separation RA sprites rely on.
    paints = [
        (0.30, 0.32, 0.22),   # olive body
        (0.18, 0.19, 0.15),   # dark track / shadowed
        (0.42, 0.42, 0.40),   # metal highlight
    ]
    for i, ob in enumerate(meshes):
        col = paints[i % len(paints)]
        m = bpy.data.materials.new(f"paint_{i}")
        m.use_nodes = True
        b = m.node_tree.nodes.get("Principled BSDF")
        b.inputs["Base Color"].default_value = (*col, 1.0)
        b.inputs["Roughness"].default_value = 0.75
        ob.data.materials.clear()
        ob.data.materials.append(m)
    lo, hi = bounds(meshes)
    centre = (lo + hi) * 0.5
    extent = max(hi.x - lo.x, hi.y - lo.y, hi.z - lo.z)
    print(f"[model] {name}: {len(meshes)} meshes, extent {extent:.3f}, centre {tuple(round(v,3) for v in centre)}")

    setup_render(size)
    setup_lights()

    cam_data = bpy.data.cameras.new("cam")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = extent * 1.45
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
    dist = extent * 4.0 + 10.0
    sc = bpy.context.scene
    for f in range(facings):
        yaw = math.radians(45.0 + 360.0 * f / facings)
        cam.location = (
            centre.x + dist * math.sin(pitch) * math.cos(yaw),
            centre.y - dist * math.sin(pitch) * math.sin(yaw),
            centre.z + dist * math.cos(pitch),
        )
        bpy.context.view_layer.update()
        sc.render.filepath = os.path.join(out, f"{name}_f{facing_pad(f, facings)}.png")
        bpy.ops.render.render(write_still=True)
        if f % 8 == 0:
            print(f"[render] {f}/{facings}", flush=True)
    print(f"[done] {facings} facings of {name} -> {out}")


def facing_pad(f, n):
    return f"{f:02d}" if n <= 100 else f"{f:03d}"


if __name__ == "__main__":
    main()
