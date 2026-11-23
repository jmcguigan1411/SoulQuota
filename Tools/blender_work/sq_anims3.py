# Soul Quota - Remaster 3 (2026-11-09): rise from the grave, scythe reap, punchier attacks.
# Reuses the pose builder + remaster pipeline of sq_anims2.py (everything above its styles()) and writes into
# Reaper_Custom.blend. Run AFTER sq_anims2.py (which would otherwise re-create the R2 attacks in their old form).
#   python3 sq_anims3.py            -> all takes below
#   python3 sq_anims3.py --only A,B -> just those
# Then: sq_export.py (Reaper_Custom.fbx) and sq_export_bodies.py (Skeleton_*.fbx).
#
# Attack takes follow the approved five beats (Previews gen7.py): anticipation ~4 f, smear 1-2 f, impact (+ hand pop),
# follow-through ~5 f (overshoot), settle ~6 f. Impact frame = 1 + ImpactFrac * (N - 1) (SQCharacter.cpp AttackStyle).
import bpy, math, sys, copy
from mathutils import Vector, Matrix, Quaternion

src = open("/home/claude/blender_work/sq_anims2.py").read()
exec(compile(src[:src.index("def styles():")], "sq_anims2_helpers", "exec"))

def T(keys, f):
    return track(keys, f)

# ------------------------------------------------------------ build hooks: "post" (per-frame pose hook) + "sink"
# sink: metres the whole body (hips subtree) is pushed below its normal place, AFTER the pose is solved, so the leg IK,
# floor clamp and springs work in body space. Root stays at rest (no root motion).
_build2 = build
def build(base_P, s, name, frame):
    P = _build2(base_P, s, name, frame)
    if s.get("post"):
        s["post"](P, s, frame)
    if s.get("sink"):
        move_subtree(P, "hips", (0.0, 0.0, -s["sink"]))
    return P

def ik2w(P, upper, lower, end, target, pole, w=1.0):
    S = head_w(P, upper)
    L1 = (head_w(P, lower) - S).length
    L2 = (head_w(P, end) - head_w(P, lower)).length
    D = Vector(target) - S
    d = max(0.05, min(D.length, (L1 + L2) * 0.999))
    dn = D.normalized()
    a = max(-1.0, min(1.0, (L1 * L1 + d * d - L2 * L2) / (2 * L1 * d)))
    pv = Vector(pole) - S
    perp = pv - dn * pv.dot(dn)
    perp = perp.normalized() if perp.length > 1e-6 else Vector((0, 0, -1))
    E_ = S + dn * (L1 * a) + perp * (L1 * math.sqrt(1 - a * a))
    aim(P, upper, E_ - S, w)
    aim(P, lower, Vector(target) - head_w(P, lower), w)

def slot_axes(P, slot):
    M = (A @ P[slot].to_3x3()).normalized()
    return M.col[0], M.col[1], M.col[2], A @ P[slot].translation

def align_slot(P, hand, slot, z_world, y_world, w=1.0):
    """rotate hand so its weapon slot's +Z (scythe pole) points along z_world and +Y (blade) toward y_world"""
    z = Vector(z_world).normalized()
    y = Vector(y_world)
    y = (y - z * y.dot(z)).normalized()
    x = y.cross(z)
    Tm = Matrix((x, y, z)).transposed()
    S = (A @ P[slot].to_3x3()).normalized()
    q = Quaternion().slerp((Tm @ S.inverted()).to_quaternion(), w)
    rot_subtree(P, hand, q, head_w(P, hand))

# ------------------------------------------------------------ five-beat key builder
# tpl: frames of the beats, e.g. dict(a0=3, A=7, S=[8], I=9, F=13, R=19, N=24)
#   a0 = anticipation starts (neutral until here), A = anticipation extreme, S = smear frame(s), I = impact,
#   F = follow-through extreme (overshoot), R = settled recovery pose, N = last frame (neutral again).
def beat_keys(tpl, n, a, i, f, r, s=None):
    S = tpl["S"]
    if s is None:
        fr = {1: [0.45], 2: [0.25, 0.6]}[len(S)]
        s = [a + k * (i - a) for k in fr]
    elif not isinstance(s, (list, tuple)):
        s = [s]
    keys = [(1, n, "s")]
    if tpl["a0"] > 1:
        keys.append((tpl["a0"], n, "s"))
    keys.append((tpl["A"], a, "s"))
    for j, (fs, vs) in enumerate(zip(S, s)):
        keys.append((fs, vs, "i" if j == 0 else "l"))
    keys.append((tpl["I"], i, "l"))
    keys.append((tpl["F"], f, "o"))
    if tpl["R"] < tpl["N"]:
        keys.append((tpl["R"], r, "s"))
    keys.append((tpl["N"], n, "s"))
    return keys

class Beats:
    def __init__(self, tpl):
        self.t = tpl
    def __call__(self, f, n, a, i, fo, r, s=None):
        return track(beat_keys(self.t, n, a, i, fo, r, s), f)

