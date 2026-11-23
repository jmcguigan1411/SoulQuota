# ====================================================================== SIGNATURE ATTACKS + E / R CASTS (one-shots)
_HS = {}
def hand_track(an):
    """per base take: dominant hand, contact frame (peak hand speed), wind-up apex (pose farthest from contact before it)"""
    if an in _HS:
        return _HS[an]
    b0, sq = seq(an)
    n = len(sq)
    pos = {h: [] for h in "lr"}
    for i in range(n):
        P = from_basis(sq[i])
        for h in "lr":
            pos[h].append(head_w(P, "hand." + h).copy())
    best = (-1, "r", 1)
    for h in "lr":
        for i in range(2, n - 2):
            v = (pos[h][i + 1] - pos[h][i - 1]).length
            if v > best[0]:
                best = (v, h, i)
    _, h, ci = best
    ai = max(range(0, ci), key=lambda i: (pos[h][i] - pos[h][ci]).length) if ci > 0 else 0
    _HS[an] = dict(hand=h, contact=b0 + ci, apex=b0 + ai, b0=b0, b1=b0 + n - 1)
    return _HS[an]

BK = {  # hand-read key frames of the KayKit takes: wind-up apex, contact, dominant hand, kind (swing / push / up / jump)
    "2H_Melee_Attack_Slice": dict(apex=9, contact=14, hand="r", kind="swing"), "2H_Melee_Attack_Chop": dict(apex=18, contact=25, hand="r", kind="chop"),
    "2H_Melee_Attack_Spin": dict(apex=18, contact=29, hand="r", kind="swing", end=62), "1H_Melee_Attack_Chop": dict(apex=13, contact=19, hand="r", kind="chop"),
    "1H_Melee_Attack_Jump_Chop": dict(apex=15, contact=22, hand="r", kind="chop"), "1H_Melee_Attack_Slice_Horizontal": dict(apex=6, contact=10, hand="r", kind="swing"),
    "1H_Melee_Attack_Stab": dict(apex=10, contact=16, hand="r", kind="push"), "Throw": dict(apex=16, contact=21, hand="r", kind="swing"),
    "Spellcast_Shoot": dict(apex=7, contact=11, hand="r", kind="push"), "Spellcast_Raise": dict(apex=6, contact=16, hand="r", kind="up", end=64),
    "Spellcast_Long": dict(apex=7, contact=14, hand="r", kind="up"), "Spellcast_Summon": dict(start=70, apex=84, contact=97, hand="r", kind="up"),
    "1H_Ranged_Shoot": dict(apex=5, contact=9, hand="r", kind="push"), "2H_Ranged_Shoot": dict(apex=5, contact=9, hand="r", kind="push"),
    "Interact": dict(apex=4, contact=12, hand="r", kind="push"), "Cheer": dict(apex=3, contact=10, hand="l", kind="up"),
    "Taunt": dict(apex=6, contact=14, hand="r", kind="up"), "Taunt_Longer": dict(apex=8, contact=15, hand="r", kind="up", end=60),
    "Block_Attack": dict(apex=6, contact=11, hand="l", kind="push"), "Unarmed_Melee_Attack_Punch_A": dict(apex=10, contact=14, hand="r", kind="push"),
    "Unarmed_Melee_Attack_Punch_B": dict(apex=11, contact=16, hand="r", kind="push"), "Jump_Full_Short": dict(apex=6, contact=14, hand="r", kind="jump"),
    "Dodge_Forward": dict(apex=2, contact=6, hand="r", kind="jump"), "2H_Melee_Attack_Spinning": dict(apex=3, contact=9, hand="r", kind="swing"),
}
STAGE = {  # staging for the camera behind: body yaw away at the wind-up / through after contact (deg), rise or sink at contact
    "swing": dict(a=55, t=62, rise=0.0, back=-10, lean=16, wide=34), "chop": dict(a=30, t=22, rise=0.07, back=-20, lean=24, wide=30),
    "push": dict(a=42, t=30, rise=0.0, back=-8, lean=14, wide=22), "up": dict(a=20, t=12, rise=0.08, back=-14, lean=-6, wide=30),
    "jump": dict(a=12, t=12, rise=0.0, back=0, lean=10, wide=16),
}

def shot_map(an, N, I, O):
    """base-time map for a one-shot: start -> wind-up apex (reached early, held a beat) -> SNAP to contact on frame I ->
    2-frame contact hold -> long eased settle to the take's end. O: overrides (contact, apex, start, end, anti)."""
    H = dict(hand_track(an)); H.update(BK.get(an, {}))
    c = O.get("contact", H["contact"]); a = O.get("apex", H["apex"]); s0 = O.get("start", H.get("start", H["b0"])); e = O.get("end", H.get("end", H["b1"]))
    a = min(a, c - 1)
    snap = O.get("snap", 2)                      # frames from apex to contact
    Ia = max(2, I - snap)                        # apex reached here
    Ih = max(2, Ia - O.get("hold_anti", 1))      # arrives at the apex a beat early (readable anticipation)
    keys = [(1, s0, "s"), (Ih, a - 0.4, "o"), (Ia, a, "l"), (I, c, "i"), (I + O.get("hold", 2), c + O.get("drift", 0.8), "o"), (N, e, "x")]
    return lambda f: K8(keys, f), dict(H, contact=c, apex=a)

