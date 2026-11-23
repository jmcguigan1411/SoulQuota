# Soul Quota - Remaster 7 (2026-11-15): pass two for the chase camera, toward punchy stylised action animation
# (principles only: anticipation, snap into contact, overshoot, settle, held ultimate poses, strong silhouettes from behind).
#   * SQ_E_<Id> / SQ_R_<Id> (42 new): every reaper's own E and R cast, built ON its own idle posture from one of ten
#     cast shapes (slam, throw, sweep, push, summon, nova, buff, dash, blink, channel) chosen by what the ability does;
#     R is bigger and holds its pose. Kits.csv lists them first, the old shared takes stay as fallbacks.
#   * SQ_Sig_Grim (new): The Shade's own scythe sweep (Slash, impact f9 of 24).
#   * SQ_Ability_* (21) and the shared SQ_Attack_* / SQ_Cast_* / SQ_Throw_R2 / SQ_Reap_Swing: whole-body layer on the baked
#     take (twist away, dip, snap through on the take's own contact frame, arms out past the outline). Lengths and
#     contact frames are unchanged.
#   * SQ_Walk_<Id> / SQ_Run_<Id> (42): pushed again (wider pumping arms, counter-rotation, deeper down pose, higher knees).
#   * state poses per reaper (new, built on the reaper's own idle): SQ_Victory_<Id>, SQ_Defeat_<Id>, SQ_Downed_<Id>,
#     SQ_Revive_<Id>, SQ_Bank_<Id>.
# Always starts from backup/Reaper_Custom_pre7.blend (so re-running never stacks layers). Then run sq_export.py.
#   python3 sq_anims7.py [--only A,B]
import bpy, math, sys, copy, shutil
PRE7 = "/home/claude/blender_work/backup/Reaper_Custom_pre7.blend"
shutil.copyfile(PRE7, "/home/claude/sq/ArtSource/Characters/Reaper_Custom.blend")
src4 = open("/home/claude/blender_work/sq_anims4.py").read()
exec(compile(src4[:src4.index("gen_locomotion()\ngen_sigs()")], "sq_anims4_defs", "exec"))
DONE7 = []
IDS = ["Grim", "Mort", "Widow", "Bonesmith", "Keeper", "Monk", "Intern", "Femur", "Digby", "Collector", "Beak", "Tallow", "Lookout",
       "Vexa", "Actuary", "Sweep", "Solace", "Pallbearers", "Ectoplasm", "Neckless", "Tickwell"]
FLOAT = {"Grim", "Widow", "Ectoplasm"}
HEAVY = {"Digby", "Vexa", "Bonesmith", "Tallow", "Pallbearers", "Monk"}
QUICK = {"Intern", "Mort", "Actuary", "Femur", "Sweep"}
HUNCH = {"Beak", "Tickwell", "Keeper", "Lookout", "Mort"}

def want(name):
    return not ONLY or name in ONLY

# ---------------------------------------------------------------- pose-level tools
def V3(v):
    return Vector(v)

def nrm(v):
    v = Vector(v)
    return v.normalized() if v.length > 1e-6 else Vector((0, 0, -1))

def mix(a, b, u):
    if isinstance(a, (int, float)):
        return a + (b - a) * u
    return tuple(x + (y - x) * u for x, y in zip(a, b))

E7 = {"s": lambda u: u * u * (3 - 2 * u), "i": lambda u: u ** 3, "o": lambda u: 1 - (1 - u) ** 3, "l": lambda u: u,
      "k": lambda u: 1 + 1.6 * (u - 1) ** 3 + 0.6 * (u - 1) ** 2 if u < 1 else 1.0}

def K(keys, f):
    """keys: [(frame, value, ease into this key)]"""
    if f <= keys[0][0]:
        return keys[0][1]
    for a, b in zip(keys, keys[1:]):
        if f <= b[0]:
            u = (f - a[0]) / max(1e-6, b[0] - a[0])
            return mix(a[1], b[1], E7[b[2] if len(b) > 2 else "s"](u))
    return keys[-1][1]

def feet_of(P):
    return {sd: (head_w(P, "foot." + sd).copy(), frame_rot(P, "foot." + sd).copy(), head_w(P, "lowerleg." + sd).copy()) for sd in "lr"}

def replant(P, F, off=None, pitch=None):
    for sd in "lr":
        tgt = F[sd][0] + (Vector(off[sd]) if off else Vector((0, 0, 0)))
        pole = F[sd][2] + Vector((SX[sd] * 0.10, -0.45, 0.05))
        ik2(P, "upperleg." + sd, "lowerleg." + sd, "foot." + sd, tgt, pole)
        q = F[sd][1]
        if pitch and pitch.get(sd):
            q = Quaternion((1, 0, 0), math.radians(pitch[sd])) @ q
        set_rot(P, "foot." + sd, q)

def body(P, hip=(0, 0, 0), hr=(0, 0, 0), sp=(0, 0, 0), ch=(0, 0, 0), hd=(0, 0, 0)):
    move_subtree(P, "hips", hip)
    rot_subtree(P, "hips", E(hr))
    rot_subtree(P, "spine", E(sp))
    rot_subtree(P, "chest", E(ch))
    rot_subtree(P, "head", E(hd))

