# Soul Quota - "Gravebat": original flying enemy (a bat-winged skull) with its own small rig and 6 takes.
# usage: python3 mq_gravebat.py
import bpy, math, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mathutils import Vector, Matrix, Quaternion
import mq_lib
from mq_lib import *

MODEL = "Gravebat"
ROT = lambda deg, ax: Matrix.Rotation(math.radians(deg), 3, ax)
PAL = dict(bone=(0.95, 0.92, 0.80), bone_shade=(0.76, 0.72, 0.62), membrane=(0.56, 0.32, 0.64), membrane_dark=(0.30, 0.16, 0.36),
           cavity=(0.07, 0.03, 0.08), glow=(1.00, 0.36, 0.78), glow2=(1.00, 0.70, 0.90), tooth=(1.00, 0.97, 0.86),
           mouth=(0.16, 0.05, 0.10), stitch=(0.22, 0.16, 0.20), hair=(0.20, 0.12, 0.24), nail=(0.18, 0.14, 0.18),
           skin=(0.46, 0.27, 0.52), skin_dark=(0.30, 0.17, 0.34), tongue=(0.86, 0.38, 0.50), metal=(0.60, 0.60, 0.62))

# ---------------------------------------------------------------- rig (Z up, facing -Y like the KayKit characters)
B = {
    "root": ((0, 0, 0), (0, 0, 0.25), None),
    "body": ((0, 0.04, 1.18), (0, 0.04, 1.40), "root"),
    "head": ((0, 0.0, 1.40), (0, 0.0, 1.78), "body"),
    "jaw": ((0, -0.02, 1.43), (0, -0.24, 1.35), "head"),
    "tail1": ((0, 0.16, 1.22), (0, 0.38, 1.10), "body"),
    "tail2": ((0, 0.38, 1.10), (0, 0.62, 1.05), "tail1"),
}
J = {}
for s, sd in ((1, "l"), (-1, "r")):
    j0, j1, j2, j3 = (Vector((0.19 * s, 0.05, 1.40)), Vector((0.52 * s, 0.07, 1.52)), Vector((0.90 * s, 0.11, 1.50)),
                      Vector((1.24 * s, 0.17, 1.38)))
    J[sd] = dict(j0=j0, j1=j1, j2=j2, j3=j3, f2=Vector((1.10 * s, 0.58, 1.30)), f3=Vector((0.72 * s, 0.70, 1.30)),
                 root_t=Vector((0.14 * s, 0.40, 1.27)))
    B["wing1." + sd] = (tuple(j0), tuple(j1), "body")
    B["wing2." + sd] = (tuple(j1), tuple(j2), "wing1." + sd)
    B["wing3." + sd] = (tuple(j2), tuple(j3), "wing2." + sd)

def make_rig():
    arm = bpy.data.armatures.new("Rig")
    rig = bpy.data.objects.new("Rig", arm)
    bpy.context.scene.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    rig.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    eb = {}
    for name, (h, t, par) in B.items():
        b = arm.edit_bones.new(name)
        b.head, b.tail = Vector(h), Vector(t)
        b.roll = 0.0
        eb[name] = b
    for name, (h, t, par) in B.items():
        if par:
            eb[name].parent = eb[par]
            eb[name].use_connect = (Vector(eb[par].tail) - Vector(h)).length < 1e-6
    bpy.ops.object.mode_set(mode='OBJECT')
    arm.bones["root"].use_deform = False
    for pb in rig.pose.bones:
        pb.rotation_mode = 'QUATERNION'
    mq_lib.read_bones(rig)
    return rig