def step_keys(fr_lift, fr_plant, d, fr_back0, fr_back1):
    """lead-foot step: planted at 0 until fr_lift, lands at d on fr_plant (impact), planted until fr_back0, back by fr_back1"""
    dy = [(1, 0, "s"), (fr_lift, 0, "s"), (fr_plant, d, "o"), (fr_back0, d, "s"), (fr_back1, 0, "s")]
    mid = 0.5 * (fr_lift + fr_plant)
    bm = 0.5 * (fr_back0 + fr_back1)
    dz = [(1, 0, "s"), (fr_lift, 0, "s"), (mid, 0.075, "s"), (fr_plant, 0, "c"), (fr_back0, 0, "s"), (bm, 0.045, "s"), (fr_back1, 0, "s")]
    return dy, dz

DONE = []
def shot3(name, N, cfg=None, base="Idle"):
    def deco(fn):
        if ONLY and name not in ONLY:
            return fn
        act = make_core(name, base, N, fn, False, cfg)
        act["sq_remaster"] = 3
        DONE.append(name)
        return fn
    return deco

NEU = spec()
def arm_b(B, f, n, a, i, fo, r, s=None):
    """arm from five-beat values; a/i/fo/r/s are dicts of arm params (missing keys = neutral)"""
    out = {}
    for k in ("sw", "ab", "el", "fin", "tw"):
        nv = n.get(k, NEU["arms"]["l"][k])
        g = lambda d: d.get(k, nv)
        sv = None if s is None else ([g(x) for x in s] if isinstance(s, (list, tuple)) else g(s))
        out[k] = B(f, nv, g(a), g(i), g(fo), g(r), sv)
    return out

def chan(B, f, base, a, i, fo, r, s=None):
    """3-vector channel (pitch, roll, yaw) or hip (x, y, z) from five-beat triples"""
    res = []
    for k in range(3):
        sv = None if s is None else ([x[k] for x in s] if isinstance(s[0], (list, tuple)) else s[k])
        res.append(B(f, base[k], a[k], i[k], fo[k], r[k], sv))
    return res

# ============================================================ 1. RISE FROM THE GRAVE (49 f = 1.6 s)
# Beats: f19 (0.6 s) right hand bursts up through the ground, f28 (0.9 s) body dragged up on planted hands,
# f37 (1.2 s) climbed out + shake off the dirt, f49 (1.6 s) hero pose. Feet end in the idle stance.
RISE_N = 49
RISE_SINK = [(1, 1.50, "s"), (12, 1.46, "s"), (19, 1.19, "s"), (22, 1.13, "s"), (24, 0.99, "s"), (28, 0.32, "s"),
             (31, 0.14, "s"), (34, 0.0, "s"), (49, 0.0, "s")]
RISE_HANDS = {"l": Vector((0.24, -0.30, 0.075)), "r": Vector((-0.24, -0.30, 0.075))}   # palms planted on the surface

def rise_post(P, s, f):
    w = s.get("ikw", 0.0)
    if w <= 1e-4:
        return
    for sd in "lr":
        tgt = RISE_HANDS[sd] + Vector((0, 0, s["sink"]))          # body space (before the sink is applied)
        sh = head_w(P, "upperarm." + sd)
        ik2w(P, "upperarm." + sd, "lowerarm." + sd, "wrist." + sd, tgt, sh + Vector((SX[sd] * 0.45, 0.55, 0.15)), w)
        # palm flat on the ground, fingers forward
        align_slot(P, "hand." + sd, "handslot." + sd, Vector((0, -1, -0.15)), Vector((-SX[sd], 0, 0)), w)

@shot3("SQ_Rise_FromGrave", RISE_N, dict(exag=(1.0, 1.0, 1.0), pop=[(19, "r", 0.22), (23, "l", 0.15), (25, "lr", 0.10)],
                                         lag={"hr": 0.0, "sp": 0.0, "ch": 0.5, "hd": 1.0}, head=0.5))