def arm_to(P, sd, up, lo, w):
    if w <= 0.001:
        return
    aim(P, "upperarm." + sd, nrm(up), w)
    aim(P, "lowerarm." + sd, nrm(lo), w)

def hand_pop(P, sd, k):
    if abs(k - 1.0) < 1e-3:
        return
    piv = P["hand." + sd].translation.copy()
    M = Matrix.Translation(piv) @ Matrix.Diagonal((k, k, k, 1.0)) @ Matrix.Translation(-piv)
    for b in ["hand." + sd] + CHILDREN["hand." + sd]:
        P[b] = M @ P[b]

def finish(name, base_act, poses, loop, tag=7):
    act = write_action(name, base_act, poses)
    act["sq_loop"] = bool(loop)
    act["sq_remaster"] = tag
    DONE7.append(name)
    return act

def idle_of(rid):
    return ACT["SQ_Idle_" + rid]

# ---------------------------------------------------------------- cast shapes (authored with the RIGHT arm leading)
# character space: +X = its left, -Y = forward, +Z = up.  pry = (pitch + forward, roll + to its left, yaw + turning left)
Z3 = (0.0, 0.0, 0.0)
def shape(ant, hit, hold=None):
    return dict(ant=ant, hit=hit, hold=hold)

def P_(hip=Z3, hr=Z3, sp=Z3, ch=Z3, hd=Z3, uR=None, lR=None, uL=None, lL=None, fR=Z3, fL=Z3, pR=0.0, pL=0.0):
    return dict(hip=hip, hr=hr, sp=sp, ch=ch, hd=hd, uR=uR, lR=lR, uL=uL, lL=lL, fR=fR, fL=fL, pR=pR, pL=pL)

