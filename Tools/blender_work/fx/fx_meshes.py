# Soul Quota effect mesh kit (Nov 10 2026): stylised shapes for SQFx particles (ESQFxShape), flat-shaded, ~1 unit across,
# pivot at the centre. Conventions: X = travel/length direction, flat shapes lie in the XY plane (read from the top-down camera),
# upright shapes (Flame, Spiral) point up +Z. SQFx normalises every mesh to 100 cm longest side, so absolute size doesn't matter.
# Output: ArtSource/FX/SM_FX_<Name>.fbx  ->  /Game/Art/FX/SM_FX_<Name> (Scripts/import_art.py FX_MESHES)
import bpy, bmesh, math, os
from mathutils import Vector, Matrix
OUT = "/home/claude/sq/ArtSource/FX/"
os.makedirs(OUT, exist_ok=True)

def new_bm():
    return bmesh.new()

def extrude_poly(bm, pts, thick=0.06, z0=0.0):
    """flat polygon (XY) given as a list of (x, y), extruded by thick around z0"""
    top = [bm.verts.new((x, y, z0 + thick / 2)) for x, y in pts]
    bot = [bm.verts.new((x, y, z0 - thick / 2)) for x, y in pts]
    bm.faces.new(top)
    bm.faces.new(list(reversed(bot)))
    n = len(pts)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new([top[i], bot[i], bot[j], top[j]])

def strip(bm, left, right, thick=0.05):
    """ribbon between two point lists (XY), extruded"""
    pts = left + list(reversed(right))
    extrude_poly(bm, pts, thick)

def ico(bm, c, r, sub=1, sx=1.0, sy=1.0, sz=1.0):
    m = Matrix.Translation(c) @ Matrix.Diagonal((r * sx, r * sy, r * sz, 1))
    bmesh.ops.create_icosphere(bm, subdivisions=sub, radius=1.0, matrix=m)

def cone(bm, a, b, r, seg=6):
    a, b = Vector(a), Vector(b)
    d = b - a
    rot = d.to_track_quat('Z', 'Y').to_matrix().to_4x4()
    m = Matrix.Translation((a + b) / 2) @ rot
    bmesh.ops.create_cone(bm, cap_ends=True, segments=seg, radius1=r, radius2=0.0, depth=d.length, matrix=m)

def torus(bm, R, r, seg=24, rseg=6, z=0.0, sz=1.0):
    vs = []
    for i in range(seg):
        a = 2 * math.pi * i / seg
        ring = []
        for j in range(rseg):
            b = 2 * math.pi * j / rseg
            ring.append(bm.verts.new(((R + r * math.cos(b)) * math.cos(a), (R + r * math.cos(b)) * math.sin(a), z + r * math.sin(b) * sz)))
        vs.append(ring)
    for i in range(seg):
        for j in range(rseg):
            a, b = vs[i][j], vs[(i + 1) % seg][j]
            c, d = vs[(i + 1) % seg][(j + 1) % rseg], vs[i][(j + 1) % rseg]
            bm.faces.new([a, b, c, d])

def arc_pts(R, a0, a1, n):
    return [(R * math.cos(a), R * math.sin(a)) for a in [a0 + (a1 - a0) * k / (n - 1) for k in range(n)]]

S = {}
def shape(name):
    def deco(fn):
        S[name] = fn
        return fn
    return deco

@shape("Crescent")      # a slash: thick in the middle, razor tips, convex toward +X
def _(bm):
    n = 15
    outer = arc_pts(0.5, -1.25, 1.25, n)
    inner = [(0.5 * math.cos(a) * (0.62 + 0.36 * abs(a) / 1.25) - 0.02, 0.5 * math.sin(a) * (0.80 + 0.2 * abs(a) / 1.25)) for a in [-1.25 + 2.5 * k / (n - 1) for k in range(n)]]
    strip(bm, outer, inner, 0.05)

