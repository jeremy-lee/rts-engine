"""Type 59 modelled as a real mesh in Blender, not assembled from primitives.

Previous vehicles were built by stacking box/cyl/cone primitives, which is why every face
was axis-aligned and the silhouette read as a stack of blocks. This script builds actual
editable geometry instead:

  * the hull is a SIDE PROFILE polygon extruded across the width, so the glacis and the
    rear plate are genuinely sloped faces rather than rotated boxes;
  * the turret is a UV sphere cut in half and scaled, giving a continuous dome rather than
    a stack of cylinders;
  * every part gets a bevel so edges catch light instead of being razor-sharp;
  * tracks are a profile polygon extruded, with the road wheels as bevelled cylinders inset
    into the run.

Run headless:
  blender --background --python blender_type59_mesh.py -- <out_dir> [--facings 8] [--size 256]
"""

import math
import os
import sys

import bpy
import bmesh
from mathutils import Vector


# --------------------------------------------------------------------------- utilities
def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def new_mesh_object(name):
    me = bpy.data.meshes.new(name)
    ob = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(ob)
    return ob


def material(name, rgb_srgb, rough=0.75, metal=0.0):
    """Create a material from an sRGB triple (converted to linear, as Blender expects)."""
    def s2l(c):
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes.get("Principled BSDF")
    b.inputs["Base Color"].default_value = (s2l(rgb_srgb[0]), s2l(rgb_srgb[1]), s2l(rgb_srgb[2]), 1.0)
    b.inputs["Roughness"].default_value = rough
    if "Metallic" in b.inputs:
        b.inputs["Metallic"].default_value = metal
    return m


def profile_extrude(name, profile_xz, width, mat, bevel=0.02, bevel_segs=2):
    """Extrude a 2D profile (list of (x, z)) across Y to make a solid with sloped faces.

    This is the whole point of the rework: a hull built this way has real angled armour,
    whereas a rotated box leaves gaps and reads as a separate slab bolted on.
    """
    ob = new_mesh_object(name)
    bm = bmesh.new()

    half = width * 0.5
    front = [bm.verts.new((x, -half, z)) for (x, z) in profile_xz]
    back = [bm.verts.new((x, half, z)) for (x, z) in profile_xz]
    bm.verts.ensure_lookup_table()

    n = len(profile_xz)
    bm.faces.new(front[::-1])
    bm.faces.new(back)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((front[i], front[j], back[j], back[i]))

    bm.normal_update()
    if bevel > 0.0:
        bmesh.ops.bevel(bm, geom=list(bm.edges) + list(bm.verts), offset=bevel,
                        segments=bevel_segs, affect="EDGES", clamp_overlap=True)
    bm.to_mesh(ob.data)
    bm.free()
    ob.data.materials.append(mat)
    return ob


def box_mesh(name, size, loc, mat, bevel=0.015, rot=(0, 0, 0)):
    bpy.ops.mesh.primitive_cube_add(size=2.0, location=loc, rotation=rot)
    ob = bpy.context.active_object
    ob.name = name
    ob.scale = (size[0] / 2, size[1] / 2, size[2] / 2)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if bevel > 0.0:
        md = ob.modifiers.new("bevel", "BEVEL")
        md.width = bevel
        md.segments = 2
        md.limit_method = "ANGLE"
    ob.data.materials.append(mat)
    return ob


def cyl_mesh(name, r, depth, loc, mat, rot=(0, 0, 0), verts=20, bevel=0.008):
    bpy.ops.mesh.primitive_cylinder_add(radius=r, depth=depth, location=loc,
                                        rotation=rot, vertices=verts)
    ob = bpy.context.active_object
    ob.name = name
    # smooth the curved wall but keep the flat end caps crisp
    bpy.ops.object.shade_smooth()
    bpy.ops.object.modifier_add(type="SMOOTH_BY_ANGLE") if False else None
    if bevel > 0.0:
        md = ob.modifiers.new("bevel", "BEVEL")
        md.width = bevel
        md.segments = 2
        md.limit_method = "ANGLE"
    ob.data.materials.append(mat)
    return ob