def rise(f):
    s = spec()
    sink = T(RISE_SINK, f)
    s["sink"] = sink
    s["post"] = rise_post
    s["ikw"] = T([(1, 0, "s"), (21, 0, "s"), (24, 1, "s"), (30, 1, "s"), (34, 0, "s")], f)
    # shake-off-the-dirt oscillation (dog shake), f35-43
    env = T([(1, 0, "s"), (34, 0, "s"), (36, 1, "s"), (40, 1, "s"), (43, 0, "s")], f)
    ph = 2 * math.pi * (f - 34) / 4.0
    shk = math.sin(ph) * env
    hero = T([(1, 0, "s"), (41, 0, "s"), (45, 1, "k"), (49, 1, "s")], f)
    # torso: curled underground -> roll away from the bursting arm -> lean over the hands -> straighten -> hero
    s["hip"] = [0.0,
                T([(1, 0.02, "s"), (19, 0.02, "s"), (26, -0.06, "s"), (31, -0.06, "s"), (37, 0.0, "s"), (45, 0.01, "s"), (49, 0.0, "s")], f),
                T([(1, -0.24, "s"), (19, -0.22, "s"), (26, -0.20, "s"), (31, -0.16, "s"), (35, -0.06, "s"), (38, -0.04, "s"),
                   (41, -0.07, "s"), (45, -0.02, "k"), (49, -0.02, "s")], f)]
    s["hr"] = [T([(1, 35, "s"), (19, 33, "s"), (24, 28, "s"), (28, 16, "s"), (31, 12, "s"), (36, 4, "s"), (41, 2, "s"), (45, 0, "s"), (49, 0, "s")], f),
               -5 * shk, 0.0]
    s["sp"] = [T([(1, 30, "s"), (12, 30, "s"), (19, 30, "s"), (24, 30, "s"), (28, 24, "s"), (31, 22, "s"), (36, 6, "s"),
                  (41, 8, "s"), (45, -6, "k"), (49, -5, "s")], f),
               T([(1, 0, "s"), (12, 2, "s"), (19, 16, "s"), (23, 10, "s"), (26, 0, "s"), (49, 0, "s")], f) + 6 * shk, 0.0]
    s["ch"] = [T([(1, 12, "s"), (19, 10, "s"), (24, 10, "s"), (28, 8, "s"), (31, 6, "s"), (36, 2, "s"), (41, 4, "s"), (45, -12, "k"), (49, -10, "s")], f),
               T([(1, 0, "s"), (12, 0, "s"), (19, 10, "s"), (24, 4, "s"), (27, 0, "s"), (49, 0, "s")], f) + 14 * shk,
               T([(1, 0, "s"), (41, 0, "s"), (45, -6, "s"), (49, -6, "s")], f) + 10 * math.sin(ph + 0.8) * env]
    s["hd"] = [T([(1, 24, "s"), (12, 24, "s"), (19, 10, "s"), (24, -8, "s"), (28, -26, "s"), (31, -14, "s"), (36, 6, "s"),
                  (41, 4, "s"), (45, -10, "k"), (49, -8, "s")], f),
               -10 * shk,
               T([(1, 0, "s"), (19, 0, "s"), (22, 10, "s"), (26, 0, "s"), (41, 0, "s"), (45, 10, "s"), (49, 10, "s")], f) + 22 * math.sin(ph + 1.6) * env]
    # arms (FK; the IK post-hook plants both palms on the surface f25-31 while the body is hauled up)
    fold = arm(sw=40, ab=6, el=128, fin=20)
    reach_up = arm(sw=252, ab=22, el=4, tw=20)        # chest pitched ~75: world-up
    plant = arm(sw=70, ab=28, el=30, tw=-20)
    side = arm(sw=12, ab=34, el=34)
    hero_r = arm(sw=150, ab=34, el=24, tw=30)
    hero_l = arm(sw=-16, ab=44, el=104, fin=20, tw=-30)
    def mix(seq):
        """seq: [(frame, arm-dict, ease)] -> arm dict at f"""
        return {k: T([(fr_, a_[k], e_) for fr_, a_, e_ in seq], f) for k in fold}
    s["arms"]["r"] = mix([(1, fold, "s"), (12, fold, "s"), (16, arm(sw=150, ab=12, el=70, tw=10), "s"), (19, reach_up, "x"),
                          (21, arm(sw=244, ab=28, el=16, tw=-10), "s"), (23, arm(sw=248, ab=24, el=8, tw=20), "s"), (25, plant, "c"),
                          (31, plant, "s"), (34, side, "s"), (41, side, "s"), (45, hero_r, "k"), (49, hero_r, "s")])
    s["arms"]["l"] = mix([(1, fold, "s"), (19, fold, "s"), (21, arm(sw=150, ab=12, el=70), "s"), (23, reach_up, "x"), (25, plant, "c"),
                          (31, plant, "s"), (34, side, "s"), (41, side, "s"), (45, hero_l, "k"), (49, hero_l, "s")])
    for sd in "lr":
        a_ = s["arms"][sd]
        a_["ab"] += 16 * math.sin(ph + (0 if sd == "l" else math.pi)) * env
        a_["el"] += 14 * math.sin(ph + 0.5) * env
    s["arms"]["r"]["wf"] = 18 * math.sin(f * 1.9) * T([(1, 0, "s"), (19, 0, "s"), (20, 1, "s"), (23, 1, "s"), (24, 0, "s")], f)
    # legs: underground they just stay in stance (moved down with the body). Left foot climbs onto the surface
    # (planted from f31 on its idle spot: local dz = sink keeps it fixed in the world), right foot follows f32-35.
    lz = T([(1, 0, "s"), (27, 0, "s"), (29.5, 0.42, "s"), (31, T(RISE_SINK, 31), "c")], f) if f < 31 else sink
    rz = T([(1, 0, "s"), (32, 0, "s"), (33.5, T(RISE_SINK, 33.5) + 0.09, "s"), (35, 0, "c")], f)
    # feet emerge vertically onto the KayKit Idle footprints (ankle + toe direction of Idle f1), so the hand-off to idle is seamless
    s["legs"]["l"] = leg(dx=0.065, dy=0.046, fy=19.3, dz=lz, fp=T([(1, 0, "s"), (28, 0, "s"), (29.5, -12, "s"), (31, 0, "s"), (49, 0, "s")], f), kout=0.22)
    s["legs"]["r"] = leg(dx=-0.021, dy=0.002, fy=11.7, dz=rz, fp=T([(1, 0, "s"), (32, 0, "s"), (33.5, -10, "s"), (35, 0, "s"), (49, 0, "s")], f), kout=0.18)
    return s