@shape("Ring")          # shockwave band
def _(bm):
    torus(bm, 0.45, 0.05, 32, 4, 0.0, 0.4)

@shape("Flame")         # a licking flame tongue, points up +Z, slight S-curve
def _(bm):
    for k, (r, dx, h) in enumerate(((0.28, 0.0, 1.0), (0.16, 0.12, 0.7), (0.14, -0.13, 0.62))):
        base = Vector((dx, 0.02 * k, -0.45))
        ico(bm, base + Vector((0, 0, r * 0.6)), r, 1, 1.0, 0.8, 0.9)
        cone(bm, base + Vector((0, 0, r * 0.7)), base + Vector((dx * 0.8 + 0.06, 0.0, h - 0.45)), r * 0.95, 7)

@shape("Puff")          # cartoon smoke cloud: lumpy cluster
def _(bm):
    for c, r in (((0, 0, 0), 0.30), ((0.22, 0.08, 0.05), 0.22), ((-0.2, 0.1, 0.02), 0.24), ((0.05, -0.2, 0.08), 0.2), ((0.0, 0.05, 0.22), 0.2)):
        ico(bm, Vector(c), r, 1)

@shape("Spiral")        # rising helix ribbon (whirlwinds, sand, time), a thin solid band
def _(bm):
    n = 40
    prev = None
    for k in range(n):
        t = k / (n - 1)
        a = t * 4 * math.pi
        R = 0.45 - 0.25 * t
        z = -0.5 + t
        w = 0.05 + 0.03 * math.sin(t * math.pi)
        ring = []
        for dr, dz in ((0.015, w), (0.015, -w), (-0.015, -w), (-0.015, w)):
            ring.append(bm.verts.new(((R + dr) * math.cos(a), (R + dr) * math.sin(a), z + dz)))
        if prev:
            for j in range(4):
                bm.faces.new([prev[j], prev[(j + 1) % 4], ring[(j + 1) % 4], ring[j]])
        else:
            bm.faces.new(ring)
        prev = ring
    bm.faces.new(list(reversed(prev)))

@shape("Shard")         # crystal/stone splinter along X
def _(bm):
    cone(bm, (-0.5, 0, 0), (0.5, 0, 0), 0.12, 5)
    cone(bm, (-0.1, 0, 0), (-0.5, 0, 0), 0.12, 5)

@shape("Bolt")          # lightning zigzag along X
def _(bm):
    xs = [(-0.5, 0.0), (-0.22, 0.12), (-0.1, -0.06), (0.15, 0.10), (0.25, -0.04), (0.5, 0.06)]
    for (x0, y0), (x1, y1) in zip(xs, xs[1:]):
        d = Vector((x1 - x0, y1 - y0, 0)).normalized()
        nrm = Vector((-d.y, d.x, 0)) * 0.035
        extrude_poly(bm, [(x0 + nrm.x, y0 + nrm.y), (x1 + nrm.x, y1 + nrm.y), (x1 - nrm.x, y1 - nrm.y), (x0 - nrm.x, y0 - nrm.y)], 0.05)

@shape("Star")          # 4-point sparkle
def _(bm):
    pts = []
    for k in range(8):
        a = math.pi / 4 * k
        r = 0.5 if k % 2 == 0 else 0.12
        pts.append((r * math.cos(a), r * math.sin(a)))
    extrude_poly(bm, pts, 0.08)

@shape("Bat")           # bat silhouette flying toward +X
def _(bm):
    extrude_poly(bm, [(0.18, 0), (0.08, 0.06), (0.02, 0.48), (-0.06, 0.34), (-0.12, 0.4), (-0.16, 0.24), (-0.22, 0.26), (-0.16, 0.06), (-0.2, 0),
                      (-0.16, -0.06), (-0.22, -0.26), (-0.16, -0.24), (-0.12, -0.4), (-0.06, -0.34), (0.02, -0.48), (0.08, -0.06)], 0.05)
    ico(bm, Vector((0.12, 0, 0.02)), 0.07, 1)

