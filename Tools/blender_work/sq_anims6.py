# Soul Quota - Remaster 6 (2026-11-15): CHASE-CAMERA readability. The game is now seen from behind and above (third-person
# chase camera), and the Remaster 4 takes were posed for a front/top view: arms worked in FRONT of the big head and torso, so
# from behind nothing broke the silhouette. This pass re-bakes, with the same names, lengths and impact frames:
#   * SQ_Sig_<Id> (20): whole-body wind-up TWIST away, snap through on the impact frame, overshoot, settle; a crouch into
#     the anticipation and a pop up on contact; arms swung WIDE of the body so the swing is seen from behind.
#   * SQ_Run_<Id> / SQ_Walk_<Id>: bigger arm swing and elbows out, more hip roll / counter-rotation, deeper bob and lean.
#   * new shared takes: SQ_Jump_Air (pose strip: f1 launch stretch .. f11 apex tuck .. f21 falling reach, sampled by vertical
#     speed in USQAnimInstance), SQ_Jump_Land (squash and recover), SQ_Dash (low forward dive pose), SQ_Slide (legs-first).
# Run AFTER sq_anims5.py, then sq_export.py.   python3 sq_anims6.py [--only A,B]
import bpy, math, sys, copy
src4 = open("/home/claude/blender_work/sq_anims4.py").read()
exec(compile(src4[:src4.index("gen_locomotion()\ngen_sigs()")], "sq_anims4_defs", "exec"))
DONE6 = []

def take6(name, N, loop, fn, cfg=None, base="Idle"):
    if ONLY and name not in ONLY:
        return
    act = make_core(name, base, N, fn, loop, cfg)
    act["sq_remaster"] = 6
    DONE6.append(name)

# ---------------------------------------------------------------- signature attacks, re-staged for the chase camera
def chase_sig(fn, N, I, side):
    A = max(2.0, I - max(2.0, 0.55 * (I - 1)))          # anticipation extreme
    F = min(N - 2.0, I + 3.0)                             # overshoot
    R = min(N - 1.0, I + 0.55 * (N - I))                  # settled
    tw = [(1, 0, "s"), (A, -1.0, "s"), (I - 1, -0.55, "i"), (I, 1.0, "l"), (F, 1.25, "o"), (R, 0.35, "s"), (N, 0, "s")]
    dip = [(1, 0, "s"), (A, 1.0, "s"), (I, -0.6, "l"), (F, -0.2, "o"), (R, 0.15, "s"), (N, 0, "s")]
    wide = [(1, 0, "s"), (A, 0.7, "s"), (I, 1.0, "l"), (F, 1.0, "s"), (R, 0.3, "s"), (N, 0, "s")]
    def wrapped(f):
        s = fn(f)
        t = track(tw, f); d = track(dip, f); w = track(wide, f)
        s["hr"] = [s["hr"][0] + 5 * d, s["hr"][1] + side * 3 * t, s["hr"][2] + side * 22 * t]
        s["sp"] = [s["sp"][0] + 6 * d, s["sp"][1], s["sp"][2] + side * 10 * t]
        s["ch"] = [s["ch"][0], s["ch"][1] + side * 4 * t, s["ch"][2] + side * 12 * t]
        s["hip"] = [s["hip"][0] + side * 0.03 * t, s["hip"][1], s["hip"][2] - 0.085 * d]
        for sd in "lr":
            a = s["arms"][sd]
            if "ik" in a:
                continue
            a["ab"] = a.get("ab", 10) + 26 * w          # arms out past the body outline
            a["el"] = max(0.0, a.get("el", 15) - 10 * w) # and straighter, so the weapon reaches
        return s
    return wrapped

def gen_sigs6():
    k = 0
    for rid, (N, fn, cfg) in SIG.items():
        c = dict(cfg)
        I = c["pop"][0][0] if c.get("pop") else round(1 + 0.35 * (N - 1))
        side = 1 if k % 2 == 0 else -1
        k += 1
        c["exag"] = (1.25, 1.35, 1.15)
        c.setdefault("pop", [(I, "lr", 0.3)])
        take6("SQ_Sig_" + rid, N, False, chase_sig(fn, N, I, side), c)

