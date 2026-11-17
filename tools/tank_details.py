"""Rewrite the remaining tank models on top of the structure helpers.

Values-only module imported by the patch script: each entry maps a function name to its
replacement source.  Kept separate from blender_vehicles.py so the model file stays a pure
model library.
"""

TYPE62 = '''
def p_type62():
    """Type 62 light tank - dome turret, three-block hull, full running gear."""
    p = []
    p += running_gear(2.30, 0.60, 0.26, r=0.19, n=4)
    p.append(boxr((0.72, 0, 0.50), (0.80, 1.18, 0.38), BODY(), (0, math.radians(34), 0)))
    p.append(box((-0.30, 0, 0.48), (1.70, 1.18, 0.40), BODY()))
    p.append(box((-1.02, 0, 0.46), (0.55, 1.14, 0.34), BODY_D()))
    p.append(box((0.10, 0, 0.68), (1.30, 1.10, 0.05), BODY()))
    p += rear_grille(-1.10, 0.56, 0.66, slats=3)
    for side in (-1, 1):
        p.append(box((0.92, side * 0.62, 0.52), (0.42, 0.20, 0.07), BODY_D()))
    p += turret_dome(-0.05, 0, 0.70, 0.52, layers=4, height=0.34, segs=20)
    for side in (-1, 1):
        p.append(boxr((0.28, side * 0.24, 0.86), (0.32, 0.24, 0.20), BODY_D(),
                      (0, 0, side * math.radians(30))))
    p += gun_assembly(0.42, 0.86, 0.10, 1.55)
    p.append(box((-0.42, 0.34, 1.02), (0.22, 0.18, 0.13), BODY_D()))
    return p
'''

WW2_MEDIUM = '''
def t_ww2_medium():
    """WW2 medium tank - cast turret, sloped glacis, full running gear."""
    p = []
    p += running_gear(2.95, 0.70, 0.28, r=0.22, n=5)
    p.append(boxr((0.98, 0, 0.56), (0.95, 1.40, 0.44), BODY(), (0, math.radians(38), 0)))
    p.append(box((-0.28, 0, 0.52), (1.95, 1.38, 0.46), BODY()))
    p.append(box((-1.28, 0, 0.50), (0.62, 1.34, 0.38), BODY_D()))
    p.append(box((0.14, 0, 0.74), (1.45, 1.20, 0.05), BODY()))
    p += rear_grille(-1.34, 0.62, 0.72, slats=3)
    for side in (-1, 1):
        p.append(boxr((0, side * 0.74, 0.58), (2.55, 0.14, 0.22), BODY(),
                      (math.radians(16) * side, 0, 0)))
    p += turret_dome(-0.08, 0, 0.76, 0.58, layers=4, height=0.40, segs=22)
    for side in (-1, 1):
        p.append(boxr((0.34, side * 0.28, 0.94), (0.36, 0.26, 0.22), BODY_D(),
                      (0, 0, side * math.radians(34))))
    p += gun_assembly(0.48, 0.94, 0.11, 1.70)
    p.append(box((-0.46, 0.36, 1.10), (0.24, 0.20, 0.14), BODY_D()))
    p.append(cyl((-0.40, 0.36, 1.14), 0.04, 0.50, METAL(), rot=(0, math.radians(70), 0), verts=6))
    return p
'''

WW2_HEAVY = '''
def t_ww2_heavy():
    """WW2 heavy tank - oversized turret, long 88, six wheels, heavy skirts."""
    p = []
    p += running_gear(3.60, 0.86, 0.32, r=0.24, n=6)
    p.append(boxr((1.16, 0, 0.62), (1.05, 1.70, 0.52), BODY(), (0, math.radians(40), 0)))
    p.append(box((-0.30, 0, 0.58), (2.30, 1.68, 0.52), BODY()))
    p.append(box((-1.58, 0, 0.56), (0.70, 1.64, 0.44), BODY_D()))
    p.append(box((0.20, 0, 0.84), (1.75, 1.46, 0.06), BODY()))
    p += rear_grille(-1.62, 0.76, 0.82, slats=4)
    for side in (-1, 1):
        p.append(box((-0.10, side * 1.02, 0.86), (0.85, 0.22, 0.24), BODY_D()))
        p.append(box((-1.30, side * 1.02, 0.86), (0.55, 0.22, 0.24), BODY_D()))
    p += turret_dome(-0.10, 0, 0.86, 0.72, layers=4, height=0.48, segs=24)
    for side in (-1, 1):
        p.append(boxr((0.46, side * 0.34, 1.08), (0.44, 0.30, 0.26), BODY_D(),
                      (0, 0, side * math.radians(36))))
    p += gun_assembly(0.62, 1.08, 0.12, 2.05)
    p.append(box((-0.56, 0.42, 1.26), (0.26, 0.22, 0.15), BODY_D()))
    p.append(cyl((-0.50, 0.42, 1.30), 0.045, 0.55, METAL(), rot=(0, math.radians(70), 0), verts=6))
    return p
'''

