# Soul Quota reapers, batch 3 - shared helpers on top of monsters/mq_lib.py (original work).
# Rig: the unchanged Reaper rig, read from a snapshot copy of ArtSource/Characters/Reaper_Custom.fbx (never written).
import bpy, math, sys, os
sys.path.insert(0, "/home/claude/blender_work/monsters")
from mathutils import Vector, Matrix
import mq_lib
from mq_lib import *

RIG_SRC = "/tmp/rq/Reaper_Custom.fbx"   # snapshot copy of ArtSource/Characters/Reaper_Custom.fbx
ROT = lambda deg, ax: Matrix.Rotation(math.radians(deg), 3, ax)
ZC = 1.107  # arm height (T-pose)

# one generic swatch layout for this batch, colours set per reaper
RLAYOUT = ["bone", "bone_shade", "socket", "glow", "glow2", "cloth", "cloth_dark", "cloth_inner",
           "cloth2", "cloth2_dark", "shirt", "shirt_dark", "accent", "accent_dark", "metal", "metal_dark",
           "gold", "gold_dark", "wood", "wood_dark", "leather", "shoe", "sole", "hair",
           "ink", "paper", "glass", "extra1", "extra2", "extra3", "extra4", "extra5"]

def set_layout():
    mq_lib.LAYOUT[:] = RLAYOUT
    mq_lib.SW.clear()
    mq_lib.SW.update({n: i for i, n in enumerate(RLAYOUT)})
    mq_lib.GLOW.clear()
    mq_lib.GLOW.update({"glow", "glow2"})

def hexc(h):
    """sRGB hex -> linear-ish float tuple used directly as pixel value (palette PNG stores sRGB bytes)"""
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4))

def load_reaper_rig():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.render.fps = 30
    bpy.ops.import_scene.fbx(filepath=RIG_SRC)
    rig = bpy.data.objects["Rig"]
    for o in list(bpy.data.objects):
        if o.type == 'MESH':
            me = o.data
            bpy.data.objects.remove(o)
            bpy.data.meshes.remove(me)
        elif o is not rig:
            bpy.data.objects.remove(o)
    for m in list(bpy.data.materials):
        bpy.data.materials.remove(m)
    for img in list(bpy.data.images):
        bpy.data.images.remove(img)
    if rig.animation_data:
        rig.animation_data.action = None
    for a in list(bpy.data.actions):
        bpy.data.actions.remove(a)
    for pb in rig.pose.bones:
        pb.matrix_basis = Matrix.Identity(4)
    read_bones(rig)
    return rig

TORSO_W = zblend([(0.58, {"hips": 1}), (0.70, {"hips": .5, "spine": .5}), (0.88, {"spine": 1}),
                  (1.04, {"chest": .7, "spine": .3}), (1.14, {"chest": 1})])

def skirt_w(z_top, z_bot, leg_max=0.6, base=None, low_leg=0.0):
    """cloth hanging from the hips: hips at z_top -> side upper leg at z_bot (centre line split 50/50)"""
    def fn(co):
        b = base(co) if base else {"hips": 1.0}
        t = max(0.0, min(1.0, (z_top - co.z) / (z_top - z_bot)))
        t = t * t * (3 - 2 * t) * leg_max
        if t <= 0:
            return b
        k = max(-1.0, min(1.0, co.x / 0.09))
        wl, wr = 0.5 + 0.5 * k, 0.5 - 0.5 * k
        out = {n: v * (1 - t) for n, v in b.items()}
        for side, ws in (("l", wl), ("r", wr)):
            if ws <= 0:
                continue
            out["upperleg." + side] = out.get("upperleg." + side, 0) + t * ws * (1 - low_leg)
            if low_leg:
                out["lowerleg." + side] = out.get("lowerleg." + side, 0) + t * ws * low_leg
        return out
    return fn

# ---------------------------------------------------------------- shared body pieces
def skull(hc, size=(.56, .50, .50), eye_r=.085, eye_y=0.0, eye_z=0.0, pupils=True, teeth=True, wfn=None, name="skull",
          eye_sw="socket", pupil_sw="glow", tilt=0.0, useg=12, vseg=8, power=0.7, jaw=True, eye_dx=None):
    """cute chunky skull; returns (part, front_y)"""
    hc = Vector(hc)
    p = Part(name, wfn or rigid("head"))
    w, d, h = size
    R = ROT(tilt, 'Y')
    sbox(p, hc, (w, d, h), useg, vseg, "bone", power=power, rot=R, taper=lambda zn: (1.0 + 0.04 * zn, 1.0 + 0.03 * zn))
    p.recolor(lambda c, n, cur: n.z < -0.6, "bone_shade")
    fy = hc.y - d / 2
    ex = eye_dx if eye_dx is not None else w * 0.21
    for sx in (1, -1):
        ec = hc + R @ Vector((ex * sx, -d / 2 + 0.03 + eye_y, 0.01 + eye_z))
        ell(p, ec, (eye_r, 0.05, eye_r * 1.12), 8, 5, eye_sw)
        if pupils:
            ell(p, ec + R @ Vector((0.0, -0.035, 0.005)), (eye_r * 0.36, 0.02, eye_r * 0.40), 6, 4, pupil_sw)
    # nose: little upside-down heart
    nc = hc + R @ Vector((0, -d / 2 + 0.005, -h * 0.17))
    for sx in (1, -1):
        ell(p, nc + R @ Vector((0.016 * sx, 0, 0.01)), (.022, .02, .028), 5, 3, "socket")
    if teeth:
        tz = -h * 0.34
        box(p, hc + R @ Vector((0, -d / 2 + 0.02, tz)), (w * 0.46, .05, .055), "socket", 0.01, R)
        for k in range(4):
            x = (k - 1.5) * w * 0.105
            box(p, hc + R @ Vector((x, -d / 2 + 0.002, tz)), (w * 0.085, .03, .05), "bone", 0.006, R)
    if jaw:
        box(p, hc + R @ Vector((0, 0.04, -h * 0.47)), (w * 0.64, d * 0.62, .09), "bone_shade", 0.03, R)
    return p, fy