@shape("Crow")          # crow silhouette, wings spread, flying +X
def _(bm):
    extrude_poly(bm, [(0.5, 0.0), (0.32, 0.05), (0.12, 0.10), (-0.05, 0.48), (-0.12, 0.42), (-0.1, 0.1), (-0.32, 0.06), (-0.48, 0.12), (-0.42, 0.0),
                      (-0.48, -0.12), (-0.32, -0.06), (-0.1, -0.1), (-0.12, -0.42), (-0.05, -0.48), (0.12, -0.10), (0.32, -0.05)], 0.06)

@shape("Moth")          # moth: big rounded wings
def _(bm):
    for sy in (1, -1):
        pts = [(0.08, 0.02 * sy)] + [(0.05 + 0.32 * math.cos(a) * (0.6 + 0.4 * math.sin(a)), sy * (0.05 + 0.42 * math.sin(a))) for a in [k * math.pi / 10 for k in range(1, 10)]] + [(-0.1, 0.02 * sy)]
        if sy < 0:
            pts = list(reversed(pts))
        extrude_poly(bm, pts, 0.04)
    cone(bm, (-0.3, 0, 0), (0.25, 0, 0), 0.05, 5)

@shape("Leaf")          # leaf along X with a mid crease
def _(bm):
    n = 9
    up = [(-0.5 + k / (n - 1), 0.2 * math.sin(math.pi * k / (n - 1))) for k in range(n)]
    dn = [(x, -y) for x, y in up[1:-1]][::-1]
    extrude_poly(bm, up + dn, 0.04)

@shape("Feather")       # feather / straw
def _(bm):
    extrude_poly(bm, [(0.5, 0), (0.3, 0.09), (-0.2, 0.08), (-0.4, 0.02), (-0.5, 0.0), (-0.4, -0.02), (-0.2, -0.08), (0.3, -0.09)], 0.03)

@shape("Rune")          # clock-rune circle: ring + 12 ticks + hands
def _(bm):
    torus(bm, 0.44, 0.03, 32, 4, 0.0, 0.5)
    torus(bm, 0.32, 0.015, 32, 4, 0.0, 0.5)
    for k in range(12):
        a = math.pi / 6 * k
        r0, r1 = (0.34, 0.42) if k % 3 else (0.30, 0.44)
        c, s = math.cos(a), math.sin(a)
        nx, ny = -s * 0.018, c * 0.018
        extrude_poly(bm, [(r0 * c + nx, r0 * s + ny), (r1 * c + nx, r1 * s + ny), (r1 * c - nx, r1 * s - ny), (r0 * c - nx, r0 * s - ny)], 0.03)
    extrude_poly(bm, [(-0.02, 0.0), (0.0, 0.25), (0.02, 0.0), (0.0, -0.03)], 0.03)
    extrude_poly(bm, [(0.0, -0.02), (0.18, 0.0), (0.0, 0.02), (-0.03, 0.0)], 0.03)

@shape("Seal")          # square stamp seal with a cross-hatch (PAID stamp)
def _(bm):
    for (x0, y0, x1, y1) in ((-0.5, -0.5, 0.5, -0.4), (-0.5, 0.4, 0.5, 0.5), (-0.5, -0.4, -0.4, 0.4), (0.4, -0.4, 0.5, 0.4)):
        extrude_poly(bm, [(x0, y0), (x1, y0), (x1, y1), (x0, y1)], 0.04)
    for k in range(3):
        y = -0.2 + 0.2 * k
        extrude_poly(bm, [(-0.3, y - 0.04), (0.3, y - 0.04), (0.3, y + 0.04), (-0.3, y + 0.04)], 0.04)

@shape("Cleaver")       # cleaver silhouette, blade toward +X
def _(bm):
    extrude_poly(bm, [(-0.5, -0.05), (-0.12, -0.05), (-0.12, -0.25), (0.5, -0.25), (0.5, 0.25), (-0.12, 0.25), (-0.12, 0.05), (-0.5, 0.05)], 0.04)

