# Soul Quota - Remaster 8 (2026-11-16): quality pass. Every player-facing take is rebuilt ON KayKit's hand-animated takes
# (CC0, already in this file: Running_*, Walking_*, Idle_*, 1H/2H melee, Spellcast_*, Throw, Hit, Death, Lie_StandUp...)
# instead of on procedural poses. Layers on the hand animation, all per reaper:
#   retime (short anticipation, fast snap, 2-frame contact hold, long eased settle) - exaggeration of the pose away from
#   neutral - the reaper's own posture (from its designed idle) - overlap (arms/head sampled a few frames late) -
#   stride / foot-lift scaling with IK-replanted feet - world-space accents (lean, drop, hover) - head squash/stretch pops.
# Always starts from backup/Reaper_Custom_pre8.blend. Then run sq_export.py.   python3 sq_anims8.py [--only Id,Id] [--fam run,sig]
import bpy, math, sys, shutil, csv
PRE8 = "/home/claude/blender_work/backup/Reaper_Custom_pre8.blend"
shutil.copyfile(PRE8, "/home/claude/sq/ArtSource/Characters/Reaper_Custom.blend")
_src = open("/home/claude/blender_work/sq_anims2.py").read()
_argv = sys.argv[:]
sys.argv = [a for a in sys.argv if a not in ("--only",)]   # sq_anims2 ONLY is by take name; we filter ourselves
exec(compile(_src[:_src.index("# ============================================================ REMASTER 2026-11-07: overlap")], "sq_anims2_helpers", "exec"))
sys.argv = _argv
def _arg(k):
    return sys.argv[sys.argv.index(k) + 1].split(",") if k in sys.argv else None
ONLY_ID, ONLY_FAM = _arg("--only"), _arg("--fam")

IDS = ["Grim", "Mort", "Widow", "Bonesmith", "Keeper", "Monk", "Intern", "Femur", "Digby", "Collector", "Beak", "Tallow", "Lookout",
       "Vexa", "Actuary", "Sweep", "Solace", "Pallbearers", "Ectoplasm", "Neckless", "Tickwell"]
FLOAT = {"Grim", "Widow", "Ectoplasm"}
HEAVY = {"Digby", "Vexa", "Bonesmith", "Pallbearers", "Monk", "Lookout"}
QUICK = {"Intern", "Mort", "Femur", "Sweep", "Tallow", "Actuary"}
HUNCH = {"Beak", "Tickwell", "Keeper", "Collector", "Solace"}
PROUD = {"Neckless"}
def btype(rid):
    for n, s in (("float", FLOAT), ("heavy", HEAVY), ("quick", QUICK), ("hunch", HUNCH), ("proud", PROUD)):
        if rid in s:
            return n
    return "norm"

UPPER = ["spine", "chest", "head", "upperarm.l", "lowerarm.l", "wrist.l", "hand.l", "upperarm.r", "lowerarm.r", "wrist.r", "hand.r"]
LEGS = ["upperleg.l", "lowerleg.l", "foot.l", "toes.l", "upperleg.r", "lowerleg.r", "foot.r", "toes.r"]
DEF = ["hips"] + UPPER + LEGS + ["handslot.l", "handslot.r"]
QI = Quaternion()

# ---------------------------------------------------------------- basis-space sampling of the hand-animated takes
_CACHE = {}
def seq(an):
    if an not in _CACHE:
        act = ACT[an]
        b0, b1 = int(act.frame_range[0]), int(act.frame_range[1])
        out = []
        for f in range(b0, b1 + 1):
            Bm = to_basis(eval_pose(act, f))
            out.append({b: (Bm[b].to_translation(), Bm[b].to_quaternion()) for b in BONES})
        for i in range(1, len(out)):          # quaternion continuity
            for b in BONES:
                if out[i - 1][b][1].dot(out[i][b][1]) < 0:
                    out[i][b] = (out[i][b][0], -out[i][b][1])
        _CACHE[an] = (b0, out)
    return _CACHE[an]

def samp(an, bf, bone, loop=False):
    b0, s = seq(an)
    n = len(s)
    x = bf - b0
    if loop:
        x %= (n - 1)
    x = max(0.0, min(n - 1.0, x))
    i = min(n - 2, int(x)); u = x - i
    l0, q0 = s[i][bone]; l1, q1 = s[i + 1][bone]
    return l0.lerp(l1, u), q0.slerp(q1, u)

