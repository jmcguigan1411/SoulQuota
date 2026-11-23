# ====================================================================== Nov 19: character layer for the 16 non-hero reapers,
# victory / defeat / bank on hand-animated bases, shared landing and dash on the hand-animated jump and dodge.
RUN_OVR.update({
    "Vexa": dict(N=21, width=1.9, k=dict(up=1.5, arm=1.3, head=0.8, hipl=2.0), tw=13, out=30, lift=0.9, warp=0.45),        # butcher's waddle, belly rolling
    "Bonesmith": dict(N=24, lean=6, tw=8, width=1.4, warp=0.45, wparm=0.8),                                             # hammer dragged, leaning into it
    "Pallbearers": dict(N=27, lean=-3, tw=4, lift=1.3, out=20, k=dict(up=1.4, arm=0.8, head=0.8, hipl=2.0), warp=0.2),  # solemn march
    "Monk": dict(N=21, lean=4, tw=5, lift=1.1, parm=0.7, k=dict(up=1.3, arm=0.6, head=0.8, hipl=1.6), warp=0.2),        # hands tucked, even stride
    "Lookout": dict(N=22, lean=8, tw=12, stride=1.2, out=26),                                                           # crouched, scanning
    "Mort": dict(N=14, lift=1.5, out=30, tw=8),                                                                        # high-kneed prance
    "Femur": dict(N=13, stride=1.35, lean=-2, out=18, warp=0.5, wparm=0.5),                                            # bounding gallop
    "Sweep": dict(N=16, k=dict(up=1.4, arm=0.35, head=0.9, hipl=1.6), out=55, parm=0.8, tw=14, lift=1.4),               # scarecrow: stiff arms, twisting torso, flailing legs
    "Tallow": dict(N=12, stride=0.95, lift=0.9, k=dict(up=1.2, arm=1.1, head=0.6, hipl=0.9), out=14, parm=0.5, warp=0.15),  # tiny quick steps, flame kept level
    "Actuary": dict(N=15, lean=-14, tw=3, lift=1.2, parm=0.6, k=dict(up=1.15, arm=0.7, head=0.8, hipl=1.3), warp=0.15),  # stiff, prim trot
    "Beak": dict(N=19, lean=4, tw=8, k=dict(up=1.3, arm=1.2, head=1.3, hipl=1.5), wparm=0.7),                           # stooped, head pecking
    "Keeper": dict(N=22, stride=0.95, lift=0.9, lean=2, tw=4, parm=0.7, warp=0.15),                                     # careful shuffle
    "Collector": dict(N=18, lean=6, out=30, tw=9, k=dict(up=1.35, arm=1.5, head=0.8, hipl=1.5), wparm=0.55),            # greedy, grabbing run
    "Solace": dict(N=21, lean=-6, tw=3, k=dict(up=1.1, arm=0.8, head=0.8, hipl=1.2), warp=0.15, wparm=0.7),             # serene, upright
    "Widow": dict(N=23, k=dict(up=1.3, arm=1.0, head=0.8, hipl=0.5), out=48, lean=8, tw=9, parm=0.6),                  # arms spread like a web
    "Ectoplasm": dict(N=21, k=dict(up=1.5, arm=0.9, head=0.8, hipl=0.9), out=20, lean=18, tw=3, wparm=0.6),            # wobbling blob, tipped forward
})
for _rid, _o in list(RUN_OVR.items()):                 # walks inherit the same character, slower and softer
    if _rid not in WALK_OVR:
        _t = btype(_rid)
        _w = {}
        if "N" in _o: _w["N"] = int(round(_o["N"] * WALK[_t]["N"] / RUN[_t]["N"] / 2) * 2 + 1)
        for _k in ("out", "tw", "lift", "stride", "width", "parm", "warp"):
            if _k in _o: _w[_k] = _o[_k]
        if "lean" in _o: _w["lean"] = _o["lean"] * 0.4
        if "k" in _o:
            _w["k"] = dict(WALK[_t]["k"]); _w["k"]["arm"] = _o["k"]["arm"] * WALK[_t]["k"]["arm"] / RUN[_t]["k"]["arm"]
        WALK_OVR[_rid] = _w