# ---------------------------------------------------------------- mesh
def wing_weights(sd):
    j = J[sd]
    segs = [(j["j0"], j["root_t"], "body"), (j["j0"], j["j1"], "wing1." + sd), (j["j1"], j["j2"], "wing2." + sd),
            (j["j2"], j["j3"], "wing3." + sd), (j["j2"], j["f2"], "wing3." + sd), (j["j2"], j["f3"], "wing3." + sd)]
    def fn(co):
        w = {}
        for a, b, bone in segs:
            d = b - a
            u = max(0.0, min(1.0, (co - a).dot(d) / d.length_squared))
            dist = (co - (a + d * u)).length + 0.02
            w[bone] = w.get(bone, 0.0) + 1.0 / dist ** 4
        s = sum(w.values())
        w = {k: v / s for k, v in w.items()}
        w = {k: v for k, v in w.items() if v > 0.05}
        s = sum(w.values())
        return {k: v / s for k, v in w.items()}
    return fn

def polyline_at(pts, u):
    L = [0.0]
    for a, b in zip(pts, pts[1:]):
        L.append(L[-1] + (b - a).length)
    t = u * L[-1]
    for i in range(len(pts) - 1):
        if t <= L[i + 1] + 1e-9:
            k = (t - L[i]) / max(1e-9, L[i + 1] - L[i])
            return pts[i].lerp(pts[i + 1], k)
    return pts[-1].copy()

def build_wing(sd, s):
    j = J[sd]
    W = wing_weights(sd)
    p = Part("membrane_" + sd, W)
    def scal(a, b, k):
        return ((a + b) / 2).lerp(j["j2"], k)
    off = Vector((0, 0.03, -0.01))
    panels = [  # (leading edge polyline, trailing edge polyline, columns) - scallops pulled toward the wrist = tattered edge
        ([j["j0"] + off, j["j1"] + off, j["j2"] + off], [j["root_t"], scal(j["root_t"], j["f3"], 0.20), j["f3"]], 8),
        ([j["j2"] + off, j["j2"] + off], [j["f3"], scal(j["f3"], j["f2"], 0.24), j["f2"]], 5),
        ([j["j2"] + off, j["j3"]], [j["f2"], scal(j["f2"], j["j3"], 0.22), j["j3"]], 5),
    ]
    vcache = {}
    def V(co):
        key = tuple(round(c, 4) for c in co)
        if key not in vcache:
            vcache[key] = p.v(co)
        return vcache[key]
    faces = []
    holes = {(0, 4), (2, 2)}  # tears in the membrane (panel, column) on the trailing half
    for pi, (lead, trail, N) in enumerate(panels):
        rows = []
        for i in range(N + 1):
            u = i / N
            a = polyline_at(lead, u)
            b = polyline_at(trail, u)
            rows.append([V(a), V(a.lerp(b, 0.5) + Vector((0, 0, -0.025))), V(b)])
        for i in range(N):
            for kk in range(2):
                if kk == 1 and (pi, i) in holes:
                    continue
                vs = [rows[i][kk], rows[i + 1][kk], rows[i + 1][kk + 1], rows[i][kk + 1]]
                uniq = []
                for v in vs:
                    if v not in uniq:
                        uniq.append(v)
                if len(uniq) >= 3:
                    faces.append(p.face(uniq, "membrane"))
    # top faces must point up (+Z)
    p.bm.normal_update()
    for f in faces:
        if f.normal.z < 0:
            f.normal_flip()
    shell(p, faces, lambda co: co - Vector((0, 0, 0.018)), "membrane_dark", "membrane_dark")
    p.recalc = False
    # wing bones (arm + fingers), drawn on top of the membrane
    q = Part("wingbones_" + sd, W)
    tube(q, [j["j0"], (j["j0"] + j["j1"]) / 2 + Vector((0, 0, 0.02)), j["j1"]], [.045, .036, .04], 6, "bone")
    tube(q, [j["j1"], j["j2"]], [.036, .03], 6, "bone")
    ell(q, j["j1"], (.05, .05, .05), 6, 4, "bone")
    ell(q, j["j2"], (.05, .05, .045), 6, 4, "bone")
    for tip in (j["j3"], j["f2"], j["f3"]):
        tube(q, [j["j2"], tip], [.024, .014], 5, "bone")
    cone(q, j["j2"] + Vector((0, -0.02, 0.03)), j["j2"] + Vector((0.03 * s, -0.12, 0.10)), .025, 4, "nail")  # thumb claw
    q.recolor(lambda c, n, cur: cur == "bone" and n.z < -0.6, "bone_shade")

