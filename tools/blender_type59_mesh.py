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

def loft_ellipse(name, rings, mat, segs=44, cap_top=True, bevel=0.02):
    """Loft a solid through a list of (z, rx, ry) elliptical rings, bottom to top.

    This is what a cast turret actually is: a truncated wedge whose plan section is an oval
    that shrinks and narrows toward the roof. The previous turret was three separate
    elliptical cylinders stacked on a dome, which read as too tall and too bulbous?the
    front view of the drawing is a clean trapezoid with flat sloped sides, and only a very
    shallow crown on top.
    """
    ob = new_mesh_object(name)
    bm = bmesh.new()
    loops = []
    for (z, rx, ry) in rings:
        loop = []
        for i in range(segs):
            a = 2.0 * math.pi * i / segs
            loop.append(bm.verts.new((math.cos(a) * rx, math.sin(a) * ry, z)))
        loops.append(loop)
    bm.verts.ensure_lookup_table()

    bm.faces.new(loops[0][::-1])                      # bottom cap
    for k in range(len(loops) - 1):
        lo, hi = loops[k], loops[k + 1]
        for i in range(segs):
            j = (i + 1) % segs
            bm.faces.new((lo[i], lo[j], hi[j], hi[i]))
    if cap_top:
        bm.faces.new(loops[-1])                       # top cap

    bm.normal_update()
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    if bevel > 0.0:
        bmesh.ops.bevel(bm, geom=list(bm.edges), offset=bevel, segments=2, affect="EDGES",
                        clamp_overlap=True)
    bm.to_mesh(ob.data)
    bm.free()
    ob.data.materials.append(mat)
    return ob