COLDWAR_MBT = '''
def t_coldwar_mbt():
    """Cold-war MBT - cast dome, long 105, seven wheels, ERA blocks on the hull."""
    p = []
    p += running_gear(3.40, 0.78, 0.30, r=0.23, n=6)
    p.append(boxr((1.05, 0, 0.58), (1.00, 1.58, 0.48), BODY(), (0, math.radians(42), 0)))
    p.append(box((-0.32, 0, 0.54), (2.15, 1.54, 0.48), BODY()))
    p.append(box((-1.48, 0, 0.52), (0.66, 1.50, 0.40), BODY_D()))
    p.append(box((0.16, 0, 0.78), (1.60, 1.34, 0.05), BODY()))
    p += rear_grille(-1.52, 0.70, 0.76, slats=3)
    for side in (-1, 1):
        for i in range(4):
            p.append(box((-1.05 + i * 0.50, side * 0.82, 0.66), (0.40, 0.18, 0.20), BODY_D()))
    p += turret_dome(-0.10, 0, 0.80, 0.66, layers=5, height=0.42, segs=24)
    for side in (-1, 1):
        p.append(box((0.30, side * 0.44, 0.98), (0.42, 0.20, 0.18), BODY_D()))
    p += gun_assembly(0.52, 0.98, 0.115, 1.90)
    p.append(cyl((-0.60, 0.40, 1.16), 0.10, 0.24, BODY_D(), verts=10))
    p.append(box((-0.44, -0.40, 1.14), (0.26, 0.22, 0.16), BODY_D()))
    return p
'''

LIGHT_TANK = '''
def t_light_tank():
    """Light tank - compact dome, short 76, four wheels, no skirts."""
    p = []
    p += running_gear(2.15, 0.54, 0.24, r=0.17, n=4, skirt=False)
    p.append(boxr((0.66, 0, 0.44), (0.70, 1.06, 0.32), BODY(), (0, math.radians(32), 0)))
    p.append(box((-0.28, 0, 0.42), (1.55, 1.06, 0.34), BODY()))
    p.append(box((-0.94, 0, 0.40), (0.50, 1.02, 0.28), BODY_D()))
    p.append(box((0.08, 0, 0.60), (1.15, 0.98, 0.04), BODY()))
    p += rear_grille(-1.00, 0.50, 0.58, slats=2)
    p += turret_dome(-0.06, 0, 0.60, 0.46, layers=3, height=0.30, segs=18)
    p += gun_assembly(0.34, 0.74, 0.09, 1.25)
    p.append(box((-0.36, 0.30, 0.88), (0.20, 0.16, 0.12), BODY_D()))
    return p
'''

SUPER_HEAVY = '''
def t_super_heavy():
    """Super-heavy tank - huge turret, very long gun, eight wheels, twin exhausts."""
    p = []
    p += running_gear(4.30, 1.00, 0.36, r=0.27, n=7)
    p.append(boxr((1.36, 0, 0.70), (1.15, 1.95, 0.60), BODY(), (0, math.radians(42), 0)))
    p.append(box((-0.34, 0, 0.66), (2.70, 1.92, 0.60), BODY()))
    p.append(box((-1.78, 0, 0.62), (0.80, 1.86, 0.48), BODY_D()))
    p.append(box((0.26, 0, 0.96), (2.05, 1.66, 0.06), BODY()))
    for i in range(4):
        p.append(box((-1.90 + i * 0.22, 0, 0.92), (0.14, 1.40, 0.05), TRACK()))
    for dy in (-0.62, 0.62):
        p.append(cyl((-1.86, dy, 0.84), 0.11, 0.36, BODY_D(),
                     rot=(0, math.radians(90), 0), verts=10))
    p += turret_dome(-0.14, 0, 0.98, 0.86, layers=5, height=0.56, segs=26)
    for side in (-1, 1):
        p.append(boxr((0.54, side * 0.40, 1.22), (0.52, 0.34, 0.30), BODY_D(),
                      (0, 0, side * math.radians(34))))
    p += gun_assembly(0.74, 1.24, 0.14, 2.40)
    p.append(box((-0.68, 0.50, 1.44), (0.30, 0.24, 0.17), BODY_D()))
    p.append(cyl((-0.60, 0.50, 1.48), 0.05, 0.60, METAL(), rot=(0, math.radians(70), 0), verts=6))
    return p
'''

WW1_RHOMBOID = '''
def t_ww1_rhomboid():
    """WW1 rhomboid - tracks wrap the whole lozenge, sponson guns on the sides."""
    p = []
    p.append(box((0, 0, 0.66), (3.20, 1.50, 0.78), BODY()))
    p.append(boxr((1.05, 0, 0.86), (1.10, 1.30, 0.50), BODY(), (0, math.radians(28), 0)))
    p.append(box((-1.20, 0, 0.72), (0.90, 1.20, 0.44), BODY_D()))
    p.append(box((0.30, 0, 1.06), (1.55, 1.10, 0.06), BODY()))
    for side in (-1, 1):
        p.append(box((0, side * 1.02, 0.58), (3.90, 0.34, 0.94), TRACK()))
        for i in range(7):
            p.append(cyl((-1.55 + i * 0.52, side * 1.20, 0.36), 0.16, 0.10, METAL(),
                         rot=(math.radians(90), 0, 0), verts=12))
        for i in range(4):
            p.append(box((-1.20 + i * 0.80, side * 1.06, 1.12), (0.34, 0.12, 0.14), BODY_D()))
    for i in range(3):
        p.append(box((-1.45 + i * 0.20, 0, 1.00), (0.12, 1.30, 0.05), TRACK()))
    p.append(box((0.98, 0.62, 1.02), (0.52, 0.36, 0.34), BODY_D()))
    p.append(cyl((1.28, 0.62, 1.02), 0.08, 0.72, METAL(), rot=(0, math.radians(90), 0), verts=8))
    p.append(box((-0.60, -0.62, 1.06), (0.50, 0.34, 0.30), BODY_D()))
    p.append(cyl((-0.86, -0.62, 1.06), 0.07, 0.60, METAL(), rot=(0, math.radians(90), 0), verts=8))
    p.append(cyl((1.62, 0, 0.72), 0.10, 0.24, BODY_D(), rot=(0, math.radians(90), 0), verts=10))
    return p
'''