# ============================================================ 2. SCYTHE REAP (21 f = 0.67 s, impact f9 = 40%)
REAP_T = dict(a0=2, A=6, S=[7, 8], I=9, F=13, R=19, N=21)
REAP_W = [(1, 0, "s"), (4, 1, "s"), (17, 1, "s"), (21, 0, "s")]

def reap_post(P, s, f):
    w = T(REAP_W, f)
    if w <= 1e-4:
        return
    fwd = bone_dir_w(P, "lowerarm.r")
    fwd.z = 0.0
    pole = (fwd.normalized() + Vector((0, 0, 0.10))).normalized()
    sweep = Vector((0, 0, 1)).cross(pole)              # counter-clockwise from above: right -> front -> left
    align_slot(P, "hand.r", "handslot.r", pole, sweep + Vector((0, 0, -0.25)), w)
    x, y, z, g = slot_axes(P, "handslot.r")
    hx, hy, hz, hg = slot_axes(P, "handslot.l")
    off = head_w(P, "wrist.l") - hg
    tgt = g + z * 0.24 + off
    ik2w(P, "upperarm.l", "lowerarm.l", "wrist.l", tgt, head_w(P, "chest") + Vector((0.6, 0.3, -0.6)), w)
    align_slot(P, "hand.l", "handslot.l", z, y, w)

@shot3("SQ_Reap_Swing", 21, dict(pop=[(9, "r", 0.28)], exag=(1.12, 1.15, 1.0), lag={"hr": -1.0, "ch": 0.3, "hd": 1.0}))
def reap(f):
    B = Beats(REAP_T)
    s = spec()
    s["hip"] = chan(B, f, [0, 0, -0.02], [0, 0.05, -0.11], [0, -0.07, -0.13], [0, -0.08, -0.14], [0, -0.04, -0.08],
                    s=[[0, 0.02, -0.08], [0, -0.03, -0.07]])
    s["hr"] = chan(B, f, [0, 0, 0], [-2, 0, -38], [6, 0, 20], [8, 0, 38], [4, 0, 16], s=[[0, 0, -18], [3, 0, 0]])
    s["sp"] = chan(B, f, [3, 0, 0], [6, 0, -20], [14, 0, 12], [16, 0, 22], [8, 0, 8])
    s["ch"] = chan(B, f, [2, 0, 0], [4, -10, -34], [8, 0, 14], [10, 8, 36], [4, 2, 12])
    s["hd"] = chan(B, f, [0, 0, 0], [6, 0, 30], [-8, 0, -10], [-6, 0, -30], [0, 0, -10])
    n = arm()
    s["arms"]["r"] = arm_b(B, f, n, dict(sw=60, ab=115, el=60, tw=-20), dict(sw=88, ab=4, el=4, tw=20),
                           dict(sw=80, ab=-55, el=10, tw=30), dict(sw=55, ab=-25, el=25, tw=10),
                           s=[dict(sw=75, ab=85, el=25), dict(sw=85, ab=45, el=10)])
    s["arms"]["l"] = arm_b(B, f, n, dict(sw=55, ab=-40, el=80), dict(sw=70, ab=-10, el=40), dict(sw=60, ab=20, el=40),
                           dict(sw=40, ab=10, el=40))
    dy, dz = step_keys(5, 9, -0.12, 17, 20)
    s["legs"]["l"] = leg(dy=T(dy, f), dz=T(dz, f))
    s["legs"]["r"] = leg(fp=T([(1, 0, "s"), (8, 0, "s"), (11, 16, "o"), (17, 10, "s"), (21, 0, "s")], f), tp=0)
    s["post"] = reap_post
    return s