def arm(side, s, rad=(.085, .08, .078, .075, .07), sleeve_sw="cloth", x0=0.18, x1=0.74, hand_sw="bone",
        hand_scale=1.3, wfn=None, cuff_sw=None, cuff_r=None, seg=6, bony_wrist=True, name="arm"):
    """sleeved arm tube shoulder -> wrist with smooth distance weights, chunky bony hand"""
    up, lo, wr, ha = "upperarm." + side, "lowerarm." + side, "wrist." + side, "hand." + side
    p = Part(name + "_" + side, wfn or segw(["chest", up, lo, wr], k=5, bias={"chest": 1.6}))
    n = len(rad)
    xs = [x0 + (x1 - x0) * i / (n - 1) for i in range(n)]
    tube(p, [(x * s, 0, ZC) for x in xs], list(rad), seg, sleeve_sw)
    p.recolor(lambda c, nn, cur: cur == sleeve_sw and nn.z < -0.6, sleeve_sw + "_dark" if sleeve_sw + "_dark" in RLAYOUT else sleeve_sw)
    if cuff_sw:
        c = Part(name + "cuff_" + side, segw([lo, wr], k=6))
        cr = cuff_r or rad[-1] * 1.25
        tube(c, [((x1 - 0.06) * s, 0, ZC), ((x1 + 0.005) * s, 0, ZC)], [cr, cr * 1.04], seg, cuff_sw)
    if bony_wrist:
        q = Part(name + "wrist_" + side, segw([lo, wr, ha], k=6))
        tube(q, [((x1 - 0.02) * s, 0, ZC), (0.83 * s, 0, ZC)], .034, 5, "bone")
    hand(side, s, hand_sw, hand_scale)

def hand(side, s, sw="bone", hs=1.3, px=0.855):
    h = Part("hand_" + side, rigid("hand." + side))
    box(h, (px * s, 0, ZC - 0.005), (.13 * hs, .16 * hs, .085 * hs), sw, 0.025)
    for k, y in enumerate((-0.05, 0.0, 0.05)):
        y *= hs
        a = Vector(((px + 0.05 * hs) * s, y, ZC - 0.005))
        b = a + Vector((0.085 * hs * s, 0, -0.03))
        tube(h, [a, (a + b) / 2 + Vector((0, 0, 0.01)), b], [.026 * hs, .024 * hs, .02 * hs], 4, sw)
    a = Vector(((px - 0.01) * s, -0.085 * hs, ZC))
    tube(h, [a, a + Vector((0.04 * s, -0.04 * hs, -0.01)), a + Vector((0.075 * s, -0.055 * hs, -0.03))], [.028 * hs, .024 * hs, .02 * hs], 4, sw)
    h.recolor(lambda c, n, cur: cur == "bone" and n.z < -0.6, "bone_shade")

def leg(side, s, trouser_sw="cloth2", r=(.095, .09, .08, .075), cuff_z=0.20, shoe_sw="shoe", sole_sw="sole",
        x=0.171, shoe_scale=1.0, top_z=0.58, seg=7, ankle=True, boot=False, name="leg", wfn=None, foot_wfn=None, toe_wfn=None):
    ul, ll, ft, ts = "upperleg." + side, "lowerleg." + side, "foot." + side, "toes." + side
    xx = x * s
    if trouser_sw:
        p = Part(name + "_" + side, wfn or segw([ul, ll], k=6))
        zs = [top_z, 0.42, 0.30, cuff_z]
        tube(p, [(xx, 0, z) for z in zs], list(r), seg, trouser_sw, cap0=True, cap1=True)
    if ankle:
        q = Part(name + "ankle_" + side, wfn or segw([ul, ll], k=6))
        tube(q, [(xx, 0.005, cuff_z + 0.02), (xx, 0.012, 0.11)], .032, 5, "bone")
    ss = shoe_scale
    f = Part(name + "foot_" + side, foot_wfn or rigid(ft))
    if boot:
        box(f, (xx, 0.0, 0.11), (.19 * ss, .21 * ss, .20), shoe_sw, 0.04 * min(1, ss))
    box(f, (xx, 0.0, 0.065), (.18 * ss, .21 * ss, .12), shoe_sw, 0.04 * min(1, ss) ** 2)
    box(f, (xx, 0.0, 0.014), (.19 * ss, .22 * ss, .028), sole_sw, 0.008)
    t = Part(name + "toe_" + side, toe_wfn or rigid(ts))
    box(t, (xx, -0.165 * ss, 0.058), (.18 * ss, .16 * ss, .105), shoe_sw, 0.045 * min(1, ss) ** 2)
    box(t, (xx, -0.17 * ss, 0.014), (.19 * ss, .17 * ss, .028), sole_sw, 0.008)