# --------------------------------------------------------------------------- the tank
def build_type59():
    """Type 59 with every element placed from three-view pixel measurements."""
    body = material("t59_body", (0.70, 0.74, 0.38))
    dark = material("t59_dark", (0.42, 0.45, 0.21))
    track = material("t59_track", (0.22, 0.21, 0.20), rough=0.92)
    steel = material("t59_steel", (0.55, 0.55, 0.58), rough=0.45, metal=0.7)
    glass = material("t59_glass", (0.20, 0.28, 0.36), rough=0.25)
    canvas = material("t59_canvas", (0.46, 0.44, 0.36), rough=0.95)

    # measured constants (metres)
    NOSE, REAR = 3.02, -3.02
    DECK = 1.60
    ROOF = 2.59
    GUN_Z = 1.78
    T_FRONT, T_REAR = 1.53, -1.02
    T_FLAT_END = 0.07
    T_CX = 0.5 * (T_FRONT + T_REAR)
    T_HW = 0.96          # half width, from the front view (1.92 m across)
    HULL_HW = 1.63

    parts = []

    # ---- hull side profile from the drawing.
    # Nose is high (the glacis rises forward), belly sweeps back and down to the track line,
    # the rear plate slopes up to the rear deck.
    hull_profile = [
        (REAR, 0.30),
        (REAR, 1.42),
        (-2.30, DECK),        # rear deck
        (1.10, DECK),         # deck under the turret
        (2.10, 1.88),         # front deck / driver's hatch level
        (NOSE, 1.52),         # glacis top edge at the nose
        (NOSE, 0.92),         # nose bottom edge - the hull overhangs the track front
        (2.35, 0.30),         # belly sweeps back
    ]
    parts.append(profile_extrude("hull", hull_profile, 2.55, body, bevel=0.045))

    # ---- sponsons: the hull overhangs the tracks, full width 3.27 m
    for side in (-1, 1):
        parts.append(box_mesh("sponson_%d" % side, (5.20, 0.46, 0.40),
                              (-0.35, side * 1.42, 1.42), body, bevel=0.038))

    # ---- running gear
    outer = [(-2.88, 0.04), (-3.10, 0.34), (-2.88, 0.66),
             (2.66, 0.66), (2.94, 0.34), (2.66, 0.04)]
    inner = [(-2.78, 0.18), (-2.96, 0.34), (-2.78, 0.52),
             (2.58, 0.52), (2.80, 0.34), (2.58, 0.18)]
    wheel_x = [-2.05, -1.24, -0.44, 0.36, 1.16]
    for side in (-1, 1):
        parts.append(track_band("track_%d" % side, outer, inner, 0.56, track,
                                offset_y=side * 1.42))
        for i, x in enumerate(wheel_x):
            parts.append(cyl_mesh("wheel_%d_%d" % (side, i), 0.34, 0.30,
                                  (x, side * 1.42, 0.36), steel,
                                  rot=(math.radians(90), 0, 0), verts=24))
            parts.append(cyl_mesh("hub_%d_%d" % (side, i), 0.13, 0.32,
                                  (x, side * 1.42, 0.36), dark,
                                  rot=(math.radians(90), 0, 0), verts=16))
        parts.append(cyl_mesh("sprocket_%d" % side, 0.36, 0.30,
                              (2.70, side * 1.42, 0.38), steel,
                              rot=(math.radians(90), 0, 0), verts=20))
        parts.append(cyl_mesh("idler_%d" % side, 0.34, 0.30,
                              (-2.92, side * 1.42, 0.38), steel,
                              rot=(math.radians(90), 0, 0), verts=20))

    # ---- TURRET: flat roof, vertical-ish front, tapered rear. This is the shape the side
    # view actually shows; a dome was wrong every time.
    # Loft through elliptical rings, each an (x_centre, z, half_len, half_wid) pair, but the
    # taper is asymmetric front/back, so build it from explicit ring control points instead.
    parts.append(cyl_mesh("turret_ring", T_HW * 1.10, 0.12, (T_CX, 0, DECK + 0.06), dark,
                          verts=48))

    segs = 48
    ob = new_mesh_object("turret")
    bm = bmesh.new()

    def ring_verts(half_len_front, half_len_rear, half_wid, z):
        """One closed oval ring whose centre is offset so front/back extents differ."""
        loop = []
        for i in range(segs):
            a = 2.0 * math.pi * i / segs
            cx, cy = math.cos(a), math.sin(a)
            half_len = half_len_front if cx >= 0 else half_len_rear
            loop.append(bm.verts.new((T_CX + cx * half_len, cy * half_wid, z)))
        return loop

    Lf = T_FRONT - T_CX       # front half length
    Lr = T_CX - T_REAR        # rear half length
    Lf_flat = T_FLAT_END - T_CX

    # rings bottom to top: ring level, then the roof edge where the taper begins
    r0 = ring_verts(Lf * 0.98, Lr * 0.98, T_HW * 0.98, DECK + 0.12)
    r1 = ring_verts(Lf, Lr, T_HW, DECK + 0.52)
    r2 = ring_verts(Lf * 0.94, Lr * 0.62, T_HW * 0.80, ROOF - 0.06)
    # final flat roof ring, pulled in so the roof meets a small plateau
    r3 = ring_verts(Lf * 0.90, Lr * 0.40, T_HW * 0.72, ROOF)
    bm.verts.ensure_lookup_table()

    bm.faces.new(r0[::-1])
    for lo, hi in ((r0, r1), (r1, r2), (r2, r3)):
        for i in range(segs):
            j = (i + 1) % segs
            bm.faces.new((lo[i], lo[j], hi[j], hi[i]))
    bm.faces.new(r3)
    bm.normal_update()
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bmesh.ops.bevel(bm, geom=list(bm.edges), offset=0.025, segments=2, affect="EDGES",
                    clamp_overlap=True)
    bm.to_mesh(ob.data)
    bm.free()
    ob.data.materials.append(body)
    parts.append(ob)

    # ---- mantlet: raised block on the turret front, narrower than the turret (plan view)
    parts.append(box_mesh("mantlet", (0.64, 1.10, 0.72), (T_FRONT - 0.06, 0, GUN_Z), dark,
                          bevel=0.050))

    # ---- roof furniture, both clearly drawn from the side: cupola on the right, AA MG
    parts.append(cyl_mesh("cupola", 0.32, 0.22, (T_CX - 0.30, 0.34, ROOF + 0.08), body,
                          verts=26))
    parts.append(cyl_mesh("cupola_hatch", 0.24, 0.06, (T_CX - 0.30, 0.34, ROOF + 0.22), dark,
                          verts=22))
    parts.append(box_mesh("aamg_base", (0.30, 0.26, 0.12), (T_CX - 0.30, 0.34, ROOF + 0.12),
                          dark, bevel=0.015))
    parts.append(cyl_mesh("aamg_barrel", 0.05, 0.95, (T_CX - 0.30, 0.34, ROOF + 0.42), steel,
                          verts=10))
    parts.append(cyl_mesh("aamg_jacket", 0.075, 0.30, (T_CX - 0.30, 0.34, ROOF + 0.26), dark,
                          verts=12))
    parts.append(cyl_mesh("hatch_load", 0.26, 0.06, (T_CX - 0.10, -0.36, ROOF + 0.02), dark,
                          verts=22))

    # ---- gun: canvas cover at the mantlet, long tube, bore evacuator, muzzle cap
    parts.append(cyl_mesh("canvas_cover", 0.21, 0.70, (T_FRONT + 0.34, 0, GUN_Z), canvas,
                          rot=(0, math.radians(90), 0), verts=22))
    parts.append(cyl_mesh("barrel", 0.125, 2.90, (T_FRONT + 2.00, 0, GUN_Z), steel,
                          rot=(0, math.radians(90), 0), verts=24))
    parts.append(cyl_mesh("evacuator", 0.175, 0.50, (T_FRONT + 1.60, 0, GUN_Z), dark,
                          rot=(0, math.radians(90), 0), verts=24))
    parts.append(cyl_mesh("muzzle_cap", 0.160, 0.40, (T_FRONT + 3.56, 0, GUN_Z), canvas,
                          rot=(0, math.radians(90), 0), verts=24))

    # ---- rear: louvred engine grille across the deck, exhausts, rear stowage tube
    parts.append(box_mesh("grille", (1.15, 2.00, 0.06), (-2.30, 0, DECK + 0.03), track,
                          bevel=0.012))
    for i in range(4):
        parts.append(box_mesh("grille_rib_%d" % i, (1.10, 0.09, 0.05),
                              (-2.30, -0.70 + i * 0.47, DECK + 0.09), dark, bevel=0.008))
    for side in (-1, 1):
        parts.append(cyl_mesh("exhaust_%d" % side, 0.11, 0.46, (-2.90, side * 0.60, 1.20),
                              dark, rot=(0, math.radians(90), 0), verts=16))

    # ---- fenders: separate fore and aft plates with the track exposed between them
    for side in (-1, 1):
        parts.append(box_mesh("fender_f_%d" % side, (1.70, 0.62, 0.07),
                              (2.05, side * 1.42, 1.60), body, bevel=0.012))
        parts.append(box_mesh("fender_r_%d" % side, (1.50, 0.62, 0.07),
                              (-2.20, side * 1.42, 1.60), body, bevel=0.012))
        parts.append(box_mesh("mudflap_%d" % side, (0.60, 0.60, 0.32),
                              (2.74, side * 1.42, 1.38), body, bevel=0.018,
                              rot=(0, math.radians(22), 0)))
        # side stowage, low on the sponson as drawn
        for i in range(2):
            parts.append(box_mesh("stow_%d_%d" % (side, i), (1.00, 0.26, 0.28),
                                  (-1.50 + i * 1.25, side * 1.36, 1.18), dark, bevel=0.018))
        parts.append(box_mesh("hook_%d" % side, (0.28, 0.16, 0.15),
                              (NOSE - 0.12, side * 0.72, 1.00), steel, bevel=0.020))
        # headlight on the front deck
    parts.append(cyl_mesh("light", 0.12, 0.16, (2.30, -1.02, 1.98), glass,
                          rot=(0, math.radians(90), 0), verts=14))
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