def build_bat():
    # skull (head)
    h = Part("skull", rigid("head"))
    hc = Vector((0, -0.03, 1.57))
    sbox(h, hc, (.58, .54, .50), 12, 9, "bone", power=0.62, taper=lambda zn: (0.86 + 0.14 * max(zn, -0.3), 0.95 + 0.05 * zn))
    h.recolor(lambda c, n, cur: n.z < -0.55, "bone_shade")
    for sx in (1, -1):
        ell(h, (0.125 * sx, -0.27, 1.57), (.105, .05, .095), 8, 5, "cavity", ROT(-12 * sx, 'Y'))
        ell(h, (0.12 * sx, -0.305, 1.565), (.045, .02, .05), 6, 4, "glow")
        ell(h, (0.135 * sx, -0.315, 1.585), (.015, .01, .015), 4, 3, "glow2")
        box(h, (0.13 * sx, -0.27, 1.675), (.17, .06, .05), "bone", 0.015, ROT(16 * sx, 'Y'))  # angry brow
        box(h, (0.22 * sx, -0.20, 1.47), (.08, .14, .07), "bone_shade", 0.02)  # cheekbone
    slab(h, ((-0.035, -0.29, 1.505), (0.035, -0.29, 1.505), (0.0, -0.29, 1.455)), (0, 0.04, 0), "cavity")  # nose hole
    stitch_line(h, (0.05, -0.18, 1.81), (0.17, -0.02, 1.82), 3, "stitch", (0, 0, 1), w=0.014, cross=0.0)  # crack
    # upper teeth + big fangs
    for x in (-0.10, -0.035, 0.035, 0.10):
        box(h, (x, -0.285, 1.425), (.05, .03, .05), "tooth", 0.008)
    for sx in (1, -1):
        cone(h, (0.135 * sx, -0.27, 1.43), (0.14 * sx, -0.285, 1.33), .03, 4, "tooth")
    # bat ears
    for sx in (1, -1):
        slab(h, ((0.10 * sx, -0.05, 1.74), (0.26 * sx, 0.07, 1.73), (0.33 * sx, 0.04, 2.02)), (0, 0.04, 0), "skin", "skin_dark")
        slab(h, ((0.14 * sx, -0.058, 1.76), (0.24 * sx, 0.04, 1.755), (0.30 * sx, 0.02, 1.94)), (0, 0.01, 0), "membrane_dark")
    # jaw
    jw = Part("jaw", rigid("jaw"))
    box(jw, (0, -0.15, 1.37), (.38, .22, .07), "bone", 0.025)
    for sx in (1, -1):
        box(jw, (0.17 * sx, -0.05, 1.40), (.05, .14, .08), "bone", 0.015)
        cone(jw, (0.11 * sx, -0.245, 1.39), (0.10 * sx, -0.255, 1.46), .024, 4, "tooth")
    box(jw, (0, -0.14, 1.392), (.24, .14, .02), "mouth", 0.0)
    jw.recolor(lambda c, n, cur: cur == "bone" and n.z < -0.6, "bone_shade")
    # furry little body behind the skull + dangling claws
    b = Part("body", rigid("body"))
    ell(b, (0, 0.13, 1.33), (.19, .17, .16), 8, 6, "membrane_dark")
    for k, (x, z) in enumerate(((0.0, 1.47), (0.07, 1.45), (-0.07, 1.45))):
        cone(b, (x, 0.12, z - 0.02), (x * 1.4, 0.18, z + 0.06), .04, 4, "hair")  # tuft
    for sx in (1, -1):
        tube(b, [(0.08 * sx, 0.18, 1.22), (0.09 * sx, 0.20, 1.12), (0.09 * sx, 0.16, 1.06)], [.025, .02, .018], 5, "bone")
        for dx in (-0.025, 0.025):
            cone(b, (0.09 * sx + dx, 0.16, 1.07), (0.09 * sx + dx * 1.5, 0.10, 1.03), .014, 4, "nail")
    # tail: vertebrae + tattered spade
    t = Part("tail", segw(["body", "tail1", "tail2"], k=5))
    pts = [Vector((0, 0.17, 1.24)), Vector((0, 0.38, 1.10)), Vector((0, 0.56, 1.06)), Vector((0, 0.66, 1.05))]
    tube(t, pts, [.04, .03, .022, .02], 5, "bone")
    for k in range(4):
        c = polyline_at(pts, 0.15 + 0.22 * k)
        ell(t, c, (.035, .035, .03), 5, 3, "bone_shade")
    q = Part("spade", rigid("tail2"))
    slab(q, ((0, 0.60, 1.05), (0.09, 0.68, 1.06), (0.04, 0.72, 1.06), (0.05, 0.82, 1.07), (0, 0.78, 1.07), (-0.05, 0.84, 1.07),
             (-0.04, 0.73, 1.06), (-0.10, 0.70, 1.06)), (0, 0, 0.025), "membrane")
    for sd, s in (("l", 1), ("r", -1)):
        build_wing(sd, s)

