"""Aircraft structure helpers, inserted verbatim into blender_vehicles.py.

Plain module-level functions that use the caller's existing primitives (box, cyl, cone,
wing) and material lambdas directly - the same pattern as the tank helpers, no closures.
"""

HELPERS = '''
# ------------------------------------------------------------------ aircraft structure
# Aircraft need a different structural grammar from tanks. The layers that survive a
# reduction to 32x36 are: a stepped nose/centre/tail fuselage, a contrasting glazed
# canopy, a wing-root fairing, a proper tail unit, podded engines and landing gear. A
# single long cylinder reads as a pipe, and a thin prop disc disappears completely.
def fuselage3(x0, x1, r_nose, r_mid, r_tail, z, segs=14):
    """Tapered nose + cylindrical centre + tapered tail, plus a dorsal spine."""
    p = []
    length = x1 - x0
    ln, lm, lt = length * 0.26, length * 0.44, length * 0.30
    p.append(cone((x1 - ln / 2, 0, z), r_nose, r_mid, ln, BODY(),
                  rot=(0, math.radians(90), 0), verts=segs))
    p.append(cyl((x1 - ln - lm / 2, 0, z), r_mid, lm, BODY(),
                 rot=(0, math.radians(90), 0), verts=segs))
    p.append(cone((x0 + lt / 2, 0, z), r_tail, r_mid, lt, BODY(),
                  rot=(0, math.radians(-90), 0), verts=segs))
    p.append(box((x0 + length * 0.40, 0, z + r_mid * 0.92),
                 (length * 0.54, r_mid * 0.55, r_mid * 0.24), BODY_D()))
    return p


def canopy_glazed(x, z, length, width, height, frames=2):
    """Glazed cockpit with dark framing bands, raised above the spine."""
    p = []
    p.append(box((x, 0, z + height * 0.5), (length, width, height * 0.55), GLASS()))
    p.append(box((x, 0, z + height * 0.94), (length * 0.9, width * 0.86, height * 0.30),
                 BODY_D()))
    for i in range(frames):
        p.append(box((x - length * 0.3 + i * length * 0.6, 0, z + height * 0.45),
                     (length * 0.10, width * 1.03, height * 0.55), BODY_D()))
    return p


def tail_unit(x, z, span, fin_h, chord=0.42):
    """Horizontal stabiliser plus vertical fin with a contrasting rudder."""
    p = []
    p.append(box((x, 0, z), (chord, span, 0.07), BODY_D()))
    p.append(box((x - chord * 0.35, 0, z + fin_h * 0.5), (chord * 0.85, 0.08, fin_h), BODY_D()))
    p.append(box((x - chord * 0.62, 0, z + fin_h * 0.55), (chord * 0.16, 0.09, fin_h * 0.8),
                 BODY()))
    return p


def wing_root_fairing(side, x, y, z, chord, thick):
    """Fairing at the wing/fuselage junction so the join is not a bare box corner."""
    return [box((x, side * y, z), (chord, 0.34, thick * 1.5), BODY())]


def landing_gear(x, y, z, strut=0.34, wheel_r=0.13, nose=True):
    """Two main legs plus a nose leg; small dark stubs that still register at 32x36."""
    p = []
    for side in (-1, 1):
        p.append(box((x, side * y, z - strut * 0.5), (0.07, 0.07, strut), METAL()))
        p.append(cyl((x, side * y, z - strut), wheel_r, 0.10, TRACK(),
                     rot=(math.radians(90), 0, 0), verts=10))
    if nose:
        p.append(box((x + 0.55, 0, z - strut * 0.4), (0.07, 0.07, strut * 0.85), METAL()))
        p.append(cyl((x + 0.55, 0, z - strut * 0.85), wheel_r * 0.9, 0.10, TRACK(),
                     rot=(math.radians(90), 0, 0), verts=10))
    return p


def engine_nacelle(x, y, z, r, length, intake=True):
    """Podded engine with a dark intake ring and an exhaust stub."""
    p = []
    p.append(cyl((x, y, z), r, length, BODY_D(), rot=(0, math.radians(90), 0), verts=12))
    if intake:
        p.append(cyl((x + length * 0.5, y, z), r * 0.72, 0.10, TRACK(),
                     rot=(0, math.radians(90), 0), verts=10))
    p.append(cyl((x - length * 0.55, y, z), r * 0.62, 0.16, METAL(),
                 rot=(0, math.radians(90), 0), verts=10))
    return p


def propeller(x, z, r, blades=4, thickness=0.07):
    """Prop as thick dark blades; a thin disc disappears when downscaled."""
    p = []
    p.append(cyl((x, 0, z), r * 0.18, 0.16, BODY_D(), rot=(0, math.radians(90), 0), verts=8))
    for i in range(blades):
        a = math.pi * i / blades
        p.append(box((x, 0, z), (0.07, r * 2.0 * abs(math.cos(a)) + thickness,
                                 r * 2.0 * abs(math.sin(a)) + thickness), TRACK()))
    return p

'''
