# ====================================================================== IDLES (hand-animated Idle_B / Idle_Combat + the reaper's posture + a personality beat)
IDLE_T = {
    "norm":  dict(base="Idle_B", k=dict(up=1.4, arm=1.2, head=1.2, hipl=1.5), breath=2.0, sway=0.012, look=[(0.30, 0), (0.40, 26), (0.58, 22), (0.68, 0)]),
    "quick": dict(base="Idle_B", k=dict(up=1.6, arm=1.4, head=1.3, hipl=2.0), breath=2.5, sway=0.02, look=[(0.12, 0), (0.17, 34), (0.30, 30), (0.35, -30), (0.50, -26), (0.56, 0), (0.80, 0), (0.84, 20), (0.92, 0)], bounce=0.018),
    "heavy": dict(base="Idle_B", k=dict(up=1.5, arm=1.2, head=1.0, hipl=2.2), breath=4.0, sway=0.02, look=[(0.40, 0), (0.55, -18), (0.75, -14), (0.9, 0)], heave=0.02),
    "hunch": dict(base="Idle_B", k=dict(up=1.3, arm=1.3, head=1.2, hipl=1.3), breath=2.2, sway=0.016, look=[(0.2, 0), (0.3, 16), (0.45, 12), (0.55, -14), (0.7, -10), (0.8, 0)], nod=5),
    "proud": dict(base="Idle_B", k=dict(up=1.3, arm=1.2, head=1.0, hipl=1.4), breath=2.6, sway=0.01, look=[(0.35, 0), (0.5, 30), (0.7, 26), (0.85, 0)]),
    "float": dict(base="Idle_B", k=dict(up=1.4, arm=1.4, head=1.2, hipl=1.0), breath=2.0, sway=0.02, look=[(0.3, 0), (0.42, -22), (0.6, -18), (0.72, 0)], hover=0.05),
}
IDLE_BEAT = {  # take, its frames a0..a1, where in the loop (u0..u1), weight
    "quick": ("Cheer", 1, 30, 0.55, 0.90, 0.8), "heavy": ("Block", 1, 33, 0.50, 0.85, 0.7), "hunch": ("Interact", 1, 40, 0.50, 0.92, 0.75),
    "proud": ("2H_Melee_Idle", 1, 33, 0.45, 0.90, 0.8), "norm": ("Use_Item", 1, 49, 0.45, 0.95, 0.7), "float": ("Spellcasting", 1, 21, 0.55, 0.85, 0.6),
}
def _idle_grim(P, f, u, N):       # drifts in a slow figure of eight, head tilting as if listening
    ph = 2 * math.pi * u
    move_subtree(P, "hips", (0.035 * math.sin(ph), 0.02 * math.sin(2 * ph), 0.02 * math.sin(2 * ph + 1.0)))
    rot_subtree(P, "head", E((0, 9 * math.sin(ph + 0.8), 0)))
    arms_out(P, 8 + 6 * math.sin(2 * ph - 0.7))
def _idle_digby(P, f, u, N):      # big slow heave of the shoulders, then a neck crack
    ph = 2 * math.pi * u
    hv = 0.5 + 0.5 * math.sin(ph * 2 - 1.2)
    arms_out(P, 9 * hv)
    rot_subtree(P, "chest", E((-4 * hv, 0, 0)))
    ck = K8([(1, 0, "s"), (N * 0.62, 0, "s"), (N * 0.66, 1, "i"), (N * 0.70, -0.5, "o"), (N * 0.76, 0, "s"), (N, 0, "s")], f)
    rot_subtree(P, "head", E((0, 22 * ck, 0)))
def _idle_intern(P, f, u, N):     # can not stand still: bounces on his toes and snaps his head about
    ph = 2 * math.pi * u
    b = abs(math.sin(ph * 6))
    move_subtree(P, "hips", (0, 0, 0.022 * b))
    rot_subtree(P, "head", E((0, 6 * math.sin(ph * 6), 0)))