SHAPES = {
    # both arms hauled overhead, then the whole body crashes down
    "slam": shape(P_(hip=(0, 0.03, 0.05), hr=(-8, 0, 0), sp=(-16, 0, 0), ch=(-12, 0, 0), hd=(-6, 0, 0),
                     uR=(-0.35, 0.30, 1), lR=(-0.2, 0.55, 0.9), uL=(0.35, 0.30, 1), lL=(0.2, 0.55, 0.9)),
                  P_(hip=(0, -0.07, -0.20), hr=(13, 0, 0), sp=(20, 0, 0), ch=(10, 0, 0), hd=(-22, 0, 0),
                     uR=(-0.40, -0.75, -0.55), lR=(-0.15, -0.85, -0.6), uL=(0.40, -0.75, -0.55), lL=(0.15, -0.85, -0.6))),
    # overarm throw: lead shoulder winds back, step, release across the body
    "throw": shape(P_(hip=(-0.02, 0.07, -0.06), hr=(-6, 0, -34), sp=(-6, 0, -16), ch=(-4, 0, -14), hd=(4, 0, 46),
                      uR=(-0.55, 0.80, 0.50), lR=(-0.20, 0.55, 0.95), uL=(0.30, -0.90, 0.20), lL=(0.25, -0.95, 0.25), fL=(0, -0.05, 0.05)),
                   P_(hip=(0.02, -0.12, -0.10), hr=(12, 0, 28), sp=(14, 0, 14), ch=(8, 0, 12), hd=(-16, 0, -40),
                      uR=(-0.10, -1.0, 0.02), lR=(0.02, -1.0, -0.30), uL=(0.80, 0.55, -0.30), lL=(0.65, 0.75, -0.15), fL=(0, -0.20, 0), pR=24)),
    # flat horizontal sweep: both arms carried from one side clean across to the other
    "sweep": shape(P_(hip=(-0.03, 0.03, -0.09), hr=(0, 0, -46), sp=(4, 0, -22), ch=(2, 0, -16), hd=(0, 0, 62),
                      uR=(-0.90, 0.62, 0.18), lR=(-0.70, 0.80, 0.30), uL=(-0.50, -0.62, 0.12), lL=(-0.90, -0.30, 0.20)),
                   P_(hip=(0.04, -0.08, -0.13), hr=(10, 0, 42), sp=(10, 0, 22), ch=(6, 0, 18), hd=(-10, 0, -60),
                      uR=(0.62, -0.72, 0.02), lR=(0.90, -0.30, 0.05), uL=(0.92, 0.42, 0.22), lL=(0.80, 0.62, 0.30), fL=(0, -0.10, 0))),
    # two-handed shove: cocked at the ribs leaning back, then both palms punch straight ahead
    "push": shape(P_(hip=(0, 0.09, -0.07), hr=(-10, 0, 0), sp=(-12, 0, 0), ch=(-8, 0, 0), hd=(12, 0, 0),
                     uR=(-0.55, 0.62, -0.42), lR=(-0.30, -0.55, 0.50), uL=(0.55, 0.62, -0.42), lL=(0.30, -0.55, 0.50)),
                  P_(hip=(0, -0.13, -0.11), hr=(14, 0, 0), sp=(14, 0, 0), ch=(6, 0, 0), hd=(-18, 0, 0),
                     uR=(-0.28, -1.0, 0.10), lR=(-0.16, -1.0, 0.16), uL=(0.28, -1.0, 0.10), lL=(0.16, -1.0, 0.16), fR=(-0.05, 0.10, 0), fL=(0.05, -0.12, 0), pR=16)),
    # gather low, then throw both arms up and wide, chest open to the sky
    "summon": shape(P_(hip=(0, 0, -0.21), sp=(26, 0, 0), ch=(12, 0, 0), hd=(8, 0, 0),
                       uR=(-0.15, -0.5, -0.85), lR=(0.25, -0.7, -0.6), uL=(0.15, -0.5, -0.85), lL=(-0.25, -0.7, -0.6)),
                    P_(hip=(0, 0.03, 0.06), hr=(-8, 0, 0), sp=(-18, 0, 0), ch=(-12, 0, 0), hd=(-14, 0, 0),
                       uR=(-0.82, 0.10, 0.72), lR=(-0.62, 0.0, 1.0), uL=(0.82, 0.10, 0.72), lL=(0.62, 0.0, 1.0))),
    # curl in tight, then burst open: arms flung wide and back, head thrown back
    "nova": shape(P_(hip=(0, 0, -0.17), sp=(28, 0, 0), ch=(16, 0, 0), hd=(18, 0, 0),
                     uR=(-0.20, -0.6, -0.5), lR=(0.5, -0.6, 0.3), uL=(0.20, -0.6, -0.5), lL=(-0.5, -0.6, 0.3)),
                  P_(hip=(0, 0.02, 0.04), hr=(-5, 0, 0), sp=(-16, 0, 0), ch=(-12, 0, 0), hd=(-18, 0, 0),
                     uR=(-0.95, 0.36, -0.12), lR=(-0.82, 0.30, -0.50), uL=(0.95, 0.36, -0.12), lL=(0.82, 0.30, -0.50), fR=(-0.06, 0, 0), fL=(0.06, 0, 0))),
    # brace with crossed arms, then stand tall and flex
    "buff": shape(P_(hip=(0, 0, -0.16), sp=(20, 0, 0), ch=(10, 0, 0), hd=(10, 0, 0),
                     uR=(-0.30, -0.7, -0.3), lR=(0.7, -0.5, 0.5), uL=(0.30, -0.7, -0.3), lL=(-0.7, -0.5, 0.5)),
                  P_(hip=(0, 0, 0.03), hr=(-4, 0, 0), sp=(-12, 0, 0), ch=(-10, 0, 0), hd=(-8, 0, 0),
                     uR=(-1.0, 0.0, 0.18), lR=(-0.35, -0.10, 1.0), uL=(1.0, 0.0, 0.18), lL=(0.35, -0.10, 1.0), fR=(-0.07, 0, 0), fL=(0.07, 0, 0))),
    # coil back, then lunge low with the lead arm speared forward
    "dash": shape(P_(hip=(0, 0.11, -0.15), hr=(-4, 0, -26), sp=(8, 0, -12), ch=(4, 0, -8), hd=(0, 0, 40),
                     uR=(-0.40, 0.80, -0.20), lR=(-0.30, -0.30, 0.60), uL=(0.30, -0.80, -0.10), lL=(0.10, -0.90, 0.30)),
                  P_(hip=(0, -0.24, -0.17), hr=(30, 0, 16), sp=(14, 0, 8), ch=(6, 0, 6), hd=(-36, 0, -22),
                     uR=(-0.10, -1.0, 0.10), lR=(-0.05, -1.0, 0.05), uL=(0.50, 0.85, 0.10), lL=(0.40, 0.90, 0.25), fR=(0, 0.20, 0.03), fL=(0, -0.22, 0), pR=38)),
    # drop into a ball, then spring out as a star
    "blink": shape(P_(hip=(0, 0.02, -0.29), sp=(34, 0, 0), ch=(16, 0, 0), hd=(18, 0, 0),
                      uR=(-0.25, -0.6, -0.7), lR=(0.4, -0.7, 0.2), uL=(0.25, -0.6, -0.7), lL=(-0.4, -0.7, 0.2)),
                   P_(hip=(0, 0, 0.09), hr=(-4, 0, 0), sp=(-12, 0, 0), ch=(-8, 0, 0), hd=(-10, 0, 0),
                      uR=(-0.92, 0.0, 0.45), lR=(-0.95, 0.0, 0.50), uL=(0.92, 0.0, 0.45), lL=(0.95, 0.0, 0.50), fR=(-0.09, 0, 0.02), fL=(0.09, 0, 0.02))),
    # sustained barrage: lead arm stabbing forward, body rocking behind it
    "channel": shape(P_(hip=(0, 0.06, -0.08), hr=(-4, 0, -20), sp=(-4, 0, -10), ch=(-2, 0, -8), hd=(2, 0, 30),
                        uR=(-0.50, 0.50, 0.30), lR=(-0.20, -0.50, 0.70), uL=(0.30, -0.80, 0.30), lL=(0.20, -0.90, 0.40)),
                     P_(hip=(0, -0.09, -0.10), hr=(8, 0, 16), sp=(8, 0, 10), ch=(4, 0, 8), hd=(-10, 0, -24),
                        uR=(-0.20, -1.0, 0.30), lR=(-0.10, -1.0, 0.36), uL=(0.60, 0.20, 0.50), lL=(0.30, -0.40, 0.90), fL=(0, -0.10, 0))),
}
ZERO = P_()