def qscale(qref, q, k):
    d = qref.inverted() @ q
    ax, ang = d.to_axis_angle()
    if ang > math.pi:
        ang -= 2 * math.pi
    return qref @ Quaternion(ax, ang * k)

def qpow(q, w):
    ax, ang = q.to_axis_angle()
    if ang > math.pi:
        ang -= 2 * math.pi
    return Quaternion(ax, ang * w)

def from_basis(B):
    P = {}
    for b in BONES:                      # BONES is parent-first (armature order)
        M = Matrix.LocRotScale(B[b][0], B[b][1], None)
        p = PARENT[b]
        P[b] = REST[b] @ M if p is None else P[p] @ (REST[p].inverted() @ REST[b]) @ M
    return P

NEUT = {b: samp("Idle", 1, b) for b in BONES}          # KayKit neutral
POST = {}                                              # per reaper: its designed idle posture (pre-8 SQ_Idle_<Id>, frame 1)
for _rid in IDS:
    _a = "SQ_Idle_" + _rid
    POST[_rid] = {b: samp(_a, 1, b) for b in BONES} if _a in ACT else dict(NEUT)

_MEAN = {}
def mean_ref(an):
    """average pose of a take (per bone): exaggerating around it amplifies the motion, not the take's standing lean"""
    if an not in _MEAN:
        b0, sq = seq(an)
        out = {}
        for b in BONES:
            l = Vector((0, 0, 0)); q = Quaternion((0, 0, 0, 0))
            for fr in sq[:-1]:
                l += fr[b][0]; q = Quaternion((q.w + fr[b][1].w, q.x + fr[b][1].x, q.y + fr[b][1].y, q.z + fr[b][1].z))
            out[b] = (l / (len(sq) - 1), q.normalized())
        _MEAN[an] = out
    return _MEAN[an]

def smooth01(u):
    u = max(0.0, min(1.0, u))
    return u * u * (3 - 2 * u)

def K8(keys, f):
    """[(frame, value, ease)] ease into key: s smooth, i ease-in (accelerate), o ease-out, l linear, x expo-out"""
    if f <= keys[0][0]:
        return keys[0][1]
    for a, b in zip(keys, keys[1:]):
        if f <= b[0]:
            u = (f - a[0]) / max(1e-6, b[0] - a[0])
            e = b[2] if len(b) > 2 else "s"
            u = {"s": smooth01(u), "i": u ** 2.2, "o": 1 - (1 - u) ** 2.2, "l": u, "x": 1 - 2.0 ** (-8 * u) if u < 1 else 1.0}[e]
            return a[1] + (b[1] - a[1]) * u
    return keys[-1][1]