# ---------------------------------------------------------------- animation
AX = Vector((1, 0, 0)); AY = Vector((0, 1, 0)); AZ = Vector((0, 0, 1))

def qw(rig, bone, axis, deg):
    M = rig.data.bones[bone].matrix_local.to_3x3()
    return Quaternion(M.inverted() @ axis, math.radians(deg))

def apply(rig, P):
    g = lambda k, d=0.0: P.get(k, d)
    pb = rig.pose.bones
    M = rig.data.bones["body"].matrix_local.to_3x3()
    pb["body"].location = M.inverted() @ Vector((g("bx"), g("by"), g("bz")))
    pb["body"].rotation_quaternion = qw(rig, "body", AZ, g("byaw")) @ qw(rig, "body", AY, g("broll")) @ qw(rig, "body", AX, g("bpitch"))
    sc = g("bscale", 1.0)
    pb["body"].scale = (sc, sc, sc)
    pb["head"].rotation_quaternion = qw(rig, "head", AZ, g("hyaw")) @ qw(rig, "head", AY, g("hroll")) @ qw(rig, "head", AX, g("hpitch"))
    pb["jaw"].rotation_quaternion = qw(rig, "jaw", AX, g("jaw"))
    for sd, s in (("l", 1), ("r", -1)):
        for i in (1, 2, 3):
            up = g("up%d" % i) + g("up%d_%s" % (i, sd))
            sw = g("sw%d" % i) + g("sw%d_%s" % (i, sd))
            tw = g("tw%d" % i) + g("tw%d_%s" % (i, sd))
            b = "wing%d.%s" % (i, sd)
            pb[b].rotation_quaternion = qw(rig, b, AY, -s * up) @ qw(rig, b, AZ, s * sw) @ qw(rig, b, AX, tw)
    pb["tail1"].rotation_quaternion = qw(rig, "tail1", AZ, g("t1y")) @ qw(rig, "tail1", AX, g("t1p"))
    pb["tail2"].rotation_quaternion = qw(rig, "tail2", AZ, g("t2y")) @ qw(rig, "tail2", AX, g("t2p"))