def _sovr(rid, **kw):
    d = dict(SHOT_OVR.get(rid, {})); d.update(kw); SHOT_OVR[rid] = d
_sovr("Vexa", hold=3, drift=0.25, ring=12, ring_tau=5, ring_per=12, tw=1.2, drop=0.08)            # the cleaver's weight carries her round
_sovr("Bonesmith", hold_anti=3, hold=4, drift=0.15, drop=0.10, ring=9, ring_tau=5, ring_per=12)   # a smith's blow: lifted, dropped, stopped
_sovr("Pallbearers", hold_anti=2, hold=3, drift=0.25, ring=6, ring_tau=6, ring_per=13, rise_x=1.3, lunge=0.03)   # solemn and deliberate
_sovr("Monk", hold=2, drift=0.4, ring=4, ring_tau=3, ring_per=8)                                 # disciplined: crisp, no wobble
_sovr("Lookout", hold=2, drift=0.4, ring=8, ring_tau=5, ring_per=11)
_sovr("Mort", hop=0.06, ring=10, ring_tau=3, ring_per=6, lunge=0.09)                              # springs out of every throw
_sovr("Femur", hop=0.08, snap=1, lunge=0.10, ring=8, ring_tau=3, ring_per=6)                      # pounces
_sovr("Sweep", ring=14, ring_tau=5, ring_per=7, counter=1.3)                                      # flops like straw
_sovr("Tallow", drop=0.01, ring_per=5)                                                            # keeps the flame level
_sovr("Actuary", snap=1.5, hold=2, drift=0.3, ring=3, ring_tau=2, ring_per=6)                     # a precise stamp, nothing wasted
_sovr("Beak", hold_anti=2, ring=8, ring_tau=4, ring_per=8, lean_x=0.7)                            # pecks
_sovr("Keeper", hold=3, drift=0.25, ring=5, ring_tau=6, ring_per=12)
_sovr("Collector", lunge=0.08, ring=9, ring_tau=4, ring_per=8, counter=1.2)                       # grabs
_sovr("Solace", hold=3, drift=0.25, ring=4, ring_tau=6, ring_per=13, drop=0.01)                   # serene
_sovr("Widow", ring=8, ring_tau=5, ring_per=10, counter=1.3)
_sovr("Ectoplasm", ring=16, ring_tau=6, ring_per=8)                                               # jelly wobble

# ---------------------------------------------------------------- idle acts for the other sixteen
def _win(f, N, u0, u1, a=0.06, d=0.08):
    """0..1 window over the loop fraction u0..u1 with eased edges"""
    return K8([(1, 0, "s"), (1 + (N - 1) * u0, 0, "s"), (1 + (N - 1) * (u0 + a), 1, "o"), (1 + (N - 1) * (u1 - d), 1, "s"), (1 + (N - 1) * u1, 0, "s"), (N, 0, "s")], f)
def _raise(P, sd, fwd, out=0.0, bend=0.0):
    sx = -1.0 if sd == "l" else 1.0
    rot_subtree(P, "upperarm." + sd, Quaternion((1, 0, 0), math.radians(-fwd)) @ Quaternion((0, 1, 0), math.radians(sx * out)))
    if bend: rot_subtree(P, "lowerarm." + sd, Quaternion((1, 0, 0), math.radians(-bend)))
def _idle_mort(P, f, u, N):        # restless: rolls his shoulders, snaps a look over each one
    ph = 2 * math.pi * u
    arms_out(P, 7 * max(0.0, math.sin(ph * 3)))
    lk = K8([(1, 0, "s"), (N * .12, 0, "s"), (N * .16, 1, "o"), (N * .26, 1, "s"), (N * .30, -1, "i"), (N * .40, -1, "s"), (N * .46, 0, "o"), (N, 0, "s")], f)
    rot_subtree(P, "chest", E((0, 0, 14 * lk))); rot_subtree(P, "head", E((0, 0, 26 * lk)))