# ---------------------------------------------------------------- the take builder
def build8(name, N, loop, base, tmap, C, rid, layer=None, feet_base=None):
    """tmap(f) -> base frame (fractional). C: dict
         k: {bone-group: exaggeration}  (up, arm, head, hipr, hipl)        post: {group: weight of the reaper posture}
         lag: {bone: frames late}      stride, lift, width: foot path scaling     float: legs trail (no IK)
    """
    kq = C.get("k", {}); pw = C.get("post", {}); lag = C.get("lag", {})
    PR = POST[rid]
    grp = lambda b: ("hip" if b == "hips" else "head" if b == "head" else "arm" if ("arm" in b or "wrist" in b or "hand" in b) else
                     "leg" if ("leg" in b or "foot" in b or "toes" in b) else "up")
    fl = C.get("float", False)
    nref = C.get("ref", NEUT)
    poses = []
    Bl = len(seq(base)[1]) - 1
    for f in range(1, N + 1):
        def bt(ff):
            v = tmap(ff)
            return v
        B = {}
        for b in BONES:
            g = grp(b)
            lg = lag.get(b, lag.get(g, 0.0))
            ff = f - lg
            if loop:
                ff = (ff - 1) % (N - 1) + 1
            else:
                ff = max(1.0, ff)
            l, q = samp(base, bt(ff), b, loop)
            if b in DEF:
                k = kq.get(b, kq.get(g, 1.0))
                if k != 1.0 and g != "leg":
                    q = qscale(nref[b][1], q, k)
                w = pw.get(b, pw.get(g, 0.0))
                if w and g != "leg":
                    q = qpow(PR[b][1] @ NEUT[b][1].inverted(), w) @ q
                if b == "hips":
                    kl = kq.get("hipl", 1.0)
                    l = nref[b][0] + (l - nref[b][0]) * kl + (PR[b][0] - NEUT[b][0]) * pw.get("hipl", pw.get("hip", 0.0))
            B[b] = (l, q)
        if fl:
            for b in LEGS:                    # floaters: the designed trailing-leg pose, swinging a little late
                B[b] = PR[b]
        edge = 1.0
        if not loop and C.get("edges", True):  # one-shots start from and return to the reaper's own idle pose (no pop at blend in/out)
            edge = min(smooth01((f - 1) / 2.0) if C.get("edge_in", True) else 1.0, smooth01((N - f) / 4.0) if C.get("edge_out", True) else 1.0)
        P = from_basis(B)
        # foot targets from the un-lagged hand-animated legs at this time
        if not fl:
            fb = feet_base or base
            Bf = {b: samp(fb, bt(f) if fb == base else C["feet_t"](f), b, loop) for b in BONES}
            Pf = from_basis(Bf)
            tg = {}
            for sd in "lr":
                p = head_w(Pf, "foot." + sd).copy()
                n0 = head_w(from_basis(NEUT), "foot." + sd) if "_nf" not in C else C["_nf"][sd]
                p.y = p.y * C.get("stride", 1.0)
                p.z = n0.z + max(0.0, p.z - n0.z) * C.get("lift", 1.0)
                p.x = p.x * C.get("width", 1.0)
                tg[sd] = (p, frame_rot(Pf, "foot." + sd).copy(), head_w(Pf, "lowerleg." + sd).copy())
        u = (f - 1) / (N - 1)
        if layer:
            layer(P, f, u)
        if not fl:
            for sd in "lr":
                p, fr, knee = tg[sd]
                off = C.get("foot_off", {}).get(sd)
                if off:
                    p = p + Vector(off)
                pole = knee + Vector(((0.10 if sd == "l" else -0.10) * C.get("kout", 1.0), -0.5, 0.05))
                ik2(P, "upperleg." + sd, "lowerleg." + sd, "foot." + sd, p, pole)
                set_rot(P, "foot." + sd, fr)
        if edge < 1.0:
            Bn = to_basis(P)
            Bb = {}
            for b in BONES:
                l, q = Bn[b].to_translation(), Bn[b].to_quaternion()
                l0, q0 = PR[b]
                if b == "hips" and fl:
                    l0 = l0 + Vector((0, 0, 0))
                Bb[b] = (l0.lerp(l, edge), q0.slerp(q, edge))
            P = from_basis(Bb)
        poses.append(P)
    if loop:
        poses[-1] = {b: m.copy() for b, m in poses[0].items()}
    act = write_action(name, ACT[base], poses)
    act["sq_loop"] = bool(loop); act["sq_remaster"] = 8; act["sq_base"] = base
    return act

def body8(P, hip=(0, 0, 0), hr=(0, 0, 0), sp=(0, 0, 0), ch=(0, 0, 0), hd=(0, 0, 0)):
    if hip != (0, 0, 0): move_subtree(P, "hips", hip)
    if hr != (0, 0, 0): rot_subtree(P, "hips", E(hr))
    if sp != (0, 0, 0): rot_subtree(P, "spine", E(sp))
    if ch != (0, 0, 0): rot_subtree(P, "chest", E(ch))
    if hd != (0, 0, 0): rot_subtree(P, "head", E(hd))

def arms_out(P, deg):
    """swing both upper arms away from the body (about the forward axis) so they clear wide torsos"""
    if abs(deg) < 0.01: return
    rot_subtree(P, "upperarm.l", Quaternion((0, 1, 0), math.radians(-deg)))
    rot_subtree(P, "upperarm.r", Quaternion((0, 1, 0), math.radians(deg)))

