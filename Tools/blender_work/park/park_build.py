# Soul Quota: bone / graveyard skatepark pieces (original work).
# Units: built in cm (Z up, Blender axes), scaled to metres on export; FBX UnitScaleFactor 100 -> UE cm, scale 1.
# Every rideable surface and its UCX_<Mesh>_## convex hulls are generated from the SAME profile points,
# so collision matches the visual exactly. Ride direction is +X for every piece (UE +X too: X is unchanged).
# Pivot: bounding-box centre of the footprint on the ground (Z = 0 = floor).
# usage: python3 park_build.py
import bpy, bmesh, math, json, os
from mathutils import Vector

OUT = "/home/claude/sq/ArtSource/Props/"
PAL_PATH = OUT + "park_palette.png"

# ---------------------------------------------------------------- palette (8x8 swatches of 16 px, UVs at swatch centres)
LAYOUT = ["bone", "bone_shade", "bone_dark", "bone_light", "stone", "stone_dark", "stone_light", "cavity",
          "moss", "moss_dark", "dirt", "dirt_dark", "wood", "wood_dark", "wood_light", "iron",
          "tooth", "gum", "cartilage", "stone_moss", "grave_hole", "plank_line", "lichen", "marrow"]
COLOURS = {  # sRGB 0-255
    "bone": (232, 222, 196), "bone_shade": (198, 184, 152), "bone_dark": (146, 130, 104), "bone_light": (248, 243, 228),
    "stone": (134, 137, 148), "stone_dark": (88, 90, 101), "stone_light": (172, 175, 184), "cavity": (30, 22, 32),
    "moss": (104, 140, 66), "moss_dark": (64, 94, 46), "dirt": (110, 84, 62), "dirt_dark": (74, 57, 45),
    "wood": (126, 84, 54), "wood_dark": (82, 53, 36), "wood_light": (164, 116, 72), "iron": (66, 68, 78),
    "tooth": (244, 240, 220), "gum": (150, 72, 82), "cartilage": (204, 176, 160), "stone_moss": (112, 128, 98),
    "grave_hole": (40, 30, 28), "plank_line": (96, 63, 40), "lichen": (170, 176, 110), "marrow": (178, 96, 84),
}
SW = {n: i for i, n in enumerate(LAYOUT)}
GRID, CELL = 8, 16


def write_palette():
    from PIL import Image
    im = Image.new("RGB", (GRID * CELL, GRID * CELL), (128, 128, 128))
    px = im.load()
    for i in range(GRID * GRID):
        c = COLOURS.get(LAYOUT[i], (128, 128, 128)) if i < len(LAYOUT) else (128, 128, 128)
        cx, cy = i % GRID, i // GRID
        # Blender UV v=0 is the bottom row of the image: swatch row cy is image row (GRID-1-cy)
        for y in range((GRID - 1 - cy) * CELL, (GRID - cy) * CELL):
            for x in range(cx * CELL, cx * CELL + CELL):
                px[x, y] = c
    im.save(PAL_PATH)


