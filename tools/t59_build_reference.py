"""build_type59() rebuilt from the three-view drawing.

Measured off the drawing (side view is the top panel, plan view the middle, front view the
lower left). Everything below is read as a FRACTION of hull length L = 6.04 m, which is the
only way to compare a drawing to a model reliably.

  side view
    turret front edge     0.28 L from the nose
    turret rear edge      0.68 L
    turret roof           ~0.88 of total height
    glacis starts         ~0.30 L back from the nose, sloping down to the nose
    barrel protrudes      ~0.55 L beyond the turret front
    five road wheels, roughly even spacing, driver's hatch at the front left

  plan view
    turret is an OVAL - long fore-aft, narrower across - with a flat-ish front where the
    mantlet sits and a tapered rear. NOT a circle, and not a bulbous loaf.
    rear hull carries a large louvred engine grille; stowage boxes run along both sides.

  front view
    turret section is a truncated wedge: narrow at the roof, wider at the ring.
    The mantlet is a distinct raised block, not the full width of the turret.

Those three observations are exactly what the previous two attempts got wrong: first a
circle that was too small, then a sphere that was too round. This version builds the turret
as a swept oval whose cross-section narrows with height, which matches all three views at
once.
"""

BUILD = '''
def build_type59():
    """Type 59 built from the three-view drawing, dimensioned off hull length."""
    body = material("t59_body", (0.70, 0.74, 0.38))
    dark = material("t59_dark", (0.42, 0.45, 0.21))
    track = material("t59_track", (0.22, 0.21, 0.20), rough=0.92)
    steel = material("t59_steel", (0.55, 0.55, 0.58), rough=0.45, metal=0.7)
    glass = material("t59_glass", (0.20, 0.28, 0.36), rough=0.25)
    canvas = material("t59_canvas", (0.46, 0.44, 0.36), rough=0.95)

    L = 6.04            # hull length
    W = 3.27            # overall width
    H = 2.59            # overall height
    NOSE = L * 0.5      # x = +3.02 is the nose
    DECK = 1.50         # hull roof height
    TRACK_TOP = 0.80
    TURRET_FRONT = NOSE - 0.28 * L      # +1.33
    TURRET_REAR = NOSE - 0.68 * L       # -1.09
    TURRET_LEN = TURRET_FRONT - TURRET_REAR   # 2.42
    TURRET_CX = 0.5 * (TURRET_FRONT + TURRET_REAR)  # +0.12
    ROOF = H * 0.88     # 2.28

    parts = []

    # ---- hull: steep glacis from the deck down to the nose, per the side view
    hull_profile = [
        (-NOSE, 0.44),
        (-NOSE, 1.34),
        (-L * 0.44, DECK),
        (L * 0.20, DECK),          # glacis begins about 0.30 L back from the nose
        (NOSE, 0.84),
        (NOSE, 0.44),
    ]
    parts.append(profile_extrude("hull", hull_profile, 2.60, body, bevel=0.045))

    # ---- sponsons over the tracks
    for side in (-1, 1):
        parts.append(box_mesh("sponson_%d" % side, (5.40, 0.42, 0.44),
                              (-0.30, side * 1.24, 1.30), body, bevel=0.040))

    # ---- running gear: five road wheels, idler forward, sprocket aft
    outer = [(-2.90, 0.06), (-3.12, 0.40), (-2.90, 0.78),
             (2.60, 0.78), (2.90, 0.40), (2.60, 0.06)]
    inner = [(-2.80, 0.22), (-2.98, 0.40), (-2.80, 0.62),
             (2.52, 0.62), (2.76, 0.40), (2.52, 0.22)]
    wheel_x = [-2.02, -1.18, -0.36, 0.44, 1.24]
    for side in (-1, 1):
        parts.append(track_band("track_%d" % side, outer, inner, 0.56, track,
                                offset_y=side * 1.36))
        for i, x in enumerate(wheel_x):
            parts.append(cyl_mesh("wheel_%d_%d" % (side, i), 0.38, 0.32,
                                  (x, side * 1.36, 0.40), steel,
                                  rot=(math.radians(90), 0, 0), verts=24))
            parts.append(cyl_mesh("hub_%d_%d" % (side, i), 0.14, 0.34,
                                  (x, side * 1.36, 0.40), dark,
                                  rot=(math.radians(90), 0, 0), verts=16))
        parts.append(cyl_mesh("sprocket_%d" % side, 0.40, 0.32,
                              (2.62, side * 1.36, 0.42), steel,
                              rot=(math.radians(90), 0, 0), verts=20))
        parts.append(cyl_mesh("idler_%d" % side, 0.38, 0.32,
                              (-2.90, side * 1.36, 0.42), steel,
                              rot=(math.radians(90), 0, 0), verts=20))

    # ---- turret: swept oval, cross-section narrowing with height (front view is a wedge).
    # Built as three stacked elliptical rings: ring -> shoulder -> roof, each smaller.
    parts.append(cyl_mesh("turret_ring", 1.14, 0.16, (TURRET_CX, 0, DECK + 0.08), dark,
                          verts=44))
    for i, (rx, ry, z, h) in enumerate([
            (1.18, 0.94, 1.66, 0.34),   # widest, just above the ring
            (1.12, 0.88, 1.98, 0.30)]): # tapers toward the roof
        seg = cyl_mesh("turret_%d" % i, 1.0, h, (TURRET_CX, 0, z), body, verts=44)
        seg.scale = (rx, ry, 1.0)
        bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
        parts.append(seg)
    roof = dome_mesh("turret_roof", 1.0, (TURRET_CX, 0, ROOF - 0.34), body, squash=0.34)
    roof.scale = (1.10, 0.86, 1.0)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    parts.append(roof)

    # front slope of the turret: a distinctly raised mantlet block, not the turret's full
    # width - the plan view shows it as a narrow nose on the turret front
    GZ = 1.86
    parts.append(box_mesh("mantlet", (0.58, 0.98, 0.66), (TURRET_FRONT - 0.10, 0, GZ), dark,
                          bevel=0.050))

    # commander's cupola plus AA machine gun on the roof - both clearly drawn in the side view
    parts.append(cyl_mesh("cupola", 0.30, 0.22, (TURRET_CX - 0.30, 0.30, ROOF - 0.10), body,
                          verts=24))
    parts.append(cyl_mesh("cupola_hatch", 0.22, 0.06, (TURRET_CX - 0.30, 0.30, ROOF + 0.04),
                          dark, verts=20))
    parts.append(box_mesh("aamg_mount", (0.26, 0.22, 0.10), (TURRET_CX - 0.30, 0.30, ROOF + 0.08),
                          dark, bevel=0.015))
    parts.append(cyl_mesh("aamg_barrel", 0.045, 0.85, (TURRET_CX + 0.10, 0.30, ROOF + 0.20),
                          steel, rot=(0, math.radians(72), 0), verts=10))
    # loader's hatch, offset the other side
    parts.append(cyl_mesh("hatch_load", 0.24, 0.06, (TURRET_CX - 0.10, -0.34, ROOF - 0.02),
                          dark, verts=20))

    # ---- gun: protrudes about 0.55 L beyond the turret front, thick, with canvas cover
    BX = TURRET_FRONT
    parts.append(cyl_mesh("canvas_cover", 0.20, 0.66, (BX + 0.16, 0, GZ), canvas,
                          rot=(0, math.radians(90), 0), verts=20))
    parts.append(cyl_mesh("barrel", 0.128, 3.10, (BX + 1.90, 0, GZ), steel,
                          rot=(0, math.radians(90), 0), verts=24))
    parts.append(cyl_mesh("evacuator", 0.176, 0.54, (BX + 1.55, 0, GZ), dark,
                          rot=(0, math.radians(90), 0), verts=24))
    parts.append(cyl_mesh("muzzle_cap", 0.160, 0.38, (BX + 3.30, 0, GZ), canvas,
                          rot=(0, math.radians(90), 0), verts=24))

    # ---- rear: large louvred grille per the plan view, exhausts, rear stowage
    parts.append(box_mesh("grille", (1.10, 2.05, 0.06), (-2.35, 0, DECK + 0.03), track,
                          bevel=0.012))
    for i in range(3):
        parts.append(box_mesh("grille_rib_%d" % i, (1.06, 0.10, 0.05),
                              (-2.35, -0.62 + i * 0.62, DECK + 0.09), dark, bevel=0.008))
    for side in (-1, 1):
        parts.append(cyl_mesh("exhaust_%d" % side, 0.10, 0.44, (-2.92, side * 0.58, 1.10),
                              dark, rot=(0, math.radians(90), 0), verts=16))

    # ---- fenders fore and aft with the track exposed between them, as drawn
    for side in (-1, 1):
        parts.append(box_mesh("fender_f_%d" % side, (1.55, 0.60, 0.07),
                              (2.10, side * 1.36, 1.48), body, bevel=0.012))
        parts.append(box_mesh("fender_r_%d" % side, (1.35, 0.60, 0.07),
                              (-2.30, side * 1.36, 1.48), body, bevel=0.012))
        parts.append(box_mesh("mudflap_%d" % side, (0.55, 0.58, 0.30),
                              (2.78, side * 1.36, 1.26), body, bevel=0.018,
                              rot=(0, math.radians(22), 0)))
        # side stowage boxes along the sponson
        for i in range(2):
            parts.append(box_mesh("stow_%d_%d" % (side, i), (0.95, 0.28, 0.30),
                                  (-1.45 + i * 1.20, side * 1.30, 1.20), dark, bevel=0.020))
        parts.append(box_mesh("hook_%d" % side, (0.26, 0.16, 0.14),
                              (NOSE - 0.14, side * 0.66, 0.92), steel, bevel=0.020))
    parts.append(cyl_mesh("light", 0.11, 0.14, (NOSE - 0.18, -0.98, 1.30), glass,
                          rot=(0, math.radians(90), 0), verts=14))
    return parts
'''

if __name__ == "__main__":
    print(BUILD)
