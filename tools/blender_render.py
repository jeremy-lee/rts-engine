"""Procedurally model a C&C-style building in Blender and render N facings.

Run headless:
    blender --background --python blender_render.py -- <out_dir> [--facings 8] [--size 512] [--kind barracks]

Why procedural instead of downloading meshes: CC0 military-building meshes are scarce
and inconsistent, while a parametric box assembly gives us exact control over silhouette,
proportions and light direction - the three things that actually make RA art read as RA.
Swapping in a real mesh later only changes build_building().

Key choices that match OpenRA's look:
  * ORTHOGRAPHIC camera, tilted 30 degrees - RA is not a perspective game
  * light fixed from the upper-left, like the original sprites
  * render at 4x target then downsample, so edges stay crisp instead of aliased
"""
import math
import os
import sys

import bpy


def clear_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def mat(name, rgb, roughness=0.85, metallic=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (*rgb, 1.0)
    bsdf.inputs["Roughness"].default_value = roughness
    if "Metallic" in bsdf.inputs:
        bsdf.inputs["Metallic"].default_value = metallic
    return m


def box(name, loc, size, material):
    # primitive_cube_add(size=2) gives edge length 2 (extent -1..1), so scaling by
    # size/2 yields an object whose edge length is exactly `size`. Using size=1 here
    # and scale=size/2 made every part HALF the intended size, which is why the roof
    # appeared to float above the walls.
    bpy.ops.mesh.primitive_cube_add(size=2.0, location=loc)
    ob = bpy.context.active_object
    ob.name = name
    ob.scale = (size[0] / 2, size[1] / 2, size[2] / 2)
    ob.data.materials.append(material)
    return ob


def build_building(kind="barracks"):
    """A box assembly. Units are metres; ~1 m per RA pixel-tile at this scale."""
    red = mat("wall_red", (0.42, 0.055, 0.055))
    dark = mat("trim_dark", (0.09, 0.09, 0.10))
    grey = mat("roof_grey", (0.30, 0.30, 0.32))
    steel = mat("steel", (0.22, 0.22, 0.24), metallic=0.6, roughness=0.45)

    parts = []
    if kind == "barracks":
        parts.append(box("hall", (0, 0, 1.0), (5.0, 3.6, 2.0), red))
        parts.append(box("roof", (0, 0, 2.1), (5.3, 3.9, 0.22), grey))
        parts.append(box("annex", (2.3, 1.2, 0.55), (2.0, 1.8, 1.1), red))
        parts.append(box("stack", (-1.7, -1.0, 2.3), (0.55, 0.55, 1.6), steel))
        parts.append(box("door", (0, -1.85, 0.7), (1.4, 0.3, 1.4), dark))
    elif kind == "factory":
        parts.append(box("hall", (0, 0, 1.4), (7.0, 4.2, 2.8), red))
        parts.append(box("roof", (0, 0, 2.9), (7.3, 4.5, 0.2), grey))
        parts.append(box("gate", (0, -2.15, 1.1), (3.0, 0.3, 2.2), dark))
        parts.append(box("tower", (-2.8, 1.6, 3.2), (1.0, 1.0, 2.0), red))
    elif kind == "power":
        parts.append(box("base", (0, 0, 0.6), (4.0, 3.0, 1.2), red))
        parts.append(box("tower_a", (-1.0, 0, 2.2), (0.9, 0.9, 3.2), steel))
        parts.append(box("tower_b", (1.0, 0, 1.8), (0.9, 0.9, 2.4), steel))
        parts.append(box("pipe", (0, 1.1, 0.9), (3.4, 0.3, 0.3), dark))
    else:
        parts.append(box("cube", (0, 0, 1.0), (3.0, 3.0, 2.0), red))
    return parts


def setup_camera(tilt_deg=30.0, ortho_scale=7.0, yaw_deg=0.0):
    """True isometric-style camera: 60 deg down, plus an adjustable yaw around Z.

    Rotating the CAMERA rather than the building is deliberate: parenting the parts to an
    empty and spinning that empty left the sub-boxes at their world offsets, so the roof
    floated off the walls. Turning the camera keeps the assembly rigid.
    """
    cam_data = bpy.data.cameras.new("cam")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = ortho_scale
    cam = bpy.data.objects.new("cam", cam_data)
    bpy.context.collection.objects.link(cam)
    bpy.context.scene.camera = cam

    pitch = math.radians(90.0 - tilt_deg)   # 60 deg when tilt is 30
    yaw = math.radians(45.0 + yaw_deg)
    dist = 24.0
    cam.location = (
        dist * math.sin(pitch) * math.cos(yaw),
        -dist * math.sin(pitch) * math.sin(yaw),
        dist * math.cos(pitch) + 1.0,
    )
    empty = bpy.data.objects.new("aim", None)
    bpy.context.collection.objects.link(empty)
    empty.location = (0.0, 0.0, 1.0)
    tc = cam.constraints.new(type="TRACK_TO")
    tc.target = empty
    tc.track_axis = "TRACK_NEGATIVE_Z"
    tc.up_axis = "UP_Y"
    return cam, empty


def setup_lights():
    """Fixed upper-left key light, plus a dim fill - RA sprites are lit this way."""
    key_data = bpy.data.lights.new("key", type="SUN")
    key_data.energy = 3.5
    key_data.angle = math.radians(8)
    key = bpy.data.objects.new("key", key_data)
    bpy.context.collection.objects.link(key)
    key.rotation_euler = (math.radians(55), 0, math.radians(-40))

    fill_data = bpy.data.lights.new("fill", type="SUN")
    fill_data.energy = 1.1
    fill = bpy.data.objects.new("fill", fill_data)
    bpy.context.collection.objects.link(fill)
    fill.rotation_euler = (math.radians(70), 0, math.radians(150))

    world = bpy.data.worlds.new("w")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0.16, 0.16, 0.17, 1)
    world.node_tree.nodes["Background"].inputs[1].default_value = 0.5
    bpy.context.scene.world = world