def mirror(p):
    def mv(v):
        return None if v is None else (-v[0], v[1], v[2])
    def mr(v):
        return (v[0], -v[1], -v[2])
    return dict(hip=mv(p["hip"]), hr=mr(p["hr"]), sp=mr(p["sp"]), ch=mr(p["ch"]), hd=mr(p["hd"]),
                uR=mv(p["uL"]), lR=mv(p["lL"]), uL=mv(p["uR"]), lL=mv(p["lR"]), fR=mv(p["fL"]), fL=mv(p["fR"]), pR=p["pL"], pL=p["pR"])

def scale_pose(p, k, kd=None):
    kd = k if kd is None else kd
    q = dict(p)
    q["hip"] = (p["hip"][0] * k, p["hip"][1] * k, p["hip"][2] * kd)
    for c in ("hr", "sp", "ch", "hd"):
        q[c] = tuple(x * k for x in p[c])
    return q

def cast_take(name, rid, shp, N, A, I, lead=1, amp=1.0, dip=1.0, hold=0, pulse=0.0, pop=0.3, base=None):
    """N frames; A = anticipation extreme, I = contact frame; hold = frames the hit pose is held (ultimates)."""
    if not want(name):
        return
    S = SHAPES[shp]
    ant, hit = scale_pose(S["ant"], amp, amp * dip), scale_pose(S["hit"], amp, amp * dip)
    if lead < 0:
        ant, hit = mirror(ant), mirror(hit)
    over = scale_pose(hit, 1.14)
    O = I + 2
    H = O + hold
    R = min(N - 3, H + max(5, int(0.42 * (N - H))))
    base_act = base or idle_of(rid)
    plant = rid not in FLOAT
    def chan(c, f, w_keys=None):
        return K([(1, ZERO[c], "s"), (A, ant[c], "s"), (I, hit[c], "i"), (O, over[c], "o"), (H, hit[c], "s"), (R, mix(ZERO[c], hit[c], 0.12), "s"), (N, ZERO[c], "s")], f)
    def armw(f):
        return K([(1, 0.0, "s"), (A, 1.0, "o"), (H, 1.0, "s"), (R, 0.25, "s"), (N, 0.0, "s")], f)
    def armdir(c, f):
        a, h = ant[c], hit[c]
        return K([(1, a, "s"), (A, a, "s"), (I, h, "i"), (O, h, "s"), (N, h, "s")], f)
    poses = []
    for f in range(1, N + 1):
        P = eval_pose(base_act, 1 + (f - 1) % int(base_act.frame_range[1] - 1))
        F = feet_of(P)
        tre = pulse * math.sin((f - O) * 1.9) if O < f < H else 0.0     # held ultimates tremble / pump
        hip = chan("hip", f); hr = chan("hr", f); sp = chan("sp", f - 0.4); ch = chan("ch", f - 0.8); hd = chan("hd", f - 1.3)
        body(P, (hip[0], hip[1], hip[2] + 0.012 * tre), hr, (sp[0] + 2.5 * tre, sp[1], sp[2]), (ch[0] + 2 * tre, ch[1], ch[2]), hd)
        w = armw(f)
        arm_to(P, "r", armdir("uR", f), armdir("lR", f - 0.3), w)
        arm_to(P, "l", armdir("uL", f - 0.5), armdir("lL", f - 0.8), w)
        if plant:
            fo = {"r": list(chan("fR", f)), "l": list(chan("fL", f))}
            for sd, c in (("r", "fR"), ("l", "fL")):   # a little arc while the foot travels
                d = Vector(hit[c]) - Vector(ant[c])
                if d.length > 0.05 and A < f < I:
                    fo[sd][2] += 0.05 * math.sin(math.pi * (f - A) / (I - A))
            replant(P, F, fo, {"r": chan("pR", f), "l": chan("pL", f)})
        if pop and I <= f <= I + 2:
            k = 1.0 + pop * (1 - (f - I) / 3.0)
            hand_pop(P, "r", k)
            if shp in ("slam", "push", "summon", "nova", "buff", "blink"):
                hand_pop(P, "l", k)
        poses.append(P)
    finish(name, base_act, poses, False)