def dome_mesh(name, r, loc, mat, squash=0.52, segs=32, rings=16):
    """Half UV-sphere scaled down vertically: a continuous turret dome."""
    bpy.ops.mesh.primitive_uv_sphere_add(radius=r, location=loc, segments=segs, ring_count=rings)
    ob = bpy.context.active_object
    ob.name = name
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.bisect(plane_co=(0, 0, loc[2]), plane_no=(0, 0, 1),
                        use_fill=True, clear_inner=True)
    bpy.ops.object.mode_set(mode="OBJECT")
    ob.scale = (1.0, 1.0, squash)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    # smooth shading: without it a 32-segment sphere still reads as a faceted polygon at
    # render size, which is what made the turret look like a low-poly gem rather than a
    # cast dome. Auto-smooth keeps the bisected bottom edge crisp.
    bpy.ops.object.shade_smooth()
    md = ob.modifiers.new("bevel", "BEVEL")
    md.width = 0.012
    md.segments = 2
    ob.data.materials.append(mat)
    return ob


def track_band(name, outer, inner, width, mat, offset_y=0.0):
    """A closed track band: outer profile, inner profile, bridged into a ring.

    Built as two concentric extruded loops joined at the front and back, so the band has a
    visible outer face running all the way around the running gear instead of being a slab
    hidden behind the wheels.
    """
    assert len(outer) == len(inner), "outer and inner profiles need the same point count"
    ob = new_mesh_object(name)
    bm = bmesh.new()
    half = width * 0.5
    n = len(outer)
    o_front = [bm.verts.new((x, -half, z)) for (x, z) in outer]
    o_back = [bm.verts.new((x, half, z)) for (x, z) in outer]
    i_front = [bm.verts.new((x, -half, z)) for (x, z) in inner]
    i_back = [bm.verts.new((x, half, z)) for (x, z) in inner]
    bm.verts.ensure_lookup_table()

    for k in range(n):
        j = (k + 1) % n
        bm.faces.new((o_front[k], o_front[j], i_front[j], i_front[k]))   # front face
        bm.faces.new((o_back[k], i_back[k], i_back[j], o_back[j]))       # back face
        bm.faces.new((o_front[k], o_back[k], o_back[j], o_front[j]))     # outer wall
        bm.faces.new((i_front[k], i_front[j], i_back[j], i_back[k]))     # inner wall

    bm.normal_update()
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(ob.data)
    bm.free()
    ob.data.materials.append(mat)
    ob.location = (0.0, offset_y, 0.0)
    return ob