def setup_render(size):
    sc = bpy.context.scene
    # Blender 4.2 renamed EEVEE to BLENDER_EEVEE_NEXT; try newest first, fall back.
    for engine in ("BLENDER_EEVEE_NEXT", "BLENDER_EEVEE", "CYCLES", "BLENDER_WORKBENCH"):
        try:
            sc.render.engine = engine
            print(f"[render] engine = {engine}")
            break
        except TypeError:
            continue
    sc.render.resolution_x = size
    sc.render.resolution_y = size
    sc.render.resolution_percentage = 100
    sc.render.film_transparent = True
    sc.render.image_settings.file_format = "PNG"
    sc.render.image_settings.color_mode = "RGBA"


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    out = argv[0] if argv else "."
    facings = 8
    size = 512
    kind = "barracks"
    i = 1
    while i < len(argv):
        if argv[i] == "--facings" and i + 1 < len(argv):
            facings = int(argv[i + 1]); i += 2
        elif argv[i] == "--size" and i + 1 < len(argv):
            size = int(argv[i + 1]); i += 2
        elif argv[i] == "--kind" and i + 1 < len(argv):
            kind = argv[i + 1]; i += 2
        else:
            i += 1
    os.makedirs(out, exist_ok=True)

    clear_scene()
    build_building(kind)

    cam, aim = setup_camera()
    setup_lights()
    setup_render(size)

    import mathutils
    sc = bpy.context.scene
    dist = 24.0
    pitch = math.radians(60.0)
    for f in range(facings):
        yaw = math.radians(45.0 + 360.0 * f / facings)
        cam.location = (
            dist * math.sin(pitch) * math.cos(yaw),
            -dist * math.sin(pitch) * math.sin(yaw),
            dist * math.cos(pitch) + 1.0,
        )
        bpy.context.view_layer.update()
        sc.render.filepath = os.path.join(out, f"{kind}_facing_{f:02d}.png")
        bpy.ops.render.render(write_still=True)
        print(f"[render] facing {f}/{facings}", flush=True)

    print(f"[done] {facings} facings of '{kind}' at {size}px -> {out}")


if __name__ == "__main__":
    main()