# ---------------------------------------------------------------- locomotion, pushed out to the sides
def chase_gait(G, run):
    G = copy.deepcopy(G)
    for k, m in (("asw", 1.45), ("bob", 1.5), ("hroll", 1.6), ("hyaw", 1.4), ("chyaw", 1.5), ("sway", 1.4), ("sproll", 1.5)):
        if k in G:
            G[k] = G[k] * m
    G["ab"] = G.get("ab", 12) + (16 if run else 9)
    G["abd"] = G.get("abd", 0) + (8 if run else 4)
    G["sp"] = G.get("sp", 4) + (6 if run else 2)
    G["hroll"] = G.get("hroll", 4) if "hroll" in G else 6
    return G

def chase_float(G, run):
    G = dict(G)
    G["lean"] = G.get("lean", 12) + (10 if run else 4)
    G["roll"] = G.get("roll", 5) * 1.8
    G["yaw"] = G.get("yaw", 6) * 1.6
    G["ab"] = G.get("ab", 30) + 14
    G["asw"] = G.get("asw", 10) * 1.6
    G["bob"] = G.get("bob", 0.04) * 1.4
    return G

def gen_locomotion6():
    for rid, G in W.items():
        if "float" in G:
            take6("SQ_Walk_" + rid, 41, True, lambda t, G=G: floater(t, chase_float(G["float"], False)), base="Running_A")
            take6("SQ_Run_" + rid, 31, True, lambda t, G=G: floater(t, dict(chase_float(G["float_run"], True), ph=1)), base="Running_A")
            continue
        wn = 33 if G["walk"]["D"] > 0.6 else 37
        gw, gr = chase_gait(G["walk"], False), chase_gait(G["run"], True)
        def mk(g, post):
            def fn(t):
                s = walker(t, g)
                if post:
                    post(s, t)
                return s
            return fn
        take6("SQ_Walk_" + rid, wn, True, mk(gw, G.get("walk_post")), None, "Walking_A")
        take6("SQ_Run_" + rid, 21, True, mk(gr, G.get("run_post")), None, "Running_A")

# ---------------------------------------------------------------- shared movement takes (Space / Shift / Ctrl)
def V(keys, f):
    return [track([(k[0], k[1][i], k[2] if len(k) > 2 else "s") for k in keys], f) for i in range(3)]

def A6(keys, f):
    return {p: track([(k[0], k[1].get(p, arm()[p]), "s") for k in keys], f) for p in ("sw", "ab", "el", "fin", "tw")}

def L6(keys, f):
    d = leg()
    out = {}
    for p in ("dx", "dy", "dz", "fp", "fy"):
        out[p] = track([(k[0], k[1].get(p, d[p]), "s") for k in keys], f)
    return leg(**out)

def jump_air(f):
    # f1 launch: stretched tall, arms thrown up and out, legs trailing | f11 apex: tucked, knees up, arms wide for balance
    # f21 falling: arms high and back, legs reaching down for the ground
    s = spec()
    s["hip"] = V([(1, (0, 0, 0.10)), (11, (0, 0, 0.16)), (21, (0, 0.02, 0.10))], f)
    s["hr"] = V([(1, (-8, 0, 0)), (11, (10, 0, 0)), (21, (-4, 0, 0))], f)
    s["sp"] = V([(1, (-10, 0, 0)), (11, (14, 0, 0)), (21, (-6, 0, 0))], f)
    s["ch"] = V([(1, (-8, 0, 0)), (11, (8, 0, 0)), (21, (-8, 0, 0))], f)
    s["hd"] = V([(1, (-14, 0, 0)), (11, (4, 0, 0)), (21, (10, 0, 0))], f)
    for sd in "lr":
        s["arms"][sd] = A6([(1, dict(sw=150, ab=38, el=12)), (11, dict(sw=30, ab=78, el=30)), (21, dict(sw=120, ab=62, el=24))], f)
    s["legs"]["l"] = L6([(1, dict(dy=0.10, dz=0.02, fp=40)), (11, dict(dy=-0.10, dz=0.30, fp=-10)), (21, dict(dy=-0.04, dz=0.04, fp=24))], f)
    s["legs"]["r"] = L6([(1, dict(dy=0.16, dz=0.06, fp=50)), (11, dict(dy=0.04, dz=0.24, fp=10)), (21, dict(dy=0.06, dz=0.08, fp=30))], f)
    return s

