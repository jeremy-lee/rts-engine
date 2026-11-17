"""Detailed rewrites of the 12 generic aircraft, built on the shared structure helpers."""

WW1_BIPLANE = '''
def a_ww1_biplane():
    """WW1 biplane: stepped fuselage, glazed cockpit, twin wings with struts, gear."""
    p = []
    p += fuselage3(-1.35, 1.30, 0.10, 0.24, 0.10, 0.62, segs=12)
    p += canopy_glazed(0.10, 0.78, 0.40, 0.30, 0.22, frames=1)
    p.append(box((0.05, 0, 1.18), (1.45, 3.10, 0.055), BODY_D()))
    p.append(box((0.05, 0, 0.12), (1.45, 3.10, 0.055), BODY_D()))
    for side in (-1, 1):
        for x in (-0.55, 0.58):
            p.append(box((x, side * 1.00, 0.65), (0.14, 0.14, 1.06), METAL()))
            p.append(box((x, side * 1.42, 0.65), (0.14, 0.14, 1.06), METAL()))
    p += tail_unit(-1.42, 0.66, 1.40, 0.55, chord=0.40)
    p += propeller(1.36, 0.62, 0.78, blades=2, thickness=0.10)
    p += landing_gear(0.10, 0.55, 0.42, strut=0.30, wheel_r=0.13)
    return p
'''

WW2_FIGHTER = '''
def a_ww2_fighter():
    """WW2 fighter: tapered fuselage, bubble canopy, tapered wings, four-blade prop."""
    p = []
    p += fuselage3(-1.45, 1.62, 0.12, 0.28, 0.10, 0.52, segs=14)
    p += canopy_glazed(0.42, 0.74, 0.62, 0.38, 0.26, frames=2)
    for side in (-1, 1):
        p += wing(side, 0.10, 1.75, 0.98, 0.40, 0.50, 12.0, 0.09, BODY_D())
        p += wing_root_fairing(side, 0.10, 0.30, 0.50, 0.70, 0.09)
    p += tail_unit(-1.42, 0.56, 1.30, 0.72, chord=0.44)
    p += propeller(1.70, 0.52, 0.86, blades=4)
    p += landing_gear(0.05, 0.58, 0.38, strut=0.30, wheel_r=0.12)
    for side in (-1, 1):
        p.append(cyl((0.10, side * 1.30, 0.46), 0.09, 0.55, BODY_D(),
                     rot=(0, math.radians(90), 0), verts=8))
    return p
'''

JET = '''
def a_jet():
    """Early jet: pointed nose with intake, swept wings, bubble canopy, gear."""
    p = []
    p += fuselage3(-1.70, 1.85, 0.10, 0.30, 0.12, 0.52, segs=16)
    p += canopy_glazed(0.55, 0.76, 0.72, 0.42, 0.28, frames=2)
    for side in (-1, 1):
        p += wing(side, -0.20, 1.80, 1.05, 0.46, 0.48, 22.0, 0.08, BODY_D())
        p += wing_root_fairing(side, -0.20, 0.32, 0.50, 0.80, 0.08)
    p += tail_unit(-1.52, 0.54, 1.45, 0.82, chord=0.46)
    p.append(cyl((-1.82, 0, 0.52), 0.20, 0.20, TRACK(), rot=(0, math.radians(90), 0), verts=12))
    p += landing_gear(0.10, 0.60, 0.36, strut=0.30, wheel_r=0.12)
    return p
'''

HELICOPTER = '''
def a_heli():
    """Utility helicopter: boxy cabin, stepped boom, thick dark rotor, tail rotor."""
    p = []
    p.append(box((0.10, 0, 0.62), (2.30, 0.92, 0.72), BODY()))
    p.append(box((1.30, 0, 0.70), (0.55, 0.72, 0.58), GLASS()))
    p.append(box((1.34, 0, 1.02), (0.42, 0.66, 0.10), BODY_D()))
    p.append(cyl((1.66, 0, 0.52), 0.26, 0.28, BODY_D(),
                 rot=(0, math.radians(90), 0), verts=12))
    p.append(cone((-1.85, 0, 0.66), 0.10, 0.26, 1.30, BODY(),
                  rot=(0, math.radians(-90), 0), verts=12))
    for side in (-1, 1):
        p.append(box((-0.25, side * 0.52, 0.34), (1.40, 0.10, 0.12), METAL()))
    p.append(box((-1.20, 0, 0.94), (0.70, 0.10, 0.34), BODY_D()))
    p.append(cyl((-0.10, 0, 1.08), 0.12, 0.32, METAL(), verts=10))
    p.append(box((-0.10, 0, 1.26), (0.28, 3.60, 0.14), TRACK()))
    p.append(box((-0.10, 0, 1.26), (3.60, 0.28, 0.14), TRACK()))
    p.append(cyl((-0.10, 0, 1.26), 0.17, 0.16, METAL(), verts=10))
    p.append(box((-2.62, 0, 0.98), (0.10, 0.74, 0.74), BODY()))
    p.append(box((-2.66, 0, 0.98), (0.12, 0.12, 0.98), TRACK()))
    p += landing_gear(-0.10, 0.55, 0.34, strut=0.26, wheel_r=0.13)
    return p
'''

