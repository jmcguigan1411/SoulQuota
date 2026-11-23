# ====================================================================== WALKS (hand-animated Walking_A/B/C)
WALK = {
    "norm":  dict(base="Walking_A", N=27, k=dict(up=1.5, arm=1.5, head=0.8, hipl=1.8), lean=2, stride=1.2, lift=1.2, width=1.0, out=14, tw=6, parm=0.4),
    "quick": dict(base="Walking_B", N=21, k=dict(up=1.5, arm=1.4, head=0.8, hipl=2.2), lean=4, stride=1.15, lift=1.4, width=0.9, out=16, tw=6, parm=0.3),
    "heavy": dict(base="Walking_C", N=37, k=dict(up=1.8, arm=1.5, head=0.8, hipl=3.0), lean=3, stride=1.1, lift=1.1, width=1.6, out=28, tw=9, parm=0.4),
    "hunch": dict(base="Walking_A", N=31, k=dict(up=1.4, arm=1.2, head=0.9, hipl=1.6), lean=2, stride=1.0, lift=1.0, width=1.0, out=14, tw=5, parm=0.6),
    "proud": dict(base="Walking_B", N=27, k=dict(up=1.5, arm=1.5, head=0.8, hipl=1.8), lean=-5, stride=1.25, lift=1.4, width=1.0, out=12, tw=8, parm=0.4),
    "float": dict(base="Walking_A", N=37, k=dict(up=1.2, arm=0.8, head=0.8, hipl=0.5), lean=7, stride=1.0, lift=1.0, width=1.0, out=22, tw=4, parm=0.8),
}
def gen_walk(rid):
    t = btype(rid)
    G = WALK[t]
    N = G["N"]; base = G["base"]
    C = dict(k=G["k"], ref=mean_ref(base), post=dict(up=0.6, head=0.7, arm=G["parm"], hip=0.4, hipl=1.0),
             lag={"lowerarm.l": 1.0, "lowerarm.r": 1.0, "wrist.l": 2.0, "wrist.r": 2.0, "hand.l": 2.0, "hand.r": 2.0, "head": 1.5, "chest": 0.6},
             stride=G["stride"], lift=G["lift"], width=G["width"], float=(t == "float"))
    def layer(P, f, u):
        ph = 2 * math.pi * u
        tw = G["tw"] * math.sin(ph)
        if t == "float":
            body8(P, hip=(0.02 * math.sin(ph), 0, 0.10 + 0.04 * math.sin(2 * ph)), hr=(G["lean"], 3 * math.sin(ph), 0), ch=(0, 0, tw), hd=(-G["lean"] * 0.7, 0, -tw * 0.6))
        else:
            body8(P, hr=(G["lean"], 0, -tw * 0.5), ch=(0, 0, tw), hd=(-G["lean"] * 0.6, 0, -tw * 0.5))
        if t == "heavy":
            body8(P, hip=(0.03 * math.sin(ph), 0, 0), hr=(0, 7 * math.sin(ph), 0), ch=(0, -5 * math.sin(ph), 0))
        arms_out(P, G["out"])
    build8("SQ_Walk_" + rid, N, True, base, loopmap(base, N), C, rid, layer)
    DONE8.append("SQ_Walk_" + rid)
FAMS["walk"] = gen_walk