def _idle_femur(P, f, u, N):       # cocks his head side to side like a dog, a little hop in the middle
    ck = K8([(1, 0, "s"), (N * .10, 1, "o"), (N * .30, 1, "s"), (N * .36, -1, "i"), (N * .56, -1, "s"), (N * .62, 0, "o"), (N, 0, "s")], f)
    rot_subtree(P, "head", E((0, 19 * ck, 0)))
    hp = max(0.0, math.sin((u - 0.70) * math.pi / 0.10)) if 0.70 < u < 0.80 else 0.0
    move_subtree(P, "hips", (0, 0, 0.05 * hp))
def _idle_sweep(P, f, u, N):       # a scarecrow in the wind: sways from the post, head lolling
    ph = 2 * math.pi * u
    g = math.sin(ph) + 0.4 * math.sin(3 * ph + 1.0)
    rot_subtree(P, "spine", E((2 * math.sin(2 * ph), 6 * g, 3 * g)))
    rot_subtree(P, "head", E((6 * math.sin(2 * ph + 0.5), -9 * g, 0)))
def _idle_tallow(P, f, u, N):      # slowly sags as if melting, then pops back upright
    d = K8([(1, 0, "s"), (N * .60, 1, "s"), (N * .66, 1.08, "s"), (N * .70, -0.25, "i"), (N * .78, 0.05, "o"), (N * .85, 0, "s"), (N, 0, "s")], f)
    move_subtree(P, "hips", (0, 0, -0.035 * max(0.0, d)))
    rot_subtree(P, "chest", E((6 * d, 0, 0))); rot_subtree(P, "head", E((8 * d, 5 * d, 0)))
    arms_out(P, 6 * d)
def _idle_actuary(P, f, u, N):     # checks his wrist, taps his foot of a head: impatient nods
    w = _win(f, N, 0.45, 0.82)
    _raise(P, "l", 55 * w, out=-12 * w, bend=70 * w)
    rot_subtree(P, "head", E((20 * w + 4 * w * math.sin(f * 1.3), 0, 14 * w)))
def _idle_vexa(P, f, u, N):        # fists on hips, a belly laugh that shakes her
    ph = 2 * math.pi * u
    w = _win(f, N, 0.40, 0.85)
    sh = math.sin(f * 1.5) * w
    arms_out(P, 16 + 3 * sh)
    rot_subtree(P, "chest", E((-7 * w + 2.2 * sh, 0, 0))); rot_subtree(P, "head", E((-12 * w - 2 * sh, 0, 0)))
    move_subtree(P, "hips", (0, 0, 0.008 * sh))
def _idle_bonesmith(P, f, u, N):   # weighs the hammer in his hand, twice, then rolls the shoulder
    w = K8([(1, 0, "s"), (N * .30, 0, "s"), (N * .36, 1, "o"), (N * .42, 0.2, "s"), (N * .48, 1, "o"), (N * .56, 0, "s"), (N, 0, "s")], f)
    _raise(P, "r", 18 * w, bend=22 * w)
    rl = _win(f, N, 0.66, 0.9)
    rot_subtree(P, "chest", E((0, 5 * rl * math.sin(f * 0.5), 6 * rl * math.sin(f * 0.25))))
def _idle_pallbearers(P, f, u, N): # bows the head and holds it, hands folded
    w = _win(f, N, 0.35, 0.80, a=0.10, d=0.12)
    arms_out(P, -8)
    rot_subtree(P, "chest", E((6 * w, 0, 0))); rot_subtree(P, "head", E((24 * w, 0, 0)))
def _idle_monk(P, f, u, N):        # almost perfectly still: hands together, one slow nod
    arms_out(P, -12)
    w = _win(f, N, 0.50, 0.80, a=0.12, d=0.12)
    rot_subtree(P, "head", E((12 * w, 0, 0)))
def _idle_lookout(P, f, u, N):     # hand to the brow, a long sweep of the horizon
    w = _win(f, N, 0.20, 0.85)
    _raise(P, "l", 95 * w, out=-25 * w, bend=95 * w)
    sw = K8([(1, 0, "s"), (N * .26, -1, "s"), (N * .72, 1, "s"), (N * .85, 0, "s"), (N, 0, "s")], f)
    rot_subtree(P, "chest", E((4 * w, 0, 18 * sw))); rot_subtree(P, "head", E((0, 0, 16 * sw)))
