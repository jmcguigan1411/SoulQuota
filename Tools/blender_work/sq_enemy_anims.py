# Soul Quota - enemy personality actions (SQ_Enemy_*) on the shared KayKit "Rig".
# Reuses the pose builder of sq_anims2.py (everything above its styles()), reading a SNAPSHOT of
# Reaper_Custom.blend (never writes the reaper file). Saves the result to sq_enemy_anims.blend;
# sq_export_bodies.py copies every SQ_Enemy_* action from there into Skeleton_<Body>.fbx.
import bpy, math, sys
from mathutils import Vector, Matrix

SNAPSHOT = "/tmp/reaper_snapshot.blend"
OUT_BLEND = "/home/claude/blender_work/sq_enemy_anims.blend"

src = open("/home/claude/blender_work/sq_anims2.py").read()
helpers = src[:src.index("def styles():")]
helpers = helpers.replace('BLEND = "/home/claude/sq/ArtSource/Characters/Reaper_Custom.blend"', 'BLEND = "%s"' % SNAPSHOT)
sys.argv = [a for a in sys.argv if a != "--only"]
exec(compile(helpers, "sq_anims2_helpers", "exec"))

# drop the reaper's own SQ_ actions from this working file so only enemy takes are new here
NEW = []
T = track

def scale_subtree(P, bone, k):
    piv = P[bone].translation.copy()
    M = Matrix.Translation(piv) @ Matrix.Diagonal((k, k, k, 1.0)) @ Matrix.Translation(-piv)
    for b in [bone] + CHILDREN[bone]:
        P[b] = M @ P[b]

def make_enemy(name, base_name, N, fn, loop, post_pose=None, cfg=None):
    # REMASTER 2026-11-07: shared pipeline (spline keys, overlap lags, head/forearm/wrist springs, impact pops)
    make_core(name, base_name, N, fn, loop, cfg, post_pose)
    NEW.append(name)

# ------------------------------------------------------------ locomotion loops
def shamble():
    # REMASTER zombie shamble: right foot drags on its toe, lurch + dip onto it, head lolls and rolls, both arms
    # reach forward at different heights with limp, clawing wrists that lag; ribs/hips sway heavily
    def post(s, t):
        lur = max(0.0, math.sin(math.pi * (t - 0.5) / 0.45)) if 0.5 <= t <= 0.95 else 0.0
        s["hd"][1] += -18 + 8 * lur + 5 * S2(t + 0.2)
        s["hd"][0] += 8 * lur + 4 * S2(2 * t + 0.3)
        s["hd"][2] += 10 * S2(t + 0.35)
        s["hip"][2] -= 0.04 * lur
        s["hip"][0] -= 0.03 * lur
        s["hr"][1] -= 8 * lur
        s["sp"][1] = -7 * lur
        s["ch"][2] += 8 * S2(t + 0.1)
        s["arms"]["l"] = arm(sw=88 + 8 * S2(t + 0.05), ab=10 + 5 * S2(t * 2), el=10 + 10 * S2(t - 0.2), fin=8, tw=-75)
        s["arms"]["r"] = arm(sw=66 + 10 * S2(t + 0.3) + 8 * lur, ab=18 + 6 * S2(t * 2 + 0.3), el=22 + 12 * S2(t + 0.1), fin=12, tw=-65)
    G = dict(D=0.45, duty=0.66, h=0.06, H=-0.08, bob=0.022, bobph=0.1, sway=0.055, hroll=6, hyaw=10, sp=18, chp=8,
             hdp=-4, nod=7, heel=8, toe=20, w=0.18, sproll=2, chroll=0, hdroll=0, asw=4, asw0=78, el=12, eld=0, ab=14,
             r=dict(duty=0.5, h=0.01, heel=-6, toe=55, basep=10, swingp=48, yc=0.09, fy=22, lift_peak=0.75))
    def fn(t):
        s = walker(t, G)
        post(s, t)
        return s
    make_enemy("SQ_Enemy_Shamble", "Walking_A", 41, fn, True, cfg=dict(head=0.9, wrist=1.0, zeta=0.32, lag={"arm": 3.0, "el": 4.5, "hd": 3.0}))

