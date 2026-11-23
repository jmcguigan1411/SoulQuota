# Soul Quota - personality locomotion / idle / ability actions on the shared KayKit "Rig".
# Poses are built per frame in armature space from a KayKit base sample (copied action), then the
# deform chain is overridden: hips/spine/chest/head by absolute rotations, legs by 2-bone IK onto
# planned foot trajectories (planted feet move back at body speed => no sliding inside the cycle),
# arms by FK angles (swing/abduction/elbow) or 2-bone IK. Root bone kept at rest (no root motion).
import bpy, math, sys
from mathutils import Vector, Matrix, Quaternion

BLEND = "/home/claude/sq/ArtSource/Characters/Reaper_Custom.blend"
ONLY = sys.argv[sys.argv.index("--only") + 1].split(",") if "--only" in sys.argv else None

bpy.ops.wm.open_mainfile(filepath=BLEND)
sc = bpy.context.scene
sc.render.fps = 30
rig = bpy.data.objects["Rig"]
A = rig.matrix_world.to_3x3().normalized()
Ainv = A.inverted()
BONES = [b.name for b in rig.data.bones]
CHILDREN = {b.name: [c.name for c in b.children_recursive] for b in rig.data.bones}
REST = {b.name: b.matrix_local.copy() for b in rig.data.bones}
PARENT = {b.name: (b.parent.name if b.parent else None) for b in rig.data.bones}
ACT = {a.name: a for a in bpy.data.actions}

def set_action(act):
    rig.animation_data.action = act
    if len(act.slots):
        rig.animation_data.action_slot = act.slots[0]

def eval_pose(act, frame):
    set_action(act)
    f = math.floor(frame)
    sc.frame_set(f, subframe=frame - f)
    return {pb.name: pb.matrix.copy() for pb in rig.pose.bones}

def head_w(P, b):
    return A @ P[b].translation

def rest_head_w(b):
    return A @ REST[b].translation

def rot_subtree(P, bone, q_world, pivot_world=None):
    if pivot_world is None:
        pivot_world = head_w(P, bone)
    Ra = (Ainv @ q_world.to_matrix() @ A).to_4x4()
    piv = Ainv @ Vector(pivot_world)
    M = Matrix.Translation(piv) @ Ra @ Matrix.Translation(-piv)
    for b in [bone] + CHILDREN[bone]:
        P[b] = M @ P[b]

def move_subtree(P, bone, dw):
    d = Ainv @ Vector(dw)
    for b in [bone] + CHILDREN[bone]:
        P[b] = Matrix.Translation(d) @ P[b]

def frame_rot(P, bone):
    return ((A @ P[bone].to_3x3()) @ (A @ REST[bone].to_3x3()).inverted()).to_quaternion()

def set_rot(P, bone, q):
    rot_subtree(P, bone, q @ frame_rot(P, bone).inverted())

def bone_dir_w(P, bone):
    return (A @ P[bone].to_3x3() @ Vector((0, 1, 0))).normalized()

def aim(P, bone, d_world, w=1.0):
    q = bone_dir_w(P, bone).rotation_difference(Vector(d_world).normalized())
    rot_subtree(P, bone, Quaternion().slerp(q, w))

def ik2(P, upper, lower, end, target, pole):
    S = head_w(P, upper)
    L1 = (head_w(P, lower) - S).length
    L2 = (head_w(P, end) - head_w(P, lower)).length
    D = Vector(target) - S
    d = max(0.05, min(D.length, (L1 + L2) * 0.9995))
    dn = D.normalized()
    a = max(-1.0, min(1.0, (L1 * L1 + d * d - L2 * L2) / (2 * L1 * d)))
    pv = Vector(pole) - S
    perp = pv - dn * pv.dot(dn)
    perp = perp.normalized() if perp.length > 1e-6 else Vector((0, -1, 0))
    E = S + dn * (L1 * a) + perp * (L1 * math.sqrt(1 - a * a))
    aim(P, upper, E - S)
    aim(P, lower, Vector(target) - head_w(P, lower))
    return D.length - (L1 + L2)

def yaw(a):
    return Quaternion((0, 0, 1), math.radians(a))

def pitch(a):  # + : top of an upright bone toward -Y (forward); tip of a hanging bone toward +Y (back)
    return Quaternion((1, 0, 0), math.radians(a))

def roll(a):   # + : top of an upright bone toward +X (character's left)
    return Quaternion((0, 1, 0), math.radians(a))

def E(pry):
    p, r, y = pry
    return yaw(y) @ pitch(p) @ roll(r)

# ------------------------------------------------------------ interpolation
def smooth(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)

def _back_out(t, s=1.4):
    t = t - 1.0
    return t * t * ((s + 1) * t + s) + 1.0

EASE = {"s": smooth, "i": lambda t: t ** 2, "o": lambda t: 1 - (1 - t) ** 2, "l": lambda t: t,
        "b": lambda t: 1 - (1 - t) ** 2 * (1 - 2.6 * t) if t < 1 else 1.0,   # b = overshoot-ish
        # REMASTER 2026-11-07 eases
        "k": _back_out,                                              # snap with ~10% overshoot, settles on the key
        "x": lambda t: 1.0 - 2.0 ** (-9.0 * t) if t < 1 else 1.0,    # expo snap: very fast start, long soft settle
        "c": lambda t: t ** 3,                                       # cubic ease-in: slow gather, accelerating hit
        "h": lambda t: 1.0 if t >= 1 else 0.0}                       # step / hold

def _key_tangent(keys, i):
    """Fritsch-Carlson monotone tangent at key i (value units per frame). Ends and extremes get 0 so key poses read
    as clear accents; keys inside a continuing move get a through-velocity (no robotic stop at every key)."""
    if i == 0 or i == len(keys) - 1:
        return 0.0
    (fa, va, _), (fb, vb, _), (fc, vc, _) = keys[i - 1], keys[i], keys[i + 1]
    h0, h1 = fb - fa, fc - fb
    if h0 <= 0 or h1 <= 0:
        return 0.0
    d0, d1 = (vb - va) / h0, (vc - vb) / h1
    if d0 * d1 <= 0:
        return 0.0
    return 3 * (h0 + h1) / ((2 * h1 + h0) / d0 + (h1 + 2 * h0) / d1)

def track(keys, f):
    """keys: [(frame, value, ease_into_this_key)]. 's' segments are monotone cubic splines through the keys
    (C1 continuous, never overshoot a hold); the other codes are explicit eases into that key."""
    if f <= keys[0][0]:
        return keys[0][1]
    if f >= keys[-1][0]:
        return keys[-1][1]
    for i in range(len(keys) - 1):
        f0, v0, _ = keys[i]
        f1, v1, e = keys[i + 1]
        if f0 <= f <= f1:
            break
    h = f1 - f0
    u = (f - f0) / h
    if e != "s":
        return v0 + (v1 - v0) * EASE[e](u)
    m0, m1 = _key_tangent(keys, i) * h, _key_tangent(keys, i + 1) * h
    u2, u3 = u * u, u * u * u
    return (2 * u3 - 3 * u2 + 1) * v0 + (u3 - 2 * u2 + u) * m0 + (-2 * u3 + 3 * u2) * v1 + (u3 - u2) * m1

def lerp(a, b, t):
    return a + (b - a) * t

S2 = lambda x: math.sin(2 * math.pi * x)
C2 = lambda x: math.cos(2 * math.pi * x)

# ------------------------------------------------------------ rig constants (world, metres)
HIP0 = rest_head_w("hips")                    # (0,0,0.406)
ANK0 = {s: rest_head_w("foot." + s) for s in "lr"}
BALL_OFF = rest_head_w("toes.l") - rest_head_w("foot.l")   # ankle -> ball of foot
HEEL_OFF = Vector((0, 0.035, -0.125))                       # ankle -> heel contact
TOEV = (A @ REST["toes.l"].to_3x3() @ Vector((0, 1, 0))) * rig.data.bones["toes.l"].length
GROUND = rest_head_w("toes.l").z                            # 0.026: ball/toe bone height when standing
SX = {"l": 1.0, "r": -1.0}
LEGLEN = (rest_head_w("lowerleg.l") - rest_head_w("upperleg.l")).length + (ANK0["l"] - rest_head_w("lowerleg.l")).length
STATS = {}

def arm(sw=5, ab=10, el=15, fin=0, tw=0):
    return dict(sw=sw, ab=ab, el=el, fin=fin, tw=tw)

def leg(dx=0.0, dy=0.0, dz=0.0, fp=0.0, fy=6.0, tp=None, kout=0.12):
    return dict(dx=dx, dy=dy, dz=dz, fp=fp, fy=fy, tp=tp, kout=kout)

def spec():
    return dict(hip=[0.0, 0.0, -0.02], hr=[0.0, 0.0, 0.0], sp=[3.0, 0.0, 0.0], ch=[2.0, 0.0, 0.0],
                hd=[0.0, 0.0, 0.0], w=0.17,
                arms={"l": arm(), "r": arm()}, legs={"l": leg(), "r": leg()})

def arm_dirs(a, sx):
    """sw: swing in the sagittal plane (0 = down, 90 = forward, 180 = up, <0 = back);
    ab: spread away from the body's midline (great-circle, works at any elevation; <0 crosses inward);
    el: elbow flexion in the swing plane; fin: forearm turned toward the midline by this much."""
    out = Vector((sx, 0, 0))
    def spread(v, ang):
        a_ = math.radians(ang)
        return (v * math.cos(a_) + out * math.sin(a_)).normalized()
    sag1 = Quaternion((1, 0, 0), math.radians(-a["sw"])) @ Vector((0, 0, -1))
    sag2 = Quaternion((1, 0, 0), math.radians(-(a["sw"] + a["el"]))) @ Vector((0, 0, -1))
    # the spread is a rotation about (sag x out); applying the same rotation to the sagittal elbow axis (-X)
    # gives an elbow axis that is defined (and continuous) for every arm direction
    Rab = Quaternion(sag1.cross(out).normalized(), math.radians(a["ab"]))
    d1 = Rab @ sag1
    n = Rab @ Vector((-1, 0, 0))
    d2 = spread(sag2, a["ab"] - a["fin"])
    return d1, d2, n

def frame_to(rest_y, rest_x, y, x):
    """rotation taking the orthonormal pair (rest_y, rest_x) onto (y, x) (x re-orthogonalised against y)"""
    def basis(yv, xv):
        yv = yv.normalized(); xv = (xv - yv * xv.dot(yv)).normalized()
        return Matrix((xv, yv, xv.cross(yv))).transposed()
    return (basis(y, x) @ basis(rest_y, rest_x).transposed()).to_quaternion()

def foot_target(base, fp, fy):
    """base: ankle position if the foot were flat there. Pitch pivots on the ball (heel up) or heel (toes up)."""
    Q = yaw(fy)
    R = Q @ pitch(fp) @ Q.inverted()
    off = Q @ (BALL_OFF if fp >= 0 else HEEL_OFF)
    piv = base + off
    return piv + R @ (-off)

def build(base_P, s, name, frame):
    P = dict(base_P)
    # root back to rest (no root motion)
    M = REST["root"] @ P["root"].inverted()
    for b in ["root"] + CHILDREN["root"]:
        P[b] = M @ P[b]
    # hips
    Eh = E(s["hr"])
    set_rot(P, "hips", Eh)
    move_subtree(P, "hips", HIP0 + Vector(s["hip"]) - head_w(P, "hips"))
    # legs: targets first, then lower the hips if a leg can't reach
    legs = s.get("legs")
    fk = s.get("fk")
    tg = {}
    if legs:
        for sd in "lr":
            L = legs[sd]
            fy = L["fy"] * SX[sd]
            base = Vector((SX[sd] * s["w"] + L["dx"], ANK0[sd].y + L["dy"], ANK0[sd].z + L["dz"]))
            tg[sd] = foot_target(base, L["fp"], fy)
            # keep ball, toe tip and heel above the floor (bone ground level = rest ball height)
            Rf = yaw(fy) @ pitch(L["fp"])
            tp_ = L["tp"] if L["tp"] is not None else min(L["fp"], 0.0)
            ball = tg[sd] + Rf @ BALL_OFF
            tip = ball + (yaw(fy) @ pitch(tp_)) @ TOEV
            heel = tg[sd] + Rf @ HEEL_OFF
            deficit = max(0.0, GROUND - ball.z, GROUND - tip.z, GROUND - 0.006 - heel.z)
            tg[sd].z += deficit
        drop = 0.0
        R = LEGLEN * 0.985
        for sd in "lr":
            hj = head_w(P, "upperleg." + sd)
            dx, dy = tg[sd].x - hj.x, tg[sd].y - hj.y
            h2 = R * R - dx * dx - dy * dy
            need = (hj.z - tg[sd].z) - math.sqrt(max(h2, 0.0))
            drop = max(drop, need)
        if drop > 0:
            move_subtree(P, "hips", (0, 0, -drop))
            st = STATS.setdefault(name, [0, 0.0])
            st[0] += 1; st[1] = max(st[1], drop)
    # torso chain
    Es = Eh @ E(s["sp"]); set_rot(P, "spine", Es)
    Ec = Es @ E(s["ch"]); set_rot(P, "chest", Ec)
    set_rot(P, "head", Ec @ E(s["hd"]))
    # legs
    if legs:
        for sd in "lr":
            L = legs[sd]
            fy = L["fy"] * SX[sd]
            Qy = yaw(fy)
            for b in ("upperleg.", "lowerleg.", "foot.", "toes."):
                set_rot(P, b + sd, Qy)
            hj = head_w(P, "upperleg." + sd)
            pole = hj + Qy @ Vector((SX[sd] * L["kout"], -1.0, -0.25))
            ik2(P, "upperleg." + sd, "lowerleg." + sd, "foot." + sd, tg[sd], pole)
            set_rot(P, "foot." + sd, Qy @ pitch(L["fp"]))
            tp = L["tp"] if L["tp"] is not None else min(L["fp"], 0.0)
            set_rot(P, "toes." + sd, Qy @ pitch(tp))
    elif fk:
        for sd in "lr":
            F = fk[sd]
            Eu = Eh @ pitch(F["th"]) @ roll(F.get("ab", 0) * SX[sd]) @ yaw(F.get("fy", 0) * SX[sd])
            set_rot(P, "upperleg." + sd, Eu)
            El = Eu @ pitch(F["kn"]); set_rot(P, "lowerleg." + sd, El)
            Ef = El @ pitch(F["ft"]); set_rot(P, "foot." + sd, Ef)
            set_rot(P, "toes." + sd, Ef @ pitch(F.get("to", 0)))
    # arms
    for sd in "lr":
        a = s["arms"][sd]
        for b in ("upperarm.", "lowerarm.", "wrist.", "hand."):
            set_rot(P, b + sd, Ec)
        if "ik" in a:
            ik2(P, "upperarm." + sd, "lowerarm." + sd, "wrist." + sd, a["ik"], a["pole"])
            d2 = bone_dir_w(P, "lowerarm." + sd)
        else:
            d1, d2, n = arm_dirs(a, SX[sd])
            rd = Vector((SX[sd], 0, 0))                 # rest arm direction (T-pose)
            rn = rd.cross(Vector((0, -1, 0)))           # rest elbow axis (flexing toward the front)
            q1 = frame_to(rd, rn, d1, n)
            q2 = frame_to(rd, rn, d2, n)
            set_rot(P, "upperarm." + sd, Ec @ q1)
            for b in ("lowerarm.", "wrist.", "hand."):
                set_rot(P, b + sd, Ec @ q2)
            d2 = Ec @ d2
        if a.get("tw"):
            rot_subtree(P, "wrist." + sd, Quaternion(d2, math.radians(a["tw"] * SX[sd])))
        # REMASTER: wrist drag / whip (secondary motion of the weapon hand) in the arm's swing plane
        if a.get("wf") and "ik" not in a:
            rot_subtree(P, "wrist." + sd, Quaternion((Ec @ n).normalized(), math.radians(a["wf"])))
    # REMASTER: impact pop of the hand (and whatever is held in handslot) - a 2-3 frame smear accent
    for sd, k in s.get("hs", {}).items():
        if abs(k - 1.0) > 1e-4:
            piv = P["hand." + sd].translation.copy()
            M = Matrix.Translation(piv) @ Matrix.Diagonal((k, k, k, 1.0)) @ Matrix.Translation(-piv)
            for b in ["hand." + sd] + CHILDREN["hand." + sd]:
                P[b] = M @ P[b]
    return P