def catmull(keys, f):
    """keys: sorted [(frame, value)]; smooth curve through keys (flat ends)"""
    if f <= keys[0][0]:
        return keys[0][1]
    if f >= keys[-1][0]:
        return keys[-1][1]
    for i in range(len(keys) - 1):
        f0, v0 = keys[i]
        f1, v1 = keys[i + 1]
        if f0 <= f <= f1:
            t = (f - f0) / (f1 - f0)
            m0 = 0.0 if i == 0 else (v1 - keys[i - 1][1]) / (f1 - keys[i - 1][0]) * (f1 - f0)
            m1 = 0.0 if i + 2 >= len(keys) else (keys[i + 2][1] - v0) / (keys[i + 2][0] - f0) * (f1 - f0)
            t2, t3 = t * t, t * t * t
            return (2 * t3 - 3 * t2 + 1) * v0 + (t3 - 2 * t2 + t) * m0 + (-2 * t3 + 3 * t2) * v1 + (t3 - t2) * m1

def channels(spec, f):
    return {k: catmull(v, f) for k, v in spec.items()}

def lagged(spec, f, src, dst, lag, gain=1.0, extra=None):
    """overlap: channel dst follows channel src `lag` frames late (follow-through on the tips)"""
    return catmull(spec[src], f - lag) * gain + (catmull(extra, f) if extra else 0.0)

def write_action(rig, name, N, posefn, loop):
    act = bpy.data.actions.new(name)
    act.use_fake_user = True
    rig.animation_data.action = act
    for f in range(1, N + 1):
        for pbn in rig.pose.bones:
            pbn.location = (0, 0, 0); pbn.rotation_quaternion = (1, 0, 0, 0); pbn.scale = (1, 1, 1)
        apply(rig, posefn(f))
        for pbn in rig.pose.bones:
            pbn.keyframe_insert("location", frame=f, group=pbn.name)
            pbn.keyframe_insert("rotation_quaternion", frame=f, group=pbn.name)
            pbn.keyframe_insert("scale", frame=f, group=pbn.name)
    act["sq_loop"] = bool(loop)
    act.frame_range = (1, N)
    return act

def fly_pose(t, amp=1.0):
    """0.5 s wingbeat. t in [0,1). Downstroke is the first ~45 %: fast and powerful, upstroke slower."""
    ph = 2 * math.pi * t
    # skewed phase: downstroke faster
    sk = ph + 0.35 * math.sin(ph)
    c = lambda lag: math.cos(sk - lag)
    s_ = lambda lag: math.sin(sk - lag)
    return dict(
        up1=amp * (34 * c(0) + 6), up2=amp * (16 * c(0.6)), up3=amp * (20 * c(1.2) + 2),
        sw1=amp * (-8 * s_(0)), sw2=amp * (6 * s_(0.7)), sw3=amp * (14 * max(0.0, s_(1.6)) - 4),
        tw1=amp * (10 * s_(0.3)), tw2=amp * (8 * s_(0.9)), tw3=amp * (10 * s_(1.5)),
        bz=0.06 * -math.cos(ph - 0.9), by=0.015 * math.sin(ph), bpitch=4 * math.sin(ph - 0.6),
        hpitch=-4 * math.sin(ph - 1.3), jaw=5 + 4 * math.sin(ph - 1.6),
        t1p=10 * math.sin(ph - 1.2), t2p=16 * math.sin(ph - 2.0), t1y=3 * math.sin(ph * 0.0), t2y=0.0)