@shape("Claw")          # three curved rake slashes along X
def _(bm):
    for k in (-1, 0, 1):
        n = 9
        left, right = [], []
        for i in range(n):
            t = i / (n - 1)
            x = -0.5 + t
            y = k * 0.2 + 0.08 * math.sin(t * math.pi) * (1 if k >= 0 else -1)
            w = 0.045 * math.sin(t * math.pi) + 0.004
            left.append((x, y + w)); right.append((x, y - w))
        strip(bm, left, right, 0.04)

@shape("Bandage")       # wavy ribbon along X
def _(bm):
    n = 17
    left = [(-0.5 + k / (n - 1), 0.12 * math.sin(k / (n - 1) * 3 * math.pi) + 0.05) for k in range(n)]
    right = [(x, y - 0.1) for x, y in left]
    strip(bm, left, right, 0.03)

@shape("Seed")          # teardrop seed / flame bolt along X
def _(bm):
    ico(bm, Vector((0.18, 0, 0)), 0.3, 2, 1.1, 1.0, 1.0)
    cone(bm, (0.1, 0, 0), (-0.5, 0, 0), 0.25, 8)

@shape("Drop")          # liquid droplet / potion splash blob, point up +Z
def _(bm):
    ico(bm, Vector((0, 0, -0.15)), 0.33, 2)
    cone(bm, (0, 0, 0.0), (0, 0, 0.5), 0.26, 10)


# ---------------------------------------------------------------- pickups (Nov 10 2026): XP crystal ladder + gold ladder
def prism(bm, base, h, r, sides=6, tip=0.35, tilt=(0, 0), twist=0.0):
    """faceted crystal: a hex prism with a pointed top and a short point below"""
    from mathutils import Euler
    rot = Euler((math.radians(tilt[0]), math.radians(tilt[1]), twist)).to_matrix()
    base = Vector(base)
    ring_lo, ring_hi = [], []
    for k in range(sides):
        a = 2 * math.pi * k / sides
        ring_lo.append(bm.verts.new(base + rot @ Vector((r * math.cos(a), r * math.sin(a), 0.0))))
        ring_hi.append(bm.verts.new(base + rot @ Vector((r * 0.92 * math.cos(a), r * 0.92 * math.sin(a), h))))
    top = bm.verts.new(base + rot @ Vector((0, 0, h + r * tip * 3)))
    bot = bm.verts.new(base + rot @ Vector((0, 0, -r * 1.0)))
    for k in range(sides):
        j = (k + 1) % sides
        bm.faces.new([ring_lo[k], ring_lo[j], ring_hi[j], ring_hi[k]])
        bm.faces.new([ring_hi[k], ring_hi[j], top])
        bm.faces.new([ring_lo[j], ring_lo[k], bot])

@shape("Crystal1")      # tier 1: one small shard
def _(bm):
    prism(bm, (0, 0, -0.3), 0.45, 0.16, 5, 0.6)

@shape("Crystal2")      # tier 2: a pair
def _(bm):
    prism(bm, (0.05, 0, -0.35), 0.55, 0.17, 6, 0.55, (8, -6))
    prism(bm, (-0.14, 0.05, -0.38), 0.32, 0.11, 5, 0.6, (-18, 22))

@shape("Crystal3")      # tier 3: a cluster
def _(bm):
    prism(bm, (0, 0, -0.38), 0.62, 0.17, 6, 0.55)
    for k, (dx, dy, h, r, tx, ty) in enumerate(((0.17, 0.02, 0.36, 0.1, 0, 28), (-0.15, 0.09, 0.42, 0.11, -22, -18), (0.0, -0.17, 0.3, 0.09, 26, 0), (-0.06, 0.18, 0.26, 0.08, -26, 6))):
        prism(bm, (dx, dy, -0.4), h, r, 5, 0.6, (tx, ty), k * 0.7)