def swatch_uv(i):
    return ((i % GRID + 0.5) / GRID, (i // GRID + 0.5) / GRID)


# ---------------------------------------------------------------- piece builder
class Piece:
    def __init__(self, name):
        self.name = name
        self.parts = []   # (bmesh, closed)
        self.hulls = []   # lists of Vector points (cm)
        self.ride = ""

    def part(self, closed=True):
        bm = bmesh.new()
        bm.faces.layers.int.new("sw")
        self.parts.append((bm, closed))
        return bm

    def hull(self, pts, deco=False):
        self.hulls.append(([Vector(p) for p in pts], deco))


def F(bm, verts, sw):
    f = bm.faces.new(verts)
    f[bm.faces.layers.int["sw"]] = SW[sw]
    return f


def V(bm, co):
    return bm.verts.new(Vector(co))


def extrude_profile(P, prof, y0, y1, top, side, bottom="stone_dark", ends=None, colour_fn=None):
    """solid under a top profile [(x,z)...] (x increasing), closed down to z=0, extruded y0..y1.
    colour_fn(i, xmid) -> swatch for top segment i (default `top`)."""
    bm = P.part()
    poly = []
    if prof[0][1] > 0.01:
        poly.append((prof[0][0], 0.0))
    poly += list(prof)
    if prof[-1][1] > 0.01:
        poly.append((prof[-1][0], 0.0))
    a = [V(bm, (x, y0, z)) for x, z in poly]
    b = [V(bm, (x, y1, z)) for x, z in poly]
    n = len(poly)
    off = 1 if prof[0][1] > 0.01 else 0
    for k in range(n):
        k2 = (k + 1) % n
        if k2 == 0:   # closing edge along the ground
            sw = bottom
        elif off <= k < off + len(prof) - 1:
            i = k - off
            sw = colour_fn(i, (poly[k][0] + poly[k2][0]) / 2) if colour_fn else top
        else:
            sw = ends or side
        F(bm, [a[k], a[k2], b[k2], b[k]], sw)
    F(bm, a[::-1], side)
    F(bm, b, side)
    return bm


def profile_hulls(P, prof, y0, y1, step=1):
    """one convex slab per profile segment: under the chord, down to the ground"""
    for i in range(0, len(prof) - 1, step):
        (xa, za), (xb, zb) = prof[i], prof[min(i + step, len(prof) - 1)]
        pts = []
        for y in (y0, y1):
            pts += [(xa, y, 0), (xb, y, 0), (xa, y, za), (xb, y, zb)]
        P.hull(pts)


def box(P, c, s, sw, top=None, hull=False):
    bm = P.part()
    x0, x1 = c[0] - s[0] / 2, c[0] + s[0] / 2
    y0, y1 = c[1] - s[1] / 2, c[1] + s[1] / 2
    z0, z1 = c[2] - s[2] / 2, c[2] + s[2] / 2
    v = [V(bm, (x, y, z)) for z in (z0, z1) for y in (y0, y1) for x in (x0, x1)]
    F(bm, [v[0], v[2], v[3], v[1]], sw)
    F(bm, [v[4], v[5], v[7], v[6]], top or sw)
    F(bm, [v[0], v[1], v[5], v[4]], sw)
    F(bm, [v[2], v[6], v[7], v[3]], sw)
    F(bm, [v[0], v[4], v[6], v[2]], sw)
    F(bm, [v[1], v[3], v[7], v[5]], sw)
    if hull:
        P.hull([vv.co.copy() for vv in v], deco=True)
    return bm


def frustum(P, base, top_, z0, z1, sw_top, sw_side, hull=True):
    """convex frustum between two rectangles (x0,x1,y0,y1) at z0 / z1"""
    bm = P.part()
    def rect(r, z):
        x0, x1, y0, y1 = r
        return [V(bm, (x0, y0, z)), V(bm, (x1, y0, z)), V(bm, (x1, y1, z)), V(bm, (x0, y1, z))]
    a, b = rect(base, z0), rect(top_, z1)
    F(bm, a[::-1], "stone_dark")
    F(bm, b, sw_top)
    for k in range(4):
        F(bm, [a[k], a[(k + 1) % 4], b[(k + 1) % 4], b[k]], sw_side)
    if hull:
        P.hull([v.co.copy() for v in a + b])
    return bm


def ico(P, c, r, sw, sq=(1, 1, 1), hull=False, subd=1):
    bm = P.part()
    res = bmesh.ops.create_icosphere(bm, subdivisions=subd, radius=1.0)
    for v in res["verts"]:
        v.co = Vector((c[0] + v.co.x * r * sq[0], c[1] + v.co.y * r * sq[1], c[2] + v.co.z * r * sq[2]))
    lay = bm.faces.layers.int["sw"]
    for f in bm.faces:
        f[lay] = SW[sw]
    if hull:
        P.hull([v.co.copy() for v in bm.verts], deco=True)
    return bm


def tube(P, pts, r, seg, sw, cap=True):
    bm = P.part(closed=cap)
    rings = []
    for i, c in enumerate(pts):
        c = Vector(c)
        t = (Vector(pts[min(i + 1, len(pts) - 1)]) - Vector(pts[max(i - 1, 0)])).normalized()
        u = t.cross(Vector((0, 0, 1)))
        if u.length < 1e-3:
            u = Vector((0, 1, 0))
        u.normalize()
        w = u.cross(t)
        rr = r[i] if isinstance(r, (list, tuple)) else r
        rings.append([V(bm, c + u * (rr * math.cos(2 * math.pi * k / seg)) + w * (rr * math.sin(2 * math.pi * k / seg))) for k in range(seg)])
    for i in range(len(rings) - 1):
        for k in range(seg):
            F(bm, [rings[i][k], rings[i][(k + 1) % seg], rings[i + 1][(k + 1) % seg], rings[i + 1][k]], sw)
    if cap:
        F(bm, rings[0][::-1], sw)
        F(bm, rings[-1], sw)
    return bm


def decal(P, origin, u, v, shape, sw, lift=0.8):
    """flat polygon on a plane (origin, unit axes u, v); shape = [(a,b)...] in cm; lifted along u x v"""
    bm = P.part(closed=False)
    o, u, v = Vector(origin), Vector(u).normalized(), Vector(v).normalized()
    n = u.cross(v).normalized()
    vs = [V(bm, o + u * a + v * b + n * lift) for a, b in shape]
    f = F(bm, vs, sw)
    f.normal_update()
    if f.normal.dot(n) < 0:
        f.normal_flip()
    return bm


def ellipse(rx, ry, n=10, cx=0, cy=0):
    return [(cx + rx * math.cos(2 * math.pi * k / n), cy + ry * math.sin(2 * math.pi * k / n)) for k in range(n)]


def lathe(P, prof_rz, thetas, colours, cap_sw):
    """solid of chorded revolution: prof_rz closed polygon [(r,z)...] swept over thetas (deg), colours[k] per edge"""
    bm = P.part()
    rings = []
    for th in thetas:
        c, s = math.cos(math.radians(th)), math.sin(math.radians(th))
        rings.append([V(bm, (r * c, r * s, z)) for r, z in prof_rz])
    n = len(prof_rz)
    for i in range(len(rings) - 1):
        for k in range(n):
            F(bm, [rings[i][k], rings[i][(k + 1) % n], rings[i + 1][(k + 1) % n], rings[i + 1][k]], colours[k])
    F(bm, rings[0], cap_sw)
    F(bm, rings[-1][::-1], cap_sw)
    return bm


# ---------------------------------------------------------------- pieces
def rib_kicker():
    P = Piece("Park_RibKicker")
    P.ride = "+X: roll up from the low end (-X), launch off the lip at +X"
    L, H, W = 300.0, 120.0, 200.0
    n = 10
    prof = [(-L / 2 + L * i / n, H * (i / n) ** 2) for i in range(n + 1)]   # max 38.7 deg at the lip
    extrude_profile(P, prof, -W / 2, W / 2, "bone", "bone_shade", ends="bone_dark",
                    colour_fn=lambda i, x: "bone_light" if i == n - 1 else "bone")
    profile_hulls(P, prof, -W / 2, W / 2)
    # rib bones along both edges, flush with the deck edge (outside the ride lane)
    for s in (-1, 1):
        r = 13.0
        pts = [(x, s * (W / 2 + r * 0.55), max(z - r * 0.9, r * 0.4)) for x, z in prof]
        tube(P, pts, r, 6, "bone_shade")
        ico(P, (L / 2 - 4, s * (W / 2 + 8), H - 24), 24, "bone", hull=True)   # rib head knobs (top = lip height)
    # stone foot at the back + moss
    box(P, (L / 2 + 15, 0, 45), (30, W + 30, 90), "stone", top="stone_moss", hull=True)
    # marrow cracks on the side faces (dark decals)
    for s in (-1, 1):
        decal(P, (40, s * W / 2, 25), (1, 0, 0), (0, 0, 1) if s < 0 else (0, 0, -1), [(-40, 0), (0, 6), (35, -4), (0, -2)], "bone_dark")
    return P


def femur_ramp():
    P = Piece("Park_FemurRamp")
    P.ride = "+X: start on the high end at -X (180 cm), slide down the shaft to the ground at +X"
    xa, za, xb, zb = -270.0, 180.0, 190.0, 40.0
    slope = (zb - za) / (xb - xa)
    # cross-section (y, depth below top): flat 70 cm top, chamfers, 50 cm thick
    cs = [(-35, 0), (35, 0), (50, 9), (50, 35), (35, 50), (-35, 50), (-50, 35), (-50, 9)]
    bm = P.part()
    ra = [V(bm, (xa, y, za - d)) for y, d in cs]
    rb = [V(bm, (xb + 5, y, zb + slope * 5 - d)) for y, d in cs]
    cols = ["bone", "bone_shade", "bone_shade", "bone_dark", "bone_dark", "bone_dark", "bone_shade", "bone_shade"]
    for k in range(8):
        F(bm, [ra[k], rb[k], rb[(k + 1) % 8], ra[(k + 1) % 8]], cols[k])
    F(bm, ra, "bone_dark")
    F(bm, rb[::-1], "bone_dark")
    P.hull([v.co.copy() for v in ra + rb])
    # dirt mound runout (Hermite: matches the shaft slope, flat at the ground) 190 -> 300
    m0 = slope * 110.0
    run = []
    for i in range(9):
        s = i / 8
        h = (2 * s ** 3 - 3 * s ** 2 + 1) * zb + (s ** 3 - 2 * s ** 2 + s) * m0
        run.append((xb + 110.0 * s, max(h, 0.0)))
    extrude_profile(P, run, -70, 70, "dirt", "dirt_dark", colour_fn=lambda i, x: "moss" if i % 3 == 1 else "dirt")
    profile_hulls(P, run, -70, 70)
    # knee condyles poking out of the mound, outside the lane
    for s in (-1, 1):
        ico(P, (205, s * 78, 12), 28, "bone", sq=(1.1, 0.9, 0.9))
    # tomb block under the hip end
    box(P, (-250, 0, 64), (100, 150, 128), "stone", top="stone_moss", hull=True)
    decal(P, (-300, 0, 70), (0, -1, 0), (0, 0, 1), [(-12, -40), (12, -40), (12, 10), (30, 10), (30, 30), (12, 30), (12, 45), (-12, 45), (-12, 30), (-30, 30), (-30, 10), (-12, 10)], "stone_dark")
    # femur head on its neck (off the -Y side) + greater trochanter (+Y side): both solid
    tube(P, [(-255, -35, 150), (-275, -80, 160)], 22, 6, "bone_shade")
    ico(P, (-282, -98, 158), 40, "bone", hull=True)
    ico(P, (-268, 80, 162), 26, "bone_shade", sq=(1.2, 1, 1.1), hull=True)
    return P


def spine_wave():
    P = Piece("Park_SpineWave")
    P.ride = "+X (either way along X): three rolling vertebra humps (pump bumps), 60 cm high"
    L, A, W = 800.0, 60.0, 180.0
    humps, per = 3, 10
    n = humps * per
    prof = [(-L / 2 + L * i / n, A * math.sin(math.pi * humps * i / n) ** 2) for i in range(n + 1)]  # max 35 deg
    def col(i, x):
        ph = (i % per) / per
        return "cartilage" if ph < 0.15 or ph >= 0.85 else ("bone_light" if 0.35 <= ph < 0.65 else "bone")
    extrude_profile(P, prof, -W / 2, W / 2, "bone", "bone_shade", colour_fn=col)
    profile_hulls(P, prof, -W / 2, W / 2)
    # transverse processes (side wings) on every hump, kept below the hump crest
    for h in range(humps):
        xc = -L / 2 + L * (h + 0.5) / humps
        for s in (-1, 1):
            bm = P.part()
            pts = [(xc - 45, s * W / 2, 0), (xc + 45, s * W / 2, 0), (xc + 25, s * (W / 2 + 60), 0), (xc - 25, s * (W / 2 + 60), 0)]
            top = [(xc - 35, s * W / 2, 38), (xc + 35, s * W / 2, 38), (xc + 18, s * (W / 2 + 55), 22), (xc - 18, s * (W / 2 + 55), 22)]
            a = [V(bm, p) for p in pts]
            b = [V(bm, p) for p in top]
            F(bm, a[::-1], "bone_dark")
            F(bm, b, "bone_shade")
            for k in range(4):
                F(bm, [a[k], a[(k + 1) % 4], b[(k + 1) % 4], b[k]], "bone_dark")
            P.hull(pts + top, deco=True)
    return P


def pelvis_quarter_pipe():
    P = Piece("Park_PelvisQuarter")
    P.ride = "+X: roll in along +X, up the curved wall to the coping at the top; the deck behind is at 236 cm"
    R, amax, W, n = 285.0, 80.0, 500.0, 12
    x0 = 0.0
    prof = [(x0 + R * math.sin(math.radians(amax * i / n)), R * (1 - math.cos(math.radians(amax * i / n)))) for i in range(n + 1)]
    xl, zl = prof[-1]
    deck = 110.0
    full = prof + [(xl + deck, zl)]
    extrude_profile(P, full, -W / 2, W / 2, "bone", "bone_shade", ends="stone_dark",
                    colour_fn=lambda i, x: "stone_moss" if i == len(full) - 2 else ("bone_light" if i >= n - 2 else "bone"))
    profile_hulls(P, prof, -W / 2, W / 2)                                     # 12 transition slabs
    P.hull([(xl, y, z) for y in (-W / 2, W / 2) for z in (0, zl)] + [(xl + deck, y, z) for y in (-W / 2, W / 2) for z in (0, zl)])
    # bone coping: front tangent flush with the wall at the lip, 13 cm proud of the deck
    r = 16.0
    a = math.radians(amax)
    cx, cz = xl + math.sin(a) * r, zl - math.cos(a) * r
    bm = tube(P, [(cx, -W / 2, cz), (cx, W / 2, cz)], r, 10, "bone_light")
    P.hull([v.co.copy() for v in bm.verts])
    for s in (-1, 1):   # coping end knobs (outside the width)
        ico(P, (cx, s * (W / 2 + 10), cz), 24, "bone", hull=True)
    # pelvis: iliac wings flaring up from the back corners of the deck + crest tubes along the sides
    for s in (-1, 1):
        bm = P.part()
        y0, y1 = s * (W / 2 - 60), s * (W / 2 + 5)
        base = [(xl + 30, y0, zl), (xl + deck, y0, zl), (xl + deck, y1, zl), (xl + 30, y1, zl)]
        top = [(xl + 50, y0 + s * 15, zl + 75), (xl + deck + 10, y0 + s * 10, zl + 95), (xl + deck + 10, y1 + s * 25, zl + 90), (xl + 45, y1 + s * 20, zl + 70)]
        av = [V(bm, p) for p in base]
        bv = [V(bm, p) for p in top]
        F(bm, av[::-1], "bone_dark")
        F(bm, bv, "bone_light")
        for k in range(4):
            F(bm, [av[k], av[(k + 1) % 4], bv[(k + 1) % 4], bv[k]], "bone" if k != 1 else "bone_shade")
        P.hull(base + top, deco=True)
        pts = [(x, s * (W / 2 + 9), max(z - 16, 12)) for x, z in prof[::2]] + [(xl + deck, s * (W / 2 + 9), zl - 16)]
        tube(P, pts, 14, 6, "bone_shade")
        # obturator foramen: dark oval on each side face
        decal(P, (xl * 0.55, s * W / 2, zl * 0.32), (1, 0, 0), (0, 0, s * -1) if s > 0 else (0, 0, 1), ellipse(55, 32, 10), "cavity")
    return P


def skull_hip():
    P = Piece("Park_SkullHip")
    P.ride = "Any side; the 4 corner edges are hips: ride diagonally over an edge to transfer. Face (eyes) looks -X"
    bx, by, tx, ty, H = 210.0, 190.0, 55.0, 45.0, 120.0   # half sizes; slopes 37.7 / 39.6 deg
    frustum(P, (-bx, bx, -by, by), (-tx, tx, -ty, ty), 0, H, "bone_light", "bone")
    # face on the -X slope: plane through the front edge
    o = Vector((-bx, 0, 0))
    u = Vector((0, 1, 0))
    v = (Vector((-tx, 0, H)) - o).normalized()
    Lv = (Vector((-tx, 0, H)) - o).length
    for s in (-1, 1):
        decal(P, o, u, v, ellipse(42, 36, 10, s * 62, Lv * 0.6), "cavity")
    decal(P, o, u, v, [(-22, Lv * 0.30), (22, Lv * 0.30), (0, Lv * 0.42)], "cavity")
    for k in range(-4, 5):   # teeth along the bottom of the face
        decal(P, o, u, v, [(k * 34 - 13, 4), (k * 34 + 13, 4), (k * 34 + 13, 34), (k * 34 - 13, 34)], "tooth", lift=0.9)
    decal(P, o, u, v, [(-170, 0), (170, 0), (160, 40), (-160, 40)], "cavity", lift=0.5)
    # crack on the cranium (+X slope) and darker hip edges (thin decals)
    o2, v2 = Vector((bx, 0, 0)), (Vector((tx, 0, H)) - Vector((bx, 0, 0))).normalized()
    decal(P, o2, Vector((0, -1, 0)), v2, [(-10, 40), (20, 90), (5, 140), (15, 175), (8, 176), (-4, 140), (10, 92), (-16, 42)], "bone_dark")
    return P


def coffin_funbox():
    P = Piece("Park_CoffinFunbox")
    P.ride = "+X or -X: up the foot ramp (-X), across the 90 cm coffin lid, down the head ramp (+X)"
    H = 90.0
    plan = [(-130, -60), (50, -105), (130, -75), (130, 75), (50, 105), (-130, 60)]   # coffin outline (foot at -X)
    bm = P.part()
    a = [V(bm, (x, y, 0)) for x, y in plan]
    b = [V(bm, (x, y, H)) for x, y in plan]
    F(bm, a[::-1], "wood_dark")
    F(bm, b, "wood")
    for k in range(6):
        F(bm, [a[k], a[(k + 1) % 6], b[(k + 1) % 6], b[k]], "wood_dark")
    P.hull([v.co.copy() for v in a + b])
    # lid: bone cross inlay + darker trim, flush
    decal(P, (0, 0, H), (1, 0, 0), (0, 1, 0), [(-90, -12), (90, -12), (90, 12), (-90, 12)], "bone", 0.6)
    decal(P, (0, 0, H), (1, 0, 0), (0, 1, 0), [(28, -55), (52, -55), (52, 55), (28, 55)], "bone", 0.7)
    decal(P, (0, 0, H), (1, 0, 0), (0, 1, 0), [(x * 0.9, y * 0.9) for x, y in plan], "wood_light", 0.3)
    # handles on the long sides (iron, small, no collision)
    for s in (-1, 1):
        for x in (-60, 20):
            yy = s * (60 + (105 - 60) * (x + 130) / 180)
            tube(P, [(x - 18, yy, 45), (x - 18, yy + s * 7, 45), (x + 18, yy + s * 7, 45), (x + 18, yy, 45)], 3, 4, "iron", cap=True)
    # plank ramps, planar (convex)
    for x_end, x_far, half in ((-130, -350, 60), (130, 350, 75)):
        prof = [(x_far, 0.0), (x_end, H)] if x_far < x_end else [(x_end, H), (x_far, 0.0)]
        n = 6
        extrude_profile(P, prof, -half, half, "wood_light", "wood_dark")
        P.hull([(x_far, y, 0) for y in (-half, half)] + [(x_end, y, 0) for y in (-half, half)] + [(x_end, y, H) for y in (-half, half)])
        # plank lines (thin flush decals across the ramp)
        L = abs(x_end - x_far)
        d = Vector((x_end - x_far, 0, H)).normalized()
        for k in range(1, n):
            t = k / n
            o = Vector((x_far + (x_end - x_far) * t, 0, H * t))
            decal(P, o, Vector((0, 1, 0)) if x_far < x_end else Vector((0, -1, 0)), d, [(-half, -2), (half, -2), (half, 2), (-half, 2)], "plank_line", 0.5)
    return P


def manual_pad():
    P = Piece("Park_TombPad")
    P.ride = "+X (along the slab): low ledger tombstone, 30 cm high, all edges chamfered 37 deg"
    frustum(P, (-180, 180, -100, 100), (-140, 140, -60, 60), 0, 30, "stone_light", "stone")
    o = Vector((0, 0, 30))
    decal(P, o, (1, 0, 0), (0, 1, 0), [(-110, -10), (60, -10), (60, -40), (80, -40), (80, -10), (110, -10), (110, 10), (80, 10), (80, 40), (60, 40), (60, 10), (-110, 10)], "stone_dark")
    for cx, cy, rx, ry in ((-120, 45, 30, 14), (125, -42, 22, 15), (-40, -50, 26, 9)):
        decal(P, o, (1, 0, 0), (0, 1, 0), ellipse(rx, ry, 8, cx, cy), "moss", 0.5)
    return P


def jawbone_bank():
    P = Piece("Park_JawBank")
    P.ride = "Ride outward from inside the U: up the 32 deg curved bank (radius 120 -> 330) to the 50 cm deck; teeth wall behind"
    r0, r1, r2, H = 120.0, 330.0, 380.0, 130.0
    span, n = 75.0, 10
    th = [-span + 2 * span * i / n for i in range(n + 1)]
    prof = [(r0, 0.0), (r1, H), (r2, H), (r2, 0.0)]
    lathe(P, prof, th, ["bone", "bone_light", "bone_shade", "stone_dark"], "bone_dark")
    for i in range(n):
        pts = []
        for t in (th[i], th[i + 1]):
            c, s = math.cos(math.radians(t)), math.sin(math.radians(t))
            pts += [(r * c, r * s, z) for r, z in prof]
        P.hull(pts)
        # one molar per segment on the outer rim (wall)
        tm = math.radians((th[i] + th[i + 1]) / 2)
        c, s = math.cos(tm), math.sin(tm)
        bm = P.part()
        hw = 2 * span / n / 2 * 0.8
        corners = []
        for rr in (r2 - 2, r2 + 40):
            for dt in (-hw, hw):
                t2 = math.radians((th[i] + th[i + 1]) / 2 + dt)
                corners.append((rr * math.cos(t2), rr * math.sin(t2)))
        lo = [V(bm, (x, y, H - 20)) for x, y in (corners[0], corners[1], corners[3], corners[2])]
        hi = [V(bm, (x * 0.97 + c * 4, y * 0.97 + s * 4, H + 38)) for x, y in (corners[0], corners[1], corners[3], corners[2])]
        F(bm, lo[::-1], "tooth")
        F(bm, hi, "tooth")
        for k in range(4):
            F(bm, [lo[k], lo[(k + 1) % 4], hi[(k + 1) % 4], hi[k]], "tooth")
        P.hull([v.co.copy() for v in lo + hi], deco=True)
        # gum band under the teeth (outer face)
    gum = [(r2 + 1, H - 30), (r2 + 1, H - 18)]
    for i in range(n):
        tm0, tm1 = math.radians(th[i]), math.radians(th[i + 1])
        bm = P.part(closed=False)
        vs = [V(bm, (r * math.cos(t), r * math.sin(t), z)) for (r, z), t in ((gum[0], tm0), (gum[0], tm1), (gum[1], tm1), (gum[1], tm0))]
        F(bm, vs, "gum")
    # condyles: rounded posts on the deck at both tips
    for t in (th[0], th[-1]):
        c, s = math.cos(math.radians(t)), math.sin(math.radians(t))
        ico(P, ((r2 - 25) * c, (r2 - 25) * s, H + 20), 30, "bone_light", sq=(1, 1, 1.3), hull=True)
    return P


def grave_mound_roller():
    P = Piece("Park_GraveRoller")
    P.ride = "+X (either way along X): one smooth 70 cm grave-mound roller"
    L, A, W, flat = 500.0, 70.0, 300.0, 60.0
    nx = 12
    xs = [-L / 2 + L * i / nx for i in range(nx + 1)]
    ys = [-W / 2, -flat, flat, W / 2]
    h = lambda x: A * (1 - math.cos(2 * math.pi * (x + L / 2) / L)) / 2
    sfac = lambda y: 1.0 if abs(y) <= flat else 0.0
    bm = P.part(closed=False)   # open height-field sheet; its border lies on the ground
    top = [[V(bm, (x, y, h(x) * sfac(y))) for y in ys] for x in xs]
    for i in range(nx):
        for j in range(3):
            a, b, c, d = top[i][j], top[i + 1][j], top[i + 1][j + 1], top[i][j + 1]
            sw = ("moss" if (i * 7 + j * 3) % 5 in (0, 2) else "moss_dark") if j == 1 else ("dirt" if (i + j) % 3 else "moss_dark")
            # pick the upper diagonal so the visual matches the convex hull top
            if (a.co.z + c.co.z) >= (b.co.z + d.co.z):
                F(bm, [a, b, c], sw); F(bm, [a, c, d], sw)
            else:
                F(bm, [a, b, d], sw); F(bm, [b, c, d], sw)
            tp = [a.co.copy(), b.co.copy(), c.co.copy(), d.co.copy()]
            P.hull(tp + [Vector((q.x, q.y, 0.0)) for q in tp])
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.01)
    # scatter: small bones/skull lying beside the mound (visual only, outside the ride lane)
    tube(P, [(-150, W / 2 - 25, 6), (-100, W / 2 - 15, 6)], 5, 5, "bone")
    ico(P, (-153, W / 2 - 26, 7), 8, "bone", subd=1)
    ico(P, (-97, W / 2 - 14, 7), 8, "bone", subd=1)
    ico(P, (170, -W / 2 + 30, 12), 15, "bone_light", sq=(1, 1, 0.85))
    # wooden grave cross at the head end, off the lane (+X end, beyond the roller)
    box(P, (L / 2 + 20, 0, 55), (12, 12, 110), "wood", hull=True)
    box(P, (L / 2 + 20, 0, 82), (12, 70, 12), "wood")
    return P


def bone_bridge():
    P = Piece("Park_BoneBridge")
    P.ride = "+X: kicker at -X, 280 cm gap (open grave) with a 40 cm bone bridge across it, land on the deck and roll down at +X"
    W, H = 220.0, 100.0
    n = 8
    prof = [(-450 + 250 * i / n, H * (i / n) ** 2) for i in range(n + 1)] + [(-140.0, H)]   # 38.7 deg at the lip
    extrude_profile(P, prof, -W / 2, W / 2, "bone", "stone", ends="stone_dark",
                    colour_fn=lambda i, x: "stone_moss" if x > -200 else ("bone_light" if i == n - 1 else "bone"))
    profile_hulls(P, prof[:-1], -W / 2, W / 2)
    P.hull([(x, y, z) for x in (-200, -140) for y in (-W / 2, W / 2) for z in (0, H)])
    land = [(140.0, H), (200.0, H), (500.0, 0.0)]                           # 18.4 deg runout
    extrude_profile(P, land, -W / 2, W / 2, "bone", "stone", ends="stone_dark",
                    colour_fn=lambda i, x: "stone_moss" if i == 0 else "bone")
    P.hull([(140, y, 0) for y in (-W / 2, W / 2)] + [(x, y, H) for x in (140, 200) for y in (-W / 2, W / 2)] + [(500, y, 0) for y in (-W / 2, W / 2)])
    # the bone bridge: flat 40 cm top flush with both decks, knobbed ends at the gap edges
    cs = [(-20, 0), (20, 0), (26, 12), (20, 30), (-20, 30), (-26, 12)]
    bm = P.part()
    ra = [V(bm, (-175, y, H - d)) for y, d in cs]
    rb = [V(bm, (175, y, H - d)) for y, d in cs]
    for k in range(6):
        F(bm, [ra[k], rb[k], rb[(k + 1) % 6], ra[(k + 1) % 6]], "bone_light" if k == 0 else "bone_shade")
    F(bm, ra, "bone_dark")
    F(bm, rb[::-1], "bone_dark")
    knobs = []
    for x in (-150, 150):
        for s in (-1, 1):
            knobs.append(ico(P, (x, s * 26, H - 27), 27, "bone"))
    P.hull([v.co.copy() for v in ra + rb] + [v.co.copy() for k in knobs for v in k.verts])
    # open grave in the gap (flat decal) + dirt piles beside the gap (outside the lane)
    decal(P, (0, 0, 0), (1, 0, 0), (0, 1, 0), [(-125, -70), (125, -70), (125, 70), (-125, 70)], "grave_hole", 0.8)
    decal(P, (0, 0, 0), (1, 0, 0), (0, 1, 0), [(-135, -80), (135, -80), (135, -70), (-135, -70)], "dirt", 0.9)
    decal(P, (0, 0, 0), (1, 0, 0), (0, 1, 0), [(-135, 70), (135, 70), (135, 80), (-135, 80)], "dirt", 0.9)
    for s in (-1, 1):
        ico(P, (0, s * (W / 2 + 30), 0), 45, "dirt", sq=(2.2, 0.8, 0.55))
    return P


PIECES = [rib_kicker, femur_ramp, spine_wave, pelvis_quarter_pipe, skull_hip, coffin_funbox, manual_pad,
          jawbone_bank, grave_mound_roller, bone_bridge]


# ---------------------------------------------------------------- objects + export
def make_material(img):
    m = bpy.data.materials.get("M_Park") or bpy.data.materials.new("M_Park")
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes.get("Principled BSDF")
    t = nt.nodes.new("ShaderNodeTexImage")
    t.image = img
    t.interpolation = 'Closest'
    nt.links.new(t.outputs["Color"], b.inputs["Base Color"])
    b.inputs["Roughness"].default_value = 0.85
    return m


def build_objects(P, mat):
    # merge parts
    bm = bmesh.new()
    lay = bm.faces.layers.int.new("sw")
    for part, closed in P.parts:
        if closed:
            bmesh.ops.recalc_face_normals(part, faces=part.faces)
        pl = part.faces.layers.int["sw"]
        vm = {v: bm.verts.new(v.co) for v in part.verts}
        for f in part.faces:
            nf = bm.faces.new([vm[v] for v in f.verts])
            nf[lay] = f[pl]
        part.free()
    # centre the footprint on the origin (XY bbox centre), floor stays at Z=0
    xs = [v.co.x for v in bm.verts]
    ys = [v.co.y for v in bm.verts]
    off = Vector(((min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2, 0))
    for v in bm.verts:
        v.co -= off
    bmesh.ops.triangulate(bm, faces=bm.faces)
    uv = bm.loops.layers.uv.new("UVMap")
    for f in bm.faces:
        u = swatch_uv(f[lay])
        for l in f.loops:
            l[uv].uv = u
    for v in bm.verts:
        v.co *= 0.01
    me = bpy.data.meshes.new(P.name)
    bm.to_mesh(me)
    bm.free()
    me.materials.append(mat)
    ob = bpy.data.objects.new(P.name, me)
    bpy.context.scene.collection.objects.link(ob)
    hulls = []
    for k, (pts, deco) in enumerate(P.hulls):
        hb = bmesh.new()
        for p in pts:
            hb.verts.new((Vector(p) - off) * 0.01)
        res = bmesh.ops.convex_hull(hb, input=hb.verts)
        bad = list({g for g in res["geom_interior"] + res["geom_unused"] if isinstance(g, bmesh.types.BMVert)})
        if bad:
            bmesh.ops.delete(hb, geom=bad, context="VERTS")
        bmesh.ops.remove_doubles(hb, verts=hb.verts, dist=1e-4)
        bmesh.ops.recalc_face_normals(hb, faces=hb.faces)
        nm = "UCX_%s_%02d" % (P.name, k)
        hm = bpy.data.meshes.new(nm)
        hb.to_mesh(hm)
        hb.free()
        ho = bpy.data.objects.new(nm, hm)
        ho["deco"] = deco
        bpy.context.scene.collection.objects.link(ho)
        hulls.append(ho)
    return ob, hulls, off


def export(ob, hulls, path):
    bpy.ops.object.select_all(action='DESELECT')
    for o in [ob] + hulls:
        o.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.fbx(
        filepath=path, use_selection=True, object_types={'MESH'},
        apply_unit_scale=True, global_scale=1.0, apply_scale_options='FBX_SCALE_UNITS',
        axis_forward='-Z', axis_up='Y', use_space_transform=True, bake_space_transform=False,
        mesh_smooth_type='FACE', use_mesh_modifiers=False, bake_anim=False, path_mode='STRIP')


P_NAME = [""]


def stats(ob, hulls):
    import mathutils
    cos = [v.co * 100 for v in ob.data.vertices]
    mn = [round(min(c[i] for c in cos), 1) for i in range(3)]
    mx = [round(max(c[i] for c in cos), 1) for i in range(3)]
    # steepest upward-facing collision face (walkable/rideable surfaces), excluding near-vertical walls
    steep = 0.0
    for h in hulls:
        if h.get("deco"):
            continue
        for p in h.data.polygons:
            nz = p.normal.z
            if nz > 0.2 and not (P_NAME[0] == "Park_PelvisQuarter" and p.center.z * 100 > 120):  # up-facing (QP top excluded)
                steep = max(steep, math.degrees(math.acos(min(1, nz))))
    tris = sum(len(p.vertices) - 2 for p in ob.data.polygons)
    return {"min_cm": mn, "max_cm": mx, "size_cm": [round(mx[i] - mn[i], 1) for i in range(3)], "tris": tris,
            "hulls": len(hulls), "steepest_ride_face_deg": round(steep, 1)}


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    write_palette()
    report = {}
    for fn in PIECES:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        img = bpy.data.images.load(PAL_PATH)
        mat = make_material(img)
        P = fn()
        ob, hulls, off = build_objects(P, mat)
        export(ob, hulls, OUT + P.name + ".fbx")
        P_NAME[0] = P.name
        st = stats(ob, hulls)
        st["deco_hulls"] = sum(1 for h in hulls if h.get("deco"))
        st["deco_ucx"] = [h.name for h in hulls if h.get("deco")]
        st["ride"] = P.ride
        st["centre_offset_cm"] = [round(off.x, 1), round(off.y, 1)]
        report[P.name] = st
        print("PIECE", P.name, json.dumps(st))
        bpy.ops.wm.save_as_mainfile(filepath="/home/claude/blender_work/park/%s.blend" % P.name)
    json.dump(report, open("/home/claude/blender_work/park/report.json", "w"), indent=1)