def _idle_beak(P, f, u, N):        # two quick pecks, then a sideways tilt to look with one eye
    pk = K8([(1, 0, "s"), (N * .30, 0, "s"), (N * .33, 1, "i"), (N * .37, 0, "o"), (N * .40, 1, "i"), (N * .45, 0, "o"), (N, 0, "s")], f)
    tl = _win(f, N, 0.58, 0.86)
    rot_subtree(P, "chest", E((7 * pk, 0, 0))); rot_subtree(P, "head", E((22 * pk, 20 * tl, 10 * tl)))
def _idle_keeper(P, f, u, N):      # lifts the lantern hand and peers past it
    w = _win(f, N, 0.40, 0.88)
    _raise(P, "l", 70 * w, out=10 * w, bend=35 * w)
    rot_subtree(P, "chest", E((8 * w, 0, -8 * w))); rot_subtree(P, "head", E((6 * w, 0, -14 * w)))
def _idle_collector(P, f, u, N):   # rubs his hands together, a sly glance each way
    w = _win(f, N, 0.25, 0.80)
    rb = math.sin(f * 1.9) * w
    _raise(P, "l", 42 * w + 4 * rb, out=-28 * w, bend=70 * w)
    _raise(P, "r", 42 * w - 4 * rb, out=-28 * w, bend=70 * w)
    gl = K8([(1, 0, "s"), (N * .40, 1, "s"), (N * .55, 1, "s"), (N * .62, -1, "s"), (N * .75, -1, "s"), (N * .85, 0, "s"), (N, 0, "s")], f)
    rot_subtree(P, "head", E((6 * w, 0, 18 * gl)))
def _idle_solace(P, f, u, N):      # opens both arms in a slow blessing and lets them fall
    w = _win(f, N, 0.40, 0.85, a=0.14, d=0.14)
    arms_out(P, 34 * w)
    _raise(P, "l", 22 * w); _raise(P, "r", 22 * w)
    rot_subtree(P, "chest", E((-6 * w, 0, 0))); rot_subtree(P, "head", E((-10 * w, 0, 0)))
def _idle_widow(P, f, u, N):       # arms ripple like spider legs, one after the other; head tilts, listening to the web
    ph = 2 * math.pi * u
    rot_subtree(P, "upperarm.l", Quaternion((0, 1, 0), math.radians(-10 - 9 * math.sin(ph * 3))))
    rot_subtree(P, "upperarm.r", Quaternion((0, 1, 0), math.radians(10 + 9 * math.sin(ph * 3 + 1.6))))
    rot_subtree(P, "lowerarm.l", Quaternion((1, 0, 0), math.radians(-14 * math.sin(ph * 3 + 0.8))))
    rot_subtree(P, "lowerarm.r", Quaternion((1, 0, 0), math.radians(-14 * math.sin(ph * 3 + 2.4))))
    rot_subtree(P, "head", E((0, 11 * math.sin(ph + 0.5), 0)))
    move_subtree(P, "hips", (0, 0, 0.03 * math.sin(ph * 2)))
def _idle_ecto(P, f, u, N):        # jelly: bobs and wobbles, never quite settling
    ph = 2 * math.pi * u
    move_subtree(P, "hips", (0.015 * math.sin(ph * 3 + 1), 0, 0.03 * math.sin(ph * 3)))
    rot_subtree(P, "spine", E((3 * math.sin(ph * 3 + 0.6), 5 * math.sin(ph * 3), 0)))
    rot_subtree(P, "head", E((0, -7 * math.sin(ph * 3 - 0.5), 0)))