@shape("Crystal4")      # tier 4 (white): a big double-ended prism with a halo ring
def _(bm):
    prism(bm, (0, 0, -0.25), 0.5, 0.24, 8, 0.45)
    torus(bm, 0.42, 0.025, 28, 4, 0.0, 0.5)

@shape("Coin")          # one coin, standing on edge (spins about Z), face toward +X
def _(bm):
    m = Matrix.Rotation(math.radians(90), 4, 'Y')
    bmesh.ops.create_cone(bm, cap_ends=True, segments=20, radius1=0.5, radius2=0.5, depth=0.09, matrix=m)
    bmesh.ops.create_cone(bm, cap_ends=True, segments=20, radius1=0.33, radius2=0.33, depth=0.12, matrix=m)

@shape("CoinStack")     # a bundle: a stack of coins with a couple leaning
def _(bm):
    for k in range(5):
        m = Matrix.Translation((0.03 * math.sin(k * 1.7), 0.03 * math.cos(k * 1.3), -0.3 + 0.1 * k))
        bmesh.ops.create_cone(bm, cap_ends=True, segments=16, radius1=0.3, radius2=0.3, depth=0.08, matrix=m)
    for dx in (0.36, -0.34):
        m = Matrix.Translation((dx, 0.08, -0.12)) @ Matrix.Rotation(math.radians(70 if dx > 0 else -70), 4, 'Y')
        bmesh.ops.create_cone(bm, cap_ends=True, segments=16, radius1=0.24, radius2=0.24, depth=0.07, matrix=m)

@shape("Purse")         # a drawstring coin purse
def _(bm):
    ico(bm, Vector((0, 0, -0.12)), 0.36, 2, 1.0, 1.0, 0.85)
    m = Matrix.Translation((0, 0, 0.2))
    bmesh.ops.create_cone(bm, cap_ends=True, segments=10, radius1=0.17, radius2=0.08, depth=0.16, matrix=m)
    for k in range(5):
        a = 2 * math.pi * k / 5
        cone(bm, (0.08 * math.cos(a), 0.08 * math.sin(a), 0.27), (0.17 * math.cos(a), 0.17 * math.sin(a), 0.42), 0.05, 4)
    torus(bm, 0.15, 0.025, 14, 4, 0.18, 1.0)

@shape("GoldBar")       # an ingot (trapezoid bar)
def _(bm):
    lo = [(-0.5, -0.24), (0.5, -0.24), (0.5, 0.24), (-0.5, 0.24)]
    hi = [(-0.38, -0.15), (0.38, -0.15), (0.38, 0.15), (-0.38, 0.15)]
    vl = [bm.verts.new((x, y, -0.13)) for x, y in lo]
    vh = [bm.verts.new((x, y, 0.13)) for x, y in hi]
    bm.faces.new(list(reversed(vl)))
    bm.faces.new(vh)
    for k in range(4):
        j = (k + 1) % 4
        bm.faces.new([vl[k], vl[j], vh[j], vh[k]])

# ---------------------------------------------------------------- Nov 19 2026: motion shapes (effects remaster)
@shape("Swoosh")        # swing trail: 120 degree tapered arc, PIVOT AT THE ARC CENTRE (spin it about Z to sweep), head at +60 deg (counter-clockwise)
def _(bm):
    n = 25
    outer, inner = [], []
    for k in range(n):
        t = k / (n - 1)                      # 0 tail .. 1 head
        a = math.radians(-60 + 120 * t)
        w = 0.15 * (t ** 1.5) * (1.0 if t < 0.86 else max(0.0, (1 - t) / 0.14) ** 0.7) + 0.004
        outer.append((0.5 * math.cos(a), 0.5 * math.sin(a)))
        inner.append(((0.5 - w) * math.cos(a), (0.5 - w) * math.sin(a)))
    strip(bm, outer, inner, 0.035)