def _idle_tickwell(P, f, u, N):   # nods off on his staff, then jerks awake
    d = K8([(1, 0, "s"), (N * 0.15, 0, "s"), (N * 0.55, 1, "s"), (N * 0.60, 1.05, "s"), (N * 0.64, -0.35, "i"), (N * 0.72, 0.1, "o"), (N * 0.8, 0, "s"), (N, 0, "s")], f)
    rot_subtree(P, "chest", E((7 * d, 0, 0)))
    rot_subtree(P, "head", E((24 * d, 4 * d, 0)))
    move_subtree(P, "hips", (0, 0, -0.012 * max(0.0, d)))
def _idle_neckless(P, f, u, N):   # parade rest: chest out, slow proud sweep of the gaze, a small flourish of the sword arm
    ph = 2 * math.pi * u
    rot_subtree(P, "chest", E((-7, 0, 0)))
    fl = K8([(1, 0, "s"), (N * 0.5, 0, "s"), (N * 0.6, 1, "o"), (N * 0.7, 0.8, "s"), (N * 0.82, 0, "s"), (N, 0, "s")], f)
    rot_subtree(P, "upperarm.r", Quaternion((0, 1, 0), math.radians(26 * fl)) @ Quaternion((1, 0, 0), math.radians(-30 * fl)))
IDLE_HERO = {"Grim": _idle_grim, "Digby": _idle_digby, "Intern": _idle_intern, "Tickwell": _idle_tickwell, "Neckless": _idle_neckless}
def gen_idle(rid):
    t = btype(rid)
    G = IDLE_T[t]
    base = G["base"]
    N = len(seq(base)[1])                      # play the hand animation at its own speed
    C = dict(k=G["k"], ref=mean_ref(base), post=dict(up=1.0, head=1.0, arm=0.85, hip=1.0, hipl=1.0), float=(t == "float"),
             spring={"head": (2.0, 0.5), "wrist.l": (2.5, 0.5), "wrist.r": (2.5, 0.5), "lowerarm.l": (2.8, 0.55), "lowerarm.r": (2.8, 0.55)})
    lk = [(1 + (N - 1) * u, v, "s") for u, v in [(0.0, 0)] + G["look"] + [(1.0, 0)]]
    beat = IDLE_BEAT.get(rid) or IDLE_BEAT.get(t)
    if beat:                                   # personality beat: part of another hand-animated take over the upper body
        an, a0, a1, u0, u1, wmax = beat
        f0, f1 = 1 + (N - 1) * u0, 1 + (N - 1) * u1
        C["ov"] = dict(an=an, t=lambda ff: a0 + (a1 - a0) * max(0.0, min(1.0, (ff - f0) / (f1 - f0))),
                       w=lambda ff: wmax * smooth01(min((ff - f0) / 5.0, (f1 - ff) / 6.0)) if f0 < ff < f1 else 0.0)
    def layer(P, f, u):
        ph = 2 * math.pi * u
        br = math.sin(ph * 2)                                  # two breaths per loop
        yaw = K8(lk, f)
        hz = G.get("hover", 0.0) * (1.0 + 0.7 * math.sin(ph * 2 - 0.6)) + G.get("bounce", 0.0) * abs(math.sin(ph * 4)) - G.get("heave", 0.0) * (0.5 + 0.5 * br)
        sw = math.sin(ph) + 0.25 * math.sin(3 * ph)          # weight shift with a settle at each side (not a pendulum)
        body8(P, hip=(G["sway"] * 1.6 * sw, 0, hz - 0.006 * abs(sw)), hr=(0, -5.0 * sw, 2.0 * sw), sp=(-G["breath"] * 0.4 * br, 3.5 * sw, -1.5 * sw),
              ch=(-G["breath"] * br, 0, yaw * 0.25), hd=(G.get("nod", 0) * math.sin(ph * 2 + 0.5) + G["breath"] * 0.6 * br, 0, yaw * 0.75))
        arms_out(P, 2.5 * br)
        hi = IDLE_HERO.get(rid)
        if hi:
            hi(P, f, u, N)
    build8("SQ_Idle_" + rid, N, True, base, loopmap(base, N), C, rid, layer)
    DONE8.append("SQ_Idle_" + rid)
FAMS["idle"] = gen_idle