# E (quick: contact on f7 of 24) and R (ultimate: bigger, contact f11, pose held, 40 frames) per reaper
KIT7 = {  # id: (E shape, R shape)
    "Grim": ("dash", "sweep"), "Mort": ("throw", "nova"), "Widow": ("push", "nova"), "Bonesmith": ("dash", "summon"),
    "Keeper": ("throw", "summon"), "Monk": ("dash", "channel"), "Intern": ("sweep", "summon"), "Femur": ("dash", "nova"),
    "Digby": ("sweep", "slam"), "Collector": ("throw", "slam"), "Beak": ("throw", "summon"), "Tallow": ("blink", "push"),
    "Lookout": ("push", "slam"), "Vexa": ("dash", "buff"), "Actuary": ("push", "channel"), "Sweep": ("blink", "nova"),
    "Solace": ("slam", "summon"), "Pallbearers": ("slam", "buff"), "Ectoplasm": ("throw", "summon"), "Neckless": ("throw", "buff"),
    "Tickwell": ("blink", "slam"),
}

def gen_kit():
    for k, rid in enumerate(IDS):
        e, r = KIT7[rid]
        lead = 1 if k % 2 == 0 else -1
        if rid == "Bonesmith":
            lead = -1          # shield arm (left) leads the bash
        amp = 1.12 if rid in HEAVY else (1.05 if rid in QUICK else 1.0)
        dip = 1.2 if rid in HEAVY else (0.8 if rid in HUNCH else 1.0)
        aE = 5 if rid in HEAVY else (3 if rid in QUICK else 4)
        cast_take("SQ_E_" + rid, rid, e, 24, aE, aE + 3, lead, amp, dip)
        aR = 7 if rid in HEAVY else (5 if rid in QUICK else 6)
        cast_take("SQ_R_" + rid, rid, r, 40, aR, aR + 5, -lead, amp * 1.12, dip, hold=10, pulse=1.0, pop=0.4)
    # The Shade's own scythe sweep: Slash style, contact f9 of 24 (ImpactFrac .35)
    cast_take("SQ_Sig_Grim", "Grim", "sweep", 24, 5, 9, 1, 1.05, 1.0, pop=0.3)

# ---------------------------------------------------------------- whole-body layer on baked takes
def hand_speed_peak(act, N):
    best, bf, prev = -1.0, max(2, round(1 + 0.35 * (N - 1))), None
    for f in range(1, N + 1):
        P = eval_pose(act, f)
        h = (head_w(P, "hand.r").copy(), head_w(P, "hand.l").copy())
        if prev is not None and 3 <= f <= N - 4:
            v = max((h[0] - prev[0]).length, (h[1] - prev[1]).length)
            if v > best:
                best, bf = v, f
        prev = h
    return bf

LAYER = {  # style -> (twist deg, dip m, pitch deg into the hit, arm widen deg)
    "twist": (26, 0.07, 6, 24), "dip": (8, 0.13, 16, 20), "rise": (10, -0.05, -12, 30), "lean": (16, 0.09, 12, 18)}
LAYERED = {  # take: (style, side, amp, contact frame or None = fastest hand)
    "SQ_Attack_Chop_R2": ("dip", 1, 1.0, 13), "SQ_Attack_Punch_R2": ("twist", 1, 1.1, 11), "SQ_Attack_Slam_R3": ("dip", -1, 1.1, 16),
    "SQ_Attack_Slash_R2": ("twist", 1, 1.15, 9), "SQ_Attack_Thrust_R3": ("lean", 1, 1.0, 9), "SQ_Cast_Long_R2": ("rise", -1, 1.0, 19),
    "SQ_Cast_Raise_R2": ("rise", 1, 1.0, 14), "SQ_Cast_Shoot_R2": ("lean", -1, 1.0, 7), "SQ_Cast_Summon_R2": ("rise", -1, 1.1, 14),
    "SQ_Throw_R2": ("twist", 1, 1.1, 8), "SQ_Reap_Swing": ("twist", 1, 0.9, 9),
    "SQ_Ability_Audit": ("dip", 1, 1.0, 13), "SQ_Ability_Banker": ("rise", -1, 0.9, None), "SQ_Ability_Builder": ("dip", 1, 1.0, 14),
    "SQ_Ability_Bulwark": ("lean", -1, 1.1, 11), "SQ_Ability_Collections": ("twist", 1, 1.0, 25), "SQ_Ability_DashStrike": ("twist", 1, 1.2, 11),
    "SQ_Ability_Fetch": ("lean", -1, 1.1, None), "SQ_Ability_Hauler": ("dip", 1, 1.0, None), "SQ_Ability_LastRites": ("rise", 1, 1.0, None),
    "SQ_Ability_Lookout": ("rise", -1, 0.9, 28), "SQ_Ability_MobileBank": ("dip", -1, 0.9, None), "SQ_Ability_OnTheJob": ("twist", -1, 1.1, None),
    "SQ_Ability_Rooftops": ("lean", 1, 1.1, None), "SQ_Ability_Scorch": ("twist", -1, 1.2, 9), "SQ_Ability_Seance": ("rise", 1, 1.0, None),
    "SQ_Ability_SlowSouls": ("rise", -1, 1.1, None), "SQ_Ability_Spite": ("twist", 1, 1.2, None), "SQ_Ability_Stampede": ("lean", 1, 1.2, 28),
    "SQ_Ability_Stopwatch": ("rise", -1, 1.0, 13), "SQ_Ability_TeamDamage": ("twist", -1, 1.0, 12), "SQ_Ability_Triage": ("dip", 1, 0.9, 12),
}