def set_scale_keys(act, bone, keys, N):
    """uniform-ish squash/stretch on one bone: keys [(frame, (sx, sy, sz))], linear between, written over the scale curves"""
    cb = act.layers[0].strips[0].channelbags[0]
    for i in range(3):
        fc = cb.fcurves.find(f'pose.bones["{bone}"].scale', index=i)
        for k in range(N):
            f = k + 1
            v = keys[0][1][i]
            for a, b in zip(keys, keys[1:]):
                if a[0] <= f <= b[0]:
                    u = smooth01((f - a[0]) / max(1e-6, b[0] - a[0])); v = a[1][i] + (b[1][i] - a[1][i]) * u; break
            else:
                if f > keys[-1][0]: v = keys[-1][1][i]
            fc.keyframe_points[k].co = (f, v)
        fc.update()

DONE8 = []
def want(rid, fam):
    return (not ONLY_ID or rid in ONLY_ID) and (not ONLY_FAM or fam in ONLY_FAM)

# ====================================================================== RUN / WALK
def loopmap(base, N):
    b0, s = seq(base)
    L = len(s) - 1
    return lambda f: b0 + (f - 1) / (N - 1) * L

RUN = {  # per body type. out = arms swung away from the torso (deg), elb = extra elbow bend, tw = chest counter-twist accent (deg)
    "norm":  dict(base="Running_A", N=19, k=dict(up=1.5, arm=1.45, head=0.8, hipl=1.6), lean=3, stride=1.15, lift=1.2, width=1.0, out=20, tw=7, parm=0.3),
    "quick": dict(base="Running_C", N=15, k=dict(up=1.3, arm=1.2, head=0.8, hipl=1.5), lean=-10, stride=1.2, lift=1.3, width=0.85, out=26, tw=5, parm=0.2),
    "heavy": dict(base="Running_A", N=23, k=dict(up=1.7, arm=1.5, head=0.8, hipl=2.4), lean=0, stride=1.1, lift=1.0, width=1.6, out=34, tw=10, parm=0.3),
    "hunch": dict(base="Running_A", N=19, k=dict(up=1.3, arm=1.2, head=0.8, hipl=1.5), lean=-4, stride=1.05, lift=1.1, width=1.0, out=24, tw=6, parm=0.5),
    "proud": dict(base="Running_B", N=19, k=dict(up=1.4, arm=1.4, head=0.8, hipl=1.7), lean=-5, stride=1.2, lift=1.35, width=1.0, out=16, tw=8, parm=0.3),
    "float": dict(base="Running_A", N=25, k=dict(up=1.2, arm=0.7, head=0.8, hipl=0.5), lean=12, stride=1.0, lift=1.0, width=1.0, out=30, tw=5, parm=0.75),
}
RUN_OVR = {}

def gen_run(rid):
    t = btype(rid)
    G = dict(RUN[t]); G.update(RUN_OVR.get(rid, {}))
    N = G["N"]; base = G["base"]
    C = dict(k=G["k"], ref=mean_ref(base), post=dict(up=0.45, head=0.6, arm=G["parm"], hip=0.3, hipl=1.0),
             lag={"lowerarm.l": 1.0, "lowerarm.r": 1.0, "wrist.l": 1.8, "wrist.r": 1.8, "hand.l": 1.8, "hand.r": 1.8, "head": 1.2, "chest": 0.5},
             stride=G["stride"], lift=G["lift"], width=G["width"], float=(t == "float"))
    def layer(P, f, u):
        ph = 2 * math.pi * u
        tw = G["tw"] * math.sin(ph)                      # chest counter-rotates against the hips once per stride pair
        if t == "float":
            body8(P, hip=(0, 0, 0.10 + 0.04 * math.sin(2 * ph)), hr=(G["lean"], 4 * math.sin(ph), 0), ch=(0, 0, tw), hd=(-G["lean"] * 0.75, 0, -tw * 0.6))
        else:
            body8(P, hr=(G["lean"], 0, -tw * 0.5), ch=(0, 0, tw), hd=(-G["lean"] * 0.6, 0, -tw * 0.5))
        if t == "heavy":
            body8(P, hr=(0, 6 * math.sin(ph), 0), ch=(0, -4 * math.sin(ph), 0))
        arms_out(P, G["out"])
    build8("SQ_Run_" + rid, N, True, base, loopmap(base, N), C, rid, layer)
    DONE8.append("SQ_Run_" + rid)

FAMS = {"run": gen_run}
