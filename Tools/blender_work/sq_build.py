# Soul Quota - "Reaper, Junior Grade" character build.
# Imports KayKit Skeleton_Mage rig (CC0) for its armature + actions, removes its meshes,
# builds an original low-poly reaper mesh with rigid / height-blended skinning.
import bpy, bmesh, math, sys
from mathutils import Vector, Matrix

SRC = "/mnt/user-data/outputs/ArtSource/Characters/Skeleton_Mage.fbx"
OUTDIR = "/home/claude/sq/ArtSource/Characters/"

# ---------------------------------------------------------------- palette
PAL = [
    ("robe",       (0.80, 0.80, 0.83), 1),
    ("robe_dark",  (0.52, 0.52, 0.58), 1),
    ("robe_inner", (0.13, 0.11, 0.17), 1),
    ("hood_trim",  (0.97, 0.97, 0.99), 1),
    ("bone",       (0.97, 0.94, 0.84), 0),
    ("bone_shade", (0.80, 0.76, 0.66), 0),
    ("eye",        (0.20, 1.00, 0.86), 0),
    ("eye_hi",     (1.00, 1.00, 1.00), 0),
    ("socket",     (0.04, 0.05, 0.07), 0),
    ("rope",       (0.80, 0.60, 0.36), 0),
    ("boot",       (0.34, 0.22, 0.16), 0),
    ("boot_cuff",  (0.50, 0.35, 0.24), 0),
    ("sole",       (0.12, 0.10, 0.10), 0),
    ("card",       (0.98, 0.95, 0.85), 0),
    ("card_red",   (0.86, 0.15, 0.15), 0),
    ("metal",      (0.26, 0.26, 0.31), 0),
    ("glow",       (0.45, 1.00, 0.90), 0),
    ("wood",       (0.50, 0.31, 0.18), 0),
    ("steel",      (0.70, 0.75, 0.82), 0),
    ("edge",       (0.96, 0.98, 1.00), 0),
    ("gold",       (0.96, 0.72, 0.24), 0),
    ("ink",        (0.08, 0.08, 0.10), 0),
    ("wood_dark",  (0.32, 0.19, 0.11), 0),
    ("pumpkin",    (0.98, 0.52, 0.12), 0),
    ("leaf",       (0.30, 0.55, 0.22), 0),
    ("hat",        (0.15, 0.14, 0.18), 0),
]
SW = {n: i for i, (n, c, t) in enumerate(PAL)}
GLOW = {"eye", "eye_hi", "glow"}
GRID, CELL = 8, 16

