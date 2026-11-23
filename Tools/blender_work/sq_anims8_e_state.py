# ====================================================================== HIT / DOWNED / REVIVE on hand-animated bases
def gen_hit(rid):
    t = btype(rid)
    N, base = 14, ("Hit_B" if t in ("proud", "norm") else "Hit_A")
    keys = [(1, 1, "s"), (3, 7 if base == "Hit_B" else 5, "o"), (6, 10 if base == "Hit_B" else 8, "s"), (N, 27 if base == "Hit_B" else 21, "x")]
    k = 1.7 if t != "heavy" else 1.35          # the big ones barely flinch, the small ones get thrown
    C = dict(k=dict(up=k, arm=k, head=0.8, hipl=1.5), post=dict(up=0.5, head=0.5, arm=0.2, hip=0.3, hipl=1.0), float=(t == "float"), spring={"head": (4.0, 0.7), "lowerarm.l": (4.5, 0.4), "lowerarm.r": (4.5, 0.4), "wrist.l": (5.0, 0.4), "wrist.r": (5.0, 0.4)})
    def layer(P, f, u):
        d = K8([(1, 0, "s"), (3, 1, "o"), (7, 0.5, "s"), (N, 0, "s")], f)
        body8(P, hip=(0, 0.05 * d, -0.03 * d + (0.10 if t == "float" else 0)), ch=(-8 * d, 0, 0))
        arms_out(P, 18 * d)
    act = build8("SQ_Hit_" + rid, N, False, base, lambda f: K8(keys, f), C, rid, layer)
    set_scale_keys(act, "head", [(1, (1, 1, 1)), (2, (1.1, 0.88, 1.1)), (4, (0.95, 1.08, 0.95)), (8, (1, 1, 1)), (N, (1, 1, 1))], N)
    DONE8.append("SQ_Hit_" + rid)

def gen_downed(rid):
    t = btype(rid)
    base, N = "Sit_Floor_Idle", 61
    C = dict(k=dict(up=1.5, arm=1.2, head=1.3, hipl=1.0), ref=mean_ref(base), post=dict(up=0.35, head=0.3, arm=0.0, hip=0.0, hipl=0.0), float=False,
             spring={"head": (1.6, 0.45)})
    f0, f1 = 1 + (N - 1) * 0.45, 1 + (N - 1) * 0.85      # a reach for help, part way through the loop
    C["ov"] = dict(an="Interact", t=lambda ff: 1 + 39 * max(0.0, min(1.0, (ff - f0) / (f1 - f0))),
                   w=lambda ff: 0.75 * smooth01(min((ff - f0) / 6.0, (f1 - ff) / 7.0)) if f0 < ff < f1 else 0.0)
    def layer(P, f, u):
        ph = 2 * math.pi * u
        sh = 1.2 * math.sin(f * 2.1) * (0.5 + 0.5 * math.sin(ph * 2))          # shivering in waves
        body8(P, sp=(10 + 3 * math.sin(ph * 2), 2 * math.sin(ph) + sh, 0), ch=(8 + 3 * math.sin(ph * 2 - 0.5), -sh, 0), hd=(18 + 5 * math.sin(ph * 2 - 1.0), 0, 10 * math.sin(ph)))
    build8("SQ_Downed_" + rid, N, True, base, loopmap(base, N), C, rid, layer)
    DONE8.append("SQ_Downed_" + rid)

def gen_revive(rid):
    t = btype(rid)
    base, N = "Sit_Floor_StandUp", 26
    keys = [(1, 1, "s"), (5, 6, "i"), (15, 30, "s"), (N, 35, "o")]
    C = dict(k=dict(up=1.25, arm=1.3, head=1.0, hipl=1.0), post=dict(up=0.3, head=0.3, arm=0.0, hip=0.0, hipl=0.0), float=False, edge_in=False,
             spring={"head": (3.0, 0.4), "lowerarm.l": (4.0, 0.4), "lowerarm.r": (4.0, 0.4), "wrist.l": (4.5, 0.4), "wrist.r": (4.5, 0.4)})
    def layer(P, f, u):
        pop = K8([(1, 0, "s"), (14, 0, "s"), (17, 1, "o"), (21, 0.3, "s"), (N, 0, "s")], f)     # up onto the toes, chest out, arms flung wide
        body8(P, hip=(0, 0, 0.05 * pop), ch=(-14 * pop, 0, 0), hd=(-8 * pop, 0, 0))
        arms_out(P, 40 * pop)
    act = build8("SQ_Revive_" + rid, N, False, base, lambda f: K8(keys, f), C, rid, layer)
    set_scale_keys(act, "head", [(1, (1, 1, 1)), (13, (1.06, 0.92, 1.06)), (17, (0.93, 1.12, 0.93)), (21, (1.03, 0.97, 1.03)), (N, (1, 1, 1))], N)
    DONE8.append("SQ_Revive_" + rid)
FAMS["hit"] = gen_hit; FAMS["downed"] = gen_downed; FAMS["revive"] = gen_revive