HEAVY_BOMBER = '''
def a_heavy_bomber():
    """Twin-engine bomber: long stepped fuselage, glazed nose, big podded engines."""
    p = []
    p += fuselage3(-2.30, 2.40, 0.16, 0.36, 0.12, 0.66, segs=16)
    p += canopy_glazed(1.55, 0.88, 0.72, 0.44, 0.26, frames=2)
    for side in (-1, 1):
        p += wing(side, -0.10, 2.20, 1.30, 0.55, 0.60, 30.0, 0.11, BODY_D())
        p += wing_root_fairing(side, -0.10, 0.40, 0.62, 0.95, 0.11)
        p += engine_nacelle(-0.20, side * 1.45, 0.58, 0.34, 1.05)
    p += tail_unit(-2.10, 0.70, 1.90, 0.95, chord=0.52)
    p += landing_gear(0.30, 0.72, 0.44, strut=0.36, wheel_r=0.15)
    return p
'''

RECON_PLANE = '''
def a_recon_plane():
    """Unarmed recon: slim fuselage, long glazed cabin, high aspect-ratio wing."""
    p = []
    p += fuselage3(-1.60, 1.70, 0.10, 0.22, 0.08, 0.54, segs=14)
    p += canopy_glazed(0.42, 0.74, 0.90, 0.34, 0.22, frames=3)
    for side in (-1, 1):
        p += wing(side, 0.10, 2.10, 0.80, 0.42, 0.50, 6.0, 0.07, BODY_D())
        p += wing_root_fairing(side, 0.10, 0.26, 0.52, 0.60, 0.07)
    p += tail_unit(-1.55, 0.58, 1.35, 0.70, chord=0.42)
    p += propeller(1.76, 0.54, 0.70, blades=3)
    p += landing_gear(0.10, 0.52, 0.36, strut=0.28, wheel_r=0.11)
    return p
'''

SEAPLANE = '''
def a_seaplane():
    """Floatplane: stepped fuselage, glazed canopy, big outboard floats on struts."""
    p = []
    p += fuselage3(-1.60, 1.75, 0.12, 0.28, 0.10, 0.72, segs=14)
    p += canopy_glazed(0.48, 0.94, 0.66, 0.38, 0.24, frames=2)
    for side in (-1, 1):
        p += wing(side, 0.05, 1.85, 0.95, 0.44, 0.48, 10.0, 0.09, BODY_D())
        p += wing_root_fairing(side, 0.05, 0.30, 0.70, 0.70, 0.09)
        p.append(cyl((0.10, side * 1.15, 0.22), 0.36, 2.40, BODY_D(),
                     rot=(0, math.radians(90), 0), verts=12))
        p.append(cone((1.35, side * 1.15, 0.22), 0.36, 0.10, 0.50, BODY_D(),
                      rot=(0, math.radians(90), 0), verts=12))
        p.append(box((0.45, side * 1.15, 0.58), (0.20, 0.20, 0.72), METAL()))
        p.append(box((-0.55, side * 1.15, 0.58), (0.20, 0.20, 0.72), METAL()))
    p += tail_unit(-1.55, 0.78, 1.40, 0.72, chord=0.44)
    p += propeller(1.82, 0.72, 0.76, blades=3)
    return p
'''

TRANSPORT_PLANE = '''
def a_transport_plane():
    """Transport: deep slab fuselage, high wing, twin engines, loading ramp at the tail."""
    p = []
    p.append(box((0.10, 0, 0.78), (3.60, 0.70, 0.74), BODY()))
    p.append(cone((2.05, 0, 0.78), 0.20, 0.35, 0.50, BODY_D(),
                  rot=(0, math.radians(90), 0), verts=14))
    p.append(box((2.10, 0, 1.00), (0.38, 0.44, 0.22), GLASS()))
    p.append(box((-1.55, 0, 0.68), (0.55, 0.66, 0.50), BODY_D()))
    p += canopy_glazed(1.72, 0.96, 0.34, 0.42, 0.18, frames=1)
    for side in (-1, 1):
        p += wing(side, 0.05, 2.40, 1.20, 0.50, 0.50, 4.0, 0.10, BODY_D())
        p += engine_nacelle(0.15, side * 1.45, 0.62, 0.30, 0.90)
    p += tail_unit(-1.60, 0.80, 2.10, 1.00, chord=0.50)
    p += landing_gear(0.10, 0.70, 0.50, strut=0.40, wheel_r=0.16)
    return p
'''