# ------------------------------------------------------------ writing actions
def to_basis(P):
    out = {}
    for b in BONES:
        p = PARENT[b]
        if p is None:
            out[b] = REST[b].inverted() @ P[b]
        else:
            out[b] = (REST[p].inverted() @ REST[b]).inverted() @ P[p].inverted() @ P[b]
    return out

def write_action(name, base_act, poses):
    act = bpy.data.actions.get(name)
    if act:
        bpy.data.actions.remove(act)
    act = base_act.copy()
    act.name = name
    act.use_fake_user = True
    cb = act.layers[0].strips[0].channelbags[0]
    objvals = {}
    for fc in list(cb.fcurves):
        if not fc.data_path.startswith("pose.bones"):
            objvals[(fc.data_path, fc.array_index)] = fc.evaluate(1)
        cb.fcurves.remove(fc)
    N = len(poses)
    for (dp, idx), val in objvals.items():
        fc = cb.fcurves.new(dp, index=idx, group_name="Object Transforms")
        fc.keyframe_points.add(2)
        fc.keyframe_points[0].co = (1, val)
        fc.keyframe_points[1].co = (N, val)
        for k in fc.keyframe_points:
            k.interpolation = 'LINEAR'
    bases = [to_basis(P) for P in poses]
    for b in BONES:
        locs, quats, scales = [], [], []
        prev = None
        for B in bases:
            l, q, s = B[b].decompose()
            if prev is not None and prev.dot(q) < 0:
                q = -q
            prev = q
            locs.append(l); quats.append(q); scales.append(s)
        for dp, vals, n in (("location", locs, 3), ("rotation_quaternion", quats, 4), ("scale", scales, 3)):
            for i in range(n):
                fc = cb.fcurves.new(f'pose.bones["{b}"].{dp}', index=i, group_name=b)
                fc.keyframe_points.add(N)
                for k in range(N):
                    fc.keyframe_points[k].co = (k + 1, vals[k][i])
                    # REMASTER: spline keys (every frame is keyed, so this only smooths sub-frame playback)
                    fc.keyframe_points[k].interpolation = 'BEZIER'
                    fc.keyframe_points[k].handle_left_type = 'AUTO_CLAMPED'
                    fc.keyframe_points[k].handle_right_type = 'AUTO_CLAMPED'
                fc.update()
    act.frame_range = (1, N)
    act.use_frame_range = True
    act["sq_frames"] = N
    print("ACTION", name, N, "frames")
    return act

# ============================================================ REMASTER 2026-11-07: overlap + secondary motion
# Every take goes through remaster_specs() before posing:
#  - overlapping action: per-channel time offsets (hips lead, chest/head/arms follow 1-3 frames later)
#  - follow-through: damped springs on the head (drag + overshoot + settle, incl. a nod from hip bounce),
#    the forearm (elbow whip) and the wrist (weapon hand drag), solved as periodic steady state for loops
#  - impact pops: 2-3 frame hand/handslot scale accent on strike frames (cfg "pop")
# cfg keys (all optional): lag={chan: frames}, head, nod, fore, wrist (spring gains), freq, zeta, pop=[(frame, sides, amt)]
import copy

LOOP_CFG = dict(lag={"hr": 0, "sp": 0.5, "ch": 1.0, "hd": 2.0, "arm": 1.5, "el": 2.5}, head=0.75, nod=120.0,
                fore=0.55, wrist=0.9, freq=2.6, zeta=0.42, wfreq=3.4)
SHOT_CFG = dict(lag={"hr": -1.0, "sp": -0.5, "ch": 0.4, "hd": 1.0, "arm": 0.0, "el": 0.0}, head=0.55, nod=90.0,
                fore=0.22, wrist=0.85, freq=3.2, zeta=0.45, wfreq=3.8, exag=(1.12, 1.15, 1.0))
REMASTER = {}          # name -> cfg overrides (filled by styles()/abilities()/enemy script)

def spring_series(target, loop, freq, zeta, fps=30.0, sub=8):
    n = len(target)
    w = 2 * math.pi * freq
    dt = 1.0 / fps / sub
    seq = (target[:-1] * 5) if loop else list(target)
    x, v = seq[0], 0.0
    out = []
    prev = seq[0]
    for tg in seq:
        for j in range(sub):
            tt = prev + (tg - prev) * (j + 1) / sub
            a = w * w * (tt - x) - 2 * zeta * w * v
            v += a * dt
            x += v * dt
        prev = tg
        out.append(x)
    if loop:
        res = out[-(n - 1):]
        return res + [res[0]]
    return out

NEUTRAL = None

def exaggerate(s, k, kh, ka):
    """push a pose further from neutral (stronger silhouettes): torso angles x k, hip offsets x kh, arm spread x ka"""
    global NEUTRAL
    if NEUTRAL is None:
        NEUTRAL = spec()
    n = NEUTRAL
    for ch in ("hr", "sp", "ch", "hd"):
        s[ch] = [n[ch][i] + k * (s[ch][i] - n[ch][i]) for i in range(3)]
    s["hip"] = [n["hip"][i] + kh * (s["hip"][i] - n["hip"][i]) for i in range(3)]
    if ka != 1.0:
        for sd in "lr":
            a = s["arms"][sd]
            if "ik" not in a:
                a["sw"] = 5 + ka * (a["sw"] - 5)
                a["el"] = max(0.0, min(150.0, 15 + ka * (a["el"] - 15)))
    return s

def remaster_specs(fn, N, loop, cfg):
    ex = cfg.get("exag", (1.0, 1.0, 1.0))
    def sample(x):
        if loop:
            s = fn(((x - 1) / (N - 1)) % 1.0)
        else:
            s = fn(min(max(x, 1.0), float(N)))
        return exaggerate(s, *ex) if ex != (1.0, 1.0, 1.0) else s
    lag = cfg["lag"]
    specs = []
    for f in range(1, N + 1):
        s = sample(f)
        for ch in ("hr", "sp", "ch", "hd"):
            o = lag.get(ch, 0.0)
            if o:
                s[ch] = list(sample(f - o)[ch])
        oa, oe = lag.get("arm", 0.0), lag.get("el", 0.0)
        if oa or oe:
            sa, se = sample(f - oa), sample(f - oe)
            for sd in "lr":
                if "ik" in s["arms"][sd]:
                    continue
                s["arms"][sd]["sw"] = sa["arms"][sd]["sw"]
                s["arms"][sd]["ab"] = sa["arms"][sd]["ab"]
                for k in ("el", "fin", "tw"):
                    s["arms"][sd][k] = se["arms"][sd][k]
        specs.append(s)
    if loop:                                    # exact seam: last frame is the first frame
        specs[-1] = copy.deepcopy(specs[0])
    fade = [1.0] * N
    if not loop:                                # springs fade out over the last frames so clips end on their pose
        for i in range(N):
            fade[i] = min(1.0, (N - 1 - i) / 5.0)
    # head: follow-through spring on the head's (approximate) world angles + nod from the hip bounce
    if cfg.get("head"):
        g = cfg["head"]
        for k in range(3):
            C = [s["hr"][k] + s["sp"][k] + s["ch"][k] for s in specs]
            Hw = [C[i] + specs[i]["hd"][k] for i in range(N)]
            X = spring_series(Hw, loop, cfg["freq"], cfg["zeta"])
            for i in range(N):
                specs[i]["hd"][k] += g * (X[i] - Hw[i]) * fade[i]
    LIM = ((-50.0, 45.0), (-30.0, 30.0), (-65.0, 65.0))
    for s_ in specs:
        s_["hd"] = [max(LIM[k][0], min(LIM[k][1], s_["hd"][k])) for k in range(3)]
    if cfg.get("nod"):
        Z = [s["hip"][2] for s in specs]
        Zs = spring_series(Z, loop, cfg["freq"] * 1.2, cfg["zeta"])
        for i in range(N):
            specs[i]["hd"][0] += max(-14.0, min(14.0, cfg["nod"] * (Zs[i] - Z[i]))) * fade[i]
            specs[i]["ch"][0] += max(-5.0, min(5.0, 0.35 * cfg["nod"] * (Zs[i] - Z[i]))) * fade[i]
    # arms: forearm whip + wrist drag (weapon hand)
    for sd in "lr":
        if any("ik" in s["arms"][sd] for s in specs):
            continue
        U = [s["arms"][sd]["sw"] for s in specs]
        F = [s["arms"][sd]["sw"] + s["arms"][sd]["el"] for s in specs]
        if cfg.get("fore"):
            Fx = spring_series(F, loop, cfg["freq"] * 1.15, cfg["zeta"])
            for i in range(N):
                el = specs[i]["arms"][sd]["el"] + cfg["fore"] * (Fx[i] - F[i]) * fade[i]
                specs[i]["arms"][sd]["el"] = max(-4.0, min(150.0, el))
        F2 = [s["arms"][sd]["sw"] + s["arms"][sd]["el"] for s in specs]
        if cfg.get("wrist"):
            Wx = spring_series(F2, loop, cfg["wfreq"], cfg["zeta"] * 0.9)
            for i in range(N):
                specs[i]["arms"][sd]["wf"] = max(-38.0, min(38.0, cfg["wrist"] * (Wx[i] - F2[i]))) * fade[i] \
                    + specs[i]["arms"][sd].get("wf", 0.0)
    # impact pops (hand + held weapon scale accent)
    for (fr, sides, amt) in cfg.get("pop", []):
        for i in range(N):
            d = (i + 1) - fr
            k = {-1: 0.45, 0: 1.0, 1: 0.55, 2: 0.18}.get(d, 0.0)
            if k:
                for sd in sides:
                    hs = specs[i].setdefault("hs", {})
                    hs[sd] = hs.get(sd, 1.0) + amt * k
    return specs

def make_core(name, base_name, N, fn, loop, cfg=None, post_pose=None):
    base = ACT[base_name]
    b0, b1 = base.frame_range
    c = copy.deepcopy(LOOP_CFG if loop else SHOT_CFG)
    over = dict(REMASTER.get(name, {}))
    if cfg:
        over.update(cfg)
    if "lag" in over:
        c["lag"].update(over.pop("lag"))
    c.update(over)
    specs = remaster_specs(fn, N, loop, c)
    poses = []
    for f in range(1, N + 1):
        if loop:
            bf = b0 + (b1 - b0) * (f - 1) / (N - 1)
        else:
            bf = b0 + ((f - 1) % int(b1 - b0))
        P = build(eval_pose(base, bf), specs[f - 1], name, f)
        if post_pose:
            post_pose(P)
        poses.append(P)
    act = write_action(name, base, poses)
    act["sq_loop"] = bool(loop)
    act["sq_remaster"] = 2
    return act

def make(name, base_name, N, fn, loop, cfg=None):
    if ONLY and name not in ONLY:
        return
    make_core(name, base_name, N, fn, loop, cfg)

# ============================================================ LOCOMOTION
def foot_cycle(u, duty, L, h, heel=12.0, toe=30.0, basep=0.0, swingp=None, lift_pow=1.0, lift_peak=0.5):
    """u in [0,1): 0 = heel strike with the foot forward. returns dy, dz, foot pitch, toe pitch"""
    u %= 1.0
    if swingp is None:
        swingp = -heel * 0.3
    if u < duty:
        s = u / duty
        dy = -L / 2 + L * s
        dz = 0.0
        if s < 0.18:
            fp = lerp(-heel, basep, smooth(s / 0.18))
        elif s > 0.62:
            fp = lerp(basep, max(toe, basep), smooth((s - 0.62) / 0.38))
        else:
            fp = basep
        tp = min(fp, 0.0)
    else:
        s = (u - duty) / (1 - duty)
        dy = L / 2 - L * smooth(s)
        # lift curve with adjustable peak position
        sp = s ** (math.log(0.5) / math.log(lift_peak)) if 0 < s < 1 else s
        dz = h * math.sin(math.pi * sp) ** lift_pow
        fp = track([(0, max(toe, basep), "s"), (0.45, swingp, "s"), (1, -heel, "s")], s)
        tp = fp * 0.5
    return dy, dz, fp, tp

def walker(t, G):
    """generic biped cycle. G: dict of style params (see styles below)."""
    s = spec()
    s["w"] = G.get("w", 0.17)
    D = G["D"]
    ph = {"l": t, "r": t + 0.5 + G.get("rphase", 0.0)}
    for sd in "lr":
        P = dict(G)
        P.update(G.get(sd, {}))
        duty = P["duty"]
        dy, dz, fp, tp = foot_cycle(ph[sd] - P.get("off", 0.0), duty, D * duty, P["h"], P.get("heel", 12), P.get("toe", 30),
                                    P.get("basep", 0.0), P.get("swingp"), P.get("lift_pow", 1.0), P.get("lift_peak", 0.5))
        s["legs"][sd] = leg(dx=SX[sd] * P.get("dxo", 0.0), dy=dy + P.get("yc", 0.0), dz=dz, fp=fp, fy=P.get("fy", 6), tp=tp,
                            kout=P.get("kout", 0.12))
    c = C2(t); sn = S2(t)
    xb = 4 * math.pi * (t - G.get("bobph", 0.15))
    # REMASTER: skewed bob - a quick, short dip into the "down" pose after contact and a broad, floaty "up" at passing
    bob = G.get("bob", 0.02) * math.cos(xb - G.get("bskew", 0.45) * math.sin(xb))
    s["hip"] = [G.get("sway", 0.02) * sn, G.get("hy", 0.0), G.get("H", -0.04) + bob]
    s["hr"] = [G.get("hp", 0), -G.get("hroll", 4) * sn + G.get("broll", 0) * sn, -G.get("hyaw", 8) * c]
    nod = G.get("nod", 4) * math.cos(4 * math.pi * (t - G.get("bobph", 0.15)) + G.get("nodph", 0.6))
    s["sp"] = [G.get("sp", 4), G.get("sproll", 2) * sn, G.get("hyaw", 8) * 0.5 * c]
    s["ch"] = [G.get("chp", 2), G.get("chroll", 0) * sn, G.get("chyaw", 8) * c]
    s["hd"] = [G.get("hdp", 0) + nod, -G.get("hdroll", 3) * sn, -G.get("chyaw", 8) * 0.6 * c]
    A_ = G.get("asw", 25)
    for sd, sg in (("l", -1), ("r", 1)):
        sw = G.get("asw0", 0) + sg * A_ * c
        el = G.get("el", 20) + G.get("eld", 10) * max(0.0, sg * c)
        s["arms"][sd] = arm(sw=sw, ab=G.get("ab", 12) + G.get("abd", 0) * sg * c, el=el, fin=G.get("fin", 0), tw=G.get("tw", 0))
    return s