def lurch():
    # NEW zombie lurch: a big lunging step onto the left leg (torso pitches forward, head snaps down then lolls up),
    # then the right leg is dragged after on its toe; arms reach forward and swing loosely with the lunge
    def post(s, t):
        lun = math.sin(math.pi * t / 0.4) if t < 0.4 else 0.0
        dr = max(0.0, math.sin(math.pi * (t - 0.45) / 0.5)) if t >= 0.45 else 0.0
        s["hip"][2] += -0.05 * lun + 0.012 * dr
        s["hip"][1] -= 0.03 * lun
        s["sp"][0] += 16 * lun - 4 * dr
        s["ch"][0] += 8 * lun
        s["hd"][0] += -10 * lun + 10 * dr
        s["hd"][1] += -14 + 6 * dr
        s["hr"][1] += -6 * dr
        for sd, o in (("l", 0.0), ("r", 0.1)):
            s["arms"][sd] = arm(sw=72 + 22 * math.sin(math.pi * min(1.0, (t + o) / 0.55)) ** 2, ab=14 + 4 * S2(t + o),
                                el=14 + 18 * S2(t - 0.15 + o), fin=10, tw=-70)
    G = dict(D=0.59, duty=0.62, h=0.10, H=-0.07, bob=0.0, sway=0.05, hroll=5, hyaw=12, sp=14, chp=6, hdp=-4, nod=0,
             heel=14, toe=26, w=0.18, sproll=2, chroll=0, hdroll=0, asw=0, asw0=78, el=12, eld=0, ab=14,
             l=dict(lift_peak=0.4), r=dict(duty=0.55, h=0.012, heel=-6, toe=55, basep=10, swingp=50, yc=0.08, fy=22, lift_peak=0.8))
    def fn(t):
        s = walker(t, G)
        post(s, t)
        return s
    make_enemy("SQ_Enemy_Lurch", "Walking_A", 33, fn, True, cfg=dict(head=0.95, wrist=1.0, zeta=0.32, lag={"arm": 3.0, "el": 4.5, "hd": 3.0}))

def scuttle():
    # low crab-like scuttle: deep crouch, knees wide, short fast steps, clawing hands out front
    G = dict(D=0.55, duty=0.5, h=0.07, H=-0.17, bob=-0.015, bobph=0.2, sway=0.03, hroll=4, hyaw=10, sp=32, chp=14,
             hdp=-30, nod=4, heel=0, toe=25, basep=10, swingp=30, lift_peak=0.4, kout=0.45, w=0.25, fy=22,
             asw=22, asw0=38, el=70, eld=10, ab=34, fin=20, tw=-50, chyaw=10, hdroll=4)
    make_enemy("SQ_Enemy_Scuttle", "Running_A", 17, lambda t: walker(t, G), True)

def hop():
    # two-footed bunny hop in place: crouch on landing, spring, feet tucked in the air, arms flung up
    def fn(t):
        s = spec()
        u = t % 1.0
        crouch = math.sin(math.pi * u / 0.25) if u < 0.25 else 0.0
        air = math.sin(math.pi * (u - 0.25) / 0.75) if u >= 0.25 else 0.0
        s["hip"] = [0.0, 0.0, -0.03 + 0.26 * air - 0.10 * crouch]
        s["hr"] = [6 * crouch - 4 * air, 0, 0]
        s["sp"] = [10 * crouch - 6 * air, 0, 0]
        s["ch"] = [4 * crouch - 6 * air, 0, 0]
        s["hd"] = [-10 * crouch + 10 * air, 0, 0]
        for sd in "lr":
            s["legs"][sd] = leg(dz=0.22 * air, fp=25 * air, fy=10, kout=0.2)
            s["arms"][sd] = arm(sw=20 + 130 * air, ab=30 + 25 * air, el=40 - 25 * air, tw=-30)
        s["w"] = 0.14
        return s
    make_enemy("SQ_Enemy_Hop", "Idle", 25, fn, True)

def crawl():
    # crawling torso: lies prone, pulls itself along with alternating arm reaches; legs collapsed to nothing
    def fn(t):
        s = spec()
        s.pop("legs")
        pull_l = S2(t)
        pull_r = S2(t + 0.5)
        s["hip"] = [0.02 * S2(t), 0.10, -0.30 + 0.01 * abs(S2(2 * t))]
        s["hr"] = [80, 8 * S2(t), 4 * S2(t)]
        s["sp"] = [6, -4 * S2(t), -6 * S2(t)]
        s["ch"] = [-4, -3 * S2(t), -8 * S2(t)]
        s["hd"] = [-58 + 8 * abs(S2(2 * t)), 10 * S2(t + 0.15), 14 * S2(t + 0.1)]   # REMASTER: lolling, searching head
        s["fk"] = {sd: dict(th=0, kn=0, ft=0, to=0, ab=0) for sd in "lr"}
        # REMASTER: long clawing reaches - the reaching hand goes high and far, then digs in and drags the body
        s["arms"]["l"] = arm(sw=138 + 34 * pull_l, ab=28 - 10 * pull_l, el=40 - 36 * pull_l, fin=12, tw=-65)
        s["arms"]["r"] = arm(sw=138 + 34 * pull_r, ab=28 - 10 * pull_r, el=40 - 36 * pull_r, fin=12, tw=-65)
        return s
    def no_legs(P):
        for sd in "lr":
            scale_subtree(P, "upperleg." + sd, 0.01)
    make_enemy("SQ_Enemy_Crawl", "Idle", 41, fn, True, post_pose=no_legs, cfg=dict(head=0.6, wrist=1.0, lag={"hd": 3.0, "arm": 1.0, "el": 2.5}))