def pelvis(sw, rx=.25, ry=.20, z0=0.44, z1=0.66, n=10):
    p = Part("pelvis", rigid("hips"))
    loft(p, [hring(z0, rx * .9, ry * .9, n), hring((z0 + z1) / 2, rx, ry, n), hring(z1, rx, ry, n)], sw)

def neck(r=0.055, top=1.40, sw="bone"):
    p = Part("neck", zblend([(1.20, {"chest": 1}), (1.30, {"chest": .4, "head": .6}), (1.36, {"head": 1})]))
    tube(p, [(0, 0.01, 1.16), (0, 0.0, (1.16 + top) / 2), (0, 0.0, top)], [r, r * .95, r * .9], 5, sw)

def gear(p, c, r_out, r_in, teeth, thick, sw, normal=Vector((0, 0, 1)), hole=0.0, tooth_h=None):
    """flat toothed gear ring (closed if hole==0), lying in the plane with `normal`"""
    c = Vector(c)
    nrm = Vector(normal).normalized()
    u = nrm.orthogonal().normalized()
    w = nrm.cross(u)
    th = tooth_h or r_out * 0.22
    n = teeth * 4
    outer = []
    for k in range(n):
        a = 2 * math.pi * k / n
        rr = r_out + (th if (k % 4) in (1, 2) else 0.0)
        outer.append(c + u * (rr * math.cos(a)) + w * (rr * math.sin(a)))
    top = [p.v(v + nrm * thick / 2) for v in outer]
    bot = [p.v(v - nrm * thick / 2) for v in outer]
    if hole > 0:
        inner = [c + u * (hole * math.cos(2 * math.pi * k / n)) + w * (hole * math.sin(2 * math.pi * k / n)) for k in range(n)]
        it = [p.v(v + nrm * thick / 2) for v in inner]
        ib = [p.v(v - nrm * thick / 2) for v in inner]
        for k in range(n):
            j = (k + 1) % n
            p.face([top[k], top[j], it[j], it[k]], sw)
            p.face([bot[j], bot[k], ib[k], ib[j]], sw)
            p.face([bot[k], bot[j], top[j], top[k]], sw)
            p.face([ib[j], ib[k], it[k], it[j]], sw)
    else:
        p.face(top, sw)
        p.face(bot[::-1], sw)
        for k in range(n):
            j = (k + 1) % n
            p.face([bot[k], bot[j], top[j], top[k]], sw)

def torus(p, c, R, r, nu, nv, sw, normal=Vector((0, 0, 1))):
    c = Vector(c)
    nrm = Vector(normal).normalized()
    u = nrm.orthogonal().normalized()
    w = nrm.cross(u)
    rings = []
    for i in range(nu):
        a = 2 * math.pi * i / nu
        d = u * math.cos(a) + w * math.sin(a)
        cc = c + d * R
        rings.append([cc + d * (r * math.cos(2 * math.pi * k / nv)) + nrm * (r * math.sin(2 * math.pi * k / nv)) for k in range(nv)])
    grid = [[p.v(co) for co in rr] for rr in rings]
    for i in range(nu):
        for k in range(nv):
            a, b = grid[i][k], grid[i][(k + 1) % nv]
            cc, dd = grid[(i + 1) % nu][(k + 1) % nv], grid[(i + 1) % nu][k]
            p.face([a, dd, cc, b], sw)

# ---------------------------------------------------------------- build + export
CH = "/home/claude/sq/ArtSource/Characters/"

def build_reaper(rid, palette, builder):
    set_layout()
    rig = load_reaper_rig()
    model = "R_" + rid
    img = make_palette(model, palette, CH + model.lower() + "_palette.png")
    mats = make_materials(model, img)
    builder()
    body = assemble(model, rig, mats)
    zs = [(rig.matrix_world @ v.co).z for v in body.data.vertices] if False else [(body.matrix_world @ v.co).z for v in body.data.vertices]
    print("MODEL", model, "TRIS", tri_count(body), "VERTS", len(body.data.vertices), "UNWEIGHTED", check_weights(body),
          "HEIGHT_CM %.1f" % (100 * (max(zs) - min(zs))), "MINZ %.3f" % min(zs),
          "GROUPS", sorted(g.name for g in body.vertex_groups))
    img.filepath = "//" + model.lower() + "_palette.png"
    bpy.ops.wm.save_as_mainfile(filepath=CH + model + ".blend", relative_remap=True)
    export_fbx(CH + model + ".fbx", [rig, body], anims=False)
    print("EXPORTED", CH + model + ".fbx")
    return body