def gen_layers():
    for name, (style, side, amp, I) in LAYERED.items():
        if not want(name) or name not in ACT:
            continue
        act = ACT[name]
        N = int(act.frame_range[1])
        if I is None:
            I = hand_speed_peak(act, N)
        I = max(4, min(N - 4, I))
        A = max(2.0, I - max(3.0, 0.5 * (I - 1)))
        O = min(N - 2.0, I + 2.5)
        R = min(N - 1.0, I + 0.6 * (N - I))
        tw_d, dip_m, pit_d, wide_d = LAYER[style]
        tw = [(1, 0.0, "s"), (A, -1.0, "s"), (I, 1.0, "i"), (O, 1.2, "o"), (R, 0.3, "s"), (N, 0.0, "s")]
        dp = [(1, 0.0, "s"), (A, 0.7 if dip_m > 0 else 1.0, "s"), (I, 1.0 if dip_m > 0 else -1.0, "i"), (O, 1.1 if dip_m > 0 else -1.15, "o"), (R, 0.2, "s"), (N, 0.0, "s")]
        pt = [(1, 0.0, "s"), (A, -0.7, "s"), (I, 1.0, "i"), (O, 1.15, "o"), (R, 0.2, "s"), (N, 0.0, "s")]
        wd = [(1, 0.0, "s"), (A, 0.8, "s"), (I, 1.0, "l"), (O, 1.0, "s"), (R, 0.3, "s"), (N, 0.0, "s")]
        poses = []
        for f in range(1, N + 1):
            P = eval_pose(act, f)
            F = feet_of(P)
            t, d, p, w = K(tw, f) * amp, K(dp, f) * amp, K(pt, f) * amp, K(wd, f) * amp
            t2, p2 = K(tw, f - 0.7) * amp, K(pt, f - 0.7) * amp
            body(P, (side * 0.03 * t, -0.05 * max(0.0, p), -abs(dip_m) * d if dip_m > 0 else 0.05 * max(0.0, -d) - 0.05 * max(0.0, d)),
                 (0.35 * pit_d * p, 0, side * tw_d * t), (0.45 * pit_d * p2, 0, side * 0.45 * tw_d * t2),
                 (0.2 * pit_d * p2, 0, side * 0.4 * tw_d * t2), (-0.6 * pit_d * p2, 0, -side * 1.3 * tw_d * t2))
            for sd, sg in (("r", 1.0), ("l", -1.0)):
                rot_subtree(P, "upperarm." + sd, Quaternion((0, 1, 0), math.radians(sg * wide_d * w)))
            replant(P, F)
            poses.append(P)
        base = act.copy()
        loop = bool(act.get("sq_loop", False))
        finish(name, base, poses, loop)
        bpy.data.actions.remove(base)

# ---------------------------------------------------------------- locomotion, pushed again
def gait7(G, run, rid):
    G = copy.deepcopy(G)
    heavy, quick = rid in HEAVY, rid in QUICK
    for k, m in (("asw", 1.9), ("bob", 1.9 if not heavy else 2.3), ("hroll", 1.9), ("hyaw", 1.6), ("chyaw", 2.0), ("sway", 1.5 if not heavy else 1.9),
                 ("sproll", 1.7), ("nod", 1.3), ("h", 1.35 if run else 1.2), ("toe", 1.2)):
        if k in G:
            G[k] = G[k] * m
    G["ab"] = G.get("ab", 12) + (22 if run else 12)
    G["abd"] = G.get("abd", 0) + (10 if run else 5)
    G["sp"] = G.get("sp", 4) + (10 if run else 3)
    G["H"] = G.get("H", -0.04) - (0.035 if run else 0.01)
    if run and G.get("asw", 0) > 20:
        G["el"] = min(105, G.get("el", 40) + 25)          # pumping, bent elbows
        G["eld"] = G.get("eld", 0) + 12
    if "hroll" not in G:
        G["hroll"] = 7
    if quick:
        G["nod"] = G.get("nod", 4) * 1.3
    return G

def float7(G, run):
    G = dict(G)
    G["lean"] = G.get("lean", 12) + (14 if run else 6)
    G["roll"] = G.get("roll", 5) * 2.2
    G["yaw"] = G.get("yaw", 6) * 2.0
    G["ab"] = G.get("ab", 30) + 18
    G["asw"] = G.get("asw", 10) * 2.0
    G["bob"] = G.get("bob", 0.04) * 1.8
    G["sway"] = G.get("sway", 0.03) * 1.8
    return G

def take7(name, N, loop, fn, cfg=None, base="Idle"):
    if not want(name):
        return
    act = make_core(name, base, N, fn, loop, cfg)
    act["sq_remaster"] = 7
    DONE7.append(name)