def jump_land(f):
    s = spec()
    s["hip"] = V([(1, (0, 0, -0.02)), (3, (0, -0.02, -0.24), "i"), (7, (0, 0, 0.02), "o"), (12, (0, 0, -0.02))], f)
    s["sp"] = V([(1, (4, 0, 0)), (3, (30, 0, 0), "i"), (7, (-4, 0, 0), "o"), (12, (3, 0, 0))], f)
    s["ch"] = V([(1, (2, 0, 0)), (3, (14, 0, 0), "i"), (7, (-4, 0, 0), "o"), (12, (2, 0, 0))], f)
    s["hd"] = V([(1, (6, 0, 0)), (3, (-16, 0, 0), "i"), (7, (6, 0, 0), "o"), (12, (0, 0, 0))], f)
    for sd in "lr":
        s["arms"][sd] = A6([(1, dict(sw=110, ab=60, el=20)), (3, dict(sw=-30, ab=64, el=20)), (7, dict(sw=20, ab=30, el=30)), (12, dict())], f)
    s["w"] = track([(1, 0.17, "s"), (3, 0.26, "i"), (12, 0.17, "s")], f)
    return s

def dash(f):
    # low forward dive: body thrown flat, lead arm spearing ahead, trailing arm and leg streaming out behind; holds f4..f8
    s = spec()
    s["hip"] = V([(1, (0, 0, -0.02)), (3, (0, -0.12, -0.20), "i"), (8, (0, -0.14, -0.22)), (13, (0, 0, -0.02))], f)
    s["hr"] = V([(1, (0, 0, 0)), (3, (34, 0, -14), "i"), (8, (36, 0, -16)), (13, (0, 0, 0))], f)
    s["sp"] = V([(1, (3, 0, 0)), (3, (20, 0, -8), "i"), (8, (22, 0, -8)), (13, (3, 0, 0))], f)
    s["ch"] = V([(1, (2, 0, 0)), (3, (8, 0, -10), "i"), (8, (8, 0, -10)), (13, (2, 0, 0))], f)
    s["hd"] = V([(1, (0, 0, 0)), (3, (-34, 0, 14), "i"), (8, (-34, 0, 14)), (13, (0, 0, 0))], f)
    s["arms"]["r"] = A6([(1, dict()), (3, dict(sw=125, ab=14, el=4)), (8, dict(sw=128, ab=14, el=2)), (13, dict())], f)
    s["arms"]["l"] = A6([(1, dict()), (3, dict(sw=-70, ab=46, el=6)), (8, dict(sw=-78, ab=50, el=4)), (13, dict())], f)
    s["legs"]["l"] = L6([(1, dict()), (3, dict(dy=-0.26, dz=0.02, fp=10)), (8, dict(dy=-0.28, dz=0.02, fp=10)), (13, dict())], f)
    s["legs"]["r"] = L6([(1, dict()), (3, dict(dy=0.34, dz=0.16, fp=60)), (8, dict(dy=0.38, dz=0.20, fp=66)), (13, dict())], f)
    return s

def slide(t):
    # legs-first: hips low and back, lead leg straight ahead, body leaning back on one hand, free arm up for balance
    s = spec()
    b = 0.012 * S2(t * 2)
    s["hip"] = [0.0, 0.10, -0.40 + b]
    s["hr"] = [-30 + 2 * S2(t), 6, 14]
    s["sp"] = [-6, 0, 6]
    s["ch"] = [6, 0, 8]
    s["hd"] = [22, 0, -12]
    s["arms"]["l"] = arm(sw=-50, ab=34, el=10)
    s["arms"]["r"] = arm(sw=60 + 6 * S2(t), ab=70, el=30)
    s["legs"]["l"] = leg(dy=-0.34, dz=0.03, fp=-20)
    s["legs"]["r"] = leg(dy=-0.10, dz=0.10, fp=20, dx=-0.04)
    return s

gen_sigs6()
gen_locomotion6()
take6("SQ_Jump_Air", 21, False, jump_air, dict(exag=(1.0, 1.0, 1.0), lag={"hr": 0, "sp": 0, "ch": 0, "hd": 0}, head=0, nod=0, fore=0, wrist=0))
take6("SQ_Jump_Land", 12, False, jump_land)
take6("SQ_Dash", 13, False, dash, dict(exag=(1.0, 1.0, 1.0)))
take6("SQ_Slide", 21, True, slide, dict(head=0, nod=0))
if not ONLY or "SQ_Marker_Remaster6" in ONLY:
    idle = ACT["Idle"]
    act = write_action("SQ_Marker_Remaster6", idle, [eval_pose(idle, 1), eval_pose(idle, 2)])
    act["sq_loop"] = False
    DONE6.append("SQ_Marker_Remaster6")
set_action(ACT["Idle"])
sc.frame_set(1)
bpy.ops.wm.save_as_mainfile(filepath=BLEND)
print("DONE6", len(DONE6), DONE6)
print("NACTIONS", len(bpy.data.actions))