@shape("Arc")           # hot edge / wind line: thin tapered 120 degree arc, pivot at the arc centre like Swoosh
def _(bm):
    n = 25
    outer, inner = [], []
    for k in range(n):
        t = k / (n - 1)
        a = math.radians(-60 + 120 * t)
        w = 0.028 * math.sin(math.pi * min(1.0, t * 1.15)) ** 0.8 * (0.35 + 0.65 * t) + 0.002
        outer.append((0.5 * math.cos(a), 0.5 * math.sin(a)))
        inner.append(((0.5 - w) * math.cos(a), (0.5 - w) * math.sin(a)))
    strip(bm, outer, inner, 0.02)

@shape("Teeth")         # jagged shock ring: a thin band with teeth pointing outward (impacts with bite instead of a plain circle)
def _(bm):
    n = 14
    outer, inner = [], []
    for k in range(n * 4):
        a = 2 * math.pi * k / (n * 4)
        ph = k % 4
        ro = (0.385, 0.42, 0.5, 0.42)[ph]
        outer.append((ro * math.cos(a), ro * math.sin(a)))
        inner.append((0.36 * math.cos(a), 0.36 * math.sin(a)))
    top_o = [bm.verts.new((x, y, 0.01)) for x, y in outer]; top_i = [bm.verts.new((x, y, 0.01)) for x, y in inner]
    bot_o = [bm.verts.new((x, y, -0.01)) for x, y in outer]; bot_i = [bm.verts.new((x, y, -0.01)) for x, y in inner]
    m = n * 4
    for k in range(m):
        j = (k + 1) % m
        bm.faces.new([top_o[k], top_o[j], top_i[j], top_i[k]]); bm.faces.new([bot_o[j], bot_o[k], bot_i[k], bot_i[j]])
        bm.faces.new([top_o[j], top_o[k], bot_o[k], bot_o[j]]); bm.faces.new([top_i[k], top_i[j], bot_i[j], bot_i[k]])

def _band(bm, r0, r1, seg=40, z=0.008, n=None):
    n = n or seg
    for k in range(seg):
        a0, a1 = 2 * math.pi * k / n, 2 * math.pi * (k + 1) / n
        extrude_poly(bm, [(r0 * math.cos(a0), r0 * math.sin(a0)), (r1 * math.cos(a0), r1 * math.sin(a0)), (r1 * math.cos(a1), r1 * math.sin(a1)), (r0 * math.cos(a1), r0 * math.sin(a1))], z * 2)
def _bar(bm, p, q, w=0.012):
    px, py = p; qx, qy = q; dx, dy = qx - px, qy - py; ln = math.hypot(dx, dy); nx, ny = -dy / ln * w, dx / ln * w
    extrude_poly(bm, [(px + nx, py + ny), (qx + nx, qy + ny), (qx - nx, qy - ny), (px - nx, py - ny)], 0.016)
def _pol(r, deg): return (r * math.cos(math.radians(deg)), r * math.sin(math.radians(deg)))

@shape("Cog")           # clock-gear outline: toothed rim, hub, three spokes and a pair of hands (Tickwell / machine marks)
def _(bm):
    _band(bm, 0.40, 0.425); _band(bm, 0.10, 0.125, 20)
    for k in range(12):
        a = 30 * k; _bar(bm, _pol(0.425, a - 5), _pol(0.5, a - 3.5), 0.009); _bar(bm, _pol(0.5, a - 3.5), _pol(0.5, a + 3.5), 0.009); _bar(bm, _pol(0.5, a + 3.5), _pol(0.425, a + 5), 0.009)
    for k in range(3): _bar(bm, _pol(0.125, 120 * k + 60), _pol(0.40, 120 * k + 60), 0.014)
    _bar(bm, (0, 0), _pol(0.33, 90), 0.01); _bar(bm, (0, 0), _pol(0.22, 10), 0.014)