IDLE_HERO.update({"Mort": _idle_mort, "Femur": _idle_femur, "Sweep": _idle_sweep, "Tallow": _idle_tallow, "Actuary": _idle_actuary, "Vexa": _idle_vexa,
                  "Bonesmith": _idle_bonesmith, "Pallbearers": _idle_pallbearers, "Monk": _idle_monk, "Lookout": _idle_lookout, "Beak": _idle_beak,
                  "Keeper": _idle_keeper, "Collector": _idle_collector, "Solace": _idle_solace, "Widow": _idle_widow, "Ectoplasm": _idle_ecto})
for _rid in ("Monk", "Pallbearers", "Sweep", "Widow", "Ectoplasm", "Tallow", "Lookout", "Keeper", "Collector", "Solace", "Actuary", "Vexa", "Beak"):
    IDLE_BEAT[_rid] = None                     # these have their own act; no borrowed beat on top
_gi0 = gen_idle
def gen_idle(rid):
    if rid in IDLE_BEAT and IDLE_BEAT[rid] is None:
        t = btype(rid); sv = IDLE_BEAT.get(t); IDLE_BEAT[t] = None
        try: _gi0(rid)
        finally: IDLE_BEAT[t] = sv
    else:
        _gi0(rid)
FAMS["idle"] = gen_idle

# ---------------------------------------------------------------- victory / defeat / bank on hand-animated bases
def gen_victory(rid):
    # all on the hand-animated Cheer (the jump-spin Taunt left the frame and looped badly); the body type sets how it is celebrated
    t = btype(rid)
    base = "Cheer"
    N = len(seq(base)[1])
    kk = {"quick": 1.5, "heavy": 1.3, "hunch": 1.15, "proud": 1.3, "float": 1.4, "norm": 1.3}[t]
    C = dict(k=dict(up=kk, arm=kk, head=1.1, hipl=1.2), post=dict(up=0.5, head=0.5, arm=0.15, hip=0.3, hipl=1.0), float=(t == "float"),
             spring={"head": (3.0, 0.45), "wrist.l": (5.0, 0.45), "wrist.r": (5.0, 0.45)})
    vr = (sum(ord(c) for c in rid) % 5) / 4.0          # 0..1, stable per reaper: staggers the accents inside a body type
    def layer(P, f, u):
        ph = 2 * math.pi * u
        hz = 0.0; chx = -6.0; yw = 5 * math.sin(ph + vr * 2)
        if t == "quick":
            hz = 0.05 * abs(math.sin(ph * (3 if vr < 0.5 else 2)))                      # bouncing on the spot
        elif t == "float":
            hz = 0.12 + 0.06 * math.sin(ph * 2); yw = 18 * math.sin(ph)                 # rises and turns side to side
        elif t == "heavy":
            st = max(0.0, math.sin(ph * 2 + vr * 3)) ** 3                               # two slow fist pumps with a stomp under each
            hz = -0.03 * st; chx = -6 - 6 * st
            _raise(P, "r" if vr < 0.5 else "l", 30 * st, bend=25 * st)
        elif t == "hunch":
            chx = 2 - 5 * (0.5 + 0.5 * math.sin(ph * 2))                                # a restrained little nod-along
        elif t == "proud":
            chx = -12
            _raise(P, "r", 50 * (0.5 + 0.5 * math.sin(ph)), out=10)                     # weapon arm held high
        body8(P, hip=(0, 0, hz), ch=(chx, 0, yw), hd=(-6, 0, -yw * 0.4))
        arms_out(P, 10)
    build8("SQ_Victory_" + rid, N, True, base, loopmap(base, N), C, rid, layer)
    DONE8.append("SQ_Victory_" + rid)