# ============================================================ 3. ATTACKS, five beats (R2 remastered in place + R3)
@shot3("SQ_Attack_Slash_R2", 24, dict(pop=[(9, "r", 0.25)], lag={"hr": -1.5, "ch": 0.3, "hd": 1.0}))
def slash(f):
    # 1H horizontal slash right-to-left. Slash ImpactFrac 0.35 -> f9 (1 + 0.35 * 23 = 9.05)
    B = Beats(dict(a0=3, A=7, S=[8], I=9, F=13, R=19, N=24))
    s = spec()
    s["hip"] = chan(B, f, [0, 0, -0.02], [0, 0.045, -0.09], [0, -0.08, -0.12], [0, -0.09, -0.12], [0, -0.06, -0.08], s=[0, -0.01, -0.06])
    s["hr"] = chan(B, f, [0, 0, 0], [-4, 0, -30], [6, 0, 16], [8, 0, 30], [4, 0, 14])
    s["sp"] = chan(B, f, [3, 0, 0], [-2, 0, -16], [14, 0, 10], [16, 0, 18], [8, 0, 8])
    s["ch"] = chan(B, f, [2, 0, 0], [-2, -8, -30], [8, 4, 12], [10, 6, 32], [5, 2, 16])
    s["hd"] = chan(B, f, [0, 0, 0], [8, 0, 26], [-6, 0, -12], [-2, 0, -26], [0, 0, -10])
    n = arm()
    s["arms"]["r"] = arm_b(B, f, n, dict(sw=42, ab=105, el=80, tw=-45), dict(sw=88, ab=2, el=2, tw=30),
                           dict(sw=76, ab=-50, el=14, tw=40), dict(sw=50, ab=-28, el=30, tw=15), s=dict(sw=70, ab=55, el=25, tw=0))
    s["arms"]["l"] = arm_b(B, f, n, dict(sw=62, ab=18, el=90), dict(sw=25, ab=35, el=40), dict(sw=-22, ab=40, el=30),
                           dict(sw=-5, ab=25, el=25))
    dy, dz = step_keys(5, 9, -0.15, 19, 23)
    s["legs"]["l"] = leg(dy=T(dy, f), dz=T(dz, f))
    s["legs"]["r"] = leg(fp=T([(1, 0, "s"), (8, 0, "s"), (11, 18, "o"), (19, 12, "s"), (24, 0, "s")], f), tp=0)
    return s

@shot3("SQ_Attack_Chop_R2", 30, dict(pop=[(13, "lr", 0.25)], exag=(1.1, 1.1, 1.0), nod=40.0))
def chop(f):
    # 2H overhead chop. Chop ImpactFrac 0.42 -> f13 (1 + 0.42 * 29 = 13.2); enemies (45% = f14) still land in the
    # impact hold (blade bottoms out f13-14). Up on the toes at the top, 2-frame smear, stomp.
    B = Beats(dict(a0=5, A=10, S=[11, 12], I=13, F=18, R=24, N=30))
    s = spec()
    pre = T([(1, 0, "s"), (3, 0, "s"), (5, 1, "s"), (8, 0, "s")], f)    # tiny dip before the rise
    hip = chan(B, f, [0, 0, -0.02], [0, 0.05, 0.015], [0, -0.10, -0.16], [0, -0.10, -0.17], [0, -0.07, -0.10],
               s=[[0, 0.03, -0.01], [0, -0.04, -0.06]])
    hip[2] -= 0.05 * pre
    s["hip"] = hip
    s["sp"] = chan(B, f, [3, 0, 0], [-18, 0, 0], [18, 0, 0], [22, 0, 0], [10, 0, 0])
    s["sp"][0] += 8 * pre
    s["ch"] = chan(B, f, [2, 0, 0], [-14, 0, 6], [10, 0, -3], [14, 0, -4], [6, 0, 0])
    s["hd"] = chan(B, f, [0, 0, 0], [-24, 0, 0], [-6, 0, 0], [2, 0, 0], [0, 0, 0])
    n = arm()
    for sd in "lr":
        s["arms"][sd] = arm_b(B, f, n, dict(sw=175, ab=20, el=80, fin=18, tw=40), dict(sw=60, ab=6, el=3, fin=30, tw=10),
                              dict(sw=38, ab=6, el=12, fin=24), dict(sw=50, ab=10, el=20, fin=10),
                              s=[dict(sw=150, ab=16, el=40, fin=22, tw=30), dict(sw=105, ab=10, el=12, fin=26, tw=20)])
        s["arms"][sd]["sw"] += 30 * pre
        s["arms"][sd]["el"] += 30 * pre
    dy, dz = step_keys(9, 13, -0.16, 25, 28)
    s["legs"]["l"] = leg(dy=T(dy, f), dz=T(dz, f))
    s["legs"]["r"] = leg(fp=T([(1, 0, "s"), (12, 0, "s"), (15, 20, "o"), (24, 12, "s"), (29, 0, "s")], f), tp=0)
    s["w"] = 0.18
    return s

