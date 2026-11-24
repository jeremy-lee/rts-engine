"""Final Type 59 build: every dimension measured off the three-view drawing.

Pixel measurements (drawing is 439x567; side view band y=45..190, front view x=0..215):

  side view                       drawing px      metres      note
    track bottom                      y=139          0.00      ground line
    hull deck / turret ring           y= 60          1.60      same line as the rear deck
    gun axis                          y= 51          1.78      0.18 m above the deck
    front deck (driver's hatch)       y= 46          1.88      slightly proud of the deck
    turret roof                       y= 11          2.59      = overall height
    nose                              x=116          +3.02
    hull rear                         x=415          -3.02      hull length 299 px = 6.04 m
    turret front                      x=190          +1.53
    turret rear                       x=316          -1.02      turret length 126 px = 2.54 m
    roof flat ends                    x=262          +0.07      flat front 57%, then tapers
    gun muzzle                        x=  4          +5.28      protrudes 2.26 m past the nose

  front view
    overall width (over tracks)       x=44..206      3.27      hull width
    turret width                      ~95 px         1.92      0.59 of hull width

  scale: 1 px = 0.0202 m on both views, which cross-checks against the published
  6.04 x 3.27 x 2.59 m envelope.

The decisive shape fact from the side view is that the turret ROOF IS FLAT from the front
edge back to about 57% of its length, and only then slopes down to the rear. It is not a
dome. Every previous attempt used a dome or a stack of rings, which is why none of them
read as a Type 59.
"""

BUILD = '''
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
'''

if __name__ == "__main__":
    print(BUILD)
