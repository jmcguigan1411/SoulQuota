# ====================================================================== IDLES (hand-animated Idle_B / Idle_Combat + the reaper's posture + a personality beat)
IDLE_T = {
    "norm":  dict(base="Idle_B", k=dict(up=1.4, arm=1.2, head=1.2, hipl=1.5), breath=2.0, sway=0.012, look=[(0.30, 0), (0.40, 26), (0.58, 22), (0.68, 0)]),
    "quick": dict(base="Idle_B", k=dict(up=1.6, arm=1.4, head=1.3, hipl=2.0), breath=2.5, sway=0.02, look=[(0.12, 0), (0.17, 34), (0.30, 30), (0.35, -30), (0.50, -26), (0.56, 0), (0.80, 0), (0.84, 20), (0.92, 0)], bounce=0.018),
    "heavy": dict(base="Idle_B", k=dict(up=1.5, arm=1.2, head=1.0, hipl=2.2), breath=4.0, sway=0.02, look=[(0.40, 0), (0.55, -18), (0.75, -14), (0.9, 0)], heave=0.02),
    "hunch": dict(base="Idle_B", k=dict(up=1.3, arm=1.3, head=1.2, hipl=1.3), breath=2.2, sway=0.016, look=[(0.2, 0), (0.3, 16), (0.45, 12), (0.55, -14), (0.7, -10), (0.8, 0)], nod=5),
    "proud": dict(base="Idle_B", k=dict(up=1.3, arm=1.2, head=1.0, hipl=1.4), breath=2.6, sway=0.01, look=[(0.35, 0), (0.5, 30), (0.7, 26), (0.85, 0)]),
    "float": dict(base="Idle_B", k=dict(up=1.4, arm=1.4, head=1.2, hipl=1.0), breath=2.0, sway=0.02, look=[(0.3, 0), (0.42, -22), (0.6, -18), (0.72, 0)], hover=0.05),
}
def gen_idle(rid):
    t = btype(rid)
    G = IDLE_T[t]
    base = G["base"]
    N = len(seq(base)[1])                      # play the hand animation at its own speed
    C = dict(k=G["k"], ref=mean_ref(base), post=dict(up=1.0, head=1.0, arm=0.85, hip=1.0, hipl=1.0), float=(t == "float"),
             lag={"head": 1.5, "wrist.l": 1.0, "wrist.r": 1.0})
    lk = [(1 + (N - 1) * u, v, "s") for u, v in [(0.0, 0)] + G["look"] + [(1.0, 0)]]
    def layer(P, f, u):
        ph = 2 * math.pi * u
        br = math.sin(ph * 2)                                  # two breaths per loop
        yaw = K8(lk, f)
        hz = G.get("hover", 0.0) * (1.0 + 0.7 * math.sin(ph * 2 - 0.6)) + G.get("bounce", 0.0) * abs(math.sin(ph * 4)) - G.get("heave", 0.0) * (0.5 + 0.5 * br)
        body8(P, hip=(G["sway"] * math.sin(ph), 0, hz), hr=(0, -2.5 * math.sin(ph), 0), sp=(-G["breath"] * 0.4 * br, 1.5 * math.sin(ph), 0),
              ch=(-G["breath"] * br, 0, yaw * 0.25), hd=(G.get("nod", 0) * math.sin(ph * 2 + 0.5) + G["breath"] * 0.6 * br, 0, yaw * 0.75))
        arms_out(P, 2.5 * br)
    build8("SQ_Idle_" + rid, N, True, base, loopmap(base, N), C, rid, layer)
    DONE8.append("SQ_Idle_" + rid)
FAMS["idle"] = gen_idle