SIG_BASE_OLD = {  # reaper: (hand-animated base, overrides)
    "Grim": ("2H_Melee_Attack_Slice", {}), "Mort": ("Throw", {}), "Widow": ("Spellcast_Shoot", {}), "Bonesmith": ("Spellcast_Summon", {}),
    "Keeper": ("Spellcast_Raise", {}), "Monk": ("2H_Melee_Attack_Spin", {}), "Intern": ("Spellcast_Shoot", {}), "Femur": ("1H_Melee_Attack_Jump_Chop", {}),
    "Digby": ("2H_Melee_Attack_Chop", {}), "Collector": ("Throw", {}), "Beak": ("Spellcast_Shoot", {}), "Tallow": ("1H_Ranged_Shoot", {}),
    "Lookout": ("2H_Ranged_Shoot", {}), "Vexa": ("2H_Melee_Attack_Spin", {}), "Actuary": ("1H_Melee_Attack_Chop", {}), "Sweep": ("Spellcast_Raise", {}),
    "Solace": ("Spellcast_Raise", {}), "Pallbearers": ("Interact", {}), "Ectoplasm": ("Spellcast_Shoot", {}), "Neckless": ("2H_Ranged_Shoot", {}),
    "Tickwell": ("Spellcast_Raise", {}),
}
SIG_BASE = {
    "Grim": ("2H_Melee_Attack_Slice", {}), "Mort": ("Throw", {}), "Widow": ("Spellcast_Shoot", {}), "Bonesmith": ("Spellcast_Summon", {}),
    "Keeper": ("Spellcast_Raise", {}), "Monk": ("2H_Melee_Attack_Spin", {}), "Intern": ("Throw", {}), "Femur": ("1H_Melee_Attack_Jump_Chop", {}),
    "Digby": ("2H_Melee_Attack_Slice", {}), "Collector": ("Throw", {}), "Beak": ("Spellcast_Shoot", {}), "Tallow": ("1H_Ranged_Shoot", {}),
    "Lookout": ("2H_Ranged_Shoot", {}), "Vexa": ("2H_Melee_Attack_Spin", {}), "Actuary": ("1H_Melee_Attack_Chop", {}), "Sweep": ("Spellcast_Raise", {}),
    "Solace": ("Spellcast_Raise", {}), "Pallbearers": ("Interact", {}), "Ectoplasm": ("Spellcast_Shoot", {}), "Neckless": ("1H_Melee_Attack_Stab", {}),
    "Tickwell": ("Spellcast_Raise", {}),
}
SHOT_T = {  # per body type: exaggeration, timing feel (tw scales the staging twist)
    "norm":  dict(k=dict(up=1.35, arm=1.4, head=1.0, hipl=1.3), snap=2, hold_anti=1, drop=0.04, lunge=0.05, out=6),
    "quick": dict(k=dict(up=1.3, arm=1.2, head=1.0, hipl=1.3), snap=1.5, hold_anti=0, drop=0.03, lunge=0.07, out=4),
    "heavy": dict(k=dict(up=1.55, arm=1.5, head=1.0, hipl=1.7), snap=2, hold_anti=2, drop=0.07, lunge=0.05, out=22, tw=1.15),
    "hunch": dict(k=dict(up=1.3, arm=1.25, head=1.0, hipl=1.2), snap=2, hold_anti=1, drop=0.03, lunge=0.03, out=6),
    "proud": dict(k=dict(up=1.4, arm=1.25, head=1.0, hipl=1.3), snap=2, hold_anti=1, drop=0.03, lunge=0.06, out=6),
    "float": dict(k=dict(up=1.4, arm=1.5, head=1.0, hipl=1.0), snap=2, hold_anti=1, drop=0.05, lunge=0.08, out=8),
}