def gen_defeat(rid):
    t = btype(rid)
    base = "Idle_B"; N = len(seq(base)[1])
    C = dict(k=dict(up=1.0, arm=0.4, head=1.0, hipl=1.0), ref=mean_ref(base), post=dict(up=0.35, head=0.4, arm=0.25, hip=0.5, hipl=1.0), float=(t == "float"),
             spring={"head": (1.6, 0.5), "lowerarm.l": (2.0, 0.5), "lowerarm.r": (2.0, 0.5)})
    deep = {"quick": 1.0, "heavy": 0.8, "hunch": 0.55, "proud": 1.1, "float": 0.8, "norm": 1.0}[t]
    if rid in ("Sweep", "Pallbearers"): deep = 0.5
    def layer(P, f, u):
        ph = 2 * math.pi * u
        sg = K8([(1, 0, "s"), (N * .30, 0, "s"), (N * .42, 1, "s"), (N * .55, 0, "s"), (N, 0, "s")], f)            # one long sigh
        shk = K8([(1, 0, "s"), (N * .62, 0, "s"), (N * .68, 1, "s"), (N * .76, -1, "s"), (N * .84, 0.6, "s"), (N * .92, 0, "s"), (N, 0, "s")], f)   # slow shake of the head
        body8(P, hip=(0, 0, -0.03 * deep - 0.012 * sg + (0.06 if t == "float" else 0)), sp=(6 * deep, 0, 0), ch=(9 * deep - 5 * sg, 0, 0), hd=(18 * deep + 4 * sg, 0, 14 * shk))
        arms_out(P, -2)
    build8("SQ_Defeat_" + rid, N, True, base, loopmap(base, N), C, rid, layer)
    DONE8.append("SQ_Defeat_" + rid)

def gen_bank(rid):
    t = btype(rid)
    base, N = "Interact", 33
    keys = [(1, 1, "s"), (8, 12, "o"), (22, 20, "l"), (N, 40, "s")]
    C = dict(k=dict(up=1.3, arm=1.3, head=1.0, hipl=1.2), post=dict(up=0.5, head=0.5, arm=0.15, hip=0.3, hipl=1.0), float=(t == "float"),
             spring={"head": (3.0, 0.5), "wrist.l": (5.0, 0.5), "wrist.r": (5.0, 0.5)})
    def layer(P, f, u):
        w = K8([(1, 0, "s"), (8, 1, "o"), (22, 1, "s"), (N - 2, 0, "s"), (N, 0, "s")], f)
        tr = math.sin(f * 2.2) * w
        body8(P, hip=(0, -0.03 * w, -0.035 * w + (0.10 if t == "float" else 0)), ch=(11 * w + 0.8 * tr, 0, 0), hd=(-8 * w, 0, 0))
        _raise(P, "l", 38 * w + 2 * tr, out=-6 * w, bend=18 * w)             # both hands offered to the lantern, trembling as the souls pour
        _raise(P, "r", 14 * w - 2 * tr, bend=10 * w)
    build8("SQ_Bank_" + rid, N, False, base, lambda f: K8(keys, f), C, rid, layer)
    DONE8.append("SQ_Bank_" + rid)
FAMS["victory"] = gen_victory; FAMS["defeat"] = gen_defeat; FAMS["bank"] = gen_bank

# ---------------------------------------------------------------- shared landing and dash (one take for every reaper: neutral posture)
POST["_N"] = dict(NEUT)
def gen_shared(rid):
    if rid != IDS[0] and not (ONLY_ID and rid == ONLY_ID[0]):
        return
    N = 12                                           # landing: hits the crouch on frame 3, a beat at the bottom, springs back up
    keys = [(1, 3, "s"), (3, 7, "l"), (5, 9, "o"), (N, 21, "s")]
    C = dict(k=dict(up=1.35, arm=1.4, head=1.0, hipl=1.25), post={}, edge_in=False,
             spring={"head": (3.5, 0.4), "lowerarm.l": (4.5, 0.4), "lowerarm.r": (4.5, 0.4), "wrist.l": (5.0, 0.4), "wrist.r": (5.0, 0.4)})
    def layer(P, f, u):
        d = K8([(1, 0.3, "s"), (3, 1, "i"), (5, 0.8, "s"), (N, 0, "s")], f)
        body8(P, ch=(10 * d, 0, 0), hd=(-6 * d, 0, 0)); arms_out(P, 26 * d)
    a = build8("SQ_Jump_Land", N, False, "Jump_Land", lambda f: K8(keys, f), C, "_N", layer)
    set_scale_keys(a, "head", [(1, (1, 1, 1)), (3, (1.1, 0.86, 1.1)), (6, (0.96, 1.06, 0.96)), (9, (1, 1, 1)), (N, (1, 1, 1))], N)
    N = 13                                           # dash: the hand-animated dodge, thrown lower and longer
    C = dict(k=dict(up=1.4, arm=1.5, head=1.0, hipl=1.3), post={},
             spring={"head": (3.5, 0.45), "lowerarm.l": (4.5, 0.4), "lowerarm.r": (4.5, 0.4)})
    def layer2(P, f, u):
        d = K8([(1, 0, "s"), (3, 1, "o"), (8, 0.9, "s"), (N, 0, "s")], f)
        body8(P, hip=(0, 0, -0.05 * d), hr=(14 * d, 0, 0), hd=(-12 * d, 0, 0)); arms_out(P, 30 * d)
    a = build8("SQ_Dash", N, False, "Dodge_Forward", lambda f: K8([(1, 1, "s"), (3, 4, "o"), (9, 9, "l"), (N, 13, "s")], f), C, "_N", layer2)
    set_scale_keys(a, "head", [(1, (1, 1, 1)), (3, (0.92, 1.14, 0.92)), (8, (0.95, 1.08, 0.95)), (11, (1.05, 0.95, 1.05)), (N, (1, 1, 1))], N)
    DONE8.extend(["SQ_Jump_Land", "SQ_Dash"])