def build_actions(rig):
    rig.animation_data_create()
    acts = []
    # Fly: 15-frame beat (0.5 s), 16 = 1
    acts.append(write_action(rig, "Fly", 16, lambda f: fly_pose((f - 1) / 15.0), True))
    # Glide: 2 s loop, wings spread with dihedral, gentle bank, tips flutter, head looks around
    def glide(f):
        t = (f - 1) / 60.0
        ph = 2 * math.pi * t
        return dict(up1=8 + 3 * math.sin(ph), up2=-4 + 2 * math.sin(ph - 0.6), up3=-6 + 3 * math.sin(3 * ph - 1.0) + 2 * math.sin(ph - 1.2),
                    sw1=-4, sw3=-6 + 2 * math.sin(2 * ph - 0.5), tw1=-4, tw3=3 * math.sin(3 * ph),
                    up1_l=6 * math.sin(ph), up1_r=-6 * math.sin(ph), up3_l=4 * math.sin(ph - 0.8), up3_r=-4 * math.sin(ph - 0.8),
                    broll=-9 * math.sin(ph), bz=0.035 * math.sin(2 * ph - 0.5), bpitch=-3 + 2 * math.sin(2 * ph),
                    hyaw=12 * math.sin(ph + 0.6), hroll=5 * math.sin(ph - 0.4), hpitch=3 * math.sin(2 * ph), jaw=4,
                    t1y=10 * math.sin(ph - 0.8), t2y=16 * math.sin(ph - 1.6), t1p=-4 + 3 * math.sin(2 * ph - 0.6), t2p=4 * math.sin(2 * ph - 1.4))
    acts.append(write_action(rig, "Glide", 61, glide, True))
    # Swoop: 1.2 s one-shot. Anticipation (rise + pull back, wings high) 1-10, dive 10-21, bite impact 22 (60 %), recover to hover 37
    S = dict(
        bz=[(1, 0), (6, 0.12), (10, 0.22), (17, -0.25), (22, -0.52), (24, -0.50), (29, -0.18), (37, 0)],
        by=[(1, 0), (10, 0.28), (16, -0.25), (22, -0.70), (24, -0.66), (30, -0.20), (37, 0)],
        bpitch=[(1, 0), (10, -24), (15, 18), (21, 38), (23, 30), (27, -14), (32, -4), (37, 0)],
        up1=[(1, 0), (5, 30), (10, 62), (14, 34), (19, 18), (22, 30), (25, -34), (29, 40), (33, -18), (37, 0)],
        sw1=[(1, 0), (10, 8), (16, 40), (21, 52), (23, 40), (26, -6), (31, 4), (37, 0)],
        sw2=[(1, 0), (10, 6), (16, 30), (21, 42), (24, 20), (28, -4), (37, 0)],
        tw1=[(1, 0), (10, -14), (18, 12), (22, 6), (25, 14), (29, -10), (37, 0)],
        jaw=[(1, 4), (12, 2), (18, 30), (21, 44), (22, 46), (24, 0), (27, 8), (31, 3), (37, 4)],
        hpitch=[(1, 0), (10, -12), (17, 10), (21, 14), (23, -12), (26, 6), (31, -3), (37, 0)],
        bscale=[(1, 1.0), (9, 0.94), (13, 1.03), (22, 1.06), (24, 0.95), (28, 1.0), (37, 1.0)],
        t1p=[(1, 0), (10, -20), (16, 22), (21, 30), (25, -10), (30, 6), (37, 0)],
    )
    def swoop(f):
        P = channels(S, f)
        # follow-through: forearm and hand repeat the upper-arm motion 2 and 4 frames late, plus their own fold
        P["up2"] = lagged(S, f, "up1", "up2", 2.0, 0.55, [(1, 0), (10, 10), (16, -26), (21, -34), (24, -6), (37, 0)])
        P["up3"] = lagged(S, f, "up1", "up3", 4.0, 0.6, [(1, 0), (10, 14), (16, -30), (21, -40), (25, 0), (37, 0)])
        P["sw3"] = lagged(S, f, "sw1", "sw3", 3.0, 0.5)
        P["tw3"] = lagged(S, f, "tw1", "tw3", 3.0, 1.0)
        P["t2p"] = lagged(S, f, "t1p", "t2p", 3.0, 1.3)
        P["hpitch"] += 0.0
        return P
    acts.append(write_action(rig, "Swoop", 37, swoop, False))
    # Hit: 0.4 s recoil
    H = dict(by=[(1, 0), (3, 0.16), (6, 0.10), (9, -0.02), (13, 0)], bz=[(1, 0), (3, 0.07), (7, -0.03), (13, 0)],
             bpitch=[(1, 0), (3, -28), (6, -6), (9, 6), (13, 0)], broll=[(1, 0), (3, 14), (7, -6), (13, 0)],
             up1=[(1, 0), (3, 36), (6, -10), (9, 8), (13, 0)], hpitch=[(1, 0), (2, -6), (4, -22), (7, 6), (13, 0)],
             hroll=[(1, 0), (4, -16), (8, 6), (13, 0)], jaw=[(1, 4), (3, 26), (7, 10), (13, 4)],
             bscale=[(1, 1), (3, 0.9), (5, 1.06), (8, 0.98), (13, 1)], t1p=[(1, 0), (3, 18), (7, -10), (13, 0)])
    def hit(f):
        P = channels(H, f)
        P["up2"] = lagged(H, f, "up1", "up2", 1.5, 0.7)
        P["up3"] = lagged(H, f, "up1", "up3", 3.0, 0.9)
        P["t2p"] = lagged(H, f, "t1p", "t2p", 2.0, 1.3)
        return P
    acts.append(write_action(rig, "Hit", 13, hit, False))
    # Death: 1.5 s. Jolt, flailing spiral fall (2 turns), slap into the ground, bounce, wings limp
    DROP = -0.81
    D = dict(bz=[(1, 0), (4, 0.10), (10, -0.05), (20, -0.40), (30, -0.85), (36, DROP - 0.11), (39, DROP + 0.02), (42, DROP + 0.04), (46, DROP)],
             spin=[(1, 0), (5, 10), (14, 160), (24, 420), (33, 640), (36, 700), (46, 704)],
             rad=[(1, 0), (8, 0.22), (24, 0.30), (36, 0.08), (46, 0.08)],
             bpitch=[(1, 0), (4, -30), (12, 20), (22, 40), (34, 30), (36, 8), (39, 14), (42, 6), (46, 6)],
             broll=[(1, 0), (4, 18), (14, -30), (24, 26), (32, -20), (36, 10), (40, 18), (46, 16)],
             jaw=[(1, 4), (4, 34), (14, 20), (24, 32), (36, 40), (39, 18), (46, 26)],
             hroll=[(1, 0), (4, -20), (36, 0), (38, -18), (46, -22)], hpitch=[(1, 0), (4, -24), (20, 10), (37, 18), (46, 12)],
             flail=[(1, 0), (5, 1), (26, 0.8), (34, 0.3), (36, 0)],
             limp=[(1, 0), (34, 0), (36, 1.0), (38, 0.7), (41, 1.0), (46, 1.0)],
             t1p=[(1, 0), (4, 20), (36, 20), (38, -16), (42, -6), (46, -8)])
    def death(f):
        P = channels(D, f)
        a = math.radians(P.pop("spin"))
        r = P.pop("rad")
        P["bx"] = r * math.sin(a)
        P["by"] = r * (1 - math.cos(a))
        P["byaw"] = math.degrees(a)
        fl = P.pop("flail")
        lp = P.pop("limp")
        ph = 2 * math.pi * f / 7.0  # frantic, uneven flaps
        P.update(up1_l=fl * 50 * math.cos(ph), up1_r=fl * 44 * math.cos(ph + 1.1), up2_l=fl * 30 * math.cos(ph - 0.8),
                 up2_r=fl * 34 * math.cos(ph + 0.4), up3_l=fl * 36 * math.cos(ph - 1.6), up3_r=fl * 30 * math.cos(ph - 0.6),
                 sw1=fl * 10 * math.sin(ph))
        # limp on the ground: wings droop and spread flat, tips curl
        P["up1"] = P.get("up1", 0) - 6 * lp
        P["up2"] = P.get("up2", 0) - 8 * lp
        P["up3"] = P.get("up3", 0) - 14 * lp
        P["sw1"] = P.get("sw1", 0) + 8 * lp
        P["t2p"] = catmull(D["t1p"], f - 2) * 1.2
        return P
    acts.append(write_action(rig, "Death", 46, death, False))
    # Spawn: 1 s. Bursts out of the ground wrapped in its wings, unfurls with a shriek, flaps up to hover
    SP = dict(bz=[(1, -1.0), (6, -0.80), (11, -0.45), (14, -0.30), (19, -0.10), (24, 0.06), (31, 0)],
              bscale=[(1, 0.25), (6, 0.75), (10, 1.12), (13, 0.96), (16, 1.02), (31, 1.0)],
              bpitch=[(1, 30), (8, 10), (12, -22), (16, -6), (22, 4), (31, 0)],
              up1=[(1, -70), (9, -64), (12, 58), (15, -30), (19, 40), (23, -24), (27, 20), (31, 0)],
              sw1=[(1, -60), (9, -54), (12, 10), (16, -6), (31, 0)],
              jaw=[(1, 0), (10, 0), (13, 42), (18, 30), (23, 8), (31, 4)],
              hpitch=[(1, 30), (9, 18), (13, -26), (17, -16), (23, 4), (31, 0)],
              t1p=[(1, -50), (10, -40), (14, 20), (20, -8), (31, 0)])
    def spawn(f):
        P = channels(SP, f)
        P["up2"] = lagged(SP, f, "up1", "up2", 2.0, 0.5, [(1, -70), (9, -64), (13, 0), (31, 0)])
        P["up3"] = lagged(SP, f, "up1", "up3", 3.5, 0.55, [(1, -60), (9, -55), (14, 0), (31, 0)])
        P["sw2"] = catmull([(1, -50), (9, -46), (13, 0), (31, 0)], f)
        P["sw3"] = catmull([(1, -40), (10, -38), (14, 0), (31, 0)], f)
        P["t2p"] = lagged(SP, f, "t1p", "t2p", 2.5, 1.2)
        return P
    acts.append(write_action(rig, "Spawn", 31, spawn, False))
    return acts