def gen_locomotion7():
    for rid, G in W.items():
        if "float" in G:
            take7("SQ_Walk_" + rid, 41, True, lambda t, G=G: floater(t, float7(G["float"], False)), base="Running_A")
            take7("SQ_Run_" + rid, 31, True, lambda t, G=G: floater(t, dict(float7(G["float_run"], True), ph=1)), base="Running_A")
            continue
        wn = 33 if G["walk"]["D"] > 0.6 else 37
        gw, gr = gait7(G["walk"], False, rid), gait7(G["run"], True, rid)
        def mk(g, post):
            def fn(t):
                s = walker(t, g)
                if post:
                    post(s, t)
                return s
            return fn
        take7("SQ_Walk_" + rid, wn, True, mk(gw, G.get("walk_post")), None, "Walking_A")
        take7("SQ_Run_" + rid, 21, True, mk(gr, G.get("run_post")), None, "Running_A")

# ---------------------------------------------------------------- state poses per reaper (on the reaper's own idle)
def state_take(name, rid, N, loop, fn):
    """fn(t in [0,1], f) -> dict(hip, hr, sp, ch, hd, uR, lR, uL, lL, w, fR, fL, pR, pL)"""
    if not want(name):
        return
    base_act = idle_of(rid)
    plant = rid not in FLOAT
    poses = []
    for f in range(1, N + 1):
        t = (f - 1) / (N - 1)
        P = eval_pose(base_act, 1 + (f - 1) % int(base_act.frame_range[1] - 1) if not loop else 1 + t * (base_act.frame_range[1] - 1))
        F = feet_of(P)
        c = fn(t, f)
        body(P, c.get("hip", Z3), c.get("hr", Z3), c.get("sp", Z3), c.get("ch", Z3), c.get("hd", Z3))
        w = c.get("w", 1.0)
        wl, wr = c.get("wl", w), c.get("wr", w)
        if c.get("uR"):
            arm_to(P, "r", c["uR"], c["lR"], wr)
        if c.get("uL"):
            arm_to(P, "l", c["uL"], c["lL"], wl)
        if plant:
            replant(P, F, {"r": c.get("fR", Z3), "l": c.get("fL", Z3)}, {"r": c.get("pR", 0.0), "l": c.get("pL", 0.0)})
        poses.append(P)
    finish(name, base_act, poses, loop)