FAMS["shared"] = gen_shared

# ---------------------------------------------------------------- stride matching: the planted foot must travel at the speed the
# game plays the cycle at 1x (AnimRunSpeed 550 cm/s, AnimWalkSpeed 250 cm/s), so feet do not skate. Half the correction goes
# into stride length and half into cycle length, which keeps each reaper's cadence character.
REF_RUN, REF_WALK = 5.91, 5.91 * 250.0 / 550.0
STRIDE_REPORT = []
def _stance_speed(name):
    """planted-foot speed estimated from the fore-aft travel of the foot over the cycle and the stance share of the gait
    (run 40 %, walk 60 %): robust to the timing warp, which makes per-frame speeds uneven on purpose"""
    act = bpy.data.actions[name]; N = int(act.frame_range[1])
    ys = [head_w(eval_pose(act, f), "foot.l").y for f in range(1, N + 1)]
    duty = 0.40 if "_Run_" in name else 0.60
    return (max(ys) - min(ys)) / (duty * (N - 1) / 30.0)
def _matched(gen, TBL, OVR, pre, ref):
    """These are short-legged characters moving fast: a fully planted foot would need a blur of tiny steps. So the match is
    partial and bounded (cycle at most 20 % shorter, stride at most 45 % longer than the reviewed gait); the rest is reported."""
    def run(rid):
        t = btype(rid); G0 = dict(TBL[t]); G0.update(OVR.get(rid, {}))
        gen(rid)
        if rid in FLOAT:
            return
        for it in range(2):
            v = _stance_speed(pre + rid)
            c = ref / max(0.01, v)
            if abs(c - 1) < 0.06: break
            o = dict(OVR.get(rid, {}))
            G = dict(TBL[t]); G.update(o)
            if it == 0:
                o["N"] = int(round(max(0.8 * G0["N"], min(1.15 * G0["N"], G0["N"] / c ** 0.5))))
                o["stride"] = max(0.8 * G0["stride"], min(1.45 * G0["stride"], G0["stride"] * c ** 0.5))
            else:
                o["stride"] = max(0.8 * G0["stride"], min(1.45 * G0["stride"], G["stride"] * c))
            OVR[rid] = o
            DONE8.remove(pre + rid)
            gen(rid)
        v = _stance_speed(pre + rid)
        STRIDE_REPORT.append("%s%s N=%d stride %.2f skate x%.2f" % (pre, rid, int(bpy.data.actions[pre + rid].frame_range[1]), OVR.get(rid, {}).get("stride", TBL[t]["stride"]), ref / max(0.01, v)))
    return run
FAMS["run"] = _matched(gen_run, RUN, RUN_OVR, "SQ_Run_", REF_RUN)
FAMS["walk"] = _matched(gen_walk, WALK, WALK_OVR, "SQ_Walk_", REF_WALK)