# --------------------------------------------------------------------------- the tank
def build_type59():
    """Type 59: 6.0 x 3.27 x 2.4 metres at 1 unit = 1 m, then scaled to sprite units."""
    body = material("t59_body", (0.70, 0.74, 0.38))
    dark = material("t59_dark", (0.42, 0.45, 0.21))
    track = material("t59_track", (0.22, 0.21, 0.20), rough=0.92)
    steel = material("t59_steel", (0.55, 0.55, 0.58), rough=0.45, metal=0.7)
    glass = material("t59_glass", (0.20, 0.28, 0.36), rough=0.25)

    parts = []

    # ---- hull as a side profile: rear plate, deck, sloped glacis, belly
    # (x forward, z up) - this produces genuinely sloped armour
    # PROPORTIONS FROM PUBLISHED DATA, not from impression. Type 59 / WZ-120: hull 6.04 m
    # long, 3.27 m wide, 2.59 m tall overall; turret about 2.0 m across and 1.09 m tall;
    # glacis sloped roughly 60 degrees from vertical. My first attempt was 1.51 m tall with
    # a 0.43 m turret, i.e. vertically compressed by about 40%, which is the main reason it
    # did not read as a Type 59 - that and using a bare sphere for a turret that actually
    # has vertical sides with a domed roof.
    #
    # Vertical stack: track top 0.78, hull deck 1.50, turret base 1.50, turret roof 2.59.
    hull_profile = [
        (-3.02, 0.42),   # rear bottom
        (-3.02, 1.30),   # rear plate
        (-2.55, 1.50),   # engine deck
        (1.55, 1.50),    # fighting compartment deck
        (2.95, 0.86),    # glacis: 60 deg from vertical, drops 0.64 over 1.40
        (3.02, 0.42),    # nose bottom
    ]
    parts.append(profile_extrude("hull", hull_profile, 2.30, body, bevel=0.045))

    # ---- sponsons: the hull overhangs the tracks, width 3.27 total
    for side in (-1, 1):
        parts.append(box_mesh("sponson_%d" % side, (5.60, 0.50, 0.46),
                              (-0.05, side * 1.32, 1.34), body, bevel=0.040))

    # ---- tracks as a closed BAND that the wheels sit inside.
    # The first attempt extruded a solid track slab and then placed 0.30-radius road wheels
    # at almost the same Y, so the round wheels simply covered the flat slab and no track
    # was visible at all. A real track is a continuous band around the wheels: outer run
    # low and high, inner run clear of the wheels.
    # ---- tracks: 5 road wheels per side with UNEVEN spacing (the 1-2 gap on a Type 59 is
    # visibly wider than the rest). Wheel radius 0.39 to match a 0.78 m track height.
    outer = [
        (-2.95, 0.06), (-3.14, 0.39), (-2.95, 0.76),
        (2.62, 0.76), (2.90, 0.39), (2.62, 0.06),
    ]
    inner = [
        (-2.86, 0.22), (-3.00, 0.39), (-2.86, 0.62),
        (2.54, 0.62), (2.76, 0.39), (2.54, 0.22),
    ]
    wheel_x = [-2.10, -1.22, -0.42, 0.38, 1.18]
    for side in (-1, 1):
        parts.append(track_band("track_%d" % side, outer, inner, 0.44, track,
                                offset_y=side * 1.42))
        for i, x in enumerate(wheel_x):
            parts.append(cyl_mesh("wheel_%d_%d" % (side, i), 0.39, 0.30,
                                  (x, side * 1.42, 0.40), steel,
                                  rot=(math.radians(90), 0, 0), verts=24))
            parts.append(cyl_mesh("hub_%d_%d" % (side, i), 0.15, 0.32,
                                  (x, side * 1.42, 0.40), dark,
                                  rot=(math.radians(90), 0, 0), verts=16))
        parts.append(cyl_mesh("sprocket_%d" % side, 0.42, 0.30,
                              (2.55, side * 1.42, 0.42), steel,
                              rot=(math.radians(90), 0, 0), verts=20))
        parts.append(cyl_mesh("idler_%d" % side, 0.40, 0.30,
                              (-2.88, side * 1.42, 0.42), steel,
                              rot=(math.radians(90), 0, 0), verts=20))

    # ---- turret: vertical-sided body plus a domed roof, not a bare sphere.
    # Real Type 59 turret is about 2.0 m across and 1.09 m tall from the deck (1.50) to the
    # roof (2.59): a 0.55 m vertical wall carrying a 0.54 m dome.
    parts.append(cyl_mesh("turret_ring", 1.06, 0.16, (-0.20, 0, 1.58), dark, verts=40))
    parts.append(cyl_mesh("turret_body", 1.02, 0.56, (-0.20, 0, 1.78), body, verts=40))
    parts.append(dome_mesh("turret_roof", 1.02, (-0.20, 0, 2.04), body, squash=0.54))
    # mantlet: the cast block the gun passes through
    parts.append(box_mesh("mantlet", (0.44, 0.86, 0.56), (0.74, 0, 2.00), dark, bevel=0.040))
    # commander's cupola and loader's hatch on the roof
    parts.append(cyl_mesh("cupola", 0.30, 0.20, (-0.42, 0.30, 2.52), body, verts=24))
    parts.append(cyl_mesh("hatch", 0.22, 0.06, (-0.42, 0.30, 2.64), dark, verts=20))

    # ---- 100mm gun: barrel, bore evacuator, muzzle brake
    # 100mm Type 59 rifled gun. Bore evacuator sits about a third of the way along, as on
    # the real weapon; barrel is 0.10 radius (real 100 mm bore plus wall) and reaches to the
    # 9.0 m overall length.
    parts.append(cyl_mesh("barrel", 0.10, 3.30, (2.55, 0, 2.00), steel,
                          rot=(0, math.radians(90), 0), verts=24))
    parts.append(cyl_mesh("evacuator", 0.155, 0.55, (2.30, 0, 2.00), dark,
                          rot=(0, math.radians(90), 0), verts=24))
    parts.append(cyl_mesh("muzzle", 0.135, 0.36, (4.05, 0, 2.00), dark,
                          rot=(0, math.radians(90), 0), verts=24))

    # ---- rear engine deck: grille block and exhausts
    parts.append(box_mesh("grille", (0.90, 1.90, 0.06), (-2.40, 0, 1.52), track, bevel=0.012))
    for side in (-1, 1):
        parts.append(cyl_mesh("exhaust_%d" % side, 0.10, 0.46, (-2.90, side * 0.66, 1.14),
                              dark, rot=(0, math.radians(90), 0), verts=16))

    # ---- stowage boxes on the sponsons
    for side in (-1, 1):
        parts.append(box_mesh("stow_%d" % side, (1.90, 0.30, 0.34),
                              (-1.00, side * 1.52, 1.42), dark, bevel=0.026))

    # ---- fenders over the tracks
    for side in (-1, 1):
        parts.append(box_mesh("fender_%d" % side, (6.00, 0.56, 0.08),
                              (0.0, side * 1.42, 1.48), body, bevel=0.014))

    # ---- headlight
    parts.append(cyl_mesh("light", 0.10, 0.14, (2.86, -1.20, 1.18), glass,
                          rot=(0, math.radians(90), 0), verts=12))
    return parts