@shot3("SQ_Attack_Punch_R2", 24, dict(pop=[(11, "r", 0.25)]))
def punch(f):
    # straight lunging thrust-punch (enemies: impact at 45% = f11, 1 + 0.45 * 23 = 11.35)
    B = Beats(dict(a0=5, A=9, S=[10], I=11, F=15, R=21, N=24))
    s = spec()
    s["hip"] = chan(B, f, [0, 0, -0.02], [0, 0.05, -0.08], [0, -0.09, -0.11], [0, -0.10, -0.12], [0, -0.06, -0.08], s=[0, -0.01, -0.07])
    s["hr"] = chan(B, f, [0, 0, 0], [0, 0, -26], [4, 0, 18], [6, 0, 22], [2, 0, 8])
    s["sp"] = chan(B, f, [3, 0, 0], [0, 0, -16], [16, 0, 14], [18, 0, 16], [8, 0, 6])
    s["ch"] = chan(B, f, [2, 0, 0], [0, 0, -30], [8, 0, 24], [10, 0, 28], [4, 0, 10])
    s["hd"] = chan(B, f, [0, 0, 0], [6, 0, 26], [-10, 0, -20], [-8, 0, -22], [-2, 0, -8])
    n = arm()
    s["arms"]["r"] = arm_b(B, f, n, dict(sw=-30, ab=30, el=125, tw=0), dict(sw=90, ab=-6, el=0, tw=85),
                           dict(sw=94, ab=-8, el=6, tw=85), dict(sw=40, ab=12, el=50, tw=30), s=dict(sw=40, ab=10, el=60, tw=40))
    s["arms"]["l"] = arm_b(B, f, n, dict(sw=75, ab=10, el=115), dict(sw=30, ab=30, el=110), dict(sw=15, ab=30, el=100),
                           dict(sw=20, ab=20, el=70))
    dy, dz = step_keys(6, 11, -0.17, 20, 23)
    s["legs"]["l"] = leg(dy=T(dy, f), dz=T(dz, f))
    s["legs"]["r"] = leg(fp=T([(1, 0, "s"), (10, 0, "s"), (13, 24, "o"), (20, 14, "s"), (24, 0, "s")], f), tp=0)
    return s

@shot3("SQ_Cast_Shoot_R2", 20, dict(pop=[(7, "r", 0.25)]))
def cast_shoot(f):
    # one-hand blast. Cast ImpactFrac 0.30 -> f7 (1 + 0.30 * 19 = 6.7). Recoil kicks the hand up after release.
    B = Beats(dict(a0=1, A=5, S=[6], I=7, F=11, R=17, N=20))
    s = spec()
    s["hip"] = chan(B, f, [0, 0, -0.02], [0, 0.04, -0.08], [0, -0.06, -0.08], [0, -0.01, -0.06], [0, -0.02, -0.04])
    s["sp"] = chan(B, f, [3, 0, 0], [-8, 0, -16], [12, 0, 12], [0, 0, 8], [4, 0, 4])
    s["ch"] = chan(B, f, [2, 0, 0], [-4, 0, -20], [6, 0, 16], [-4, 0, 10], [2, 0, 4])
    s["hd"] = chan(B, f, [0, 0, 0], [8, 0, 12], [-8, 0, -6], [-14, 0, -4], [-4, 0, 0])
    n = arm()
    s["arms"]["r"] = arm_b(B, f, n, dict(sw=40, ab=35, el=130, tw=40), dict(sw=92, ab=2, el=0, tw=-65),
                           dict(sw=115, ab=6, el=22, tw=-60), dict(sw=60, ab=12, el=25, tw=-20), s=dict(sw=75, ab=12, el=50, tw=-20))
    s["arms"]["l"] = arm_b(B, f, n, dict(sw=35, ab=20, el=75), dict(sw=-15, ab=38, el=30), dict(sw=-10, ab=30, el=30),
                           dict(sw=0, ab=15, el=20))
    dy, dz = step_keys(3, 7, -0.11, 15, 18)
    s["legs"]["l"] = leg(dy=T(dy, f), dz=T(dz, f))
    return s

@shot3("SQ_Cast_Raise_R2", 30, dict(pop=[(14, "lr", 0.25)]))
def cast_raise(f):
    # gather low, fling both arms to the sky. Raise ImpactFrac 0.45 -> f14 (1 + 0.45 * 29 = 14.05). Trembling hold.
    B = Beats(dict(a0=6, A=11, S=[12, 13], I=14, F=18, R=24, N=30))
    s = spec()
    tr = math.sin(f * 2.7) * T([(1, 0, "s"), (14, 0, "s"), (15, 1, "s"), (21, 1, "s"), (24, 0, "s")], f)
    s["hip"] = chan(B, f, [0, 0, -0.02], [0, 0.02, -0.13], [0, 0, 0.035], [0, 0, 0.045], [0, 0, 0.0], s=[[0, 0.01, -0.06], [0, 0, 0.0]])
    s["sp"] = chan(B, f, [3, 0, 0], [18, 0, 0], [-10, 0, 0], [-12, 0, 0], [-6, 0, 0])
    s["sp"][0] += 2 * tr
    s["ch"] = chan(B, f, [2, 0, 0], [10, 0, 0], [-10, 0, 0], [-12, 0, 0], [-5, 0, 0])
    s["hd"] = chan(B, f, [0, 0, 0], [14, 0, 0], [-18, 0, 0], [-22, 0, 0], [-12, 0, 0])
    s["hd"][1] += 4 * tr
    n = arm()
    for sd in "lr":
        s["arms"][sd] = arm_b(B, f, n, dict(sw=30, ab=-14, el=95, fin=50), dict(sw=168, ab=34, el=6, tw=-60),
                              dict(sw=160, ab=36, el=10, tw=-60), dict(sw=158, ab=30, el=12, tw=-50),
                              s=[dict(sw=70, ab=0, el=60, fin=30, tw=-20), dict(sw=130, ab=20, el=20, fin=10, tw=-45)])
        s["arms"][sd]["sw"] += 3 * tr
    s["legs"] = {sd: leg(fp=T([(1, 0, "s"), (12, 0, "s"), (15, 22, "o"), (22, 14, "s"), (28, 0, "s")], f), tp=0) for sd in "lr"}
    return s