def swatch_uv(i):
    return ((i % GRID + 0.5) / GRID, (i // GRID + 0.5) / GRID)

def make_palette():
    W = GRID * CELL
    imgs = []
    for nm, mask in (("reaper_palette", False), ("reaper_palette_tintmask", True)):
        img = bpy.data.images.new(nm, W, W, alpha=False)
        px = [0.0] * (W * W * 4)
        for i in range(GRID * GRID):
            if i < len(PAL):
                n, c, tint = PAL[i]
            else:
                c, tint = (0.5, 0.5, 0.5), 0
            col = (float(tint),) * 3 if mask else c
            cx, cy = i % GRID, i // GRID
            for y in range(cy * CELL, cy * CELL + CELL):
                for x in range(cx * CELL, cx * CELL + CELL):
                    k = (y * W + x) * 4
                    px[k:k + 4] = [col[0], col[1], col[2], 1.0]
        img.pixels = px
        img.filepath_raw = OUTDIR + nm + ".png"
        img.file_format = 'PNG'
        img.save()
        imgs.append(img)
    return imgs[0]

# ---------------------------------------------------------------- part builder
class Part:
    def __init__(self, name, weights, mirror_names=None):
        self.name = name
        self.bm = bmesh.new()
        self.sw = self.bm.faces.layers.int.new("sw")
        self.weights = weights  # fn(co)->dict

    def v(self, co):
        return self.bm.verts.new(Vector(co))

    def face(self, verts, sw):
        f = self.bm.faces.new(verts)
        f[self.sw] = SW[sw]
        return f

    def tag_new(self, faces, sw):
        for f in faces:
            f[self.sw] = SW[sw]

parts = []

def ring(center, u, v, ru, rv, n, phase=0.0):
    c, u, v = Vector(center), Vector(u), Vector(v)
    return [c + u * (ru * math.cos(phase + 2 * math.pi * k / n)) + v * (rv * math.sin(phase + 2 * math.pi * k / n)) for k in range(n)]

def loft(p, rings, sw, closed=True, cap0=True, cap1=True, sw_cap=None):
    """rings: list of lists of Vector (same count). returns vert grid + faces"""
    grid = [[p.v(co) for co in r] for r in rings]
    faces = []
    n = len(rings[0])
    m = n if closed else n - 1
    for i in range(len(grid) - 1):
        for j in range(m):
            a, b = grid[i][j], grid[i][(j + 1) % n]
            c, d = grid[i + 1][(j + 1) % n], grid[i + 1][j]
            faces.append(p.face([a, b, c, d], sw))
    if closed and cap0:
        faces.append(p.face(grid[0][::-1], sw_cap or sw))
    if closed and cap1:
        faces.append(p.face(grid[-1], sw_cap or sw))
    return grid, faces

def shell(p, faces, inward, inner_sw, rim_sw):
    """Give an open surface thickness: inward(Vector)->Vector gives inner position."""
    fset = set(faces)
    vmap = {}
    for f in faces:
        for v in f.verts:
            if v not in vmap:
                vmap[v] = p.v(inward(v.co))
    new = []
    for f in faces:
        nf = p.face([vmap[v] for v in reversed(f.verts)], inner_sw)
        new.append(nf)
    for f in faces:
        for l in f.loops:
            e = l.edge
            lf = [x for x in e.link_faces if x in fset]
            if len(lf) == 1:
                a, b = l.vert, l.link_loop_next.vert
                new.append(p.face([b, a, vmap[a], vmap[b]], rim_sw))
    return new

def box(p, center, size, sw, bevel=0.0, rot=None, segs=1):
    mat = Matrix.Translation(Vector(center))
    if rot is not None:
        mat = mat @ rot.to_4x4()
    mat = mat @ Matrix.Diagonal((size[0], size[1], size[2], 1.0))
    r = bmesh.ops.create_cube(p.bm, size=1.0, matrix=mat)
    verts = r["verts"]
    faces = list({f for v in verts for f in v.link_faces})
    if bevel > 0:
        edges = list({e for v in verts for e in v.link_edges})
        br = bmesh.ops.bevel(p.bm, geom=edges + verts, offset=bevel, segments=segs, affect='EDGES', profile=0.5, clamp_overlap=True)
        faces = [f for f in faces if f.is_valid] + list(br["faces"])
    for f in faces:
        f[p.sw] = SW[sw]
    return list({v for f in faces for v in f.verts})

def finish_tags(p, verts, sw):
    for v in verts:
        if v.is_valid:
            for f in v.link_faces:
                f[p.sw] = SW[sw]

def boxs(p, center, size, sw, bevel=0.0, rot=None, segs=1):
    vs = box(p, center, size, sw, bevel, rot, segs)
    # bevel creates new verts - tag everything newly connected
    return vs

def ellipsoid(p, center, radii, useg, vseg, sw):
    mat = Matrix.Translation(Vector(center)) @ Matrix.Diagonal((radii[0], radii[1], radii[2], 1.0))
    r = bmesh.ops.create_uvsphere(p.bm, u_segments=useg, v_segments=vseg, radius=1.0, matrix=mat)
    for f in {f for v in r["verts"] for f in v.link_faces}:
        f[p.sw] = SW[sw]
    return r["verts"]

def cyl(p, a, b, r1, r2, seg, sw, caps=True):
    a, b = Vector(a), Vector(b)
    d = b - a
    rot = Vector((0, 0, 1)).rotation_difference(d.normalized()).to_matrix().to_4x4()
    mat = Matrix.Translation((a + b) / 2) @ rot
    r = bmesh.ops.create_cone(p.bm, cap_ends=caps, cap_tris=False, segments=seg, radius1=r1, radius2=r2, depth=d.length, matrix=mat)
    for f in {f for v in r["verts"] for f in v.link_faces}:
        f[p.sw] = SW[sw]
    return r["verts"]

def disc(p, center, rx, rz, n, sw, normal_y=-1, phase=0.0):
    """flat polygon in the XZ plane, facing -Y"""
    c = Vector(center)
    pts = [c + Vector((rx * math.cos(phase + 2 * math.pi * k / n), 0, rz * math.sin(phase + 2 * math.pi * k / n))) for k in range(n)]
    vs = [p.v(x) for x in pts]
    f = p.face(vs, sw)
    f.normal_update()
    if f.normal.y * normal_y < 0:
        f.normal_flip()
    return f

def quad_xz(p, pts, sw):
    vs = [p.v(x) for x in pts]
    f = p.face(vs, sw)
    f.normal_update()
    if f.normal.y > 0:
        f.normal_flip()
    return f

def zblend(keys):
    keys = sorted(keys, key=lambda k: k[0])
    def fn(co, keys=keys):
        z = co.z
        if z <= keys[0][0]:
            return dict(keys[0][1])
        if z >= keys[-1][0]:
            return dict(keys[-1][1])
        for (z0, w0), (z1, w1) in zip(keys, keys[1:]):
            if z0 <= z <= z1:
                t = (z - z0) / (z1 - z0)
                out = {}
                for k in set(w0) | set(w1):
                    out[k] = w0.get(k, 0) * (1 - t) + w1.get(k, 0) * t
                return out
    return fn

def xblend(keys, s):
    keys = sorted(keys, key=lambda k: k[0])
    def fn(co, keys=keys):
        x = co.x * s
        if x <= keys[0][0]:
            return dict(keys[0][1])
        if x >= keys[-1][0]:
            return dict(keys[-1][1])
        for (x0, w0), (x1, w1) in zip(keys, keys[1:]):
            if x0 <= x <= x1:
                t = (x - x0) / (x1 - x0)
                out = {}
                for k in set(w0) | set(w1):
                    out[k] = w0.get(k, 0) * (1 - t) + w1.get(k, 0) * t
                return out
    return fn

def rigid(bone):
    return lambda co: {bone: 1.0}

def tag_all(p, sw):
    for f in p.bm.faces:
        f[p.sw] = SW[sw]

# ---------------------------------------------------------------- body parts
TORSO_W = zblend([(0.56, {"hips": 1}), (0.68, {"hips": .5, "spine": .5}), (0.86, {"spine": 1}),
                  (1.04, {"chest": .7, "spine": .3}), (1.14, {"chest": 1})])

def build_torso():
    p = Part("torso", TORSO_W)
    n = 12
    spec = [(0.55, .26, .23), (0.70, .285, .25), (0.86, .295, .255), (1.02, .285, .235), (1.16, .22, .19), (1.27, .12, .11)]
    rings = [ring((0, 0, z), (1, 0, 0), (0, -1, 0), rx, ry, n, math.pi / n) for z, rx, ry in spec]
    grid, faces = loft(p, rings, "robe")
    parts.append(p)
    # robe front overlap seam + two bone toggles (character's right panel over the left)
    q = Part("seam", TORSO_W)
    for z0, z1 in ((0.66, 0.80), (0.80, 1.0)):
        zm = (z0 + z1) / 2
        q2 = None
    pts = [(0.66, -0.262), (0.82, -0.272), (1.02, -0.252)]
    for (za, ya), (zb, yb) in zip(pts, pts[1:]):
        c = Vector((-0.035, (ya + yb) / 2 - 0.004, (za + zb) / 2))
        ang = math.atan2(yb - ya, zb - za)
        box(q, c, (.022, .012, (zb - za) + 0.01), "robe_dark", 0.0, Matrix.Rotation(-ang, 3, 'X'))
    for z, y in ((0.74, -0.272), (0.93, -0.268)):
        box(q, (-0.035, y - 0.01, z), (.05, .02, .022), "bone", 0.006)
    parts.append(q)

def build_capelet():
    p = Part("capelet", zblend([(1.0, {"chest": 1}), (1.3, {"chest": 1})]))
    n = 16
    hem_drop = [0.0, 0.06, 0.0, 0.08, 0.01, 0.05, 0.0, 0.07, 0.0, 0.06, 0.01, 0.08, 0.0, 0.05, 0.0, 0.07]
    r0 = ring((0, 0.0, 1.31), (1, 0, 0), (0, -1, 0), .17, .16, n)
    r1 = ring((0, 0.0, 1.21), (1, 0, 0), (0, -1, 0), .32, .27, n)
    r2 = ring((0, 0.01, 1.09), (1, 0, 0), (0, -1, 0), .37, .31, n)
    r2 = [co - Vector((0, 0, hem_drop[k])) for k, co in enumerate(r2)]
    grid, faces = loft(p, [r0, r1, r2], "robe", closed=True, cap0=False, cap1=False)
    shell(p, faces, lambda co: Vector((co.x * .9, co.y * .9, co.z - 0.015)), "robe_inner", "robe_dark")
    parts.append(p)

def build_skirt():
    hem_l = [0.0, 0.07, 0.015, 0.05, 0.0, 0.09, 0.02, 0.06, 0.0]
    hem_r = [0.0, 0.05, 0.0, 0.08, 0.02, 0.04, 0.0, 0.07, 0.01]
    for side, s, hem in (("l", 1, hem_l), ("r", -1, hem_r)):
        w = zblend([(0.56, {"hips": 1}), (0.48, {"hips": .45, "upperleg." + side: .55}), (0.40, {"upperleg." + side: 1})])
        p = Part("skirt_" + side, w)
        cols = 8
        spec = [(0.585, .275, .245, 86), (0.48, .30, .27, 78), (0.44, .308, .278, 72), (0.40, .315, .285, 66)]
        rings = []
        for ri, (z, rx, ry, amax) in enumerate(spec):
            rr = []
            for j in range(cols + 1):
                a = math.radians(-amax + 2 * amax * j / cols)
                x, y = rx * math.cos(a), ry * math.sin(a)
                zz = z - (hem[j] if ri == 3 else hem[j] * 0.4 if ri == 2 else 0)
                rr.append(Vector((x * s, y, zz)))
            rings.append(rr)
        grid, faces = loft(p, rings, "robe", closed=False)
        p.tag_new(faces[-cols:], "robe_dark")
        shell(p, faces, lambda co: Vector((co.x * .86, co.y * .86, co.z)), "robe_inner", "robe_dark")
        parts.append(p)
    # dark under-robe core so the slits never see through
    p = Part("underskirt", zblend([(0.5, {"hips": 1})]))
    rings = [ring((0, 0, z), (1, 0, 0), (0, -1, 0), rx, ry, 10) for z, rx, ry in [(0.45, .17, .15), (0.6, .24, .21)]]
    loft(p, rings, "robe_inner")
    parts.append(p)

def build_belt():
    p = Part("belt", rigid("hips"))
    n, m = 18, 5
    zc, rx, ry, tr = 0.615, .29, .26, .032
    grid = []
    for i in range(n):
        a = 2 * math.pi * i / n
        c = Vector((rx * math.cos(a), ry * math.sin(a), zc))
        out = Vector((math.cos(a) / rx, math.sin(a) / ry, 0)).normalized()
        grid.append([p.v(c + out * (tr * math.cos(2 * math.pi * j / m)) + Vector((0, 0, tr * math.sin(2 * math.pi * j / m)))) for j in range(m)])
    for i in range(n):
        for j in range(m):
            p.face([grid[i][j], grid[(i + 1) % n][j], grid[(i + 1) % n][(j + 1) % m], grid[i][(j + 1) % m]], "rope")
    # knot front-left and two dangling ends
    vs = ellipsoid(p, (0.13, -0.255, 0.615), (.05, .04, .045), 6, 4, "rope")
    vs += cyl(p, (0.12, -0.27, 0.6), (0.10, -0.285, 0.47), .02, .018, 5, "rope")
    vs += cyl(p, (0.15, -0.265, 0.6), (0.19, -0.27, 0.49), .02, .018, 5, "rope")
    finish_tags(p, vs, "rope")
    vs = ellipsoid(p, (0.10, -0.285, 0.465), (.026, .026, .03), 5, 3, "rope")
    vs += ellipsoid(p, (0.19, -0.27, 0.485), (.026, .026, .03), 5, 3, "rope")
    finish_tags(p, vs, "rope")
    parts.append(p)

def build_lantern():
    p = Part("lantern", rigid("hips"))
    cx, cy = 0.415, 0.03
    vs = cyl(p, (0.30, cy, 0.61), (cx, cy, 0.54), .012, .012, 4, "metal")
    finish_tags(p, vs, "metal")
    vs = cyl(p, (cx, cy, 0.495), (cx, cy, 0.545), .068, .022, 6, "metal")  # roof
    vs += cyl(p, (cx, cy, 0.545), (cx, cy, 0.565), .016, .016, 6, "metal")  # cap knob
    vs += cyl(p, (cx, cy, 0.355), (cx, cy, 0.385), .058, .066, 6, "metal")  # base
    for k in range(3):
        a = 2 * math.pi * k / 3 + 0.5
        px, py = cx + .058 * math.cos(a), cy + .058 * math.sin(a)
        vs += box(p, (px, py, 0.44), (.016, .016, .12), "metal")
    finish_tags(p, vs, "metal")
    vs = cyl(p, (cx, cy, 0.385), (cx, cy, 0.495), .045, .045, 6, "glow")
    finish_tags(p, vs, "glow")
    # scale up about the belt hook for readability
    for v in p.bm.verts:
        v.co = Vector((0.30, 0.03, 0.61)) + (v.co - Vector((0.30, 0.03, 0.61))) * 1.25
    parts.append(p)

def build_badge():
    w = TORSO_W(Vector((0.12, -0.22, 0.9)))
    p = Part("badge", lambda co, w=w: dict(w))
    c = Vector((0.125, -0.232, 0.905))
    yaw = Matrix.Rotation(math.radians(22), 3, 'Z')
    tilt = Matrix.Rotation(math.radians(-4), 3, 'X')
    R = yaw @ tilt
    def P(local):
        return c + R @ Vector(local)
    vs = box(p, P((0, 0, 0)), (.15, .02, .11), "card", 0.006, R)
    finish_tags(p, vs, "card")
    vs = box(p, P((0, -0.004, 0.034)), (.152, .02, .026), "card_red", 0, R)
    finish_tags(p, vs, "card_red")
    for k, x in enumerate((-0.05, -0.02, 0.01)):
        vs = box(p, P((x, -0.0115, -0.004)), (.014, .004, .016), "ink")
        finish_tags(p, vs, "ink")
    vs = box(p, P((0.0, -0.0115, -0.034)), (.11, .004, .008), "ink", 0, R)
    finish_tags(p, vs, "ink")
    vs = box(p, P((0, -0.006, 0.062)), (.04, .03, .03), "metal", 0.004, R)  # clip
    finish_tags(p, vs, "metal")
    parts.append(p)

def build_arm(side, s):
    up, lo, wr, ha = "upperarm." + side, "lowerarm." + side, "wrist." + side, "hand." + side
    zc = 1.107
    # upper sleeve
    p = Part("sleeve_up_" + side, xblend([(0.2, {"chest": .5, up: .5}), (0.29, {up: 1})], s))
    n = 8
    rings = [ring((x * s, 0, zc), (0, -1, 0), (0, 0, 1), r, r * 1.02, n, math.pi / n) for x, r in [(0.17, .10), (0.32, .105), (0.465, .098)]]
    loft(p, rings, "robe")
    parts.append(p)
    # bell sleeve, tattered, hollow
    p = Part("sleeve_lo_" + side, rigid(lo))
    n = 10
    jag = [0.0, 0.05, 0.0, 0.035, 0.0, 0.055, 0.0, 0.03, 0.0, 0.045]
    r0 = ring((0.43 * s, 0, zc), (0, -1, 0), (0, 0, 1), .102, .102, n)
    r1 = ring((0.58 * s, 0, zc - 0.008), (0, -1, 0), (0, 0, 1), .13, .13, n)
    r2 = ring((0.69 * s, 0, zc - 0.02), (0, -1, 0), (0, 0, 1), .165, .16, n)
    r2 = [co + Vector((jag[k] * s, 0, 0)) for k, co in enumerate(r2)]
    grid, faces = loft(p, [r0, r1, r2], "robe", closed=True, cap0=True, cap1=False)
    capf = faces[-1]
    side_faces = faces[:-1]
    shell(p, side_faces, lambda co, s=s: Vector((co.x, co.y * .82, zc + (co.z - zc) * .82)), "robe_inner", "robe_dark")
    parts.append(p)
    # wrist bone
    p = Part("wrist_" + side, xblend([(0.70, {lo: 1}), (0.76, {wr: 1})], s))
    vs = cyl(p, (0.62 * s, 0, zc), (0.80 * s, 0, zc), .03, .03, 6, "bone")
    tag_all(p, "bone")
    parts.append(p)
    # chunky bony fist
    p = Part("hand_" + side, rigid(ha))
    vs = box(p, (0.865 * s, 0, 1.092), (.14, .14, .12), "bone", 0.03)
    tag_all(p, "bone")
    for k, y in enumerate((-0.045, -0.015, 0.015, 0.045)):
        vs = box(p, (0.925 * s, y * 1.1, 1.045 - abs(y) * 0.2), (.05, .031, .08), "bone", 0.009)
    vs = box(p, (0.84 * s, -0.075, 1.065), (.06, .04, .05), "bone", 0.01, Matrix.Rotation(math.radians(-25 * s), 3, 'Z'))
    for f in p.bm.faces:
        if f[p.sw] != SW["bone"]:
            f[p.sw] = SW["bone"]
    # knuckle shading: faces of finger boxes pointing -z get shade
    p.bm.normal_update()
    for f in p.bm.faces:
        if f.normal.z < -0.6:
            f[p.sw] = SW["bone_shade"]
    parts.append(p)

def build_leg(side, s):
    x = 0.171 * s
    p = Part("thigh_" + side, rigid("upperleg." + side))
    cyl(p, (x, 0, 0.52), (x, -0.008, 0.30), .045, .04, 6, "bone")
    tag_all(p, "bone")
    parts.append(p)
    p = Part("shin_" + side, rigid("lowerleg." + side))
    ellipsoid(p, (x, -0.012, 0.29), (.062, .062, .058), 6, 4, "bone")
    cyl(p, (x, -0.008, 0.29), (x, 0.012, 0.17), .045, .042, 6, "bone")
    tag_all(p, "bone")
    for f in p.bm.faces:
        f.normal_update()
        if f.normal.z < -0.5:
            f[p.sw] = SW["bone_shade"]
    parts.append(p)
    # boot
    p = Part("boot_" + side, rigid("foot." + side))
    cyl(p, (x, 0.015, 0.07), (x, 0.015, 0.225), .094, .1, 8, "boot")
    cyl(p, (x, 0.015, 0.20), (x, 0.015, 0.262), .118, .124, 8, "boot_cuff")
    box(p, (x, -0.02, 0.072), (.205, .235, .13), "boot", 0.04)
    box(p, (x, -0.01, 0.017), (.215, .25, .034), "sole", 0.01)
    parts.append(p)
    p = Part("toe_" + side, rigid("toes." + side))
    box(p, (x, -0.165, 0.068), (.215, .16, .125), "boot", 0.05)
    box(p, (x, -0.17, 0.017), (.225, .17, .034), "sole", 0.012)
    parts.append(p)

HOOD_C = Vector((0, 0.015, 1.60))
HOOD_R = Vector((0.44, 0.42, 0.42))

def build_hood():
    p = Part("hood", rigid("head"))
    n = 16
    lats = [-64, -44, -20, 6, 34, 54, 70]
    grid = []
    for la in lats:
        phi = math.radians(la)
        r = []
        for k in range(n):
            th = 2 * math.pi * (k + 0.5) / n  # columns centred on front
            th = 2 * math.pi * k / n
            x = HOOD_R.x * math.cos(phi) * math.sin(th)
            y = -HOOD_R.y * math.cos(phi) * math.cos(th)
            if y > 0:
                y *= 1.12  # bigger back bulge
            z = HOOD_R.z * math.sin(phi)
            if la == -62:
                z -= 0.0
            r.append(p.v(HOOD_C + Vector((x, y, z))))
        grid.append(r)
    # floppy tip: rings along a curve bending backwards
    top = HOOD_C + Vector((0, 0.0, HOOD_R.z * math.sin(math.radians(70))))
    ctrl = [(Vector((0, 0.07, 0.10)), .125), (Vector((0, 0.20, 0.155)), .08), (Vector((0, 0.33, 0.15)), .045)]
    prev = top
    for off, rr in ctrl:
        cpos = top + off
        tang = (cpos - prev).normalized()
        u = Vector((1, 0, 0))
        v = tang.cross(u).normalized()
        r = []
        for k in range(n):
            th = 2 * math.pi * k / n
            # match lon convention: x = sin(th), front(-y) = cos(th) -> use -v as "front"
            r.append(p.v(cpos + u * (rr * math.sin(th)) - v * (rr * math.cos(th))))
        grid.append(r)
        lats.append(90)
        prev = cpos
    tip = p.v(top + Vector((0, 0.44, 0.10)))
    faces = []
    opening = []
    for i in range(len(lats) - 1):
        for k in range(n):
            a, b = grid[i][k], grid[i][(k + 1) % n]
            c, d = grid[i + 1][(k + 1) % n], grid[i + 1][k]
            # face centre longitude from front
            thc = 2 * math.pi * (k + 0.5) / n
            ang = math.degrees(min(thc, 2 * math.pi - thc))
            is_open = (ang < 58) and (1 <= i <= 3)
            if is_open:
                continue
            faces.append(p.face([a, b, c, d], "robe"))
    main_faces = list(faces)
    for k in range(n):
        faces.append(p.face([grid[-1][k], grid[-1][(k + 1) % n], tip], "robe"))
    def inward(co):
        d = co - HOOD_C
        return HOOD_C + Vector((d.x * .86, d.y * .86 - 0.0, d.z * .86 - 0.01))
    tip_rings = set(v for r in grid[len(grid) - 4:] for v in r)
    main_faces = [f for f in main_faces if not all(v in tip_rings for v in f.verts)]
    newf = shell(p, main_faces, inward, "robe_inner", "robe_dark")
    # the opening rim gets the trim colour, neck rim stays dark
    for f in newf:
        if f[p.sw] == SW["robe_dark"]:
            cz = sum((v.co.z for v in f.verts), 0) / len(f.verts)
            if cz > HOOD_C.z - 0.33:
                f[p.sw] = SW["hood_trim"]
    parts.append(p)
    # stitched patch on the back of the hood
    q = Part("hood_patch", rigid("head"))
    lon, lat = math.radians(205), math.radians(22)
    pos = HOOD_C + Vector((HOOD_R.x * math.cos(lat) * math.sin(lon), -HOOD_R.y * 1.12 * math.cos(lat) * math.cos(lon), HOOD_R.z * math.sin(lat)))
    nrm = (pos - HOOD_C).normalized()
    R = Vector((0, 0, 1)).rotation_difference(nrm).to_matrix() @ Matrix.Rotation(math.radians(12), 3, 'Z')
    box(q, pos - nrm * 0.004, (.15, .13, .03), "hood_trim", 0.0, R)
    for k in range(4):
        ang = math.radians(45 + 90 * k)
        for t in (-1, 1):
            local = Vector((0.062 * (1 if k in (0, 3) else -1), 0.052 * (1 if k < 2 else -1), 0.016))
        local = Vector((0.06 * math.copysign(1, math.cos(ang)), 0.05 * math.copysign(1, math.sin(ang)), 0.016))
        box(q, pos + R @ local, (.035, .009, .006), "robe_inner", 0.0, R @ Matrix.Rotation(ang, 3, 'Z'))
    parts.append(q)

FACE_Y = -0.345

def build_skull():
    p = Part("skull", rigid("head"))
    c = Vector((0, -0.17, 1.585))
    vs = ellipsoid(p, c, (.28, .25, .26), 14, 9, "bone")
    for v in vs:
        if v.co.y < FACE_Y:
            v.co.y = FACE_Y
    tag_all(p, "bone")
    # cheek / underside shading
    p.bm.normal_update()
    for f in p.bm.faces:
        if f.normal.z < -0.55:
            f[p.sw] = SW["bone_shade"]
    parts.append(p)
    # face decals
    d = Part("face", rigid("head"))
    ez = 1.615
    for s in (1, -1):
        ex = 0.112 * s
        disc(d, (ex, FACE_Y - 0.004, ez), .088, .094, 10, "socket")
        disc(d, (ex, FACE_Y - 0.008, ez - 0.004), .07, .076, 10, "eye")
        disc(d, (ex + 0.025 * s * 0 - 0.022, FACE_Y - 0.012, ez + 0.028), .022, .022, 6, "eye_hi")
    # nose (inverted triangle)
    quad_xz(d, [Vector((-0.028, FACE_Y - 0.004, 1.548)), Vector((0.028, FACE_Y - 0.004, 1.548)), Vector((0.0, FACE_Y - 0.004, 1.51))], "socket")
    # grin: curved strip
    segs = 8
    top, bot = [], []
    for k in range(segs + 1):
        t = -1 + 2 * k / segs
        x = 0.12 * t
        zc = 1.447 + 0.032 * t * t
        th = 0.014 + 0.008 * (1 - t * t)
        top.append(d.v((x, FACE_Y - 0.004, zc + th / 2)))
        bot.append(d.v((x, FACE_Y - 0.004, zc - th / 2)))
    for k in range(segs):
        f = d.face([bot[k], bot[k + 1], top[k + 1], top[k]], "socket")
    for t in (-0.6, -0.2, 0.2, 0.6):
        x = 0.12 * t
        zc = 1.447 + 0.032 * t * t
        quad_xz(d, [Vector((x - 0.006, FACE_Y - 0.005, zc - 0.03)), Vector((x + 0.006, FACE_Y - 0.005, zc - 0.03)),
                    Vector((x + 0.006, FACE_Y - 0.005, zc + 0.026)), Vector((x - 0.006, FACE_Y - 0.005, zc + 0.026))], "socket")
    d.bm.normal_update()
    for f in d.bm.faces:
        if f.normal.y > 0:
            f.normal_flip()
    parts.append(d)

# ---------------------------------------------------------------- assemble
def part_to_object(p, rig, mats):
    bm = p.bm
    if p.name not in ("face",):
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    uv = bm.loops.layers.uv.new("UVMap")
    for f in bm.faces:
        u = swatch_uv(f[p.sw])
        for l in f.loops:
            l[uv].uv = u
        name = PAL[f[p.sw]][0]
        f.material_index = 1 if name in GLOW else 0
    me = bpy.data.meshes.new(p.name)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(p.name, me)
    bpy.context.scene.collection.objects.link(ob)
    for m in mats:
        me.materials.append(m)
    groups = {}
    for v in me.vertices:
        w = p.weights(v.co)
        for b, val in w.items():
            if val <= 1e-4:
                continue
            if b not in groups:
                groups[b] = ob.vertex_groups.new(name=b)
            groups[b].add([v.index], val, 'REPLACE')
    return ob

def make_materials(img):
    mats = []
    for name, emis in (("M_Reaper", 0.0), ("M_Reaper_Glow", 4.0)):
        m = bpy.data.materials.new(name)
        m.use_nodes = True
        nt = m.node_tree
        bsdf = nt.nodes.get("Principled BSDF")
        tex = nt.nodes.new("ShaderNodeTexImage")
        tex.image = img
        tex.interpolation = 'Closest'
        nt.nodes.active = tex
        nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
        bsdf.inputs["Roughness"].default_value = 0.75
        if emis:
            nt.links.new(tex.outputs["Color"], bsdf.inputs["Emission Color"])
            bsdf.inputs["Emission Strength"].default_value = emis
        mats.append(m)
    return mats

def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=SRC)
    rig = bpy.data.objects["Rig"]
    for o in list(bpy.data.objects):
        if o.type == 'MESH':
            me = o.data
            bpy.data.objects.remove(o)
            bpy.data.meshes.remove(me)
    for m in list(bpy.data.materials):
        bpy.data.materials.remove(m)
    # rest pose for building
    rig.animation_data.action = None
    for pb in rig.pose.bones:
        pb.matrix_basis = Matrix.Identity(4)
    img = make_palette()
    mats = make_materials(img)

    build_torso(); build_capelet(); build_skirt(); build_belt(); build_lantern(); build_badge()
    build_arm("l", 1); build_arm("r", -1)
    build_leg("l", 1); build_leg("r", -1)
    build_hood(); build_skull()

    objs = [part_to_object(p, rig, mats) for p in parts]
    bpy.ops.object.select_all(action='DESELECT')
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.join()
    body = bpy.context.view_layer.objects.active
    body.name = "Reaper_Body"
    body.data.name = "Reaper_Body"
    # mesh in rig local space, like the KayKit source meshes
    body.data.transform(rig.matrix_world.inverted())
    body.matrix_world = rig.matrix_world.copy()
    body.parent = rig
    body.matrix_parent_inverse = Matrix.Identity(4)
    body.location = (0, 0, 0); body.rotation_euler = (0, 0, 0); body.scale = (1, 1, 1)
    mod = body.modifiers.new("Armature", 'ARMATURE')
    mod.object = rig
    # merge coincident verts inside the same part only isn't needed; shading
    bpy.ops.object.select_all(action='DESELECT')
    body.select_set(True)
    bpy.context.view_layer.objects.active = body
    bpy.ops.object.shade_flat()
    # normalize weights
    bpy.ops.object.mode_set(mode='WEIGHT_PAINT')
    bpy.ops.object.vertex_group_normalize_all(lock_active=False)
    bpy.ops.object.mode_set(mode='OBJECT')
    tris = sum(len(p.vertices) - 2 for p in body.data.polygons)
    print("BODY_TRIS", tris, "VERTS", len(body.data.vertices), "GROUPS", [g.name for g in body.vertex_groups])
    bpy.ops.wm.save_as_mainfile(filepath=OUTDIR + "Reaper_Custom_base.blend")

if __name__ == "__main__":
    main()