def rider():
    # sits on someone's shoulders: legs straddled forward, waving both arms, wobbling to keep balance
    def fn(t):
        s = spec()
        s.pop("legs")
        s["hip"] = [0.03 * S2(t), 0.0, 0.0]
        s["hr"] = [-4, 6 * S2(t), 4 * S2(t + 0.25)]
        s["sp"] = [-6, -5 * S2(t), 0]
        s["ch"] = [-6, -3 * S2(t), 6 * S2(t)]
        s["hd"] = [-12, 8 * S2(t + 0.1), 14 * S2(t * 2)]
        s["fk"] = {sd: dict(th=-80, kn=75, ft=10, to=0, ab=32, fy=10) for sd in "lr"}
        s["arms"]["l"] = arm(sw=150 + 25 * S2(2 * t), ab=40 + 15 * S2(2 * t), el=30 + 25 * S2(2 * t + 0.2), tw=30)
        s["arms"]["r"] = arm(sw=150 + 25 * S2(2 * t + 0.5), ab=40 + 15 * S2(2 * t + 0.5), el=30 + 25 * S2(2 * t + 0.7), tw=30)
        return s
    make_enemy("SQ_Enemy_Rider", "Idle", 49, fn, True)

def dizzy():
    # stunned: hips circle, head lolls round, arms dangle, feet planted wide
    def fn(t):
        s = spec()
        c, sn = C2(t), S2(t)
        s["hip"] = [0.04 * c, 0.04 * sn, -0.06]
        s["hr"] = [6 * sn, 8 * c, 0]
        s["sp"] = [10 + 6 * sn, 6 * c, 0]
        s["ch"] = [6, 5 * c, 8 * sn]
        s["hd"] = [-10 + 18 * sn, 20 * c, 10 * sn]
        for sd, o in (("l", 0.0), ("r", 0.3)):
            s["arms"][sd] = arm(sw=10 + 14 * S2(t + o), ab=20 + 10 * C2(t + o), el=15 + 10 * S2(t - 0.2 + o))
        s["legs"] = {sd: leg(fy=18, kout=0.3) for sd in "lr"}
        s["w"] = 0.22
        return s
    make_enemy("SQ_Enemy_Dizzy", "Idle", 49, fn, True)

# ------------------------------------------------------------ one-shots
def swipe():
    # telegraphed double-arm overhead smash: big wind-up (lean back, arms high) to frame 13, slam at 14-16
    N = 30
    def fn(f):
        s = spec()
        s["hip"] = [0.0, T([(1, 0, "s"), (12, 0.05, "s"), (15, -0.08, "o"), (22, -0.06, "s"), (30, 0, "s")], f),
                    T([(1, -0.02, "s"), (12, -0.01, "s"), (15, -0.13, "o"), (22, -0.11, "s"), (30, -0.02, "s")], f)]
        s["sp"][0] = T([(1, 3, "s"), (12, -16, "s"), (15, 28, "o"), (22, 22, "s"), (30, 3, "s")], f)
        s["ch"][0] = T([(1, 2, "s"), (12, -12, "s"), (15, 18, "o"), (22, 14, "s"), (30, 2, "s")], f)
        s["hd"][0] = T([(1, 0, "s"), (12, -20, "s"), (15, 10, "o"), (30, 0, "s")], f)
        tremble = math.sin(f * 3.1) * T([(1, 0, "s"), (8, 0, "s"), (11, 1, "s"), (13, 1, "s"), (14, 0, "s")], f)
        for sd in "lr":
            s["arms"][sd] = arm(sw=T([(1, 5, "s"), (12, 168, "s"), (14, 55, "c"), (16, 44, "k"), (22, 60, "s"), (30, 5, "s")], f) + 4 * tremble,
                                ab=T([(1, 10, "s"), (12, 20, "s"), (15, 8, "s"), (30, 10, "s")], f),
                                el=T([(1, 15, "s"), (12, 70, "s"), (14, 5, "c"), (22, 12, "s"), (30, 15, "s")], f),
                                tw=T([(1, 0, "s"), (12, 40, "s"), (30, 0, "s")], f))
        s["legs"]["l"] = leg(dy=T([(1, 0, "s"), (12, 0, "s"), (15, -0.12, "o"), (24, -0.12, "s"), (30, 0, "s")], f),
                             dz=T([(1, 0, "s"), (12, 0.0, "s"), (13, 0.05, "s"), (15, 0, "s"), (30, 0, "s")], f), fy=8)
        s["legs"]["r"] = leg(fy=8)
        return s
    make_enemy("SQ_Enemy_Swipe", "Idle", N, fn, False, cfg=dict(pop=[(14, "lr", 0.2)]))