def shot8(name, rid, base, N, I, O=None, big=1.0, hold=None):
    O = dict(O or {})
    t = btype(rid)
    T = SHOT_T[t]
    O.setdefault("snap", T["snap"]); O.setdefault("hold_anti", T["hold_anti"])
    if hold is not None:
        O["hold"] = hold; O["drift"] = 0.5 + 0.12 * hold
    tmap, H = shot_map(base, N, I, O)
    k = {g: 1 + (v - 1) * big for g, v in T["k"].items()}
    if base in ("1H_Melee_Attack_Jump_Chop", "Jump_Full_Short", "Taunt", "Taunt_Longer", "Dodge_Forward"):
        k["hipl"] = 0.85                       # these takes already leave the ground: keep the hand-animated arc
    C = dict(k=k, post=dict(up=0.5, head=0.5, arm=0.15, hip=0.3, hipl=1.0), float=(t == "float"),
             lag={"head": 1.3, "wrist.l": 0.6, "wrist.r": 0.6, "hand.l": 0.6, "hand.r": 0.6})
    Hh = O.get("hold", 2)
    S = STAGE[H.get("kind", "swing")]
    sg = -1.0 if H["hand"] == "r" else 1.0          # right hand: right shoulder comes BACK toward the camera in the wind-up
    Ia = max(2, I - O["snap"])
    tws = T.get("tw", 1.0) * big
    def layer(P, f, u):
        yw = K8([(1, 0, "s"), (Ia, sg * S["a"] * tws, "o"), (I, -sg * S["t"] * tws * 0.55, "i"), (I + Hh, -sg * S["t"] * tws, "o"), (min(N - 1, I + Hh + 10), 0, "s"), (N, 0, "s")], f)
        rz = K8([(1, 0, "s"), (Ia, -0.5 * S["rise"], "o"), (I, S["rise"] * big, "i"), (I + Hh, S["rise"] * big, "s"), (min(N - 1, I + Hh + 9), 0, "s"), (N, 0, "s")], f)
        bk = K8([(1, 0, "s"), (Ia, S["back"] * 0.7, "o"), (I, -S["back"] * 0.6, "i"), (I + Hh, -S["back"] * 0.4, "s"), (min(N - 1, I + Hh + 9), 0, "s"), (N, 0, "s")], f)
        ln = K8([(1, 0, "s"), (Ia, 0, "s"), (I, S["lean"] * big, "i"), (I + Hh, S["lean"] * big * 0.9, "s"), (min(N - 1, I + Hh + 9), 0, "s"), (N, 0, "s")], f)
        wd = K8([(1, 0, "s"), (Ia, S["wide"] * 0.8, "o"), (I, S["wide"], "l"), (I + Hh, S["wide"] * 0.9, "s"), (min(N - 1, I + Hh + 8), 0, "s"), (N, 0, "s")], f)
        body8(P, hip=(0, 0, rz), hr=(ln * 0.5, 0, yw * 0.6), ch=(bk + ln * 0.5, 0, yw * 0.4), hd=(-(bk + ln) * 0.5, 0, -yw * 0.55))
        arms_out(P, wd)
        # anticipation: sink; contact: drive forward and stamp; then recover
        dz = K8([(1, 0, "s"), (max(2, I - O["snap"]), -T["drop"] * big, "o"), (I, -T["drop"] * big * 1.3, "i"), (I + Hh, -T["drop"] * big * 0.8, "s"), (min(N, I + Hh + 8), 0, "s"), (N, 0, "s")], f)
        dy = K8([(1, 0, "s"), (max(2, I - O["snap"]), T["lunge"] * big * 0.6, "o"), (I, -T["lunge"] * big, "i"), (I + Hh, -T["lunge"] * big * 0.8, "s"), (min(N, I + Hh + 9), 0, "s"), (N, 0, "s")], f)
        hov = (0.10 + 0.02 * math.sin(2 * math.pi * u)) if t == "float" else 0.0
        body8(P, hip=(0, dy, dz + hov))
        arms_out(P, T["out"])
    act = build8(name, N, False, base, tmap, C, rid, layer)
    # head squash and stretch round the hit (reads as snap from the chase camera; head is a leaf bone)
    s = 0.07 * big
    set_scale_keys(act, "head", [(1, (1, 1, 1)), (max(2, I - O["snap"]), (1 + s, 1 - s, 1 + s)), (max(2, I - 1), (1 - s, 1 + 1.4 * s, 1 - s)),
                                 (I + 1, (1 + 1.2 * s, 1 - 1.2 * s, 1 + 1.2 * s)), (I + 4, (1 - 0.3 * s, 1 + 0.3 * s, 1 - 0.3 * s)), (min(N, I + 8), (1, 1, 1)), (N, (1, 1, 1))], N)
    act["sq_impact"] = I
    DONE8.append(name)
    return act

SIG_FRAC = {"Shoot": 0.20, "Cast": 0.30, "Throw": 0.30, "Slash": 0.35, "Chop": 0.42, "Spin": 0.40, "Summon": 0.40, "Raise": 0.45, "Slam": 0.50, "Stab": 0.35}
SIG_STYLE = {"Grim": "Slash", "Mort": "Throw", "Widow": "Cast", "Bonesmith": "Summon", "Keeper": "Raise", "Monk": "Spin", "Intern": "Cast", "Femur": "Slam",
             "Digby": "Chop", "Collector": "Throw", "Beak": "Cast", "Tallow": "Shoot", "Lookout": "Shoot", "Vexa": "Spin", "Actuary": "Slam", "Sweep": "Cast",
             "Solace": "Cast", "Pallbearers": "Summon", "Ectoplasm": "Cast", "Neckless": "Shoot", "Tickwell": "Raise"}
def gen_sig(rid):
    name = "SQ_Sig_" + rid
    N = int(ACT[name].frame_range[1]) if name in ACT else 24
    I = max(4, round(1 + SIG_FRAC[SIG_STYLE[rid]] * (N - 1)))
    base, O = SIG_BASE[rid]
    shot8(name, rid, base, N, I, O)
FAMS["sig"] = gen_sig