@shot3("SQ_Throw_R2", 24, dict(pop=[(8, "r", 0.22)]))
def throw(f):
    # overhand throw. Throw ImpactFrac 0.30 -> f8 (1 + 0.30 * 23 = 7.9): arm cocked behind the head, step, whip
    B = Beats(dict(a0=2, A=6, S=[7], I=8, F=12, R=18, N=24))
    s = spec()
    s["hip"] = chan(B, f, [0, 0, -0.02], [0, 0.06, -0.06], [0, -0.08, -0.09], [0, -0.10, -0.11], [0, -0.06, -0.07], s=[0, -0.01, -0.05])
    s["hr"] = chan(B, f, [0, 0, 0], [0, 0, -24], [0, 0, 12], [0, 0, 20], [0, 0, 8])
    s["sp"] = chan(B, f, [3, 0, 0], [-14, 0, -14], [10, 0, 10], [18, 0, 12], [8, 0, 4])
    s["ch"] = chan(B, f, [2, 0, 0], [-12, 0, -26], [10, 0, 16], [14, 0, 24], [5, 0, 8])
    s["hd"] = chan(B, f, [0, 0, 0], [-10, 0, 20], [-8, 0, -8], [-14, 0, -10], [-4, 0, -4])
    n = arm()
    s["arms"]["r"] = arm_b(B, f, n, dict(sw=165, ab=40, el=130, tw=50), dict(sw=95, ab=10, el=6, tw=-20),
                           dict(sw=15, ab=-22, el=20, tw=-20), dict(sw=10, ab=0, el=20), s=dict(sw=135, ab=25, el=70, tw=15))
    s["arms"]["l"] = arm_b(B, f, n, dict(sw=100, ab=20, el=10), dict(sw=40, ab=30, el=40), dict(sw=-25, ab=30, el=30),
                           dict(sw=-5, ab=15, el=20))
    dy, dz = step_keys(3, 8, -0.17, 19, 23)
    s["legs"]["l"] = leg(dy=T(dy, f), dz=T(dz, f))
    s["legs"]["r"] = leg(fp=T([(1, 0, "s"), (7, 0, "s"), (10, 30, "o"), (18, 20, "s"), (23, 0, "s")], f), tp=0)
    return s

@shot3("SQ_Attack_Slam_R3", 31, dict(pop=[(16, "lr", 0.28)], exag=(1.1, 1.0, 1.0), nod=60.0))
def slam(f):
    # leap slam. Slam ImpactFrac 0.50 -> f16 (1 + 0.50 * 30 = 16): crouch f3-7, take off f9, apex f12 with the weapon
    # overhead, smear f15 on the way down, both feet land + weapon hits f16, deep compression f18, settle by f26.
    s = spec()
    air = T([(1, 0, "s"), (8.6, 0, "s"), (12, 1, "o"), (16, 0, "i"), (31, 0, "s")], f)       # feet off the ground
    s["hip"] = [0.0,
                T([(1, 0, "s"), (3, 0, "s"), (7, 0.04, "s"), (12, -0.01, "s"), (16, -0.07, "s"), (19, -0.08, "s"), (26, -0.04, "s"), (31, 0, "s")], f),
                T([(1, -0.02, "s"), (3, -0.02, "s"), (7, -0.17, "s"), (9, 0.03, "x"), (12, 0.31, "o"), (15, 0.12, "i"), (16, -0.17, "l"),
                   (18, -0.22, "o"), (21, -0.15, "s"), (26, -0.07, "s"), (31, -0.02, "s")], f)]
    s["sp"] = [T([(1, 3, "s"), (3, 3, "s"), (7, 30, "s"), (9, 4, "s"), (12, -16, "s"), (15, 10, "i"), (16, 32, "l"), (18, 36, "o"),
                  (24, 16, "s"), (31, 3, "s")], f), 0, 0]
    s["ch"] = [T([(1, 2, "s"), (7, 14, "s"), (12, -14, "s"), (15, 4, "i"), (16, 14, "l"), (18, 18, "o"), (24, 8, "s"), (31, 2, "s")], f), 0, 0]
    s["hd"] = [T([(1, 0, "s"), (7, -14, "s"), (12, -26, "s"), (16, -12, "s"), (18, -6, "s"), (24, -4, "s"), (31, 0, "s")], f), 0, 0]
    for sd in "lr":
        s["arms"][sd] = arm(
            sw=T([(1, 5, "s"), (3, 5, "s"), (7, -45, "s"), (9, 60, "o"), (12, 178, "s"), (14, 165, "s"), (15, 115, "i"), (16, 48, "l"),
                  (18, 32, "o"), (24, 45, "s"), (31, 5, "s")], f),
            ab=T([(1, 10, "s"), (7, 24, "s"), (12, 18, "s"), (16, 10, "s"), (24, 14, "s"), (31, 10, "s")], f),
            el=T([(1, 15, "s"), (7, 20, "s"), (12, 80, "s"), (14, 70, "s"), (15, 25, "i"), (16, 3, "l"), (19, 12, "s"), (31, 15, "s")], f),
            fin=T([(1, 0, "s"), (12, 20, "s"), (16, 30, "s"), (31, 0, "s")], f),
            tw=T([(1, 0, "s"), (12, 40, "s"), (16, 10, "s"), (31, 0, "s")], f))
        s["legs"][sd] = leg(dz=0.24 * air,
                            fp=T([(1, 0, "s"), (6, 0, "s"), (8.6, 32, "s"), (10, 30, "s"), (13, 10, "s"), (15.4, 4, "s"), (16, 0, "s"), (31, 0, "s")], f),
                            tp=T([(1, 0, "s"), (8.6, 0, "s"), (10, -14, "s"), (14, -6, "s"), (16, 0, "s")], f), kout=0.22)
    s["w"] = 0.18
    return s