def fuse():
    # the fuse is lit: panicked flailing, little hops, head shaking (bomber wind-up)
    N = 24
    def fn(f):
        s = spec()
        hopk = abs(math.sin(f * math.pi / 6.0))
        s["hip"][2] = -0.03 + 0.07 * hopk
        s["sp"][0] = 6
        s["hd"] = [-6, 0, 30 * math.sin(f * 1.3)]
        s["arms"]["l"] = arm(sw=150 + 25 * math.sin(f * 1.1), ab=35 + 20 * math.sin(f * 0.9), el=40 + 30 * math.sin(f * 1.7), tw=30)
        s["arms"]["r"] = arm(sw=150 + 25 * math.sin(f * 1.1 + 2.0), ab=35 + 20 * math.sin(f * 0.9 + 1.0), el=40 + 30 * math.sin(f * 1.7 + 1.0), tw=30)
        for sd, o in (("l", 0.0), ("r", math.pi)):
            s["legs"][sd] = leg(dz=0.06 * max(0.0, math.sin(f * math.pi / 6.0 + o)), fy=12)
        return s
    make_enemy("SQ_Enemy_Fuse", "Idle", N, fn, False, cfg=dict(head=0.9, zeta=0.3))

def roar():
    # phase-change roar: crouch and gather, then rise chest out, arms wide and up, head back, trembling
    N = 45
    def fn(f):
        s = spec()
        tr = math.sin(f * 2.6) * T([(1, 0, "s"), (14, 0, "s"), (16, 1, "s"), (34, 1, "s"), (38, 0, "s")], f)
        s["hip"][2] = T([(1, -0.02, "s"), (10, -0.12, "s"), (16, 0.0, "o"), (34, -0.01, "s"), (45, -0.02, "s")], f)
        s["sp"][0] = T([(1, 3, "s"), (10, 26, "s"), (16, -14, "o"), (34, -12, "s"), (45, 3, "s")], f) + tr * 2
        s["ch"][0] = T([(1, 2, "s"), (10, 16, "s"), (16, -18, "o"), (34, -14, "s"), (45, 2, "s")], f)
        s["hd"] = [T([(1, 0, "s"), (10, 24, "s"), (16, -40, "o"), (34, -36, "s"), (45, 0, "s")], f), tr * 6, 0]
        for sd in "lr":
            s["arms"][sd] = arm(sw=T([(1, 5, "s"), (10, 30, "s"), (16, 120, "o"), (34, 115, "s"), (45, 5, "s")], f),
                                ab=T([(1, 10, "s"), (10, 20, "s"), (16, 70, "o"), (34, 66, "s"), (45, 10, "s")], f) + tr * 3,
                                el=T([(1, 15, "s"), (10, 100, "s"), (16, 30, "o"), (34, 34, "s"), (45, 15, "s")], f),
                                tw=T([(1, 0, "s"), (16, -50, "s"), (34, -50, "s"), (45, 0, "s")], f))
        s["legs"] = {sd: leg(fy=14, kout=0.25) for sd in "lr"}
        s["w"] = 0.22
        return s
    make_enemy("SQ_Enemy_Roar", "Idle", N, fn, False, cfg=dict(pop=[(16, "lr", 0.15)], exag=(1.1, 1.1, 1.0)))

shamble(); lurch(); scuttle(); hop(); crawl(); rider(); dizzy(); swipe(); fuse(); roar()

for k, v in STATS.items():
    print("HIPDROP", k, "frames", v[0], "max", round(v[1], 3))
# keep only the new enemy actions + the KayKit bases in this file (smaller, and the export copies SQ_Enemy_* only)
set_action(ACT["Idle"])
sc.frame_set(1)
bpy.ops.wm.save_as_mainfile(filepath=OUT_BLEND)
print("ENEMY ACTIONS", NEW)