def walk_style(name, N, G, post=None, base="Walking_A", cfg=None):
    def fn(t):
        s = walker(t, G)
        if post:
            post(s, t)
        return s
    make(name, base, N, fn, True, cfg)

def styles():
    # ---- walks (period = N-1 frames, 30 fps)
    walk_style("SQ_Walk_Sneak", 49, dict(D=0.80, duty=0.62, h=0.14, H=-0.13, bob=0.024, bobph=0.25, sway=0.03, hroll=3, hyaw=10,
               sp=18, chp=8, hdp=-14, nod=5, basep=28, heel=-10, toe=45, swingp=40, lift_peak=0.4, kout=0.25, w=0.19,
               asw=16, asw0=72, el=70, eld=8, ab=48, fin=30, tw=-50, chyaw=14, hdroll=6),
               post=lambda s, t: s["hd"].__setitem__(2, s["hd"][2] + 30 * S2(t + 0.1)))  # look left/right once per cycle
    walk_style("SQ_Walk_March", 33, dict(D=0.75, duty=0.58, h=0.24, H=-0.05, bob=0.03, bobph=0.3, sway=0.015, hroll=2, hyaw=4,
               sp=-3, chp=-3, hdp=-1, nod=2, heel=4, toe=18, swingp=-5, lift_peak=0.38, lift_pow=0.7, kout=0.05,
               asw=64, asw0=8, el=4, eld=0, ab=6, chyaw=3, sproll=0, hdroll=0), cfg=dict(nod=60.0))
    walk_style("SQ_Walk_Waddle", 29, dict(D=0.42, duty=0.62, h=0.06, H=-0.06, bob=0.022, bobph=0.25, sway=0.085, hroll=-7, broll=11,
               hyaw=6, sp=0, chp=0, hdp=-4, nod=3, heel=6, toe=15, fy=24, w=0.24, kout=0.35, sproll=8, chroll=5, hdroll=-10,
               asw=6, ab=38, abd=12, el=25, eld=0, tw=-30, chyaw=4))
    walk_style("SQ_Walk_Swagger", 41, dict(D=0.85, duty=0.6, h=0.08, H=-0.05, bob=0.038, bobph=0.2, sway=0.055, hroll=10, hyaw=20,
               sp=-7, chp=-5, hdp=-14, nod=9, nodph=0.0, heel=14, toe=28, fy=12, w=0.18, kout=0.2, sproll=-6, chroll=-6,
               hdroll=9, asw=34, asw0=-6, ab=24, el=25, eld=25, abd=8, chyaw=28, tw=20))
    def limp_post(s, t):
        # lurch: a quick dip and lean over the bad (right) leg while it takes weight (t ~ 0.5-0.95)
        lur = max(0.0, math.sin(math.pi * (t - 0.5) / 0.45)) if 0.5 <= t <= 0.95 else 0.0
        s["hr"][1] = -4 * S2(t) - 10 * lur
        s["sp"][1] = -8 * lur + 2
        s["ch"][1] = -6 * lur
        s["hip"][2] -= 0.045 * lur
        s["hip"][0] = 0.035 * S2(t) - 0.035 * lur
        s["arms"]["r"] = arm(sw=10 + 8 * C2(t + 0.25), ab=12 + 10 * lur, el=8 + 6 * S2(t))   # dangling arm swings out on the lurch
        s["hd"][1] += -12 + 6 * lur                                            # head lolls to the right, bounces on the lurch
        s["hd"][0] += 10 * lur
    walk_style("SQ_Walk_Limp", 41, dict(D=0.55, duty=0.66, h=0.07, H=-0.06, bob=0.03, bobph=0.1, sway=0.05, hroll=4,
               broll=0, hyaw=12, sp=14, chp=6, hdp=-12, nod=5, heel=12, toe=25, w=0.18, sproll=0, chroll=0, hdroll=0,
               asw=28, asw0=12, el=55, eld=20, ab=20,
               r=dict(duty=0.45, h=0.010, heel=-6, toe=58, basep=8, swingp=52, yc=0.09, fy=22, lift_peak=0.72)),
               post=limp_post)
    walk_style("SQ_Walk_Prance", 25, dict(D=0.70, duty=0.40, h=0.20, H=-0.02, bob=0.08, bobph=0.2, sway=0.02, hroll=4, hyaw=6,
               sp=-8, chp=-6, hdp=-16, nod=8, basep=20, heel=-10, toe=45, swingp=55, lift_peak=0.4, kout=0.1, w=0.15,
               asw=35, asw0=40, ab=55, abd=15, el=55, eld=25, fin=10, tw=40, chyaw=10, hdroll=10))
    walk_style("SQ_Walk_Shuffle", 33, dict(D=0.24, duty=0.72, h=0.018, H=-0.12, bob=0.008, bobph=0.25, sway=0.04, hroll=2,
               broll=8, hyaw=4, sp=36, chp=18, hdp=-40, nod=4, heel=3, toe=8, swingp=0, w=0.15, fy=10, kout=0.2,
               asw=8, asw0=24, el=35, eld=6, ab=24, fin=18, chyaw=4, sproll=4, hdroll=5))
    # ---- runs
    walk_style("SQ_Run_Sprint", 19, dict(D=1.55, duty=0.36, h=0.20, H=-0.05, bob=-0.05, bobph=0.18, sway=0.01, hroll=4, hyaw=10,
               sp=20, chp=6, hdp=-16, nod=6, heel=6, toe=40, swingp=60, lift_peak=0.4, lift_pow=0.8, kout=0.05,
               asw=82, asw0=15, el=90, eld=0, ab=24, fin=20, chyaw=12, sproll=0, hdroll=2), base="Running_A")
    walk_style("SQ_Run_Scurry", 15, dict(D=0.95, duty=0.42, h=0.10, H=-0.15, bob=-0.025, bobph=0.2, sway=0.015, hroll=3, hyaw=8,
               sp=34, chp=12, hdp=-38, nod=3, heel=0, toe=35, basep=12, swingp=40, lift_peak=0.4, kout=0.2,
               asw=6, asw0=-62, el=5, eld=0, ab=18, tw=-60, chyaw=4, hdroll=2), base="Running_A")
    def gallop_post(s, t):
        # long flight phase (t ~0.32-0.88): body rises, then a nose-down landing and a rear-back push-off
        fl = math.sin(math.pi * (t - 0.32) / 0.56) if 0.32 <= t <= 0.88 else 0.0
        s["hip"][2] += 0.07 * fl - 0.02
        p = 16 * S2(t - 0.1)
        s["hr"][0] += p * 0.4
        s["sp"][0] += p
        s["hd"][0] -= p * 1.2
        for sd in "lr":   # both hands forward holding reins, bouncing together
            s["arms"][sd] = arm(sw=48 + 14 * S2(t - 0.15), ab=10, el=70 - 12 * S2(t - 0.15), fin=35, tw=-20)
    walk_style("SQ_Run_Gallop", 21, dict(D=1.25, duty=0.32, h=0.17, H=-0.06, bob=0.0, sway=0.0, hroll=2, hyaw=4,
               sp=10, chp=0, hdp=-10, nod=0, heel=8, toe=40, swingp=45, lift_peak=0.45, rphase=-0.38, kout=0.1,
               asw=0, chyaw=0, sproll=0, hdroll=0), post=gallop_post, base="Running_A")
    def heavy_post(s, t):
        # extra hip slam on each footfall and head drop
        k = max(0.0, math.cos(4 * math.pi * (t - 0.42)))
        s["hip"][2] -= 0.04 * k ** 6
        s["hd"][0] += 6 * k ** 6
    walk_style("SQ_Run_Heavy", 25, dict(D=1.05, duty=0.46, h=0.15, H=-0.09, bob=-0.035, bobph=0.23, sway=0.06, hroll=8, broll=6,
               hyaw=12, sp=20, chp=8, hdp=-6, nod=3, heel=10, toe=25, swingp=20, lift_peak=0.45, w=0.22, fy=14,
               kout=0.3, asw=40, asw0=10, ab=35, abd=6, el=55, eld=20, fin=0, chyaw=16, sproll=6, chroll=4, hdroll=6),
               post=heavy_post, base="Running_A")
    def run_float(t):
        s = spec()
        s.pop("legs")
        bob = 0.04 * S2(t)
        s["hip"] = [0.035 * S2(t * 1 + 0.25), 0.0, 0.20 + bob]
        s["hr"] = [18 + 3 * S2(t - 0.15), 5 * S2(t + 0.25), 6 * S2(t)]
        s["sp"] = [8, -4 * S2(t + 0.25), -4 * S2(t)]
        s["ch"] = [4 + 3 * S2(t - 0.3), -3 * S2(t + 0.25), -5 * S2(t)]
        s["hd"] = [-22 - 4 * S2(t - 0.4), 4 * S2(t), 4 * S2(t)]
        s["fk"] = {sd: dict(th=24 + 8 * S2(t - 0.25 + 0.1 * (sd == "r")), kn=28 + 10 * S2(t - 0.4 + 0.1 * (sd == "r")),
                            ft=35 + 8 * S2(t - 0.55), to=10, ab=4) for sd in "lr"}
        for sd, o in (("l", 0.0), ("r", 0.08)):
            s["arms"][sd] = arm(sw=-40 + 10 * S2(t - 0.3 + o), ab=32 + 8 * S2(t - 0.45 + o), el=25 + 15 * S2(t - 0.6 + o), tw=30)
        return s
    make("SQ_Run_Float", "Running_A", 41, run_float, True)
    walk_style("SQ_Run_Waddle", 19, dict(D=0.66, duty=0.5, h=0.08, H=-0.07, bob=-0.02, bobph=0.25, sway=0.065, hroll=-6, broll=10,
               hyaw=6, sp=12, chp=2, hdp=-6, nod=4, heel=6, toe=20, fy=24, w=0.24, kout=0.35, sproll=7, chroll=4,
               hdroll=-10, asw=10, ab=48, abd=18, el=20, eld=0, tw=-30, chyaw=4), base="Running_A")

    # ---- idles (planted feet)
    def idle_nervous(t):
        s = spec()
        sh = 0.03 * S2(2 * t)                         # weight shifts L/R twice
        s["hip"] = [sh, 0.0, -0.05 + 0.006 * S2(8 * t)]
        s["hr"] = [0, -5 * S2(2 * t), 0]
        s["sp"] = [10, 3 * S2(2 * t), 0]
        s["ch"] = [8, 0, 6 * S2(2 * t + 0.1)]
        look = track([(0, 0, "s"), (0.08, 45, "o"), (0.22, 42, "s"), (0.28, -50, "o"), (0.42, -46, "s"), (0.5, 10, "o"),
                      (0.56, 30, "o"), (0.68, 26, "s"), (0.74, -35, "o"), (0.88, -30, "s"), (1.0, 0, "s")], t)
        s["hd"] = [-6 + 4 * S2(4 * t), 6 * S2(3 * t), look]
        wr = 6 * S2(8 * t)                             # wringing hands
        s["arms"]["l"] = arm(sw=48 + wr, ab=8, el=75 - wr, fin=55, tw=-20 + 25 * S2(8 * t))
        s["arms"]["r"] = arm(sw=48 - wr, ab=8, el=75 + wr, fin=55, tw=-20 - 25 * S2(8 * t))
        tap = max(0.0, S2(8 * t))                      # right toe taps 8 times
        s["legs"]["l"] = leg(fy=10)
        s["legs"]["r"] = leg(fp=-22 * tap, fy=10, dy=0.03)
        s["w"] = 0.15
        return s
    make("SQ_Idle_Nervous", "Idle", 73, idle_nervous, True)

    def idle_proud(t):
        s = spec()
        br = S2(t)
        s["hip"] = [0.012 * S2(t + 0.25), 0.0, -0.025 + 0.006 * br]
        s["hr"] = [-4, -2 * S2(t + 0.25), 0]
        s["sp"] = [-8 - 2 * br, 0, 0]
        s["ch"] = [-10 - 4 * br, 0, 0]
        s["hd"] = [-14 + 3 * br, 4 * S2(t * 2), 8 * S2(t)]
        for sd in "lr":
            # wrists on the hip bones, elbows out wide
            hp = HIP0 + Vector((SX[sd] * 0.33, -0.02, 0.33 + 0.01 * br))      # fists on the waist, outside the robe
            sh = HIP0 + Vector((SX[sd] * 1.2, 0.25, 0.55))                     # elbows pushed wide
            s["arms"][sd] = dict(ik=hp, pole=sh)
        s["legs"] = {sd: leg(fy=14) for sd in "lr"}
        s["w"] = 0.22
        return s
    make("SQ_Idle_Proud", "Idle", 61, idle_proud, True)

    def idle_hunched(t):
        s = spec()
        sw_ = S2(t)
        s["hip"] = [0.025 * sw_, 0.02, -0.11 + 0.008 * S2(2 * t)]
        s["hr"] = [6, -3 * sw_, 3 * S2(t + 0.25)]
        s["sp"] = [34 + 2 * S2(2 * t), 3 * sw_, 0]
        s["ch"] = [18, 2 * sw_, 0]
        s["hd"] = [-42 + 3 * S2(2 * t - 0.2), -5 * sw_, 8 * S2(t * 0.5) if False else 6 * S2(t + 0.1)]
        for sd, o in (("l", 0.0), ("r", 0.15)):
            s["arms"][sd] = arm(sw=22 + 5 * S2(t - 0.2 + o), ab=8, el=30 + 6 * S2(2 * t + o), fin=12)
        s["legs"] = {sd: leg(fy=12, kout=0.25) for sd in "lr"}
        s["w"] = 0.15
        return s
    make("SQ_Idle_Hunched", "Idle", 61, idle_hunched, True)

    def idle_float(t):
        s = spec()
        s.pop("legs")
        b = S2(t)
        s["hip"] = [0.015 * S2(t + 0.25), 0.0, 0.30 + 0.05 * b]
        s["hr"] = [4 + 2 * S2(t - 0.15), 3 * S2(t + 0.25), 0]
        s["sp"] = [3, -2 * S2(t + 0.25), 0]
        s["ch"] = [2 + 2 * S2(t - 0.25), 0, 3 * S2(t)]
        s["hd"] = [-4 - 3 * S2(t - 0.35), 4 * S2(t + 0.1), 6 * S2(t * 1)]
        s["fk"] = {sd: dict(th=6 + 7 * S2(t - 0.2 + 0.08 * (sd == "r")), kn=24 + 10 * S2(t - 0.35 + 0.08 * (sd == "r")),
                            ft=26 + 6 * S2(t - 0.5), to=6, ab=5) for sd in "lr"}
        for sd, o in (("l", 0.0), ("r", 0.1)):
            s["arms"][sd] = arm(sw=12 + 6 * S2(t - 0.3 + o), ab=40 + 12 * S2(t - 0.3 + o), el=30 + 14 * S2(t - 0.5 + o), tw=25)
        return s
    make("SQ_Idle_Float", "Idle", 61, idle_float, True)

    def idle_menace(t):
        s = spec()
        sw_ = S2(t)
        s["hip"] = [0.05 * sw_, 0.05, -0.12 + 0.012 * S2(2 * t)]
        s["hr"] = [10, -6 * sw_, 8 * S2(t + 0.25)]
        s["sp"] = [24, 8 * sw_, -6 * S2(t + 0.25)]
        s["ch"] = [12, 6 * sw_, -10 * S2(t + 0.25)]
        s["hd"] = [-30 + 4 * S2(2 * t), -10 * sw_, 14 * S2(t + 0.25)]
        for sd, o in (("l", 0.0), ("r", 0.5)):
            s["arms"][sd] = arm(sw=35 + 10 * S2(t + o), ab=48 + 8 * S2(t + o + 0.25), el=55 + 15 * S2(2 * t + o), fin=10, tw=-40)
        s["legs"] = {sd: leg(fy=16, kout=0.3) for sd in "lr"}
        s["legs"]["l"]["dy"] = -0.08
        s["legs"]["r"]["dy"] = 0.10
        s["w"] = 0.23
        return s
    make("SQ_Idle_Menace", "Idle", 49, idle_menace, True)