@shape("Web")           # spider web: eight spokes and three sagging rings of thread (Widow)
def _(bm):
    for k in range(8): _bar(bm, _pol(0.04, 45 * k), _pol(0.5, 45 * k), 0.007)
    for rr in (0.17, 0.31, 0.45):
        for k in range(8):
            a, b, mid = _pol(rr, 45 * k), _pol(rr, 45 * k + 45), _pol(rr * 0.86, 45 * k + 22.5)
            _bar(bm, a, mid, 0.006); _bar(bm, mid, b, 0.006)

@shape("Casket")        # long six-sided outline, doubled, with a lid line (Pallbearers / grave marks)
def _(bm):
    for sc in (1.0, 0.8):
        pts = [(0.5 * sc, 0.0), (0.22 * sc, 0.27 * sc), (-0.5 * sc, 0.17 * sc), (-0.5 * sc, -0.17 * sc), (0.22 * sc, -0.27 * sc)]
        pts = [(0.5 * sc, 0.12 * sc), (0.22 * sc, 0.27 * sc), (-0.5 * sc, 0.17 * sc), (-0.5 * sc, -0.17 * sc), (0.22 * sc, -0.27 * sc), (0.5 * sc, -0.12 * sc)]
        for k in range(6): _bar(bm, pts[k], pts[(k + 1) % 6], 0.012 if sc == 1.0 else 0.007)
    _bar(bm, (-0.3, 0), (0.3, 0), 0.006); _bar(bm, (0.12, -0.1), (0.12, 0.1), 0.006)

@shape("Sigil")         # magic circle drawn in thin lines: two rings, a six-point star and tick marks (ground mark for casters)
def _(bm):
    def band(r0, r1, seg=40, z=0.008):
        for k in range(seg):
            a0, a1 = 2 * math.pi * k / seg, 2 * math.pi * (k + 1) / seg
            q = [(r0 * math.cos(a0), r0 * math.sin(a0)), (r1 * math.cos(a0), r1 * math.sin(a0)), (r1 * math.cos(a1), r1 * math.sin(a1)), (r0 * math.cos(a1), r0 * math.sin(a1))]
            extrude_poly(bm, q, z * 2)
    def bar(p, q, w=0.012):
        px, py = p; qx, qy = q; dx, dy = qx - px, qy - py; ln = math.hypot(dx, dy); nx, ny = -dy / ln * w, dx / ln * w
        extrude_poly(bm, [(px + nx, py + ny), (qx + nx, qy + ny), (qx - nx, qy - ny), (px - nx, py - ny)], 0.016)
    band(0.47, 0.5); band(0.385, 0.40); band(0.16, 0.172, 24)
    pts = [(0.385 * math.cos(math.radians(90 + 60 * k)), 0.385 * math.sin(math.radians(90 + 60 * k))) for k in range(6)]
    for k in range(6): bar(pts[k], pts[(k + 2) % 6])
    for k in range(12):
        a = math.radians(30 * k + 15); bar((0.41 * math.cos(a), 0.41 * math.sin(a)), (0.46 * math.cos(a), 0.46 * math.sin(a)), 0.008)

@shape("Streak")        # speed line / spark: needle along X, fattest a third back from the head (+X)
def _(bm):
    tail, head = bm.verts.new((-0.5, 0, 0)), bm.verts.new((0.5, 0, 0))
    ring = [bm.verts.new((0.18, 0.045 * math.cos(a), 0.045 * math.sin(a))) for a in [k * math.pi / 2 for k in range(4)]]
    for j in range(4):
        bm.faces.new([head, ring[j], ring[(j + 1) % 4]])
        bm.faces.new([tail, ring[(j + 1) % 4], ring[j]])

