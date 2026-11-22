"""Replacement build_type59() derived from the reference photograph.

Measurements read off the Baidu Baike museum photo of a Type 59 (3/4 front-left):
  hull 6.04 x 3.27 x 2.59 m
  track run: top at 0.80, width 0.58 (photo shows wide links, clearly chunky)
  glacis: from the deck at 1.50 sloping steeply forward and down to the nose at ~0.85
  turret: OVAL in plan - about 2.5 m fore-aft by 2.0 m across - sitting slightly aft of
          centre, with a LOW roof only ~0.85 m above the deck
  gun: thick tube with a grey canvas dust cover over its base at the mantlet, a bore
       evacuator a third of the way along, and a muzzle cap
  extras visible in the photo: front fender stowage boxes, tow hooks, a cylindrical
       stowage tube across the rear plate, a large two-leaf roof hatch.
"""

BUILD = '''
def build_type59():
    """Type 59 built to the reference photo's proportions."""
    body = material("t59_body", (0.70, 0.74, 0.38))
    dark = material("t59_dark", (0.42, 0.45, 0.21))
    track = material("t59_track", (0.22, 0.21, 0.20), rough=0.92)
    steel = material("t59_steel", (0.55, 0.55, 0.58), rough=0.45, metal=0.7)
    glass = material("t59_glass", (0.20, 0.28, 0.36), rough=0.25)
    canvas = material("t59_canvas", (0.46, 0.44, 0.36), rough=0.95)

    parts = []

    # ---- hull: steeply sloped glacis running down to the nose, as in the photo
    hull_profile = [
        (-3.02, 0.44),   # rear bottom
        (-3.02, 1.34),   # rear plate
        (-2.62, 1.50),   # engine deck
        (1.42, 1.50),    # fighting compartment deck
        (2.86, 0.86),    # glacis: 0.64 drop over 1.44 run = very steep
        (2.98, 0.44),    # nose bottom
    ]
    parts.append(profile_extrude("hull", hull_profile, 2.34, body, bevel=0.045))

    # ---- sponsons over the tracks
    for side in (-1, 1):
        parts.append(box_mesh("sponson_%d" % side, (5.55, 0.54, 0.44),
                              (-0.10, side * 1.33, 1.32), body, bevel=0.040))

    # ---- tracks: wide links (0.58) with a low, rounded run
    outer = [
        (-2.92, 0.06), (-3.14, 0.40), (-2.92, 0.78),
        (2.62, 0.78), (2.92, 0.40), (2.62, 0.06),
    ]
    inner = [
        (-2.82, 0.22), (-3.00, 0.40), (-2.82, 0.62),
        (2.54, 0.62), (2.78, 0.40), (2.54, 0.22),
    ]
    wheel_x = [-2.05, -1.20, -0.40, 0.40, 1.20]
    for side in (-1, 1):
        parts.append(track_band("track_%d" % side, outer, inner, 0.58, track,
                                offset_y=side * 1.40))
        for i, x in enumerate(wheel_x):
            parts.append(cyl_mesh("wheel_%d_%d" % (side, i), 0.38, 0.34,
                                  (x, side * 1.40, 0.40), steel,
                                  rot=(math.radians(90), 0, 0), verts=24))
            parts.append(cyl_mesh("hub_%d_%d" % (side, i), 0.15, 0.36,
                                  (x, side * 1.40, 0.40), dark,
                                  rot=(math.radians(90), 0, 0), verts=16))
        parts.append(cyl_mesh("sprocket_%d" % side, 0.42, 0.34,
                              (2.62, side * 1.40, 0.42), steel,
                              rot=(math.radians(90), 0, 0), verts=20))
        parts.append(cyl_mesh("idler_%d" % side, 0.40, 0.34,
                              (-2.92, side * 1.40, 0.42), steel,
                              rot=(math.radians(90), 0, 0), verts=20))

    # ---- turret: OVAL in plan and LOW. Photo shows roughly 2.5 m fore-aft by 2.0 m across
    # with the roof only about 0.85 m above the deck, sitting a little aft of centre.
    parts.append(cyl_mesh("turret_ring", 1.02, 0.14, (-0.28, 0, 1.57), dark, verts=40))
    t_body = cyl_mesh("turret_body", 0.98, 0.44, (-0.28, 0, 1.79), body, verts=40)
    t_body.scale = (1.22, 1.00, 1.0)          # fore-aft stretch = oval plan
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    parts.append(t_body)

    roof = dome_mesh("turret_roof", 0.98, (-0.28, 0, 2.01), body, squash=0.40)
    roof.scale = (1.22, 1.00, 1.0)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    parts.append(roof)

    # two-leaf roof hatch (photo shows a large oval hatch offset to the right)
    parts.append(cyl_mesh("hatch_a", 0.30, 0.07, (-0.30, 0.34, 2.30), dark, verts=24))
    parts.append(cyl_mesh("hatch_b", 0.22, 0.06, (-0.44, -0.30, 2.28), dark, verts=20))
    # commander's cupola
    parts.append(cyl_mesh("cupola", 0.24, 0.16, (-0.46, 0.34, 2.36), body, verts=20))

    # ---- mantlet and gun
    parts.append(box_mesh("mantlet", (0.40, 0.92, 0.54), (0.62, 0, 2.00), dark, bevel=0.045))
    # canvas dust cover where the barrel leaves the mantlet (grey, clearly visible in photo)
    parts.append(cyl_mesh("canvas_cover", 0.155, 0.62, (0.92, 0, 2.00), canvas,
                          rot=(0, math.radians(90), 0), verts=20))
    parts.append(cyl_mesh("barrel", 0.105, 3.20, (2.72, 0, 2.00), steel,
                          rot=(0, math.radians(90), 0), verts=24))
    parts.append(cyl_mesh("evacuator", 0.150, 0.52, (2.42, 0, 2.00), dark,
                          rot=(0, math.radians(90), 0), verts=24))
    parts.append(cyl_mesh("muzzle_cap", 0.135, 0.34, (4.24, 0, 2.00), canvas,
                          rot=(0, math.radians(90), 0), verts=24))

    # ---- rear: engine grille, exhausts, and the cylindrical stowage tube on the back plate
    parts.append(box_mesh("grille", (0.95, 1.95, 0.06), (-2.45, 0, 1.52), track, bevel=0.012))
    for side in (-1, 1):
        parts.append(cyl_mesh("exhaust_%d" % side, 0.10, 0.46, (-2.95, side * 0.62, 1.16),
                              dark, rot=(0, math.radians(90), 0), verts=16))
    parts.append(cyl_mesh("rear_tube", 0.20, 2.10, (-3.10, 0, 1.20), dark,
                          rot=(math.radians(90), 0, 0), verts=20))

    # ---- front fender stowage boxes (clearly visible in the photo)
    for side in (-1, 1):
        parts.append(box_mesh("fender_%d" % side, (5.95, 0.58, 0.09),
                              (0.0, side * 1.40, 1.50), body, bevel=0.014))
        parts.append(box_mesh("fbox_%d" % side, (0.90, 0.54, 0.30),
                              (2.10, side * 1.40, 1.66), dark, bevel=0.024))
        # tow hook on the nose
        parts.append(box_mesh("hook_%d" % side, (0.26, 0.16, 0.14),
                              (2.96, side * 0.70, 0.92), steel, bevel=0.020))
    parts.append(cyl_mesh("light", 0.11, 0.14, (2.90, -1.10, 1.30), glass,
                          rot=(0, math.radians(90), 0), verts=14))
    return parts
'''

if __name__ == "__main__":
    print(BUILD)