@shot3("SQ_Attack_Thrust_R3", 24, dict(pop=[(9, "r", 0.25)]))
def thrust(f):
    # lunge thrust (spear, beak, rapier). Stab ImpactFrac 0.35 -> f9 (1 + 0.35 * 23 = 9.05). Rock back, big lunge
    # with the left foot (30 cm), arm fully extended at f9, overshoot reach f13, recover the foot f19-23.
    B = Beats(dict(a0=3, A=7, S=[8], I=9, F=13, R=18, N=24))
    s = spec()
    s["hip"] = chan(B, f, [0, 0, -0.02], [0, 0.08, -0.06], [0, -0.17, -0.14], [0, -0.19, -0.15], [0, -0.14, -0.11], s=[0, -0.06, -0.07])
    # hold the lunge until the foot comes back
    if f > 18:
        k = T([(18, 0, "s"), (19, 0, "s"), (23, 1, "s"), (24, 1, "s")], f)
        s["hip"][1] = lerp(-0.14, 0.0, k)
        s["hip"][2] = lerp(-0.11, -0.02, k)
    s["hr"] = chan(B, f, [0, 0, 0], [-6, 0, -20], [10, 0, 14], [12, 0, 16], [6, 0, 8])
    s["sp"] = chan(B, f, [3, 0, 0], [-8, 0, -12], [20, 0, 10], [24, 0, 12], [12, 0, 4])
    s["ch"] = chan(B, f, [2, 0, 0], [-6, 0, -22], [10, 0, 18], [12, 0, 22], [6, 0, 8])
    s["hd"] = chan(B, f, [0, 0, 0], [8, 0, 18], [-16, 0, -16], [-18, 0, -18], [-8, 0, -6])
    n = arm()
    s["arms"]["r"] = arm_b(B, f, n, dict(sw=30, ab=40, el=135, tw=20), dict(sw=92, ab=-4, el=0, tw=0),
                           dict(sw=96, ab=-6, el=2, tw=0), dict(sw=70, ab=10, el=40, tw=0), s=dict(sw=70, ab=15, el=60, tw=10))
    s["arms"]["l"] = arm_b(B, f, n, dict(sw=70, ab=15, el=80), dict(sw=-30, ab=30, el=20), dict(sw=-38, ab=30, el=15),
                           dict(sw=-15, ab=20, el=25))
    dy, dz = step_keys(4, 9, -0.30, 19, 23)
    dz = [(fr_, v * (1.2 if v > 0 else 1.0), e) for fr_, v, e in dz]
    s["legs"]["l"] = leg(dy=T(dy, f), dz=T(dz, f))
    s["legs"]["r"] = leg(fp=T([(1, 0, "s"), (8, 0, "s"), (11, 26, "o"), (18, 18, "s"), (22, 0, "s")], f), tp=0)
    return s

# ============================================================ 4. importer marker (2-frame KayKit Idle copy)
if not ONLY or "SQ_Marker_Remaster3" in ONLY:
    idle = ACT["Idle"]
    act = write_action("SQ_Marker_Remaster3", idle, [eval_pose(idle, 1), eval_pose(idle, 2)])
    act["sq_loop"] = False
    DONE.append("SQ_Marker_Remaster3")

for k, v in STATS.items():
    print("HIPDROP", k, "frames", v[0], "max", round(v[1], 3))
set_action(ACT["Idle"])
sc.frame_set(1)
bpy.ops.wm.save_as_mainfile(filepath=BLEND)
print("DONE", DONE)
print("NACTIONS", len(bpy.data.actions), "SQ", len([a for a in bpy.data.actions if a.name.startswith("SQ_")]))