# ============================================================ ABILITIES (one-shots)
def T(keys, f):
    return track(keys, f)

def neutral_ends(N, a=6, b=None):
    """weight track 0 -> 1 -> 0 used to blend from/to neutral"""
    b = b if b is not None else N - 7
    return [(1, 0, "s"), (a, 1, "s"), (b, 1, "s"), (N, 0, "s")]

def blend_arm(w, a):
    n = arm()
    return {k: lerp(n[k], a.get(k, n[k]), w) for k in n}

def abilities():
    abil = {}

    def reg(name, N, base="Idle"):
        def deco(fn):
            abil[name] = (N, fn, base)
            return fn
        return deco

    @reg("TeamDamage", 32)
    def _(f):
        s = spec()
        s["hip"][2] = T([(1, -0.02, "s"), (8, -0.10, "s"), (12, -0.01, "o"), (16, -0.05, "s"), (19, -0.01, "o"), (25, -0.03, "s"), (32, -0.02, "s")], f)
        s["sp"][0] = T([(1, 3, "s"), (8, 16, "s"), (12, -8, "o"), (25, -4, "s"), (32, 3, "s")], f)
        s["ch"][0] = T([(1, 2, "s"), (8, 10, "s"), (12, -12, "o"), (16, -6, "s"), (19, -12, "o"), (25, -6, "s"), (32, 2, "s")], f)
        s["ch"][2] = T([(1, 0, "s"), (8, 15, "s"), (12, -10, "o"), (25, -5, "s"), (32, 0, "s")], f)
        s["hd"][0] = T([(1, 0, "s"), (8, 10, "s"), (12, -24, "o"), (25, -15, "s"), (32, 0, "s")], f)
        sw = T([(1, 5, "s"), (8, 40, "s"), (12, 172, "k"), (16, 148, "s"), (19, 172, "k"), (25, 160, "s"), (32, 5, "s")], f)
        el = T([(1, 15, "s"), (8, 120, "s"), (12, 8, "o"), (16, 50, "s"), (19, 8, "o"), (25, 20, "s"), (32, 15, "s")], f)
        s["arms"]["r"] = arm(sw=sw, ab=T([(1, 10, "s"), (8, 30, "s"), (12, 12, "s"), (32, 10, "s")], f), el=el, tw=T([(1, 0, "s"), (12, 60, "s"), (32, 0, "s")], f))
        w = T(neutral_ends(32, 8, 25), f)
        s["arms"]["l"] = blend_arm(w, arm(sw=-12, ab=28, el=95, fin=10, tw=-40))
        return s

    @reg("Hauler", 38)
    def _(f):
        s = spec()
        s["hip"][2] = T([(1, -0.02, "s"), (10, -0.15, "s"), (14, -0.03, "o"), (19, -0.04, "s"), (23, -0.11, "s"), (30, -0.07, "s"), (38, -0.02, "s")], f)
        s["sp"][0] = T([(1, 3, "s"), (10, 38, "s"), (15, -12, "o"), (19, -6, "s"), (24, 16, "s"), (31, 12, "s"), (38, 3, "s")], f)
        s["ch"][0] = T([(1, 2, "s"), (10, 18, "s"), (15, -10, "o"), (24, 6, "s"), (38, 2, "s")], f)
        s["ch"][2] = T([(1, 0, "s"), (10, 0, "s"), (15, -25, "o"), (24, -18, "s"), (38, 0, "s")], f)
        s["ch"][1] = T([(1, 0, "s"), (15, 0, "s"), (24, 8, "s"), (38, 0, "s")], f)
        s["hd"][0] = T([(1, 0, "s"), (10, -20, "s"), (15, -6, "s"), (24, -12, "s"), (38, 0, "s")], f)
        s["hd"][2] = T([(1, 0, "s"), (15, 12, "s"), (24, 22, "s"), (38, 0, "s")], f)
        # both hands: low reach -> swing up to grip a sack above the right shoulder -> held there
        for sd in "lr":
            s["arms"][sd] = arm(
                sw=T([(1, 5, "s"), (10, 55, "s"), (16, 165, "s"), (24, 150, "s"), (31, 150, "s"), (38, 5, "s")], f),
                ab=T([(1, 10, "s"), (10, 12, "s"), (15, 5 if sd == "l" else 50, "o"), (31, 5 if sd == "l" else 50, "s"), (38, 10, "s")], f),
                el=T([(1, 15, "s"), (10, 10, "s"), (16, 95, "s"), (24, 110, "s"), (31, 110, "s"), (38, 15, "s")], f),
                fin=T([(1, 0, "s"), (10, 20, "s"), (15, 0, "s"), (38, 0, "s")], f),
                tw=T([(1, 0, "s"), (15, 50, "s"), (31, 50, "s"), (38, 0, "s")], f))
        s["legs"] = {sd: leg(fy=14, kout=0.25) for sd in "lr"}
        s["w"] = 0.21
        return s

    @reg("SlowSouls", 40)
    def _(f):
        s = spec()
        tr = math.sin(f * 2.4) * T([(1, 0, "s"), (12, 0, "s"), (14, 1, "s"), (32, 1, "s"), (36, 0, "s")], f)
        s["hip"][2] = T([(1, -0.02, "s"), (8, -0.07, "s"), (13, 0.0, "o"), (32, -0.01, "s"), (40, -0.02, "s")], f)
        s["sp"][0] = T([(1, 3, "s"), (8, 18, "s"), (13, -14, "o"), (32, -12, "s"), (40, 3, "s")], f)
        s["ch"][0] = T([(1, 2, "s"), (8, 14, "s"), (13, -18, "o"), (32, -14, "s"), (40, 2, "s")], f) + tr * 2
        s["hd"][0] = T([(1, 0, "s"), (8, 20, "s"), (13, -45, "o"), (32, -40, "s"), (40, 0, "s")], f)
        s["hd"][1] = tr * 6
        for sd in "lr":
            wav = 8 * math.sin(f * 0.6 + (0 if sd == "l" else 1.7)) * T([(1, 0, "s"), (14, 1, "s"), (32, 1, "s"), (38, 0, "s")], f)
            s["arms"][sd] = arm(sw=T([(1, 5, "s"), (8, 40, "s"), (13, 22, "o"), (32, 26, "s"), (40, 5, "s")], f) + wav,
                                ab=T([(1, 10, "s"), (8, 10, "s"), (13, 105, "o"), (32, 100, "s"), (40, 10, "s")], f) + tr * 3,
                                el=T([(1, 15, "s"), (8, 110, "s"), (13, 12, "o"), (32, 18, "s"), (40, 15, "s")], f),
                                fin=T([(1, 0, "s"), (8, 40, "s"), (13, 0, "s"), (40, 0, "s")], f),
                                tw=T([(1, 0, "s"), (13, -70, "s"), (32, -70, "s"), (40, 0, "s")], f))
        s["legs"] = {sd: leg(fy=10, fp=T([(1, 0, "s"), (10, 0, "s"), (13, 18, "o"), (32, 15, "s"), (38, 0, "s")], f)) for sd in "lr"}
        return s

    @reg("Builder", 36)
    def _(f):
        s = spec()
        s["hip"][2] = T([(1, -0.02, "s"), (9, -0.04, "s"), (14, -0.26, "i"), (17, -0.23, "s"), (27, -0.22, "s"), (36, -0.02, "s")], f)
        s["hip"][1] = T([(1, 0, "s"), (14, 0.06, "i"), (27, 0.06, "s"), (36, 0, "s")], f)
        s["sp"][0] = T([(1, 3, "s"), (9, -12, "s"), (14, 16, "i"), (16, 12, "s"), (27, 10, "s"), (36, 3, "s")], f)
        s["ch"][0] = T([(1, 2, "s"), (9, -8, "s"), (14, 14, "i"), (27, 10, "s"), (36, 2, "s")], f)
        s["ch"][2] = T([(1, 0, "s"), (9, 12, "s"), (14, -6, "i"), (36, 0, "s")], f)
        s["hd"][0] = T([(1, 0, "s"), (9, -20, "s"), (14, -4, "i"), (17, -10, "s"), (36, 0, "s")], f)
        s["arms"]["r"] = arm(sw=T([(1, 5, "s"), (9, 175, "o"), (11, 182, "s"), (14, 62, "c"), (16, 74, "o"), (18, 64, "s"), (27, 66, "s"), (36, 5, "s")], f),
                             ab=T([(1, 10, "s"), (9, 25, "s"), (14, 30, "s"), (36, 10, "s")], f),
                             el=T([(1, 15, "s"), (9, 85, "o"), (14, 2, "i"), (16, 12, "s"), (36, 15, "s")], f),
                             tw=T([(1, 0, "s"), (9, 60, "s"), (27, 60, "s"), (36, 0, "s")], f))
        w = T(neutral_ends(36, 9, 28), f)
        s["arms"]["l"] = blend_arm(w, arm(sw=62, ab=42, el=22, fin=10, tw=-40))
        # left foot steps forward, right knee drops to the ground (toes tucked)
        lift = T([(1, 0, "s"), (5, 0, "s"), (9, 0.06, "s"), (13, 0, "s"), (28, 0, "s"), (31, 0.05, "s"), (35, 0, "s")], f)
        s["legs"]["l"] = leg(dy=T([(1, 0, "s"), (5, 0, "s"), (13, -0.16, "s"), (28, -0.16, "s"), (35, 0, "s")], f), dz=lift, fy=6)
        s["legs"]["r"] = leg(dy=T([(1, 0, "s"), (14, 0.12, "s"), (27, 0.12, "s"), (36, 0, "s")], f),
                             fp=T([(1, 0, "s"), (14, 62, "i"), (27, 62, "s"), (36, 0, "s")], f), tp=0, fy=6)
        return s

    @reg("Banker", 42)
    def _(f):
        s = spec()
        cnt = max(0.0, math.sin((f - 20) * math.pi / 5.0)) * T([(1, 0, "s"), (19, 0, "s"), (21, 1, "s"), (34, 1, "s"), (36, 0, "s")], f)
        s["hip"][2] = -0.04
        s["sp"][0] = T([(1, 3, "s"), (8, 6, "s"), (34, 8, "s"), (42, 3, "s")], f)
        s["ch"][0] = T([(1, 2, "s"), (8, 4, "s"), (34, 4, "s"), (42, 2, "s")], f)
        s["hd"][0] = T([(1, 0, "s"), (8, 10, "s"), (34, 8, "s"), (42, 0, "s")], f) + 8 * cnt
        s["hd"][1] = T([(1, 0, "s"), (8, 10, "s"), (20, 10, "s"), (24, -6, "s"), (42, 0, "s")], f)
        w = T(neutral_ends(42, 8, 35), f)
        s["arms"]["l"] = blend_arm(w, arm(sw=68, ab=34, el=30, fin=30, tw=-80))       # cupped palm held out
        pour = T([(1, 0, "s"), (8, 0, "s"), (12, 1, "s"), (18, 1, "s"), (21, 0, "s"), (42, 0, "s")], f)
        s["arms"]["r"] = blend_arm(w, arm(sw=98 - 22 * cnt, ab=22, el=40 + 18 * cnt, fin=38, tw=-10 + 110 * pour + 8 * math.sin(f * 2.0) * pour))
        return s

    @reg("DashStrike", 26)
    def _(f):
        s = spec()
        s["hip"] = [0.0, T([(1, 0, "s"), (7, 0.04, "s"), (11, -0.10, "o"), (16, -0.09, "s"), (26, 0, "s")], f),
                    T([(1, -0.02, "s"), (7, -0.08, "s"), (11, -0.14, "o"), (16, -0.13, "s"), (26, -0.02, "s")], f)]
        s["hr"][2] = T([(1, 0, "s"), (7, -25, "s"), (11, 20, "o"), (16, 18, "s"), (26, 0, "s")], f)
        s["sp"][0] = T([(1, 3, "s"), (7, 2, "s"), (11, 16, "o"), (16, 13, "s"), (26, 3, "s")], f)
        s["ch"][2] = T([(1, 0, "s"), (7, -25, "s"), (11, 25, "o"), (16, 20, "s"), (26, 0, "s")], f)
        s["hd"] = [T([(1, 0, "s"), (11, -20, "o"), (16, -18, "s"), (26, 0, "s")], f), 0, T([(1, 0, "s"), (7, 35, "s"), (11, -40, "o"), (16, -35, "s"), (26, 0, "s")], f)]
        s["arms"]["r"] = arm(sw=T([(1, 5, "s"), (4, 12, "s"), (7, -40, "s"), (11, 88, "k"), (16, 84, "s"), (26, 5, "s")], f),
                             ab=T([(1, 10, "s"), (7, 20, "s"), (11, -2, "o"), (26, 10, "s")], f),
                             el=T([(1, 15, "s"), (7, 125, "s"), (11, 0, "o"), (16, 6, "s"), (26, 15, "s")], f),
                             tw=T([(1, 0, "s"), (7, 0, "s"), (11, 90, "o"), (16, 90, "s"), (26, 0, "s")], f))
        s["arms"]["l"] = arm(sw=T([(1, 5, "s"), (7, 70, "s"), (11, -30, "o"), (16, -25, "s"), (26, 5, "s")], f),
                             ab=T([(1, 10, "s"), (7, 15, "s"), (11, 30, "o"), (26, 10, "s")], f),
                             el=T([(1, 15, "s"), (7, 70, "s"), (11, 30, "o"), (26, 15, "s")], f), fin=T([(1, 0, "s"), (7, 30, "s"), (11, 0, "s")], f))
        s["legs"]["l"] = leg(dy=T([(1, 0, "s"), (5, 0, "s"), (11, -0.24, "o"), (19, -0.24, "s"), (25, 0, "s")], f),
                             dz=T([(1, 0, "s"), (6, 0, "s"), (8, 0.07, "s"), (11, 0, "s"), (19, 0, "s"), (22, 0.05, "s"), (25, 0, "s")], f), fy=8)
        s["legs"]["r"] = leg(dy=T([(1, 0, "s"), (11, 0.10, "o"), (19, 0.10, "s"), (26, 0, "s")], f),
                             fp=T([(1, 0, "s"), (9, 0, "s"), (11, 35, "o"), (19, 30, "s"), (26, 0, "s")], f), tp=0, fy=20)
        return s

    @reg("OnTheJob", 42)
    def _(f):
        s = spec()
        sc_ = T([(1, 0, "s"), (4, 1, "s"), (22, 1, "s"), (25, 0, "s"), (42, 0, "s")], f)
        th = T([(1, 0, "s"), (23, 0, "s"), (27, 1, "o"), (35, 1, "s"), (41, 0, "s"), (42, 0, "s")], f)
        jit = math.sin(f * 2.9) * sc_
        s["hip"][2] = -0.03 + 0.012 * jit + T([(1, 0, "s"), (24, 0, "s"), (27, 0.02, "o"), (35, 0.015, "s"), (42, 0, "s")], f)
        s["sp"][0] = 3 + 5 * sc_ - 10 * th
        s["ch"][0] = 2 + 3 * sc_ - 6 * th
        s["ch"][2] = 6 * sc_ + 10 * th
        s["hd"] = [8 * sc_ + 6 * math.sin(f * 1.45) * sc_ - 14 * th, 14 * th, 6 * jit + 18 * th]
        s["arms"]["l"] = blend_arm(max(sc_, 0), arm(sw=76, ab=30, el=42, fin=40, tw=-60))     # notepad held out
        s["arms"]["r"] = blend_arm(sc_ + th, arm(
            sw=lerp(80 + 5 * jit, 100, th), ab=lerp(16, 28, th), el=lerp(40 + 15 * math.sin(f * 3.7), 40, th),
            fin=lerp(42 + 10 * math.sin(f * 2.1), 5, th), tw=lerp(-20, -90, th)))
        return s

    @reg("Fetch", 38)
    def _(f):
        s = spec()
        sn = math.sin(f * 2.2) * T([(1, 0, "s"), (5, 1, "s"), (14, 1, "s"), (16, 0, "s")], f)
        s["hip"] = [0.0, T([(1, 0, "s"), (14, 0.04, "s"), (17, 0.06, "s"), (21, -0.16, "o"), (30, -0.14, "s"), (38, 0, "s")], f),
                    T([(1, -0.02, "s"), (8, -0.09, "s"), (17, -0.19, "s"), (20, 0.06, "o"), (23, -0.17, "i"), (30, -0.14, "s"), (38, -0.02, "s")], f)]
        s["sp"][0] = T([(1, 3, "s"), (8, 36, "s"), (17, 40, "s"), (20, 12, "o"), (23, 42, "i"), (30, 36, "s"), (38, 3, "s")], f)
        s["ch"][0] = T([(1, 2, "s"), (8, 16, "s"), (20, 0, "o"), (23, 18, "s"), (38, 2, "s")], f)
        s["hd"] = [T([(1, 0, "s"), (8, -30, "s"), (17, -40, "s"), (20, -20, "s"), (23, -35, "s"), (38, 0, "s")], f) + 4 * sn,
                   6 * sn, T([(1, 0, "s"), (6, 25, "s"), (10, -25, "s"), (14, 0, "s"), (38, 0, "s")], f) + 4 * sn]
        for sd, o in (("l", 0), ("r", 2)):
            s["arms"][sd] = arm(sw=T([(1, 5, "s"), (8, 40, "s"), (17, -20, "s"), (20, 110, "o"), (23, 80, "i"), (30, 75, "s"), (38, 5, "s")], f),
                                ab=T([(1, 10, "s"), (8, 20, "s"), (17, 15, "s"), (20, 22, "s"), (38, 10, "s")], f),
                                el=T([(1, 15, "s"), (8, 60, "s"), (17, 30, "s"), (20, 25, "s"), (23, 45, "s"), (30, 50, "s"), (38, 15, "s")], f),
                                fin=8, tw=-40)
        s["legs"] = {sd: leg(dy=T([(1, 0, "s"), (17, 0.0, "s"), (21, -0.17, "o"), (30, -0.15, "s"), (38, 0, "s")], f),
                             dz=T([(1, 0, "s"), (17, 0, "s"), (19, 0.12, "o"), (21, 0, "i"), (38, 0, "s")], f),
                             fp=T([(1, 0, "s"), (13, 0, "s"), (17, 25, "s"), (19, 40, "s"), (21, 0, "s"), (38, 0, "s")], f), tp=0, fy=12, kout=0.3) for sd in "lr"}
        s["w"] = 0.19
        return s

    @reg("Bulwark", 32)
    def _(f):
        s = spec()
        hit = math.sin((f - 11) * 1.9) * T([(1, 0, "s"), (10, 0, "s"), (11, 1, "s"), (16, 0, "s")], f)
        s["hip"] = [0, T([(1, 0, "s"), (8, 0.03, "s"), (26, 0.03, "s"), (32, 0, "s")], f) + 0.02 * hit,
                    T([(1, -0.02, "s"), (8, -0.17, "o"), (26, -0.16, "s"), (32, -0.02, "s")], f)]
        s["sp"][0] = T([(1, 3, "s"), (8, 9, "o"), (26, 8, "s"), (32, 3, "s")], f) - 4 * hit
        s["ch"][0] = T([(1, 2, "s"), (8, 4, "o"), (26, 4, "s"), (32, 2, "s")], f)
        s["hd"][0] = T([(1, 0, "s"), (8, -4, "s"), (26, -4, "s"), (32, 0, "s")], f) + 6 * hit
        w = T(neutral_ends(32, 8, 26), f)
        s["arms"]["l"] = blend_arm(w, arm(sw=86, ab=34, el=62, fin=80, tw=-90))          # forearm held across, well out in front
        s["arms"]["r"] = blend_arm(w, arm(sw=72, ab=26, el=70, fin=62, tw=-40))          # palm braced behind the "shield"
        for sd, y in (("l", -0.12), ("r", 0.12)):
            s["legs"][sd] = leg(dx=SX[sd] * T([(1, 0, "s"), (7, 0.08, "s"), (26, 0.08, "s"), (31, 0, "s")], f),
                                dy=T([(1, 0, "s"), (7, y, "s"), (26, y, "s"), (31, 0, "s")], f),
                                dz=T([(1, 0, "s"), (3 + (sd == "r") * 2, 0.06, "s"), (6 + (sd == "r") * 1, 0, "s"), (26, 0, "s"), (28, 0.05, "s"), (31, 0, "s")], f),
                                fy=T([(1, 6, "s"), (7, 22, "s"), (26, 22, "s"), (31, 6, "s")], f), kout=0.35)
        return s

    @reg("Collections", 42)
    def _(f):
        s = spec()
        rub = T([(1, 0, "s"), (5, 1, "s"), (20, 1, "s"), (23, 0, "s"), (42, 0, "s")], f)
        pt = T([(1, 0, "s"), (21, 0, "s"), (25, 1, "o"), (35, 1, "s"), (41, 0, "s"), (42, 0, "s")], f)
        r = math.sin(f * 1.6)
        s["hip"][2] = -0.05 + 0.01 * r * rub
        s["sp"][0] = 3 + 6 * rub + 10 * pt
        s["ch"][0] = 2 + 4 * rub + 4 * pt
        s["ch"][2] = 5 * r * rub + 12 * pt
        s["hd"] = [2 * rub - 18 * pt, 8 * r * rub, -6 * r * rub - 10 * pt]
        s["arms"]["l"] = blend_arm(rub + pt * 0.8, arm(sw=lerp(74 + 7 * r, 40, pt), ab=lerp(22, 18, pt), el=lerp(48 - 6 * r, 90, pt),
                                                       fin=lerp(56, 60, pt), tw=-50))
        s["arms"]["r"] = blend_arm(rub + pt, arm(sw=lerp(74 - 7 * r, 92, pt), ab=lerp(22, 4, pt), el=lerp(48 + 6 * r, 2, pt),
                                                 fin=lerp(56, 4, pt), tw=lerp(-50, 30, pt)))
        return s

    @reg("Triage", 28)
    def _(f):
        s = spec()
        s["hip"] = [0, T([(1, 0, "s"), (12, -0.06, "o"), (18, -0.06, "s"), (28, 0, "s")], f),
                    T([(1, -0.02, "s"), (8, -0.03, "s"), (12, -0.10, "o"), (18, -0.09, "s"), (28, -0.02, "s")], f)]
        s["sp"][0] = T([(1, 3, "s"), (8, -10, "s"), (12, 12, "o"), (18, 10, "s"), (28, 3, "s")], f)
        s["ch"][2] = T([(1, 0, "s"), (8, -18, "s"), (12, 14, "o"), (18, 12, "s"), (28, 0, "s")], f)
        s["hd"] = [T([(1, 0, "s"), (8, -14, "s"), (12, -8, "s"), (28, 0, "s")], f), 0, T([(1, 0, "s"), (8, 12, "s"), (12, -6, "s"), (28, 0, "s")], f)]
        push = 4 * math.sin(f * 1.5) * T([(1, 0, "s"), (12, 0, "s"), (13, 1, "s"), (18, 0, "s")], f)
        s["arms"]["r"] = arm(sw=T([(1, 5, "s"), (8, 158, "o"), (12, 82, "c"), (14, 74, "o"), (18, 84, "s"), (28, 5, "s")], f) + push,
                             ab=T([(1, 10, "s"), (8, 22, "s"), (12, 12, "s"), (28, 10, "s")], f),
                             el=T([(1, 15, "s"), (8, 95, "o"), (12, 0, "i"), (18, 4, "s"), (28, 15, "s")], f),
                             tw=T([(1, 0, "s"), (8, 90, "s"), (18, 90, "s"), (28, 0, "s")], f))
        w = T(neutral_ends(28, 8, 20), f)
        s["arms"]["l"] = blend_arm(w, arm(sw=T([(1, 70, "s"), (8, 78, "s"), (12, 45, "s")], f), ab=40, el=25, fin=15, tw=-70))
        s["legs"]["l"] = leg(dy=T([(1, 0, "s"), (6, 0, "s"), (12, -0.14, "o"), (21, -0.14, "s"), (27, 0, "s")], f),
                             dz=T([(1, 0, "s"), (7, 0, "s"), (9, 0.05, "s"), (12, 0, "s"), (21, 0, "s"), (24, 0.04, "s"), (27, 0, "s")], f))
        return s

    @reg("Scorch", 24)
    def _(f):
        s = spec()
        shake = math.sin(f * 2.6) * T([(1, 0, "s"), (9, 0, "s"), (10, 1, "s"), (16, 1, "s"), (19, 0, "s")], f)
        s["hip"][2] = T([(1, -0.02, "s"), (6, -0.06, "s"), (9, -0.03, "o"), (24, -0.02, "s")], f)
        s["hr"][2] = T([(1, 0, "s"), (6, -12, "s"), (9, 10, "o"), (24, 0, "s")], f)
        s["ch"][2] = T([(1, 0, "s"), (6, -22, "s"), (9, 22, "o"), (16, 18, "s"), (24, 0, "s")], f)
        s["ch"][1] = T([(1, 0, "s"), (6, 6, "s"), (9, -8, "o"), (24, 0, "s")], f)
        s["hd"] = [T([(1, 0, "s"), (6, 8, "s"), (9, -6, "s"), (24, 0, "s")], f), 0, T([(1, 0, "s"), (6, 25, "s"), (9, -25, "o"), (16, -20, "s"), (24, 0, "s")], f)]
        s["arms"]["r"] = arm(sw=T([(1, 5, "s"), (6, 45, "s"), (9, 82, "k"), (16, 78, "s"), (24, 5, "s")], f) + 3 * shake,
                             ab=T([(1, 10, "s"), (6, -5, "s"), (9, 48, "o"), (16, 45, "s"), (24, 10, "s")], f),
                             el=T([(1, 15, "s"), (6, 135, "s"), (9, 4, "o"), (16, 10, "s"), (24, 15, "s")], f),
                             fin=T([(1, 0, "s"), (6, 60, "s"), (9, 0, "o")], f),
                             tw=T([(1, 0, "s"), (6, -60, "s"), (9, 70, "o"), (16, 60, "s"), (24, 0, "s")], f) + 35 * shake)
        w = T(neutral_ends(24, 6, 18), f)
        s["arms"]["l"] = blend_arm(w, arm(sw=-10, ab=28, el=40, tw=-30))
        return s

    @reg("Lookout", 44)
    def _(f):
        s = spec()
        sh = T([(1, 0, "s"), (8, 1, "s"), (24, 1, "s"), (29, 0, "s"), (44, 0, "s")], f)
        pt = T([(1, 0, "s"), (24, 0, "s"), (28, 1, "o"), (37, 1, "s"), (43, 0, "s"), (44, 0, "s")], f)
        scan = T([(1, 0, "s"), (8, 0, "s"), (13, -35, "s"), (16, -32, "s"), (21, 35, "s"), (24, 30, "s"), (28, 8, "s"), (37, 10, "s"), (44, 0, "s")], f)
        s["hip"][2] = -0.02 + 0.02 * sh
        s["hr"][2] = scan * 0.2
        s["sp"][0] = 3 + 8 * sh + 10 * pt
        s["ch"][2] = scan * 0.35 + 8 * pt
        s["hd"] = [-6 * sh - 8 * pt, -6 * pt, scan * 0.6]
        s["legs"] = {sd: leg(fp=22 * sh, tp=0) for sd in "lr"}
        # left hand shading the eyes (IK onto the brow), right arm points out
        s["arms"]["l"] = blend_arm(sh, arm(sw=115, ab=58, el=115, fin=75, tw=-80))
        s["arms"]["r"] = blend_arm(pt, arm(sw=95, ab=12, el=0, fin=0, tw=60))
        return s

    @reg("Spite", 38)
    def _(f):
        s = spec()
        tr = math.sin(f * 3.1) * T([(1, 0, "s"), (13, 0, "s"), (15, 1, "s"), (30, 1, "s"), (33, 0, "s")], f)
        s["hip"][2] = T([(1, -0.02, "s"), (7, 0.0, "s"), (10, -0.13, "i"), (13, -0.10, "s"), (30, -0.06, "s"), (38, -0.02, "s")], f) + 0.01 * tr
        s["sp"][0] = T([(1, 3, "s"), (7, 12, "s"), (10, 28, "i"), (14, -6, "o"), (30, -8, "s"), (38, 3, "s")], f)
        s["ch"][0] = T([(1, 2, "s"), (7, 10, "s"), (10, 18, "i"), (14, -18, "o"), (30, -15, "s"), (38, 2, "s")], f) + 2 * tr
        s["hd"] = [T([(1, 0, "s"), (7, 15, "s"), (10, 20, "s"), (14, -42, "o"), (30, -38, "s"), (38, 0, "s")], f), 5 * tr, 4 * tr]
        for sd in "lr":
            s["arms"][sd] = arm(sw=T([(1, 5, "s"), (7, 50, "s"), (10, -25, "i"), (14, -32, "s"), (30, -30, "s"), (38, 5, "s")], f) + 3 * tr,
                                ab=T([(1, 10, "s"), (7, 45, "s"), (10, 30, "s"), (14, 50, "o"), (30, 48, "s"), (38, 10, "s")], f),
                                el=T([(1, 15, "s"), (7, 130, "s"), (10, 10, "i"), (14, 25, "s"), (38, 15, "s")], f), tw=-60)
        s["legs"]["r"] = leg(dz=T([(1, 0, "s"), (7, 0.20, "o"), (10, 0, "i"), (38, 0, "s")], f),
                             dy=T([(1, 0, "s"), (7, -0.06, "s"), (10, -0.03, "s"), (38, -0.03, "s")], f),
                             fp=T([(1, 0, "s"), (7, -10, "s"), (10, 0, "i")], f), kout=0.2)
        s["legs"]["l"] = leg(fy=10)
        return s

    @reg("Audit", 28)
    def _(f):
        s = spec()
        imp = math.sin((f - 13) * 2.0) * T([(1, 0, "s"), (12, 0, "s"), (13, 1, "s"), (18, 0, "s")], f)
        s["hip"][2] = T([(1, -0.02, "s"), (9, 0.0, "s"), (13, -0.09, "i"), (19, -0.07, "s"), (28, -0.02, "s")], f) + 0.01 * imp
        s["sp"][0] = T([(1, 3, "s"), (9, -12, "s"), (13, 12, "i"), (19, 10, "s"), (28, 3, "s")], f)
        s["ch"][0] = T([(1, 2, "s"), (9, -8, "s"), (13, 4, "i"), (28, 2, "s")], f)
        s["ch"][2] = T([(1, 0, "s"), (9, 15, "s"), (13, -5, "i"), (28, 0, "s")], f)
        s["hd"][0] = T([(1, 0, "s"), (9, -22, "s"), (13, 4, "i"), (19, 2, "s"), (28, 0, "s")], f) + 6 * imp
        s["arms"]["r"] = arm(sw=T([(1, 5, "s"), (9, 168, "o"), (13, 68, "c"), (15, 62, "o"), (19, 70, "s"), (28, 5, "s")], f) + 3 * imp,
                             ab=T([(1, 10, "s"), (9, 22, "s"), (13, 26, "s"), (28, 10, "s")], f),
                             el=T([(1, 15, "s"), (9, 95, "o"), (13, 30, "i"), (19, 32, "s"), (28, 15, "s")], f),
                             fin=T([(1, 0, "s"), (13, 20, "s"), (28, 0, "s")], f), tw=-80 * T(neutral_ends(28, 6, 21), f))
        w = T(neutral_ends(28, 7, 21), f)
        s["arms"]["l"] = blend_arm(w, arm(sw=66, ab=38, el=20, fin=20, tw=-60))
        return s

    @reg("Rooftops", 32)
    def _(f):
        s = spec()
        air = T([(1, 0, "s"), (11, 0, "s"), (15, 1, "o"), (19, 1, "s"), (23, 0, "i"), (32, 0, "s")], f)
        s["hip"][2] = T([(1, -0.02, "s"), (10, -0.21, "s"), (12, -0.08, "o"), (16, 0.28, "o"), (19, 0.26, "s"), (23, -0.02, "i"),
                         (26, -0.15, "o"), (32, -0.02, "s")], f)
        s["sp"][0] = T([(1, 3, "s"), (10, 36, "s"), (14, -6, "o"), (19, 4, "s"), (23, 20, "s"), (26, 28, "s"), (32, 3, "s")], f)
        s["ch"][0] = T([(1, 2, "s"), (10, 12, "s"), (14, -12, "o"), (26, 8, "s"), (32, 2, "s")], f)
        s["hd"][0] = T([(1, 0, "s"), (10, -30, "s"), (14, -18, "s"), (26, -15, "s"), (32, 0, "s")], f)
        for sd in "lr":
            s["arms"][sd] = arm(sw=T([(1, 5, "s"), (9, -50, "s"), (15, 165, "s"), (19, 150, "s"), (23, 40, "s"), (26, 30, "s"), (32, 5, "s")], f),
                                ab=T([(1, 10, "s"), (10, 18, "s"), (14, 28, "s"), (23, 35, "s"), (32, 10, "s")], f),
                                el=T([(1, 15, "s"), (10, 10, "s"), (14, 20, "s"), (23, 40, "s"), (32, 15, "s")], f), tw=20)
            s["legs"][sd] = leg(dz=0.30 * air, fp=T([(1, 0, "s"), (9, 0, "s"), (12, 45, "o"), (16, 30, "s"), (21, 10, "s"), (23, 0, "s")], f),
                                tp=T([(1, 0, "s"), (12, 0, "s"), (16, 25, "s"), (23, 0, "s")], f), fy=8, kout=0.2)
        return s

    @reg("LastRites", 44)
    def _(f):
        s = spec()
        pray = T([(1, 0, "s"), (7, 1, "s"), (14, 1, "s"), (18, 0, "s"), (44, 0, "s")], f)
        up = T([(1, 0, "s"), (15, 0, "s"), (20, 1, "o"), (36, 1, "s"), (43, 0, "s"), (44, 0, "s")], f)
        glow = math.sin(f * 0.5) * up
        s["hip"][2] = -0.02 - 0.05 * pray + 0.015 * up
        s["sp"][0] = 3 + 4 * pray - 12 * up
        s["ch"][0] = 2 + 2 * pray - 12 * up + 2 * glow
        s["hd"] = [12 * pray - 28 * up, 0, 0]
        s["legs"] = {sd: leg(fp=16 * up, tp=0) for sd in "lr"}
        for sd in "lr":
            p = arm(sw=72, ab=24, el=62, fin=76, tw=-95)          # palms together, held out in front of the chest
            u = arm(sw=165 + 3 * glow, ab=32 + 4 * glow, el=12, tw=70)
            s["arms"][sd] = blend_arm(1.0, {k: lerp(arm()[k], p[k], pray) * (1 - up) + u[k] * up for k in p})
        return s

    @reg("MobileBank", 40)
    def _(f):
        s = spec()
        st = math.sin(f * 2.8) * T([(1, 0, "s"), (10, 0, "s"), (12, 1, "s"), (18, 1, "s"), (20, 0, "s")], f)
        wob = math.sin((f - 22) * 0.45) * T([(1, 0, "s"), (22, 0, "s"), (24, 1, "s"), (33, 1, "s"), (37, 0, "s")], f)
        s["hip"] = [0.03 * wob, 0, T([(1, -0.02, "s"), (10, -0.20, "s"), (18, -0.18, "s"), (23, -0.02, "o"), (33, -0.04, "s"), (40, -0.02, "s")], f) + 0.008 * st]
        s["hr"][1] = -6 * wob
        s["sp"][0] = T([(1, 3, "s"), (10, 30, "s"), (18, 26, "s"), (23, -6, "o"), (33, -8, "s"), (40, 3, "s")], f)
        s["sp"][1] = 6 * wob
        s["ch"][0] = T([(1, 2, "s"), (10, 12, "s"), (23, -10, "o"), (33, -8, "s"), (40, 2, "s")], f) + 2 * st
        s["ch"][1] = 6 * wob
        s["hd"] = [T([(1, 0, "s"), (10, -15, "s"), (18, -20, "s"), (23, -28, "s"), (33, -25, "s"), (40, 0, "s")], f), 4 * st - 6 * wob, 3 * st]
        for sd in "lr":
            s["arms"][sd] = arm(sw=T([(1, 5, "s"), (10, 42, "s"), (18, 42, "s"), (23, 168, "o"), (33, 165, "s"), (40, 5, "s")], f),
                                ab=T([(1, 10, "s"), (10, 24, "s"), (23, 40, "s"), (33, 40, "s"), (40, 10, "s")], f),
                                el=T([(1, 15, "s"), (10, 12, "s"), (18, 14, "s"), (23, 45, "o"), (33, 42, "s"), (40, 15, "s")], f) + 4 * st,
                                fin=T([(1, 0, "s"), (10, 20, "s"), (23, 62, "s"), (33, 62, "s"), (40, 0, "s")], f),
                                tw=T([(1, 0, "s"), (10, -60, "s"), (23, 60, "s"), (33, 60, "s"), (40, 0, "s")], f))
        s["legs"] = {sd: leg(fy=18, kout=0.3) for sd in "lr"}
        s["w"] = 0.22
        return s

    @reg("Seance", 44)
    def _(f):
        s = spec()
        w = T([(1, 0, "s"), (9, 1, "s"), (36, 1, "s"), (44, 0, "s")], f)
        a = 2 * math.pi * (f - 9) / 13.5                 # two circles between f9 and f36
        cs, sn = math.cos(a) * w, math.sin(a) * w
        s["hip"] = [0.03 * cs, 0.02 * sn, -0.04 * w - 0.02 * (1 - w)]
        s["hr"] = [3 * sn, -4 * cs, 0]
        s["sp"] = [3 + 6 * w + 5 * sn, 5 * cs, 0]
        s["ch"] = [2 - 8 * w + 4 * sn, 5 * cs, 0]
        s["hd"] = [-14 * w + 6 * sn, -10 * cs, 0]
        s["legs"] = {sd: leg(fy=12) for sd in "lr"}
        s["w"] = 0.2
        for sd in "lr":
            s["arms"][sd] = blend_arm(w, arm(sw=112 + 18 * sn * SX[sd], ab=38 + 18 * cs, el=30 + 10 * sn, fin=0, tw=-75))
        return s

    @reg("Stampede", 38)
    def _(f):
        s = spec()
        rear = T([(1, 0, "s"), (6, -0.3, "s"), (12, 1, "o"), (22, 1, "s"), (28, -0.5, "s"), (31, -0.4, "s"), (38, 0, "s")], f)
        paw = math.sin((f - 12) * 0.9) * T([(1, 0, "s"), (11, 0, "s"), (13, 1, "s"), (23, 1, "s"), (25, 0, "s")], f)
        s["hip"] = [0, 0.06 * max(rear, 0) - 0.08 * max(-rear, 0) * 1.5, -0.02 + 0.0 * rear - 0.14 * max(-rear, 0)]
        s["hr"][0] = -10 * max(rear, 0) + 10 * max(-rear, 0)
        s["sp"][0] = 3 - 22 * max(rear, 0) + 40 * max(-rear, 0)
        s["ch"][0] = 2 - 14 * max(rear, 0) + 10 * max(-rear, 0)
        s["hd"] = [-22 * max(rear, 0) - 20 * max(-rear, 0), 6 * paw, 8 * paw]
        ru, sl = max(rear, 0.0), min(1.0, max(-rear, 0.0) * 2)
        for sd, sg in (("l", 1), ("r", -1)):
            up_ = arm(sw=100 + 30 * paw * sg, ab=22, el=115 - 20 * paw * sg, fin=10, tw=-60)   # pawing hooves
            dn_ = arm(sw=70, ab=22, el=25, fin=10, tw=-60)                                     # slam down
            n_ = arm()
            s["arms"][sd] = {k: n_[k] + ru * (up_[k] - n_[k]) + sl * (dn_[k] - n_[k]) for k in n_}
        s["legs"]["l"] = leg(dz=0.17 * max(rear, 0) * (0.7 + 0.3 * abs(paw)), dy=-0.08 * max(rear, 0), fp=-10 * max(rear, 0), kout=0.1)
        s["legs"]["r"] = leg(fp=0, fy=8)
        return s

    @reg("Stopwatch", 30)
    def _(f):
        s = spec()
        hold = T(neutral_ends(30, 9, 23), f)
        click = T([(1, 0, "s"), (10, 0, "s"), (13, 1, "o"), (15, 0.6, "s"), (17, 1, "s"), (22, 0, "s"), (30, 0, "s")], f)
        jolt = T([(1, 0, "s"), (13, 0, "s"), (14, 1, "o"), (17, 0, "s")], f)
        s["hip"][2] = -0.02 - 0.03 * jolt
        s["sp"][0] = 3 + 4 * hold
        s["ch"] = [2 - 4 * hold + 6 * jolt, 0, 10 * hold]
        s["hd"] = [-4 * hold + 8 * jolt, 10 * hold, 14 * hold]
        s["arms"]["l"] = blend_arm(hold, arm(sw=108, ab=40, el=50, fin=40, tw=-90))      # watch held up and out, in front of the face
        s["arms"]["r"] = blend_arm(max(hold * 0.4, click), arm(sw=100, ab=8, el=60, fin=70, tw=-30) if click > 0.05 else arm(sw=15, ab=14, el=40))
        return s

    for name, (N, fn, base) in abil.items():
        make("SQ_Ability_" + name, base, N, fn, False)