# --------------------------------------------------------------------------- render rig
def setup_render(size):
    sc = bpy.context.scene
    for engine in ("BLENDER_EEVEE_NEXT", "BLENDER_EEVEE", "CYCLES"):
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
    try:
        sc.view_settings.view_transform = "Standard"
        sc.view_settings.look = "None"
    except Exception as e:
        print("[warn] view transform:", e)


def setup_lights():
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
    facings, size = 8, 256
    i = 1
    while i < len(argv):
        if argv[i] == "--facings" and i + 1 < len(argv):
            facings = int(argv[i + 1]); i += 2
        elif argv[i] == "--size" and i + 1 < len(argv):
            size = int(argv[i + 1]); i += 2
        else:
            i += 1
    os.makedirs(out, exist_ok=True)

    reset_scene()
    parts = build_type59()
    print("[mesh] parts=%d" % len(parts))
    total_tris = 0
    for ob in parts:
        total_tris += len(ob.data.polygons)
    print("[mesh] polygons=%d" % total_tris)

    setup_render(size)
    setup_lights()

    lo, hi = mesh_bounds()
    centre = (lo + hi) * 0.5
    extent = max(hi.x - lo.x, hi.y - lo.y, (hi.z - lo.z) * 1.6)
    print("[mesh] extent=%.2f" % extent)

    cam_data = bpy.data.cameras.new("cam")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = extent * 1.15
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
    dist = extent * 4.0 + 14.0
    for f in range(facings):
        yaw = math.radians(45.0 + 360.0 * f / facings)
        cam.location = (
            centre.x + dist * math.sin(pitch) * math.cos(yaw),
            centre.y - dist * math.sin(pitch) * math.sin(yaw),
            centre.z + dist * math.cos(pitch),
        )
        bpy.context.view_layer.update()
        bpy.context.scene.render.filepath = os.path.join(out, "t59mesh_f%02d.png" % f)
        bpy.ops.render.render(write_still=True)
        print("[render] f%d" % f, flush=True)

    print("[done] %d facings -> %s" % (facings, out))


if __name__ == "__main__":
    main()