def gen_states():
    for k, rid in enumerate(IDS):
        sd = 1 if k % 2 == 0 else -1
        heavy, quick, hunch, flt = rid in HEAVY, rid in QUICK, rid in HUNCH, rid in FLOAT
        # ---- victory (loop, 31): small ones hop and pump both fists; heavies raise one fist and rock; hunched ones cackle;
        # floaters spread wide and sway
        def victory(t, f, sd=sd):
            hop = abs(math.sin(2 * math.pi * t * (2 if quick or not (heavy or flt) else 1)))
            if heavy:
                return dict(hip=(sd * 0.03 * S2(t), 0, -0.04 + 0.03 * hop), hr=(-4, 4 * S2(t), 0), sp=(-10, 0, 0), ch=(-8, 0, sd * 6), hd=(-10, 0, 0),
                            uR=(-0.4 * (sd > 0) - 0.3, -0.1, 1.0 if sd > 0 else -0.9), lR=(-0.2, -0.1, 1.0 if sd > 0 else -0.6),
                            uL=(0.4 * (sd < 0) + 0.3, -0.1, 1.0 if sd < 0 else -0.9), lL=(0.2, -0.1, 1.0 if sd < 0 else -0.6), w=0.9 + 0.1 * hop)
            if flt:
                return dict(hip=(0.05 * S2(t), 0, 0.05 * S2(2 * t)), hr=(-8, 8 * S2(t), 10 * S2(t)), sp=(-10, 0, 0), ch=(-8, 0, 0), hd=(-12, 0, 0),
                            uR=(-0.9, 0.1, 0.5 + 0.2 * S2(2 * t)), lR=(-0.8, 0.0, 0.9), uL=(0.9, 0.1, 0.5 - 0.2 * S2(2 * t)), lL=(0.8, 0.0, 0.9))
            if hunch:
                return dict(hip=(0, 0, -0.03 + 0.03 * hop), sp=(-6 + 5 * hop, 0, 0), ch=(-6, 0, 0), hd=(-18 + 8 * hop, 0, 0),
                            uR=(-0.6, -0.5, 0.3 + 0.2 * hop), lR=(-0.3, -0.4, 0.9), uL=(0.6, -0.5, 0.3 + 0.2 * hop), lL=(0.3, -0.4, 0.9))
            return dict(hip=(0, 0, 0.09 * hop), hr=(-4, 0, 0), sp=(-10, 0, 0), ch=(-8, 0, 0), hd=(-12, 0, 0),
                        uR=(-0.45, 0.0, 0.9), lR=(-0.25, -0.1, 0.6 + 0.4 * hop), uL=(0.45, 0.0, 0.9), lL=(0.25, -0.1, 0.6 + 0.4 * (1 - hop)),
                        fR=(0, 0, 0.07 * hop), fL=(0, 0, 0.07 * hop), pR=20 * hop, pL=20 * hop)
        state_take("SQ_Victory_" + rid, rid, 31, True, victory)
        # ---- defeat (loop, 41): slumped, head hanging, slow sigh
        def defeat(t, f, sd=sd):
            sigh = S2(t)
            return dict(hip=(sd * 0.02, 0.02, -0.10 - 0.01 * sigh), hr=(6, 0, 0), sp=(30 + 3 * sigh, 0, sd * 4), ch=(18 + 2 * sigh, 0, 0), hd=(30, sd * 8, 4 * S2(t * 2)),
                        uR=(-0.15, -0.3, -0.95), lR=(-0.05, -0.25, -1.0), uL=(0.15, -0.3, -0.95), lL=(0.05, -0.25, -1.0), w=0.95)
        state_take("SQ_Defeat_" + rid, rid, 41, True, defeat)
        # ---- downed (loop, 31): on its knees, one hand on the ground, the other clutching the chest, laboured breathing
        def downed(t, f, sd=sd):
            br = S2(t)
            prop_u, prop_l = (-sd * 0.45, -0.55, -0.8), (-sd * 0.25, -0.5, -0.9)
            cl_u, cl_l = (sd * 0.25, -0.5, -0.8), (-sd * 0.7, -0.5, 0.35)
            c = dict(hip=(sd * 0.03, 0.03, -0.30 - 0.01 * br), hr=(22, sd * 5, 0), sp=(26 + 3 * br, 0, sd * 6), ch=(12 + 2 * br, 0, 0), hd=(22 - 3 * br, sd * 6, 0),
                     fR=(0, 0.16, 0), fL=(0, 0.22, 0), pR=30, pL=40)
            if sd > 0:
                c.update(uL=prop_u, lL=prop_l, uR=cl_u, lR=cl_l)
            else:
                c.update(uR=prop_u, lR=prop_l, uL=cl_u, lL=cl_l)
            return c
        state_take("SQ_Downed_" + rid, rid, 31, True, downed)
        # ---- revive (one shot, 26): up off the knees, overshoot into a tall stretch, settle
        def revive(t, f, sd=sd):
            d = downed(0.0, 1)
            up = dict(hip=(0, 0, 0.07), hr=(-6, 0, 0), sp=(-14, 0, 0), ch=(-10, 0, 0), hd=(-12, 0, 0), fR=Z3, fL=Z3, pR=0.0, pL=0.0)
            out = {}
            for c in ("hip", "hr", "sp", "ch", "hd", "fR", "fL"):
                out[c] = K([(1, d.get(c, Z3), "s"), (5, mix(d.get(c, Z3), Z3, 0.15), "s"), (12, up.get(c, Z3), "i"), (15, tuple(x * 1.1 for x in up.get(c, Z3)), "o"), (26, Z3, "s")], f)
            for c in ("pR", "pL"):
                out[c] = K([(1, d[c], "s"), (10, 0.0, "s"), (26, 0.0, "s")], f)
            out.update(uR=(-0.7, 0.1, 0.8), lR=(-0.5, 0.0, 1.0), uL=(0.7, 0.1, 0.8), lL=(0.5, 0.0, 1.0),
                       w=K([(1, 0.0, "s"), (8, 0.0, "s"), (13, 1.0, "i"), (17, 1.0, "s"), (26, 0.0, "s")], f))
            return out
        state_take("SQ_Revive_" + rid, rid, 26, False, revive)
        # ---- bank at the lantern (33, plays as a repeated one-shot): both hands offered forward and up, pouring
        def bank(t, f, sd=sd):
            w = K([(1, 0.0, "s"), (7, 1.0, "o"), (26, 1.0, "s"), (33, 0.0, "s")], f)
            pour = math.sin(math.pi * min(1.0, max(0.0, (f - 5) / 22.0)))
            return dict(hip=(0, -0.03 * w, -0.05 * w), hr=(6 * w, 0, 0), sp=(10 * w - 6 * pour, 0, 0), ch=(4 * w, 0, 0), hd=(-14 * w - 6 * pour, 0, 0),
                        uR=(-0.62, -0.7, 0.35 + 0.35 * pour), lR=(-0.45, -0.55, 0.75 + 0.3 * pour), uL=(0.62, -0.7, 0.35 + 0.35 * pour), lL=(0.45, -0.55, 0.75 + 0.3 * pour), w=w)
        state_take("SQ_Bank_" + rid, rid, 33, False, bank)

gen_layers()
gen_kit()
gen_locomotion7()
gen_states()
if want("SQ_Marker_Remaster7"):
    idle = ACT["Idle"]
    act = write_action("SQ_Marker_Remaster7", idle, [eval_pose(idle, 1), eval_pose(idle, 2)])
    act["sq_loop"] = False
    DONE7.append("SQ_Marker_Remaster7")
set_action(ACT["Idle"])
sc.frame_set(1)
bpy.ops.wm.save_as_mainfile(filepath=BLEND)
print("DONE7", len(DONE7), DONE7)
print("NACTIONS", len(bpy.data.actions))