# ============================================================ REMASTER 2026-11-07: new takes
# Better SQ_ versions of the KayKit takes the CSVs use most (new names; the KayKit originals stay untouched).
# Impact frames follow the code's timing: enemy AttackAnim = 45% of the clip, Upgrades AttackStyle ImpactFrac.
def remaster_new():
    # ---- locomotion
    # drop-in for Walking_A/B/C: same ground speed as Walking_A (0.74 m/s), 33 frames; bouncy, readable, loose arms
    walk_style("SQ_Walk_Remaster", 33, dict(D=0.786, duty=0.6, h=0.10, H=-0.045, bob=0.032, bobph=0.30, bskew=0.5, sway=0.04,
               hroll=7, hyaw=11, sp=5, chp=2, hdp=-5, nod=4, nodph=0.3, heel=18, toe=34, swingp=-8, lift_peak=0.42, fy=8,
               w=0.17, kout=0.16, sproll=3, chroll=-3, hdroll=4, asw=36, asw0=4, ab=14, abd=4, el=20, eld=30, chyaw=15, tw=12))
    # drop-in for Running_A/B/C: Running_B's ground speed (2.19 m/s) in a snappier 20-frame cycle with real flight
    walk_style("SQ_Run_Remaster", 21, dict(D=1.46, duty=0.34, h=0.24, H=-0.07, bob=-0.05, bobph=0.2, bskew=0.0, sway=0.015, yc=-0.06,
               hroll=6, hyaw=14, sp=17, chp=5, hdp=-14, nod=5, heel=10, toe=45, swingp=65, lift_peak=0.36, lift_pow=0.8,
               kout=0.08, asw=72, asw0=20, el=95, eld=10, ab=20, fin=15, chyaw=16, sproll=1, hdroll=2), base="Running_A")

    def idle_r(t):
        # contrapposto idle: weight on the left leg, hip cocked, slow breath, a glance to the side and back
        s = spec()
        br = S2(t * 2)
        shift = 0.5 + 0.5 * S2(t - 0.25)
        s["hip"] = [0.025 + 0.012 * S2(t), 0.0, -0.035 + 0.006 * br - 0.008 * shift]
        s["hr"] = [0, -5 - 2 * S2(t), 3 * S2(t + 0.1)]
        s["sp"] = [4 - 1.5 * br, 3 + 1.5 * S2(t), 0]
        s["ch"] = [2 - 2.5 * br, 2 + 1 * S2(t - 0.1), 2 * S2(t + 0.2)]
        look = track([(0, 0, "s"), (0.35, 0, "s"), (0.45, 28, "x"), (0.65, 24, "s"), (0.75, 0, "s"), (1.0, 0, "s")], t)
        s["hd"] = [-3 + 2 * br, -5 - 2 * S2(t - 0.2), look]
        for sd, o in (("l", 0.0), ("r", 0.15)):
            s["arms"][sd] = arm(sw=6 + 3 * S2(t - 0.2 + o), ab=12 + 3 * S2(2 * t + o), el=18 + 6 * S2(t - 0.3 + o), fin=6)
        s["legs"]["l"] = leg(fy=8)
        s["legs"]["r"] = leg(fy=16, dx=-0.02, dy=-0.04, fp=6 + 2 * S2(t))
        s["w"] = 0.17
        return s
    make("SQ_Idle_Remaster", "Idle", 61, idle_r, True)

    # ---- hit reactions (drop-in for Hit_A / Hit_B)
    @shot("SQ_Hit_A_R2", 18, dict(head=0.45, freq=3.4, exag=(1.0, 1.0, 1.0), lag={"hr": 0, "sp": 0, "ch": 0.5, "hd": 1.0}))
    def _(f):
        # struck in the chest: 2-frame snap back, arms fly up, then a quick wobbly recovery
        s = spec()
        k = T([(1, 0, "s"), (3, 1, "x"), (7, 0.7, "s"), (12, -0.15, "s"), (18, 0, "s")], f)
        s["hip"] = [0, 0.06 * k, -0.02 - 0.05 * max(k, 0)]
        s["hr"][0] = -8 * k
        s["sp"][0] = 3 - 14 * k
        s["ch"][0] = 2 - 10 * k
        s["hd"][0] = -14 * k
        for sd, o in (("l", 0), ("r", 1)):
            s["arms"][sd] = arm(sw=5 + 60 * T([(1, 0, "s"), (3 + o, 1, "x"), (9, 0.4, "s"), (18, 0, "s")], f),
                                ab=10 + 35 * max(k, 0), el=15 + 50 * max(k, 0), tw=20 * k)
        s["legs"]["r"] = leg(dy=0.06 * T([(1, 0, "s"), (4, 1, "o"), (14, 1, "s"), (18, 0, "s")], f),
                             dz=0.05 * T([(1, 0, "s"), (3, 1, "s"), (5, 0, "s"), (14, 0, "s"), (16, 0.6, "s"), (18, 0, "s")], f))
        return s

    @shot("SQ_Hit_B_R2", 22, dict(head=0.45, freq=3.0, exag=(1.0, 1.0, 1.0), lag={"hr": 0, "sp": 0, "ch": 0.5, "hd": 1.0}))
    def _(f):
        # struck from the right: twist and buckle left, stagger half a step, shake it off
        s = spec()
        k = T([(1, 0, "s"), (3, 1, "x"), (8, 0.8, "s"), (15, -0.1, "s"), (22, 0, "s")], f)
        sh = math.sin(f * 2.2) * T([(1, 0, "s"), (14, 0, "s"), (16, 1, "s"), (20, 0, "s")], f)
        s["hip"] = [0.06 * k, 0.02 * k, -0.02 - 0.07 * max(k, 0)]
        s["hr"] = [4 * k, 6 * k, 18 * k]
        s["sp"] = [3 + 10 * k, 5 * k, 12 * k]
        s["ch"] = [2 + 6 * k, 4 * k, 14 * k]
        s["hd"] = [6 * k, 8 * k, 10 * k + 10 * sh]
        s["arms"]["r"] = arm(sw=5 + 30 * k, ab=10 + 50 * max(k, 0), el=15 + 60 * max(k, 0), tw=-30 * k)
        s["arms"]["l"] = arm(sw=5 - 15 * k, ab=10 + 25 * max(k, 0), el=15 + 20 * max(k, 0))
        s["legs"]["l"] = leg(dx=0.07 * T([(1, 0, "s"), (5, 1, "o"), (17, 1, "s"), (22, 0, "s")], f),
                             dz=0.06 * T([(1, 0, "s"), (3, 1, "s"), (5, 0, "s"), (17, 0, "s"), (19, 0.6, "s"), (22, 0, "s")], f), fy=10)
        return s

    # ---- melee
    @shot("SQ_Attack_Chop_R2", 30, dict(pop=[(14, "lr", 0.2)], exag=(1.1, 1.1, 1.0), nod=40.0))
    def _(f):
        # 2H overhead chop, impact f14 (45%; Chop style 42% = f13.2): rise and arch back, whip down with a stomp
        s = spec()
        s["hip"] = [0.0, T([(1, 0, "s"), (10, 0.05, "s"), (14, -0.10, "c"), (20, -0.08, "s"), (30, 0, "s")], f),
                    T([(1, -0.02, "s"), (4, -0.06, "s"), (10, 0.01, "s"), (14, -0.15, "c"), (17, -0.12, "o"), (24, -0.09, "s"), (30, -0.02, "s")], f)]
        s["sp"][0] = T([(1, 3, "s"), (4, 8, "s"), (10, -16, "s"), (14, 16, "c"), (17, 18, "o"), (24, 10, "s"), (30, 3, "s")], f)
        s["ch"][0] = T([(1, 2, "s"), (10, -12, "s"), (14, 10, "c"), (17, 12, "o"), (30, 2, "s")], f)
        s["ch"][2] = T([(1, 0, "s"), (10, 8, "s"), (14, -4, "s"), (30, 0, "s")], f)
        s["hd"][0] = T([(1, 0, "s"), (4, 6, "s"), (10, -22, "s"), (14, -4, "c"), (18, 2, "o"), (30, 0, "s")], f)
        for sd in "lr":
            s["arms"][sd] = arm(sw=T([(1, 5, "s"), (4, 30, "s"), (10, 172, "o"), (14, 62, "c"), (17, 40, "k"), (22, 48, "s"), (30, 5, "s")], f),
                                ab=T([(1, 10, "s"), (10, 22, "s"), (14, 6, "s"), (30, 10, "s")], f),
                                el=T([(1, 15, "s"), (4, 60, "s"), (10, 75, "s"), (14, 4, "c"), (18, 14, "s"), (30, 15, "s")], f),
                                fin=T([(1, 0, "s"), (10, 18, "s"), (14, 30, "s"), (30, 0, "s")], f), tw=T([(1, 0, "s"), (10, 40, "s"), (30, 0, "s")], f))
        s["legs"]["l"] = leg(dy=T([(1, 0, "s"), (10, 0, "s"), (14, -0.16, "o"), (25, -0.16, "s"), (30, 0, "s")], f),
                             dz=T([(1, 0, "s"), (10, 0, "s"), (12, 0.08, "s"), (14, 0, "c"), (25, 0, "s"), (27, 0.04, "s"), (30, 0, "s")], f), fy=8)
        s["legs"]["r"] = leg(dy=T([(1, 0, "s"), (14, 0.06, "o"), (25, 0.06, "s"), (30, 0, "s")], f),
                             fp=T([(1, 0, "s"), (10, 0, "s"), (14, 20, "o"), (25, 15, "s"), (30, 0, "s")], f), tp=0, fy=10)
        s["w"] = 0.19
        return s

    @shot("SQ_Attack_Slash_R2", 24, dict(pop=[(9, "r", 0.2)], lag={"hr": -1.5, "ch": 0.3, "hd": 1.0}))
    def _(f):
        # 1H horizontal slash right-to-left, impact f9 (Slash style 35%): coil right, hips fire first, arm whips across
        s = spec()
        s["hip"] = [0.0, T([(1, 0, "s"), (6, 0.03, "s"), (9, -0.07, "c"), (14, -0.06, "s"), (24, 0, "s")], f),
                    T([(1, -0.02, "s"), (6, -0.07, "s"), (9, -0.10, "c"), (14, -0.09, "s"), (24, -0.02, "s")], f)]
        s["hr"][2] = T([(1, 0, "s"), (6, -24, "s"), (9, 14, "c"), (13, 26, "o"), (24, 0, "s")], f)
        s["sp"] = [T([(1, 3, "s"), (6, 0, "s"), (9, 12, "c"), (24, 3, "s")], f), 0, T([(1, 0, "s"), (6, -14, "s"), (9, 10, "c"), (13, 16, "o"), (24, 0, "s")], f)]
        s["ch"] = [T([(1, 2, "s"), (9, 8, "s"), (24, 2, "s")], f), T([(1, 0, "s"), (6, -6, "s"), (9, 4, "s"), (24, 0, "s")], f),
                   T([(1, 0, "s"), (6, -26, "s"), (9, 10, "c"), (13, 28, "o"), (18, 22, "s"), (24, 0, "s")], f)]
        s["hd"] = [T([(1, 0, "s"), (6, 6, "s"), (9, -4, "s"), (24, 0, "s")], f), 0, T([(1, 0, "s"), (6, 22, "s"), (9, -12, "s"), (13, -24, "s"), (24, 0, "s")], f)]
        s["arms"]["r"] = arm(sw=T([(1, 5, "s"), (6, 40, "s"), (9, 88, "c"), (13, 74, "o"), (18, 60, "s"), (24, 5, "s")], f),
                             ab=T([(1, 10, "s"), (6, 88, "o"), (9, 5, "c"), (13, -42, "k"), (18, -30, "s"), (24, 10, "s")], f),
                             el=T([(1, 15, "s"), (6, 70, "s"), (9, 4, "c"), (13, 12, "s"), (18, 30, "s"), (24, 15, "s")], f),
                             tw=T([(1, 0, "s"), (6, -40, "s"), (9, 30, "s"), (24, 0, "s")], f))
        s["arms"]["l"] = arm(sw=T([(1, 5, "s"), (6, 55, "s"), (9, 20, "s"), (13, -20, "o"), (24, 5, "s")], f),
                             ab=T([(1, 10, "s"), (6, 20, "s"), (9, 35, "s"), (24, 10, "s")], f),
                             el=T([(1, 15, "s"), (6, 80, "s"), (9, 40, "s"), (24, 15, "s")], f))
        s["legs"]["l"] = leg(dy=T([(1, 0, "s"), (5, 0, "s"), (9, -0.13, "o"), (19, -0.13, "s"), (24, 0, "s")], f),
                             dz=T([(1, 0, "s"), (5, 0, "s"), (7, 0.06, "s"), (9, 0, "c"), (19, 0, "s"), (21, 0.04, "s"), (24, 0, "s")], f), fy=4)
        s["legs"]["r"] = leg(fp=T([(1, 0, "s"), (8, 0, "s"), (11, 18, "o"), (19, 14, "s"), (24, 0, "s")], f), tp=0, fy=16)
        return s

    @shot("SQ_Attack_Punch_R2", 24, dict(pop=[(11, "r", 0.2)]))
    def _(f):
        # unarmed haymaker, impact f11 (enemy 45%): cock the fist back, twist through, overshoot and recoil
        s = spec()
        s["hip"] = [0.0, T([(1, 0, "s"), (7, 0.04, "s"), (11, -0.08, "c"), (16, -0.07, "s"), (24, 0, "s")], f),
                    T([(1, -0.02, "s"), (7, -0.07, "s"), (11, -0.09, "c"), (24, -0.02, "s")], f)]
        s["hr"][2] = T([(1, 0, "s"), (7, -22, "s"), (11, 16, "c"), (14, 20, "o"), (24, 0, "s")], f)
        s["sp"] = [T([(1, 3, "s"), (7, 0, "s"), (11, 16, "c"), (24, 3, "s")], f), 0, T([(1, 0, "s"), (7, -16, "s"), (11, 14, "c"), (24, 0, "s")], f)]
        s["ch"][2] = T([(1, 0, "s"), (7, -28, "s"), (11, 22, "c"), (14, 28, "o"), (24, 0, "s")], f)
        s["hd"] = [T([(1, 0, "s"), (7, 4, "s"), (11, -10, "s"), (24, 0, "s")], f), 0, T([(1, 0, "s"), (7, 24, "s"), (11, -18, "s"), (24, 0, "s")], f)]
        s["arms"]["r"] = arm(sw=T([(1, 5, "s"), (7, -28, "s"), (11, 92, "c"), (14, 84, "k"), (24, 5, "s")], f),
                             ab=T([(1, 10, "s"), (7, 34, "s"), (11, -4, "c"), (24, 10, "s")], f),
                             el=T([(1, 15, "s"), (7, 120, "s"), (11, 0, "c"), (14, 10, "s"), (24, 15, "s")], f),
                             tw=T([(1, 0, "s"), (11, 80, "s"), (24, 0, "s")], f))
        s["arms"]["l"] = arm(sw=T([(1, 5, "s"), (7, 70, "s"), (11, 30, "s"), (14, -10, "o"), (24, 5, "s")], f),
                             ab=T([(1, 10, "s"), (7, 12, "s"), (11, 30, "s"), (24, 10, "s")], f),
                             el=T([(1, 15, "s"), (7, 110, "s"), (11, 60, "s"), (24, 15, "s")], f))
        s["legs"]["l"] = leg(dy=T([(1, 0, "s"), (6, 0, "s"), (11, -0.15, "o"), (19, -0.15, "s"), (24, 0, "s")], f),
                             dz=T([(1, 0, "s"), (6, 0, "s"), (8, 0.06, "s"), (11, 0, "c"), (19, 0, "s"), (21, 0.04, "s"), (24, 0, "s")], f), fy=6)
        s["legs"]["r"] = leg(fp=T([(1, 0, "s"), (9, 0, "s"), (12, 22, "o"), (19, 16, "s"), (24, 0, "s")], f), tp=0, fy=18)
        return s

    # ---- casting
    @shot("SQ_Cast_Shoot_R2", 20, dict(pop=[(7, "r", 0.22)]))
    def _(f):
        # quick one-hand blast, release f7 (Cast style 30%): pull the hand to the shoulder, thrust, recoil up
        s = spec()
        s["hip"] = [0.0, T([(1, 0, "s"), (5, 0.03, "s"), (7, -0.05, "c"), (10, -0.03, "s"), (20, 0, "s")], f), T([(1, -0.02, "s"), (5, -0.06, "s"), (7, -0.07, "s"), (20, -0.02, "s")], f)]
        s["sp"] = [T([(1, 3, "s"), (5, -6, "s"), (7, 10, "c"), (10, 2, "o"), (20, 3, "s")], f), 0, T([(1, 0, "s"), (5, -14, "s"), (7, 10, "c"), (20, 0, "s")], f)]
        s["ch"][2] = T([(1, 0, "s"), (5, -16, "s"), (7, 14, "c"), (20, 0, "s")], f)
        s["hd"] = [T([(1, 0, "s"), (5, 6, "s"), (7, -8, "s"), (10, -14, "o"), (20, 0, "s")], f), 0, T([(1, 0, "s"), (5, 10, "s"), (7, -6, "s"), (20, 0, "s")], f)]
        s["arms"]["r"] = arm(sw=T([(1, 5, "s"), (5, 45, "s"), (7, 92, "c"), (10, 112, "o"), (14, 90, "s"), (20, 5, "s")], f),
                             ab=T([(1, 10, "s"), (5, 30, "s"), (7, 4, "c"), (20, 10, "s")], f),
                             el=T([(1, 15, "s"), (5, 125, "s"), (7, 0, "c"), (10, 20, "s"), (20, 15, "s")], f),
                             fin=T([(1, 0, "s"), (7, 0, "s"), (20, 0, "s")], f), tw=T([(1, 0, "s"), (5, 40, "s"), (7, -60, "s"), (20, 0, "s")], f))
        s["arms"]["l"] = arm(sw=T([(1, 5, "s"), (5, 30, "s"), (7, -15, "s"), (20, 5, "s")], f), ab=T([(1, 10, "s"), (7, 35, "s"), (20, 10, "s")], f),
                             el=T([(1, 15, "s"), (5, 70, "s"), (7, 30, "s"), (20, 15, "s")], f))
        s["legs"]["l"] = leg(dy=T([(1, 0, "s"), (4, 0, "s"), (7, -0.10, "o"), (15, -0.10, "s"), (20, 0, "s")], f),
                             dz=T([(1, 0, "s"), (4, 0, "s"), (5, 0.05, "s"), (7, 0, "s"), (15, 0, "s"), (17, 0.03, "s"), (20, 0, "s")], f))
        return s

    @shot("SQ_Cast_Raise_R2", 30, dict(pop=[(14, "lr", 0.2)]))
    def _(f):
        # gather low, then fling both arms to the sky on f14 (Raise style 45%), up on the toes, trembling hold
        s = spec()
        tr = math.sin(f * 2.7) * T([(1, 0, "s"), (14, 0, "s"), (15, 1, "s"), (21, 1, "s"), (24, 0, "s")], f)
        s["hip"][2] = T([(1, -0.02, "s"), (10, -0.10, "s"), (14, 0.03, "c"), (16, 0.04, "o"), (22, 0.0, "s"), (30, -0.02, "s")], f)
        s["sp"][0] = T([(1, 3, "s"), (10, 14, "s"), (14, -8, "c"), (16, -10, "o"), (22, -6, "s"), (30, 3, "s")], f) + 2 * tr
        s["ch"][0] = T([(1, 2, "s"), (10, 8, "s"), (14, -8, "c"), (22, -5, "s"), (30, 2, "s")], f)
        s["hd"][0] = T([(1, 0, "s"), (10, 10, "s"), (14, -16, "c"), (17, -20, "o"), (24, -12, "s"), (30, 0, "s")], f)
        s["hd"][1] = 4 * tr
        for sd in "lr":
            s["arms"][sd] = arm(sw=T([(1, 5, "s"), (10, 34, "s"), (14, 168, "c"), (17, 160, "o"), (22, 162, "s"), (30, 5, "s")], f) + 3 * tr,
                                ab=T([(1, 10, "s"), (10, -12, "s"), (14, 34, "c"), (22, 30, "s"), (30, 10, "s")], f),
                                el=T([(1, 15, "s"), (10, 90, "s"), (14, 6, "c"), (22, 12, "s"), (30, 15, "s")], f),
                                fin=T([(1, 0, "s"), (10, 50, "s"), (14, 0, "s")], f), tw=T([(1, 0, "s"), (14, -60, "s"), (24, -50, "s"), (30, 0, "s")], f))
        s["legs"] = {sd: leg(fy=10, fp=T([(1, 0, "s"), (12, 0, "s"), (15, 20, "o"), (22, 14, "s"), (28, 0, "s")], f), tp=0) for sd in "lr"}
        return s

    @shot("SQ_Cast_Summon_R2", 34, dict(pop=[(14, "lr", 0.18)]))
    def _(f):
        # hands draw a circle up and out, then slam palms down to call something up from the ground (f14, Summon 40%)
        s = spec()
        rise = T([(1, 0, "s"), (14, 0, "s"), (20, 1, "s"), (26, 1, "s"), (34, 0, "s")], f)
        s["hip"][2] = T([(1, -0.02, "s"), (9, 0.02, "s"), (14, -0.2, "c"), (17, -0.18, "o"), (24, -0.1, "s"), (34, -0.02, "s")], f)
        s["sp"][0] = T([(1, 3, "s"), (9, -10, "s"), (14, 24, "c"), (17, 26, "o"), (24, 10, "s"), (34, 3, "s")], f)
        s["ch"][0] = T([(1, 2, "s"), (9, -10, "s"), (14, 14, "c"), (24, 4, "s"), (34, 2, "s")], f)
        s["hd"][0] = T([(1, 0, "s"), (9, -18, "s"), (14, -16, "c"), (24, -14, "s"), (34, 0, "s")], f)
        for sd in "lr":
            s["arms"][sd] = arm(sw=T([(1, 5, "s"), (5, 60, "s"), (9, 150, "s"), (14, 52, "c"), (17, 44, "k"), (24, 70, "s"), (34, 5, "s")], f),
                                ab=T([(1, 10, "s"), (5, 30, "s"), (9, 70, "s"), (14, 22, "c"), (24, 40, "s"), (34, 10, "s")], f),
                                el=T([(1, 15, "s"), (9, 30, "s"), (14, 8, "c"), (24, 30, "s"), (34, 15, "s")], f),
                                fin=T([(1, 0, "s"), (14, 0, "s"), (34, 0, "s")], f), tw=T([(1, 0, "s"), (9, -40, "s"), (14, -90, "s"), (24, -70, "s"), (34, 0, "s")], f))
        s["legs"] = {sd: leg(fy=14, kout=0.3, dx=SX[sd] * 0.05 * T([(1, 0, "s"), (12, 0, "s"), (14, 1, "o"), (26, 1, "s"), (32, 0, "s")], f),
                             dz=0.05 * T([(1, 0, "s"), (10 + (sd == "r"), 0, "s"), (12 + (sd == "r"), 1, "s"), (14, 0, "c"), (26, 0, "s"), (28 + (sd == "r"), 0.7, "s"), (31 + (sd == "r"), 0, "s")], f)) for sd in "lr"}
        s["w"] = 0.19
        return s

    @shot("SQ_Cast_Long_R2", 40, dict(pop=[(19, "lr", 0.2)]))
    def _(f):
        # channelled spell: hands weave two loops, then a two-handed push on f19 (45%), recoil and settle
        s = spec()
        wv = T([(1, 0, "s"), (4, 1, "s"), (16, 1, "s"), (18, 0, "s")], f)
        a = 2 * math.pi * (f - 4) / 6.0
        push = T([(1, 0, "s"), (16, 0, "s"), (19, 1, "c"), (22, 0.9, "o"), (32, 0.6, "s"), (40, 0, "s")], f)
        s["hip"] = [0.02 * math.cos(a) * wv, T([(1, 0, "s"), (16, 0.04, "s"), (19, -0.07, "c"), (30, -0.05, "s"), (40, 0, "s")], f),
                    -0.04 * wv - 0.07 * push - 0.02 * (1 - max(wv, push))]
        s["sp"] = [3 + 6 * wv + 3 * math.sin(a) * wv - 4 * T([(1, 0, "s"), (16, 1, "s"), (19, 0, "s")], f) + 14 * push, 4 * math.cos(a) * wv, 0]
        s["ch"] = [2 + 3 * math.sin(a) * wv + 4 * push, 3 * math.cos(a) * wv, 0]
        s["hd"] = [-6 * wv + 4 * math.sin(a + 0.6) * wv - 8 * push, -5 * math.cos(a) * wv, 0]
        for sd in "lr":
            sg = SX[sd]
            wa = arm(sw=88 + 18 * math.sin(a + (0 if sd == "l" else math.pi)), ab=30 + 14 * math.cos(a + (0 if sd == "l" else math.pi)),
                     el=60 + 15 * math.sin(a), fin=30, tw=-70)
            pa = arm(sw=92, ab=14, el=0, fin=0, tw=-90)
            pre = T([(1, 0, "s"), (16, 0, "s"), (17, 1, "s"), (19, 0, "c")], f)
            cock = arm(sw=60, ab=40, el=120, fin=20, tw=-40)
            n_ = arm()
            s["arms"][sd] = {k: n_[k] + wv * (wa[k] - n_[k]) + pre * (cock[k] - n_[k]) + push * (pa[k] - n_[k]) for k in n_}
        s["legs"]["l"] = leg(dy=-0.12 * T([(1, 0, "s"), (16, 0, "s"), (19, 1, "o"), (34, 1, "s"), (40, 0, "s")], f),
                             dz=0.05 * T([(1, 0, "s"), (16, 0, "s"), (17, 1, "s"), (19, 0, "c"), (34, 0, "s"), (37, 0.8, "s"), (40, 0, "s")], f))
        return s

    @shot("SQ_Throw_R2", 24, dict(pop=[(8, "r", 0.18)]))
    def _(f):
        # overhand throw, release f8 (Throw style 30%): arm cocked behind the head, step and whip, follow through low
        s = spec()
        s["hip"] = [0.0, T([(1, 0, "s"), (5, 0.05, "s"), (8, -0.08, "c"), (13, -0.09, "s"), (24, 0, "s")], f),
                    T([(1, -0.02, "s"), (5, -0.05, "s"), (8, -0.08, "c"), (13, -0.09, "o"), (24, -0.02, "s")], f)]
        s["hr"][2] = T([(1, 0, "s"), (5, -20, "s"), (8, 12, "c"), (12, 18, "o"), (24, 0, "s")], f)
        s["sp"] = [T([(1, 3, "s"), (5, -12, "s"), (8, 10, "c"), (12, 16, "o"), (24, 3, "s")], f), 0, T([(1, 0, "s"), (5, -14, "s"), (8, 10, "c"), (24, 0, "s")], f)]
        s["ch"] = [T([(1, 2, "s"), (5, -10, "s"), (8, 10, "c"), (24, 2, "s")], f), 0, T([(1, 0, "s"), (5, -22, "s"), (8, 16, "c"), (12, 22, "o"), (24, 0, "s")], f)]
        s["hd"] = [T([(1, 0, "s"), (5, -10, "s"), (8, -10, "s"), (12, -16, "s"), (24, 0, "s")], f), 0, T([(1, 0, "s"), (5, 18, "s"), (8, -8, "s"), (24, 0, "s")], f)]
        s["arms"]["r"] = arm(sw=T([(1, 5, "s"), (5, 160, "o"), (8, 92, "c"), (12, 20, "o"), (16, 10, "s"), (24, 5, "s")], f),
                             ab=T([(1, 10, "s"), (5, 40, "s"), (8, 10, "c"), (12, -20, "k"), (24, 10, "s")], f),
                             el=T([(1, 15, "s"), (5, 125, "s"), (8, 6, "c"), (12, 20, "s"), (24, 15, "s")], f),
                             tw=T([(1, 0, "s"), (5, 50, "s"), (8, -20, "s"), (24, 0, "s")], f))
        s["arms"]["l"] = arm(sw=T([(1, 5, "s"), (5, 95, "s"), (8, 40, "s"), (12, -25, "o"), (24, 5, "s")], f),
                             ab=T([(1, 10, "s"), (5, 20, "s"), (8, 30, "s"), (24, 10, "s")], f), el=T([(1, 15, "s"), (5, 10, "s"), (8, 40, "s"), (24, 15, "s")], f))
        s["legs"]["l"] = leg(dy=T([(1, 0, "s"), (3, 0, "s"), (7, -0.16, "o"), (19, -0.16, "s"), (24, 0, "s")], f),
                             dz=T([(1, 0, "s"), (3, 0, "s"), (5, 0.07, "s"), (7, 0, "c"), (19, 0, "s"), (21, 0.04, "s"), (24, 0, "s")], f), fy=4)
        s["legs"]["r"] = leg(fp=T([(1, 0, "s"), (7, 0, "s"), (10, 30, "o"), (19, 22, "s"), (24, 0, "s")], f), tp=0, fy=16)
        return s

    # ---- importer marker: 2-frame copy of the KayKit Idle (tells Scripts/import_art.py to re-import the remaster)
    if not ONLY or "SQ_Marker_Remaster2" in ONLY:
        idle = ACT["Idle"]
        act = write_action("SQ_Marker_Remaster2", idle, [eval_pose(idle, 1), eval_pose(idle, 2)])
        act["sq_loop"] = False