GROUND_ATTACK = '''
def a_ground_attack():
    """Ground-attack: armoured nose, chin gun, underwing stores, straight wing."""
    p = []
    p += fuselage3(-1.60, 1.75, 0.20, 0.34, 0.12, 0.56, segs=14)
    p += canopy_glazed(0.55, 0.82, 0.66, 0.40, 0.26, frames=2)
    for side in (-1, 1):
        p += wing(side, -0.10, 1.90, 1.02, 0.48, 0.50, 12.0, 0.10, BODY_D())
        p += wing_root_fairing(side, -0.10, 0.34, 0.54, 0.85, 0.10)
        for i in range(2):
            p.append(cyl((-0.20 - i * 0.55, side * 1.05, 0.40), 0.10, 0.60, BODY_D(),
                         rot=(0, math.radians(90), 0), verts=8))
    p.append(cyl((1.92, 0, 0.40), 0.11, 0.80, METAL(), rot=(0, math.radians(90), 0), verts=8))
    p.append(box((1.55, 0, 0.34), (0.30, 0.24, 0.20), BODY_D()))
    p += tail_unit(-1.55, 0.60, 1.35, 0.80, chord=0.46)
    p += propeller(1.80, 0.56, 0.80, blades=4)
    p += landing_gear(0.10, 0.58, 0.38, strut=0.30, wheel_r=0.12)
    return p
'''

JET_BOMBER = '''
def a_jet_bomber():
    """Swept-wing jet bomber: slim fuselage, podded engines on the wing, glazed nose."""
    p = []
    p += fuselage3(-2.10, 2.15, 0.14, 0.32, 0.12, 0.60, segs=16)
    p += canopy_glazed(1.45, 0.86, 0.60, 0.40, 0.24, frames=2)
    for side in (-1, 1):
        p += wing(side, -0.30, 2.00, 1.20, 0.52, 0.58, 32.0, 0.10, BODY_D())
        p += wing_root_fairing(side, -0.30, 0.40, 0.58, 0.95, 0.10)
        p += engine_nacelle(-0.55, side * 1.35, 0.44, 0.24, 0.85)
    p += tail_unit(-1.85, 0.64, 1.75, 0.88, chord=0.50)
    p += landing_gear(0.20, 0.66, 0.42, strut=0.34, wheel_r=0.14)
    return p
'''

VTOL = '''
def a_vtol():
    """Tilt-rotor VTOL: boxy fuselage, two tilt nacelles with thick dark rotors."""
    p = []
    p.append(box((0.10, 0, 0.62), (2.60, 0.88, 0.62), BODY()))
    p.append(box((1.45, 0, 0.70), (0.62, 0.70, 0.50), GLASS()))
    p.append(box((0.05, 0, 0.98), (1.35, 0.34, 0.12), BODY_D()))
    p.append(cone((-1.60, 0, 0.64), 0.14, 0.26, 0.75, BODY(),
                  rot=(0, math.radians(-90), 0), verts=12))
    for side in (-1, 1):
        p.append(box((0.22, side * 0.95, 0.66), (0.55, 1.10, 0.14), BODY_D()))
        p.append(cyl((0.22, side * 1.38, 0.80), 0.13, 0.32, METAL(), verts=10))
        p.append(box((0.22, side * 1.38, 1.00), (0.16, 1.9, 0.12), TRACK()))
        p.append(box((0.22, side * 1.38, 1.00), (1.9, 0.16, 0.12), TRACK()))
    p += tail_unit(-1.60, 0.78, 1.55, 0.72, chord=0.44)
    p += landing_gear(-0.10, 0.50, 0.34, strut=0.24, wheel_r=0.12)
    return p
'''

AIRSHIP = '''
def a_zeppelin_air():
    """Airship: banded envelope, large cruciform tail, podded engines, glazed gondola."""
    p = []
    p.append(cyl((0, 0, 0.95), 0.56, 4.40, BODY(), rot=(0, math.radians(90), 0), verts=20))
    p.append(cone((2.60, 0, 0.95), 0.12, 0.56, 0.85, BODY_D(),
                  rot=(0, math.radians(90), 0), verts=20))
    p.append(cone((-2.60, 0, 0.95), 0.10, 0.56, 0.85, BODY(),
                  rot=(0, math.radians(-90), 0), verts=20))
    for x in (-1.75, -0.85, 0.05, 0.95, 1.85):
        p.append(cyl((x, 0, 0.95), 0.59, 0.12, BODY_D(),
                     rot=(0, math.radians(90), 0), verts=20))
    p.append(box((-2.05, 0, 0.95), (1.00, 3.40, 0.12), BODY_D()))
    p.append(box((-2.05, 0, 0.95), (1.00, 0.12, 2.60), BODY_D()))
    p.append(box((-2.55, 0, 0.95), (0.30, 1.50, 0.10), ACCENT()))
    for dy in (-0.75, 0.75):
        p.append(box((-0.30, dy, 0.42), (0.85, 0.34, 0.30), BODY_D()))
        p.append(cyl((0.20, dy, 0.42), 0.15, 0.24, METAL(),
                     rot=(0, math.radians(90), 0), verts=8))
    p.append(box((0.55, 0, 0.30), (1.35, 0.60, 0.36), BODY_D()))
    p.append(box((0.95, 0, 0.34), (0.42, 0.52, 0.22), GLASS()))
    p.append(box((0.55, 0, 0.10), (1.00, 0.45, 0.12), METAL()))
    return p
'''