def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.render.fps = 30
    rig = make_rig()
    img = make_palette(MODEL, PAL, CH + "gravebat_palette.png")
    mats = make_materials(MODEL, img)
    build_bat()
    body = assemble(MODEL, rig, mats)
    acts = build_actions(rig)
    # ground check: lowest vertex at the end of Death
    import bmesh as _bm
    rig.animation_data.action = bpy.data.actions["Death"]
    rig.animation_data.action_slot = bpy.data.actions["Death"].slots[0]
    for f in (34, 36, 39, 42, 46):
        bpy.context.scene.frame_set(f)
        dg = bpy.context.evaluated_depsgraph_get()
        ev = body.evaluated_get(dg)
        zs = [(ev.matrix_world @ v.co).z for v in ev.data.vertices]
        print("DEATH frame", f, "min z", round(min(zs), 3))
    rig.animation_data.action = bpy.data.actions["Fly"]
    rig.animation_data.action_slot = bpy.data.actions["Fly"].slots[0]
    bpy.context.scene.frame_set(1)
    for pb in rig.pose.bones:
        pass
    print("MODEL", MODEL, "TRIS", tri_count(body), "VERTS", len(body.data.vertices), "UNWEIGHTED", check_weights(body),
          "BONES", [b.name for b in rig.data.bones], "ACTIONS", [(a.name, tuple(a.frame_range)) for a in acts])
    img.filepath = "//gravebat_palette.png"
    bpy.ops.wm.save_as_mainfile(filepath=CH + MODEL + ".blend", relative_remap=True)
    export_fbx(CH + MODEL + ".fbx", [rig, body], anims=True)
    print("EXPORTED", CH + MODEL + ".fbx")

if __name__ == "__main__":
    main()