def shot(name, N, cfg=None, base="Idle"):
    def deco(fn):
        make(name, base, N, fn, False, cfg)
        return fn
    return deco

# REMASTER: impact pops / per-take overrides (frames = the documented key/impact frames, unchanged)
REMASTER.update({
    "SQ_Ability_TeamDamage": dict(pop=[(12, "r", 0.18), (19, "r", 0.10)]),
    "SQ_Ability_Builder": dict(pop=[(14, "r", 0.22)]),
    "SQ_Ability_DashStrike": dict(pop=[(11, "r", 0.22)]),
    "SQ_Ability_Triage": dict(pop=[(12, "r", 0.18)]),
    "SQ_Ability_Scorch": dict(pop=[(9, "r", 0.22)]),
    "SQ_Ability_Audit": dict(pop=[(13, "r", 0.22)]),
    "SQ_Ability_Bulwark": dict(pop=[(11, "l", 0.12)]),
    "SQ_Ability_Collections": dict(pop=[(25, "r", 0.15)]),
    "SQ_Ability_Lookout": dict(pop=[(28, "r", 0.12)]),
    "SQ_Ability_Stopwatch": dict(pop=[(13, "l", 0.15)]),
    "SQ_Ability_Stampede": dict(pop=[(28, "lr", 0.15)]),
    "SQ_Ability_Spite": dict(exag=(1.3, 1.25, 1.0)),
    "SQ_Ability_Hauler": dict(exag=(1.2, 1.15, 1.0)),
    "SQ_Ability_LastRites": dict(exag=(1.15, 1.0, 1.0)),
    "SQ_Ability_Seance": dict(exag=(1.2, 1.0, 1.0), head=0.4),
    "SQ_Idle_Proud": dict(lag={"arm": 0, "el": 0}),
})

styles()
abilities()
remaster_new()

for k, v in STATS.items():
    print("HIPDROP", k, "frames", v[0], "max", round(v[1], 3))
set_action(ACT["Idle"])
sc.frame_set(1)
bpy.ops.wm.save_as_mainfile(filepath=BLEND)
print("NACTIONS", len(bpy.data.actions), "SQ", len([a for a in bpy.data.actions if a.name.startswith("SQ_")]))