@shape("Wave")          # shockwave: flat band with a crisp outer edge that thins to nothing inward
def _(bm):
    seg = 48
    o_t, o_b, i_m = [], [], []
    for k in range(seg):
        a = 2 * math.pi * k / seg
        c, s_ = math.cos(a), math.sin(a)
        o_t.append(bm.verts.new((0.5 * c, 0.5 * s_, 0.014)))
        o_b.append(bm.verts.new((0.5 * c, 0.5 * s_, -0.006)))
        i_m.append(bm.verts.new((0.455 * c, 0.455 * s_, 0.0)))
    for k in range(seg):
        j = (k + 1) % seg
        bm.faces.new([o_t[k], o_t[j], i_m[j], i_m[k]])
        bm.faces.new([o_b[j], o_b[k], i_m[k], i_m[j]])
        bm.faces.new([o_t[j], o_t[k], o_b[k], o_b[j]])

@shape("Spikes")        # impact starburst: long and short spikes radiating in XY
def _(bm):
    pts = []
    n = 16
    for k in range(n):
        a = 2 * math.pi * k / n
        r = (0.5, 0.11, 0.30, 0.11)[k % 4]
        pts.append((r * math.cos(a), r * math.sin(a)))
    extrude_poly(bm, pts, 0.03)

@shape("Ember")         # diamond mote
def _(bm):
    top, bot = bm.verts.new((0, 0, 0.5)), bm.verts.new((0, 0, -0.5))
    ring = [bm.verts.new((0.26 * math.cos(a), 0.26 * math.sin(a), 0.08)) for a in [k * math.pi / 2 for k in range(4)]]
    for j in range(4):
        bm.faces.new([top, ring[j], ring[(j + 1) % 4]])
        bm.faces.new([bot, ring[(j + 1) % 4], ring[j]])

@shape("Petal")         # energy blade / teardrop: point leads (+X), round back
def _(bm):
    pts = [(0.5, 0.0)]
    for k in range(1, 12):
        t = k / 12
        pts.append((0.5 - t * 1.0, 0.21 * math.sin(t * math.pi) ** 0.75))
    pts.append((-0.5, 0.0))
    for k in range(11, 0, -1):
        t = k / 12
        pts.append((0.5 - t * 1.0, -0.21 * math.sin(t * math.pi) ** 0.75))
    extrude_poly(bm, pts, 0.04)

@shape("Chevron")       # arrowhead / shock chevron pointing +X
def _(bm):
    pts = [(0.5, 0.0), (-0.15, 0.42), (-0.5, 0.42), (0.05, 0.0), (-0.5, -0.42), (-0.15, -0.42)]
    extrude_poly(bm, pts, 0.04)

@shape("Wisp")          # little ghost flame: round head, tail curling up (+Z)
def _(bm):
    ico(bm, Vector((0, 0, -0.22)), 0.26, 2, 1.0, 0.9, 1.0)
    cone(bm, Vector((0.0, 0, -0.08)), Vector((0.10, 0, 0.30)), 0.20, 8)
    cone(bm, Vector((0.08, 0, 0.18)), Vector((-0.06, 0, 0.5)), 0.10, 6)


def build(name):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bm = new_bm()
    S[name](bm)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new("SM_FX_" + name)
    bm.to_mesh(me)
    bm.free()
    for p in me.polygons:
        p.use_smooth = False
    ob = bpy.data.objects.new("SM_FX_" + name, me)
    bpy.context.scene.collection.objects.link(ob)
    mat = bpy.data.materials.new("M_FX")
    me.materials.append(mat)
    bpy.context.view_layer.objects.active = ob
    ob.select_set(True)
    bpy.ops.export_scene.fbx(filepath=OUT + "SM_FX_" + name + ".fbx", use_selection=True, object_types={'MESH'}, apply_unit_scale=True,
                             axis_forward='-Z', axis_up='Y', mesh_smooth_type='FACE', add_leaf_bones=False, bake_anim=False)
    dims = ob.dimensions
    print("FX", name, "tris", sum(len(p.vertices) - 2 for p in me.polygons), "dims %.2f %.2f %.2f" % tuple(dims))

import sys
ONLY = sys.argv[1:]
for n in S:
    if ONLY and n not in ONLY:
        continue
    build(n)
print("FXDONE", len(S))
