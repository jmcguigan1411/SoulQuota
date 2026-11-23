# Soul Quota - Remaster 4 (2026-11-09): monster roster signature animations. Every monster gets its own idle, walk, run and
# signature attack (SQ_Idle_<Id>, SQ_Walk_<Id>, SQ_Run_<Id>, SQ_Sig_<Id>) on the shared Reaper rig.
# Built with the sq_anims2/3 pose solver (IK feet, springs, lag, impact pops). Run AFTER sq_anims3.py, then sq_export.py.
#   python3 sq_anims4.py [--only A,B]
# Attack takes keep the five beats and land on their AttackStyle's ImpactFrac (SQCharacter.cpp SQAttackStyleRows):
#   impact frame = 1 + ImpactFrac * (N - 1).  Shoot .20/N21->f5  Cast .30/N24->f8  Throw .30/N24->f8  Slash .35/N24->f9
#   Chop .42/N30->f13  Spin .40/N31->f13  Summon .40/N31->f13  Raise .45/N30->f14  Slam .50/N31->f16
import bpy, math, sys
src3 = open("/home/claude/blender_work/sq_anims3.py").read()
exec(compile(src3[:src3.index("# ============================================================ 1. RISE")], "sq_anims3_helpers", "exec"))
DONE4 = []

def take(name, N, loop, fn, cfg=None, base="Idle"):
    if ONLY and name not in ONLY:
        return
    act = make_core(name, base, N, fn, loop, cfg)
    act["sq_remaster"] = 4
    DONE4.append(name)

def walk4(name, N, G, post=None, base="Walking_A", cfg=None):
    def fn(t):
        s = walker(t, G)
        if post:
            post(s, t)
        return s
    take(name, N, True, fn, cfg, base)

def floater(t, G):
    """legless glide (gown hides the legs): hover, bob, lean into travel, trailing arms"""
    s = spec()
    s.pop("legs")
    ph = G.get("ph", 1)
    bob = G.get("bob", 0.04) * S2(t * ph)
    s["hip"] = [G.get("sway", 0.03) * S2(t + 0.25), 0.0, G.get("lift", 0.16) + bob]
    s["hr"] = [G.get("lean", 12) + 3 * S2(t * ph - 0.15), G.get("roll", 5) * S2(t + 0.25), G.get("yaw", 6) * S2(t)]
    s["sp"] = [G.get("sp", 6), -4 * S2(t + 0.25), -4 * S2(t)]
    s["ch"] = [G.get("chp", 4) + 3 * S2(t * ph - 0.3), -3 * S2(t + 0.25), -5 * S2(t)]
    s["hd"] = [G.get("hdp", -12) - 4 * S2(t * ph - 0.4), 4 * S2(t), 4 * S2(t)]
    s["fk"] = {sd: dict(th=G.get("th", 18) + 8 * S2(t * ph - 0.25 + 0.1 * (sd == "r")), kn=G.get("kn", 24) + 10 * S2(t * ph - 0.4 + 0.1 * (sd == "r")),
                        ft=30 + 8 * S2(t - 0.55), to=10, ab=4) for sd in "lr"}
    for sd, o in (("l", 0.0), ("r", G.get("aoff", 0.08))):
        s["arms"][sd] = arm(sw=G.get("asw0", -35) + G.get("asw", 10) * S2(t * ph - 0.3 + o), ab=G.get("ab", 30) + 8 * S2(t * ph - 0.45 + o),
                            el=G.get("el", 25) + 15 * S2(t * ph - 0.6 + o), tw=G.get("tw", 30), fin=G.get("fin", 0))
    return s

def idle4(name, G, post=None, N=61):
    """planted idle with a personality: breath, weight shift, glance, hunch/proud posture, arm pose, optional fidget"""
    def fn(t):
        s = spec()
        br = S2(t * G.get("breaths", 2))
        sh = G.get("shift", 0.02) * S2(t - 0.25)
        s["hip"] = [sh, 0.0, G.get("H", -0.04) + G.get("breath_z", 0.006) * br]
        s["hr"] = [G.get("hp", 0), -G.get("hroll", 4) * S2(t - 0.25), 0]
        s["sp"] = [G.get("sp", 4) - 1.5 * br, 2 * S2(t), 0]
        s["ch"] = [G.get("chp", 2) - G.get("breath_ch", 2.5) * br, 0, 2 * S2(t + 0.2)]
        look = track([(0, 0, "s"), (0.35, 0, "s"), (0.45, G.get("look", 28), "x"), (0.65, G.get("look", 28) * 0.85, "s"), (0.75, 0, "s"), (1.0, 0, "s")], t)
        s["hd"] = [G.get("hdp", -3) + 2 * br, G.get("hdroll", -4), look]
        for sd, o in (("l", 0.0), ("r", 0.15)):
            a = dict(G.get("arm", {}))
            a.update(G.get("arm_" + sd, {}))
            s["arms"][sd] = arm(sw=a.get("sw", 6) + 3 * S2(t - 0.2 + o), ab=a.get("ab", 12) + 3 * S2(2 * t + o), el=a.get("el", 18) + 6 * S2(t - 0.3 + o),
                                fin=a.get("fin", 6), tw=a.get("tw", 0))
        s["legs"]["l"] = leg(fy=G.get("fy", 8), dx=G.get("stance", 0.0))
        s["legs"]["r"] = leg(fy=G.get("fy", 8) + 6, dx=-0.02 - G.get("stance", 0.0), dy=-0.04, fp=6 + 2 * S2(t))
        s["w"] = G.get("w", 0.17)
        if post:
            post(s, t)
        return s
    take(name, N, True, fn, dict(G.get("cfg", {})))

def float_idle(name, G, N=61):
    def fn(t):
        s = floater(t, dict(G, ph=2))
        return s
    take(name, N, True, fn, base="Idle")

# ====================================================================== LOCOMOTION + IDLES (per monster)
# Gait language per monster (approved previews, Monster Roster boards 1-7)
W = {}
# Headless Horseman: long proud strides, chest out, cape-swinging arm; run = charging gallop lean
W["Neckless"] = dict(walk=dict(D=0.86, duty=0.58, h=0.20, H=-0.04, bob=0.034, bobph=0.3, sway=0.02, hroll=4, hyaw=8, sp=-6, chp=-6, hdp=0, nod=0,
                               heel=10, toe=24, swingp=-4, lift_peak=0.4, kout=0.08, asw=40, asw0=6, el=12, eld=6, ab=10, chyaw=6, sproll=0, hdroll=0,
                               l=dict(), ),
                     run=dict(D=1.40, duty=0.34, h=0.22, H=-0.07, bob=-0.05, bobph=0.2, sway=0.01, hroll=4, hyaw=10, sp=24, chp=8, hdp=0, nod=0, heel=8, toe=40,
                              swingp=60, lift_peak=0.4, kout=0.06, asw=70, asw0=18, el=90, eld=0, ab=22, chyaw=10, hdroll=0),
                     idle=dict(H=-0.03, sp=-6, chp=-8, hdp=0, look=0, hroll=3, arm_l=dict(sw=30, ab=22, el=80, fin=30), arm_r=dict(sw=10, ab=14, el=24), stance=0.03))
# Pumpkin Man: heavy-headed waddle, head bob lags behind; scurrying run
W["Tallow"] = dict(walk=dict(D=0.44, duty=0.6, h=0.07, H=-0.06, bob=0.03, bobph=0.25, sway=0.07, hroll=-6, broll=10, hyaw=6, sp=6, chp=2, hdp=-6, nod=10,
                             nodph=0.2, heel=6, toe=16, fy=20, w=0.22, kout=0.3, sproll=7, chroll=4, hdroll=-12, asw=22, ab=40, abd=10, el=30, eld=10, tw=-20, chyaw=6),
                   run=dict(D=0.95, duty=0.42, h=0.12, H=-0.10, bob=-0.03, bobph=0.2, sway=0.04, hroll=4, hyaw=8, sp=22, chp=8, hdp=-14, nod=8, heel=0, toe=35,
                            basep=10, swingp=40, lift_peak=0.4, kout=0.2, asw=48, asw0=20, el=40, eld=10, ab=40, chyaw=6, hdroll=8),
                   idle=dict(H=-0.05, sp=6, hdp=-6, hdroll=-10, look=35, arm=dict(sw=8, ab=28, el=30), breath_z=0.012,
                             cfg=dict(lag={"hd": 2.5})))
# Zombie Warrior: heavy stomp, left-leg limp, shield forward, sword dragging
def zombie_post(s, t):
    lur = max(0.0, math.sin(math.pi * (t - 0.0) / 0.45)) if t <= 0.45 else 0.0
    s["hip"][2] -= 0.04 * lur
    s["hr"][1] += 8 * lur
    s["hd"][1] += 8 * lur
    s["arms"]["l"] = arm(sw=55 + 4 * S2(t), ab=20, el=70, fin=30)     # shield up
    s["arms"]["r"] = arm(sw=-10 + 8 * C2(t + 0.25), ab=14, el=10, tw=20)  # sword drags
W["Bonesmith"] = dict(walk=dict(D=0.62, duty=0.64, h=0.10, H=-0.07, bob=0.035, bobph=0.25, sway=0.05, hroll=5, hyaw=10, sp=12, chp=4, hdp=-6, nod=6,
                                heel=14, toe=22, w=0.21, kout=0.25, sproll=3, chroll=0, hdroll=3, asw=10, asw0=0, el=20, ab=16,
                                l=dict(duty=0.5, h=0.03, heel=-4, toe=40, basep=6, swingp=30, lift_peak=0.7)), walk_post=zombie_post,
                      run=dict(D=1.05, duty=0.46, h=0.15, H=-0.09, bob=-0.035, bobph=0.23, sway=0.06, hroll=8, broll=6, hyaw=12, sp=20, chp=8, hdp=-6, nod=3,
                               heel=10, toe=25, swingp=20, lift_peak=0.45, w=0.22, fy=14, kout=0.3, asw=30, asw0=20, ab=30, el=60, eld=20, chyaw=16),
                      idle=dict(H=-0.06, sp=10, chp=6, hdp=-8, hdroll=-8, look=20, arm_l=dict(sw=50, ab=20, el=72, fin=30), arm_r=dict(sw=-6, ab=16, el=12), stance=0.04, w=0.21))
# Undertaker: slow dignified glide, tiny steps, upright, hands clasped behind
W["Pallbearers"] = dict(walk=dict(D=0.62, duty=0.66, h=0.05, H=-0.03, bob=0.008, bobph=0.25, sway=0.008, hroll=1, hyaw=3, sp=-2, chp=-4, hdp=4, nod=1,
                                  heel=6, toe=14, swingp=0, kout=0.05, asw=4, asw0=-30, el=60, eld=0, ab=22, fin=40, chyaw=2, sproll=0, hdroll=0),
                        run=dict(D=1.10, duty=0.44, h=0.10, H=-0.05, bob=-0.02, bobph=0.2, sway=0.01, hroll=2, hyaw=4, sp=10, chp=0, hdp=-2, nod=2, heel=8, toe=28,
                                 swingp=30, kout=0.06, asw=50, asw0=10, el=70, eld=0, ab=14, chyaw=4),
                        idle=dict(H=-0.03, sp=-3, chp=-5, hdp=6, look=15, hroll=1, arm=dict(sw=-28, ab=24, el=60, fin=45)))
# Ghost: floating, veil streaming, arms trailing like sheets
W["Widow"] = dict(float=dict(lift=0.22, bob=0.05, lean=16, roll=7, yaw=8, asw0=-40, asw=14, ab=42, el=30, tw=40), float_run=dict(lift=0.24, bob=0.035, lean=28, asw0=-60, asw=8, ab=30),
                  float_idle=dict(lift=0.2, bob=0.06, lean=2, roll=6, asw0=10, asw=10, ab=40, el=40))
# Witch: hunched shuffle tapping the broom; run = skimming hop
W["Beak"] = dict(walk=dict(D=0.40, duty=0.68, h=0.04, H=-0.10, bob=0.02, bobph=0.25, sway=0.045, hroll=3, broll=6, hyaw=6, sp=30, chp=14, hdp=-24, nod=6,
                           heel=4, toe=10, swingp=0, w=0.16, fy=12, kout=0.2, asw=10, asw0=30, el=50, eld=10, ab=20, fin=20, chyaw=6, sproll=4, hdroll=6),
                 run=dict(D=1.00, duty=0.34, h=0.20, H=-0.06, bob=0.04, bobph=0.2, sway=0.02, hroll=4, hyaw=6, sp=20, chp=10, hdp=-16, nod=8, basep=20,
                          heel=-10, toe=45, swingp=55, lift_peak=0.4, kout=0.1, asw=20, asw0=50, ab=40, el=60, chyaw=8),
                 idle=dict(H=-0.09, sp=28, chp=12, hdp=-20, look=40, arm_r=dict(sw=40, ab=12, el=60), arm_l=dict(sw=12, ab=20, el=50, fin=20), breaths=3))
# Wizard: slow old walk leaning on the staff, a tick every 4th step
W["Tickwell"] = dict(walk=dict(D=0.36, duty=0.70, h=0.03, H=-0.09, bob=0.012, bobph=0.25, sway=0.04, hroll=2, broll=6, hyaw=4, sp=22, chp=10, hdp=-14, nod=4,
                               heel=4, toe=10, swingp=0, w=0.15, fy=10, kout=0.2, asw=6, asw0=10, el=30, ab=20, chyaw=4, sproll=3, hdroll=4,
                               r=dict(asw0=55)),
                     run=dict(D=0.80, duty=0.5, h=0.08, H=-0.08, bob=-0.02, bobph=0.25, sway=0.05, hroll=-5, broll=8, hyaw=6, sp=18, chp=4, hdp=-8, nod=4,
                              heel=6, toe=20, fy=20, w=0.2, kout=0.3, asw=12, ab=40, abd=14, el=30, chyaw=4),
                     idle=dict(H=-0.07, sp=18, chp=8, hdp=-10, look=-30, arm_r=dict(sw=48, ab=10, el=58, fin=10), arm_l=dict(sw=20, ab=20, el=70, fin=40)))
# Shade: legless glide, menacing lean
W["Grim"] = dict(float=dict(lift=0.12, bob=0.03, lean=14, asw0=-20, asw=10, ab=24, el=30), float_run=dict(lift=0.14, bob=0.02, lean=30, asw0=-55, asw=6, ab=24),
                 float_idle=dict(lift=0.10, bob=0.04, lean=4, asw0=8, asw=6, ab=20, el=40))
# Vampire: gliding swagger, cape-hand flourish
W["Collector"] = dict(walk=dict(D=0.80, duty=0.60, h=0.06, H=-0.03, bob=0.012, bobph=0.2, sway=0.02, hroll=3, hyaw=8, sp=-6, chp=-6, hdp=-4, nod=1,
                                heel=10, toe=22, kout=0.08, asw=14, asw0=-10, el=40, eld=20, ab=30, tw=20, chyaw=10, hdroll=3,
                                r=dict(asw0=30)),
                      run=dict(D=1.30, duty=0.36, h=0.16, H=-0.05, bob=-0.03, bobph=0.2, sway=0.01, hroll=3, hyaw=8, sp=26, chp=10, hdp=-10, nod=2, heel=8, toe=40,
                               swingp=50, kout=0.06, asw=6, asw0=-60, el=20, ab=40, tw=50, chyaw=6),
                      idle=dict(H=-0.03, sp=-6, chp=-7, hdp=6, look=22, arm_l=dict(sw=-24, ab=30, el=70, fin=40), arm_r=dict(sw=36, ab=10, el=70, tw=30)))
# Mummy: stiff arms-forward shamble
W["Monk"] = dict(walk=dict(D=0.46, duty=0.66, h=0.05, H=-0.06, bob=0.02, bobph=0.25, sway=0.05, hroll=6, broll=4, hyaw=4, sp=4, chp=2, hdp=-4, nod=3,
                           heel=2, toe=10, swingp=0, w=0.18, fy=6, kout=0.06, asw=4, asw0=88, el=4, eld=0, ab=8, chyaw=4, sproll=3, hdroll=8),
                 run=dict(D=0.90, duty=0.48, h=0.08, H=-0.07, bob=-0.025, bobph=0.25, sway=0.06, hroll=8, hyaw=6, sp=12, chp=4, hdp=-6, nod=4, heel=4, toe=20,
                          swingp=10, kout=0.08, asw=8, asw0=85, el=6, ab=8, chyaw=4),
                 idle=dict(H=-0.05, sp=4, hdp=-6, hdroll=-10, look=18, arm=dict(sw=40, ab=10, el=20), breaths=1))
# Werewolf: loping half-crouch; run = near all-fours bound
def wolf_run_post(s, t):
    fl = math.sin(math.pi * (t - 0.3) / 0.55) if 0.3 <= t <= 0.85 else 0.0
    s["hip"][2] += 0.05 * fl
    for sd, o in (("l", 0.0), ("r", 0.1)):
        s["arms"][sd] = arm(sw=70 + 40 * S2(t - 0.15 + o), ab=18, el=40 - 20 * S2(t - 0.3 + o), fin=10, tw=-10)
W["Femur"] = dict(walk=dict(D=0.82, duty=0.58, h=0.12, H=-0.12, bob=0.03, bobph=0.25, sway=0.03, hroll=4, hyaw=10, sp=22, chp=10, hdp=-12, nod=5,
                            basep=18, heel=-6, toe=40, swingp=30, lift_peak=0.4, kout=0.25, w=0.2, asw=30, asw0=24, el=55, eld=15, ab=30, fin=10, chyaw=10, hdroll=4),
                  run=dict(D=1.60, duty=0.30, h=0.22, H=-0.16, bob=0.0, sway=0.0, hroll=2, hyaw=4, sp=42, chp=14, hdp=-30, nod=0, heel=8, toe=40, swingp=45,
                           lift_peak=0.45, rphase=-0.38, kout=0.12, asw=0, chyaw=0, sproll=0, hdroll=0), run_post=wolf_run_post,
                  idle=dict(H=-0.10, sp=20, chp=10, hdp=-8, look=45, arm=dict(sw=24, ab=30, el=50, fin=10), breaths=4, breath_ch=4, stance=0.05, w=0.2))
# Patchwork Brute: lumbering stomp, big right arm swinging wide
W["Digby"] = dict(walk=dict(D=0.66, duty=0.62, h=0.10, H=-0.07, bob=0.04, bobph=0.25, sway=0.07, hroll=8, broll=6, hyaw=10, sp=14, chp=6, hdp=-6, nod=6,
                            heel=12, toe=22, w=0.24, fy=14, kout=0.3, sproll=6, chroll=4, hdroll=6, asw=26, asw0=8, ab=30, el=30, eld=20, chyaw=14,
                            r=dict(asw0=0)),
                  run=dict(D=1.05, duty=0.46, h=0.15, H=-0.09, bob=-0.04, bobph=0.23, sway=0.07, hroll=8, broll=6, hyaw=12, sp=20, chp=8, hdp=-6, nod=3,
                           heel=10, toe=25, swingp=20, lift_peak=0.45, w=0.24, fy=14, kout=0.3, asw=44, asw0=10, ab=36, el=55, eld=20, chyaw=16),
                  idle=dict(H=-0.06, sp=12, chp=6, hdp=-4, look=-18, arm_r=dict(sw=4, ab=24, el=14), arm_l=dict(sw=10, ab=18, el=30), stance=0.05, w=0.24, breath_z=0.01))
# Gargoyle: crouched hop-walk, wings tucked
def garg_post(s, t):
    hop = max(0.0, math.sin(2 * math.pi * t * 2)) ** 2
    s["hip"][2] += 0.04 * hop
W["Lookout"] = dict(walk=dict(D=0.60, duty=0.50, h=0.12, H=-0.14, bob=0.03, bobph=0.25, sway=0.03, hroll=4, hyaw=6, sp=26, chp=10, hdp=-18, nod=6,
                              basep=14, heel=0, toe=35, swingp=30, kout=0.3, w=0.2, asw=14, asw0=20, el=60, ab=34, fin=20, chyaw=6), walk_post=garg_post,
                    run=dict(D=1.20, duty=0.34, h=0.20, H=-0.12, bob=0.02, bobph=0.2, sway=0.02, hroll=4, hyaw=8, sp=34, chp=12, hdp=-26, nod=4, heel=4, toe=40,
                             swingp=45, kout=0.2, asw=20, asw0=-30, el=40, ab=40, chyaw=8),
                    idle=dict(H=-0.16, sp=26, chp=12, hdp=-16, look=50, arm=dict(sw=40, ab=30, el=70, fin=20), stance=0.06, w=0.21))
# Bog Hag: slow wading walk, high knees as if knee-deep
W["Keeper"] = dict(walk=dict(D=0.52, duty=0.60, h=0.16, H=-0.08, bob=0.02, bobph=0.25, sway=0.05, hroll=5, broll=6, hyaw=6, sp=18, chp=8, hdp=-14, nod=4,
                             heel=4, toe=20, swingp=10, lift_peak=0.45, lift_pow=0.7, w=0.17, kout=0.2, asw=10, asw0=20, el=40, ab=24, fin=10, chyaw=6, hdroll=6,
                             r=dict(asw0=50)),
                   run=dict(D=0.90, duty=0.48, h=0.14, H=-0.09, bob=-0.02, bobph=0.25, sway=0.05, hroll=6, broll=8, hyaw=6, sp=20, chp=6, hdp=-12, nod=4,
                            heel=6, toe=22, fy=14, w=0.2, kout=0.3, asw=18, ab=34, el=36, chyaw=6),
                   idle=dict(H=-0.07, sp=18, chp=8, hdp=-12, look=30, arm_r=dict(sw=46, ab=12, el=58), arm_l=dict(sw=14, ab=24, el=50, fin=20)))
# Imp: hyper scurry, tiny fast steps, arms flailing; cartwheel-ish stumble on the run every so often
W["Intern"] = dict(walk=dict(D=0.62, duty=0.42, h=0.16, H=-0.03, bob=0.07, bobph=0.2, sway=0.03, hroll=6, hyaw=10, sp=-4, chp=-4, hdp=-10, nod=10,
                             basep=18, heel=-10, toe=45, swingp=50, lift_peak=0.4, kout=0.12, w=0.14, asw=46, asw0=30, ab=46, abd=14, el=50, eld=25, fin=10, tw=30, chyaw=12, hdroll=10),
                   run=dict(D=0.95, duty=0.40, h=0.10, H=-0.12, bob=-0.025, bobph=0.2, sway=0.02, hroll=3, hyaw=8, sp=30, chp=12, hdp=-30, nod=3, heel=0, toe=35,
                            basep=12, swingp=40, lift_peak=0.4, kout=0.2, asw=70, asw0=10, el=60, ab=50, chyaw=6, hdroll=4),
                   idle=dict(H=-0.04, sp=10, chp=8, look=55, hroll=6, shift=0.035, arm_l=dict(sw=48, ab=8, el=75, fin=55), arm_r=dict(sw=20, ab=16, el=40), breaths=4))
# Scarecrow: floppy, loose-limbed (big arm swing from the shoulder pole, wobbly knees)
def scarecrow_post(s, t):
    for sd, o in (("l", 0.0), ("r", 0.5)):
        s["arms"][sd]["ab"] = 70 + 10 * S2(t * 2 + o)
        s["arms"][sd]["el"] = 10 + 20 * max(0.0, S2(t + o))
    s["hd"][1] += 12 * S2(t + 0.1)
W["Sweep"] = dict(walk=dict(D=0.74, duty=0.56, h=0.14, H=-0.06, bob=0.05, bobph=0.25, sway=0.05, hroll=8, broll=6, hyaw=12, sp=6, chp=2, hdp=-6, nod=10,
                            heel=10, toe=30, swingp=10, kout=0.35, w=0.17, asw=30, asw0=10, ab=70, el=10, chyaw=18, sproll=8, chroll=6, hdroll=14), walk_post=scarecrow_post,
                  run=dict(D=1.30, duty=0.38, h=0.20, H=-0.07, bob=-0.04, bobph=0.2, sway=0.03, hroll=8, hyaw=14, sp=16, chp=4, hdp=-10, nod=8, heel=10, toe=40,
                           swingp=50, kout=0.3, asw=40, asw0=0, ab=75, el=10, chyaw=20, hdroll=12), run_post=scarecrow_post,
                  idle=dict(H=-0.05, sp=4, hdp=-8, hdroll=-14, look=25, arm=dict(sw=6, ab=72, el=8), cfg=dict(lag={"hd": 2.0})))
# Reanimator: brisk surgeon walk, paddles held up
W["Solace"] = dict(walk=dict(D=0.80, duty=0.58, h=0.10, H=-0.04, bob=0.03, bobph=0.3, sway=0.02, hroll=5, hyaw=10, sp=6, chp=2, hdp=-4, nod=3,
                             heel=14, toe=30, swingp=-6, kout=0.1, asw=6, asw0=70, el=100, eld=0, ab=16, fin=0, chyaw=10, hdroll=3),
                   run=dict(D=1.40, duty=0.35, h=0.20, H=-0.06, bob=-0.05, bobph=0.2, sway=0.01, hroll=5, hyaw=12, sp=18, chp=6, hdp=-12, nod=4, heel=10, toe=42,
                            swingp=60, kout=0.08, asw=40, asw0=40, el=95, ab=18, chyaw=12),
                   idle=dict(H=-0.04, sp=4, hdp=-2, look=-26, arm=dict(sw=60, ab=14, el=100), shift=0.01))
# Ghoul courier: fast hunched knuckle-dragging sprint
W["Mort"] = dict(walk=dict(D=0.80, duty=0.54, h=0.11, H=-0.10, bob=0.03, bobph=0.25, sway=0.03, hroll=5, hyaw=10, sp=28, chp=12, hdp=-22, nod=5,
                           heel=4, toe=34, basep=10, swingp=30, kout=0.22, w=0.19, asw=34, asw0=-6, el=12, eld=10, ab=20, chyaw=12, hdroll=5),
                 run=dict(D=1.55, duty=0.34, h=0.18, H=-0.12, bob=-0.05, bobph=0.18, sway=0.01, hroll=4, hyaw=10, sp=36, chp=12, hdp=-30, nod=6, heel=6, toe=40,
                          swingp=60, lift_peak=0.4, kout=0.08, asw=70, asw0=-10, el=40, ab=22, chyaw=12),
                 idle=dict(H=-0.09, sp=26, chp=12, hdp=-18, look=40, arm=dict(sw=-4, ab=22, el=14), breaths=3, shift=0.03))
# Moth Auditor: fluttering hover-hop
def moth_post(s, t):
    hop = max(0.0, math.sin(2 * math.pi * t * 2)) ** 1.5
    s["hip"][2] += 0.06 * hop
W["Actuary"] = dict(walk=dict(D=0.66, duty=0.44, h=0.14, H=-0.04, bob=0.05, bobph=0.2, sway=0.02, hroll=4, hyaw=6, sp=-2, chp=-4, hdp=-6, nod=4,
                              basep=20, heel=-10, toe=45, swingp=50, lift_peak=0.4, kout=0.1, w=0.15, asw=12, asw0=30, el=70, ab=18, chyaw=6,
                              l=dict(asw0=10)), walk_post=moth_post,
                    run=dict(D=1.20, duty=0.36, h=0.20, H=-0.05, bob=0.03, bobph=0.2, sway=0.01, hroll=4, hyaw=8, sp=14, chp=4, hdp=-10, nod=4, heel=8, toe=40,
                             swingp=50, kout=0.06, asw=30, asw0=20, el=70, ab=20, chyaw=8), run_post=moth_post,
                    idle=dict(H=-0.03, sp=-2, chp=-4, hdp=4, look=-20, arm_l=dict(sw=28, ab=18, el=80, fin=40), arm_r=dict(sw=20, ab=10, el=60)))
# Puppeteer: elegant glide, hands up working the strings
W["Ectoplasm"] = dict(float=dict(lift=0.08, bob=0.02, lean=6, roll=3, yaw=4, asw0=60, asw=8, ab=30, el=70, tw=10), float_run=dict(lift=0.10, bob=0.02, lean=16, asw0=20, asw=10, ab=30, el=60),
                      float_idle=dict(lift=0.06, bob=0.03, lean=0, asw0=70, asw=14, ab=26, el=70))
# Butcher: heavy waddle, cleavers scraping
W["Vexa"] = dict(walk=dict(D=0.52, duty=0.62, h=0.07, H=-0.06, bob=0.03, bobph=0.25, sway=0.08, hroll=-7, broll=11, hyaw=6, sp=2, chp=-4, hdp=-4, nod=4,
                           heel=6, toe=16, fy=22, w=0.25, kout=0.35, sproll=8, chroll=5, hdroll=-10, asw=10, asw0=30, ab=40, abd=10, el=60, eld=0, tw=-20, chyaw=4),
                 run=dict(D=1.00, duty=0.46, h=0.13, H=-0.08, bob=-0.035, bobph=0.23, sway=0.07, hroll=8, broll=8, hyaw=10, sp=16, chp=4, hdp=-6, nod=4,
                          heel=10, toe=25, swingp=20, w=0.25, fy=18, kout=0.35, asw=30, asw0=30, ab=40, el=70, chyaw=12),
                 idle=dict(H=-0.05, sp=0, chp=-6, hdp=-2, look=-20, arm=dict(sw=24, ab=34, el=70, tw=-20), stance=0.06, w=0.25, breaths=2, breath_z=0.012))

def gen_locomotion():
    for rid, G in W.items():
        if "float" in G:
            take("SQ_Walk_" + rid, 41, True, lambda t, G=G: floater(t, G["float"]), base="Running_A")
            take("SQ_Run_" + rid, 31, True, lambda t, G=G: floater(t, dict(G["float_run"], ph=1)), base="Running_A")
            float_idle("SQ_Idle_" + rid, G["float_idle"])
            continue
        wn = 33 if G["walk"]["D"] > 0.6 else 37
        walk4("SQ_Walk_" + rid, wn, G["walk"], post=G.get("walk_post"))
        walk4("SQ_Run_" + rid, 21, G["run"], post=G.get("run_post"), base="Running_A")
        idle4("SQ_Idle_" + rid, G["idle"])

# ====================================================================== SIGNATURE ATTACKS
SIG = {}
def sig(rid, N, cfg=None):
    def deco(fn):
        SIG[rid] = (N, fn, cfg or {})
        return fn
    return deco

def lunge(s, f, B, fwd=-0.12, down=-0.10):
    s["hip"] = chan(B, f, [0, 0, -0.02], [0, 0.05, -0.04], [0, fwd, down], [0, fwd * 1.1, down * 1.05], [0, fwd * 0.6, down * 0.6])

# Headless Horseman (Shoot .20, N21 -> f5): thrusts the lantern head forward, its glare fires; sabre raised in salute
@sig("Neckless", 21, dict(pop=[(5, "l", 0.25)]))
def _(f):
    B = Beats(dict(a0=1, A=3, S=[4], I=5, F=9, R=15, N=21))
    s = spec()
    lunge(s, f, B, -0.10, -0.06)
    s["sp"] = chan(B, f, [3, 0, 0], [-8, 0, 10], [12, 0, -8], [14, 0, -10], [6, 0, -4])
    s["ch"] = chan(B, f, [2, 0, 0], [-6, 0, 16], [8, 0, -14], [10, 0, -16], [4, 0, -6])
    n = arm()
    s["arms"]["l"] = arm_b(B, f, n, dict(sw=40, ab=20, el=100, fin=20), dict(sw=95, ab=0, el=2), dict(sw=100, ab=-4, el=0), dict(sw=70, ab=10, el=30))
    s["arms"]["r"] = arm_b(B, f, n, dict(sw=120, ab=40, el=60, tw=30), dict(sw=150, ab=30, el=20, tw=20), dict(sw=155, ab=30, el=15), dict(sw=40, ab=20, el=40))
    dy, dz = step_keys(2, 5, -0.16, 14, 19)
    s["legs"]["l"] = leg(dy=T(dy, f), dz=T(dz, f))
    return s

# Pumpkin Man (Shoot .20, N21 -> f5): head tilts back, glows, then SPITS forward; vine arms fling back on the recoil
@sig("Tallow", 21, dict(pop=[(5, "lr", 0.2)], lag={"hd": 1.5}))
def _(f):
    B = Beats(dict(a0=1, A=3, S=[4], I=5, F=8, R=14, N=21))
    s = spec()
    s["hip"] = chan(B, f, [0, 0, -0.05], [0, 0.06, -0.02], [0, -0.06, -0.10], [0, -0.07, -0.11], [0, -0.03, -0.07])
    s["sp"] = chan(B, f, [6, 0, 0], [-18, 0, 0], [20, 0, 0], [24, 0, 0], [10, 0, 0])
    s["ch"] = chan(B, f, [2, 0, 0], [-14, 0, 0], [14, 0, 0], [16, 0, 0], [6, 0, 0])
    s["hd"] = chan(B, f, [-6, 0, 0], [-34, 0, 0], [18, 0, 0], [22, 0, 0], [0, 0, 0])
    n = arm(sw=8, ab=28, el=30)
    for sd in "lr":
        s["arms"][sd] = arm_b(B, f, n, dict(sw=60, ab=50, el=60), dict(sw=-40, ab=40, el=10), dict(sw=-50, ab=45, el=6), dict(sw=0, ab=34, el=24))
    return s

# Zombie Warrior (Summon .40, N31 -> f13): rears back, two-hand greatsword plant into the ground, shield braced
@sig("Bonesmith", 31, dict(pop=[(13, "r", 0.25)], exag=(1.1, 1.1, 1.0), nod=40.0))
def _(f):
    B = Beats(dict(a0=4, A=10, S=[11, 12], I=13, F=17, R=24, N=31))
    s = spec()
    s["hip"] = chan(B, f, [0, 0, -0.06], [0, 0.05, 0.0], [0, -0.10, -0.18], [0, -0.10, -0.20], [0, -0.06, -0.12])
    s["sp"] = chan(B, f, [10, 0, 0], [-16, 0, 6], [22, 0, 0], [26, 0, 0], [14, 0, 0])
    s["ch"] = chan(B, f, [6, 0, 0], [-12, 0, 10], [12, 0, 0], [14, 0, 0], [8, 0, 0])
    s["hd"] = chan(B, f, [-8, 0, 0], [-20, 0, 0], [-4, 0, 0], [2, 0, 0], [-6, 0, 0])
    n = arm()
    s["arms"]["r"] = arm_b(B, f, n, dict(sw=170, ab=20, el=70, tw=30), dict(sw=40, ab=6, el=4), dict(sw=30, ab=6, el=8), dict(sw=10, ab=14, el=14))
    s["arms"]["l"] = arm_b(B, f, arm(sw=50, ab=20, el=72, fin=30), dict(sw=70, ab=24, el=80, fin=30), dict(sw=60, ab=16, el=70, fin=30), dict(sw=60, ab=16, el=70, fin=30), dict(sw=52, ab=20, el=72, fin=30))
    dy, dz = step_keys(8, 13, -0.18, 25, 29)
    s["legs"]["l"] = leg(dy=T(dy, f), dz=T(dz, f))
    s["legs"]["r"] = leg(fp=T([(1, 0, "s"), (12, 0, "s"), (15, 20, "o"), (24, 12, "s"), (30, 0, "s")], f), tp=0)
    return s

# Undertaker (Summon .40, N31 -> f13): heaves the coffin up over the shoulder and SLAMS it down end-first; tips his hat after
@sig("Pallbearers", 31, dict(pop=[(13, "lr", 0.25)], exag=(1.1, 1.1, 1.0), nod=40.0))
def _(f):
    B = Beats(dict(a0=3, A=9, S=[11, 12], I=13, F=17, R=23, N=31))
    s = spec()
    s["hip"] = chan(B, f, [0, 0, -0.02], [0, 0.04, -0.04], [0, -0.08, -0.16], [0, -0.08, -0.17], [0, -0.04, -0.08])
    s["sp"] = chan(B, f, [-2, 0, 0], [-14, 0, -18], [20, 0, 4], [22, 0, 6], [6, 0, 0])
    s["ch"] = chan(B, f, [-4, 0, 0], [-10, 0, -20], [14, 0, 6], [16, 0, 8], [2, 0, 0])
    n = arm(sw=-28, ab=24, el=60, fin=45)
    s["arms"]["r"] = arm_b(B, f, n, dict(sw=160, ab=40, el=60, tw=30), dict(sw=50, ab=6, el=6), dict(sw=40, ab=6, el=10), dict(sw=10, ab=14, el=20))
    s["arms"]["l"] = arm_b(B, f, n, dict(sw=150, ab=30, el=70), dict(sw=50, ab=12, el=10), dict(sw=40, ab=10, el=12), dict(sw=6, ab=14, el=20))
    # hat tip at the end (left hand to the brim)
    if f > 22:
        k = T([(22, 0, "s"), (26, 1, "o"), (29, 1, "s"), (31, 0, "s")], f)
        s["arms"]["l"] = arm(sw=lerp(6, 150, k), ab=lerp(14, 20, k), el=lerp(20, 120, k), fin=lerp(0, 40, k))
        s["hd"][0] += 10 * k
    dy, dz = step_keys(7, 13, -0.14, 24, 29)
    s["legs"]["l"] = leg(dy=T(dy, f), dz=T(dz, f))
    return s

# Ghost (Cast .30, N24 -> f8): the WAIL - sucks in, arms flung back, then the whole sheet surges forward screaming
@sig("Widow", 24, dict(pop=[(8, "lr", 0.2)], lag={"hd": 1.0, "arm": 1.5}))
def _(f):
    B = Beats(dict(a0=1, A=5, S=[7], I=8, F=12, R=18, N=24))
    s = floater(0.0, W["Widow"]["float_idle"])
    s["hip"] = chan(B, f, [0, 0, 0.2], [0, 0.08, 0.26], [0, -0.14, 0.18], [0, -0.16, 0.17], [0, -0.06, 0.19])
    s["hr"] = chan(B, f, [2, 0, 0], [-14, 0, 0], [16, 0, 0], [20, 0, 0], [6, 0, 0])
    s["ch"] = chan(B, f, [4, 0, 0], [-16, 0, 0], [10, 0, 0], [14, 0, 0], [4, 0, 0])
    s["hd"] = chan(B, f, [-6, 0, 0], [-30, 0, 0], [12, 0, 0], [16, 0, 0], [0, 0, 0])
    n = arm(sw=10, ab=40, el=40)
    for sd in "lr":
        s["arms"][sd] = arm_b(B, f, n, dict(sw=-60, ab=60, el=30, tw=40), dict(sw=40, ab=80, el=10), dict(sw=50, ab=85, el=8), dict(sw=20, ab=50, el=30))
    return s

# Witch (Cast .30, N24 -> f8): underarm flask lob with a hop and a cackle (head thrown back on the follow-through)
@sig("Beak", 24, dict(pop=[(8, "r", 0.22)]))
def _(f):
    B = Beats(dict(a0=1, A=5, S=[7], I=8, F=12, R=18, N=24))
    s = spec()
    s["hip"] = chan(B, f, [0, 0, -0.09], [0, 0.04, -0.13], [0, -0.06, -0.04], [0, -0.07, -0.03], [0, -0.03, -0.08])
    s["sp"] = chan(B, f, [28, 0, 0], [36, 0, -16], [14, 0, 10], [10, 0, 12], [24, 0, 4])
    s["ch"] = chan(B, f, [12, 0, 0], [16, 0, -14], [4, 0, 10], [0, 0, 12], [10, 0, 4])
    s["hd"] = chan(B, f, [-20, 0, 0], [-10, 0, 10], [-24, 0, -6], [-36, 0, -8], [-22, 0, 0])
    n = arm(sw=40, ab=12, el=60)
    s["arms"]["r"] = arm_b(B, f, n, dict(sw=-50, ab=20, el=30, tw=-30), dict(sw=110, ab=10, el=10, tw=20), dict(sw=130, ab=10, el=6), dict(sw=60, ab=14, el=40))
    s["arms"]["l"] = arm_b(B, f, arm(sw=12, ab=20, el=50, fin=20), dict(sw=40, ab=30, el=60), dict(sw=-10, ab=30, el=30), dict(sw=-20, ab=30, el=30), dict(sw=6, ab=24, el=46))
    s["legs"]["r"] = leg(dz=T([(1, 0, "s"), (4, 0, "s"), (7, 0.05, "s"), (9, 0, "c"), (24, 0, "s")], f), fy=12)
    return s

# Wizard (Raise .45, N30 -> f14): staff swept up in a spiral, hourglass flips at the top, slammed down - the clock-tick ring
@sig("Tickwell", 30, dict(pop=[(14, "r", 0.25)], nod=40.0))
def _(f):
    B = Beats(dict(a0=3, A=10, S=[12, 13], I=14, F=18, R=24, N=30))
    s = spec()
    s["hip"] = chan(B, f, [0, 0, -0.07], [0, 0.02, 0.02], [0, -0.06, -0.12], [0, -0.06, -0.13], [0, -0.03, -0.08])
    s["sp"] = chan(B, f, [18, 0, 0], [-10, 0, 20], [16, 0, -4], [18, 0, -6], [16, 0, 0])
    s["ch"] = chan(B, f, [8, 0, 0], [-12, 0, 24], [8, 0, -8], [10, 0, -10], [8, 0, 0])
    s["hd"] = chan(B, f, [-10, 0, 0], [-30, 0, 10], [-6, 0, 0], [-4, 0, 0], [-10, 0, 0])
    n = arm(sw=48, ab=10, el=58, fin=10)
    s["arms"]["r"] = arm_b(B, f, n, dict(sw=175, ab=30, el=10, tw=60), dict(sw=70, ab=10, el=10, tw=0), dict(sw=60, ab=10, el=14), dict(sw=50, ab=10, el=40))
    s["arms"]["l"] = arm_b(B, f, arm(sw=20, ab=20, el=70, fin=40), dict(sw=120, ab=60, el=30, fin=0), dict(sw=40, ab=50, el=20), dict(sw=30, ab=45, el=20), dict(sw=20, ab=24, el=60, fin=30))
    return s

# Butcher (Spin .40, N31 -> f13): cleaver WHIRLWIND - wind up twisted, full spin with both cleavers out, stumble out of it
@sig("Vexa", 31, dict(pop=[(13, "lr", 0.22)], lag={"hd": 1.5, "ch": 0.5}))
def _(f):
    B = Beats(dict(a0=3, A=8, S=[10, 11], I=13, F=17, R=24, N=31))
    s = spec()
    yaw = T([(1, 0, "s"), (3, 0, "s"), (8, -40, "s"), (13, 200, "l"), (17, 330, "o"), (24, 360, "s"), (31, 360, "s")], f) % 360.0
    yaw = yaw if yaw <= 180 else yaw - 360
    s["hip"] = chan(B, f, [0, 0, -0.05], [0, 0.03, -0.09], [0, -0.04, -0.12], [0, -0.04, -0.13], [0, 0, -0.08])
    s["hr"] = [0, 0, yaw * 0.55]
    s["sp"] = [6, 0, yaw * 0.25]
    s["ch"] = [2, 0, yaw * 0.2]
    s["hd"] = [0, 0, -yaw * 0.2]
    n = arm(sw=24, ab=34, el=70, tw=-20)
    for sd in "lr":
        s["arms"][sd] = arm_b(B, f, n, dict(sw=40, ab=40, el=100), dict(sw=80, ab=88, el=6), dict(sw=78, ab=92, el=4), dict(sw=40, ab=50, el=50))
    return s

# Vampire (Throw .30, N24 -> f8): cane flourish then a flick that fans three coins; cape arm sweeps the other way
@sig("Collector", 24, dict(pop=[(8, "r", 0.2)]))
def _(f):
    B = Beats(dict(a0=1, A=5, S=[7], I=8, F=12, R=18, N=24))
    s = spec()
    s["hip"] = chan(B, f, [0, 0, -0.03], [0, 0.03, -0.04], [0, -0.06, -0.06], [0, -0.07, -0.06], [0, -0.03, -0.04])
    s["sp"] = chan(B, f, [-6, 0, 0], [-10, 0, -24], [6, 0, 20], [8, 0, 26], [-2, 0, 8])
    s["ch"] = chan(B, f, [-6, 0, 0], [-8, 0, -26], [6, 0, 22], [8, 0, 28], [-4, 0, 8])
    s["hd"] = chan(B, f, [6, 0, 0], [8, 0, 26], [0, 0, -14], [0, 0, -20], [6, 0, -4])
    n = arm(sw=36, ab=10, el=70, tw=30)
    s["arms"]["r"] = arm_b(B, f, n, dict(sw=90, ab=-30, el=120, tw=60), dict(sw=85, ab=60, el=10, tw=-20), dict(sw=80, ab=80, el=8), dict(sw=50, ab=30, el=50))
    s["arms"]["l"] = arm_b(B, f, arm(sw=-24, ab=30, el=70, fin=40), dict(sw=30, ab=60, el=40), dict(sw=-20, ab=70, el=20), dict(sw=-30, ab=70, el=20), dict(sw=-24, ab=34, el=60))
    return s

# Mummy (Spin .40, N31 -> f13): double bandage lash, arms crossing in an X then whipping out wide
@sig("Monk", 31, dict(pop=[(13, "lr", 0.22)], lag={"el": 2.0}))
def _(f):
    B = Beats(dict(a0=3, A=9, S=[11, 12], I=13, F=17, R=24, N=31))
    s = spec()
    s["hip"] = chan(B, f, [0, 0, -0.05], [0, 0.04, -0.06], [0, -0.08, -0.10], [0, -0.09, -0.10], [0, -0.04, -0.07])
    s["sp"] = chan(B, f, [4, 0, 0], [-10, 0, 0], [16, 0, 0], [18, 0, 0], [6, 0, 0])
    s["hd"] = chan(B, f, [-4, 0, 0], [-14, 0, 0], [6, 0, 0], [8, 0, 0], [-4, 0, 0])
    n = arm(sw=40, ab=10, el=20)
    for sd in "lr":
        s["arms"][sd] = arm_b(B, f, n, dict(sw=130, ab=-40, el=50, tw=40), dict(sw=90, ab=80, el=4, tw=-20), dict(sw=85, ab=95, el=2), dict(sw=50, ab=40, el=20))
    dy, dz = step_keys(6, 13, -0.14, 24, 29)
    s["legs"]["l"] = leg(dy=T(dy, f), dz=T(dz, f))
    return s

# Werewolf (Slam .50, N31 -> f16): crouch, POUNCE up, two-claw smash into the ground
@sig("Femur", 31, dict(pop=[(16, "lr", 0.25)], exag=(1.1, 1.1, 1.0), nod=40.0))
def _(f):
    B = Beats(dict(a0=3, A=10, S=[13, 15], I=16, F=20, R=26, N=31))
    s = spec()
    jump = T([(1, 0, "s"), (10, 0, "s"), (13, 0.30, "o"), (15, 0.10, "s"), (16, 0, "c"), (31, 0, "s")], f)
    s["hip"] = chan(B, f, [0, 0, -0.10], [0, 0.08, -0.24], [0, -0.20, -0.22], [0, -0.22, -0.24], [0, -0.10, -0.14])
    s["hip"][2] += jump
    s["sp"] = chan(B, f, [20, 0, 0], [30, 0, 0], [40, 0, 0], [44, 0, 0], [26, 0, 0])
    s["ch"] = chan(B, f, [10, 0, 0], [-10, 0, 0], [20, 0, 0], [24, 0, 0], [12, 0, 0])
    s["hd"] = chan(B, f, [-8, 0, 0], [-30, 0, 0], [-20, 0, 0], [-16, 0, 0], [-8, 0, 0])
    n = arm(sw=24, ab=30, el=50, fin=10)
    for sd in "lr":
        s["arms"][sd] = arm_b(B, f, n, dict(sw=170, ab=40, el=40), dict(sw=70, ab=20, el=10), dict(sw=55, ab=20, el=16), dict(sw=40, ab=26, el=30))
    dy, dz = step_keys(10, 16, -0.24, 26, 30)
    s["legs"]["l"] = leg(dy=T(dy, f), dz=T(dz, f))
    s["legs"]["r"] = leg(dy=T([(1, 0, "s"), (10, 0, "s"), (16, -0.12, "o"), (26, -0.12, "s"), (30, 0, "s")], f))
    return s

# Patchwork Brute (Chop .42, N30 -> f13): huge shovel overhead with the big arm, ground-crack stomp
@sig("Digby", 30, dict(pop=[(13, "r", 0.3)], exag=(1.15, 1.1, 1.0), nod=40.0))
def _(f):
    B = Beats(dict(a0=4, A=10, S=[11, 12], I=13, F=18, R=24, N=30))
    s = spec()
    s["hip"] = chan(B, f, [0, 0, -0.06], [0, 0.06, 0.0], [0, -0.10, -0.18], [0, -0.10, -0.19], [0, -0.06, -0.11])
    s["sp"] = chan(B, f, [12, 0, 0], [-20, 0, 10], [22, 0, -4], [26, 0, -6], [14, 0, 0])
    s["ch"] = chan(B, f, [6, 0, 0], [-16, 0, 16], [12, 0, -6], [14, 0, -8], [6, 0, 0])
    s["hd"] = chan(B, f, [-4, 0, 0], [-24, 0, 0], [-6, 0, 0], [0, 0, 0], [-4, 0, 0])
    n = arm(sw=4, ab=24, el=14)
    s["arms"]["r"] = arm_b(B, f, n, dict(sw=178, ab=20, el=60, tw=40), dict(sw=55, ab=6, el=4), dict(sw=35, ab=6, el=12), dict(sw=20, ab=14, el=20))
    s["arms"]["l"] = arm_b(B, f, arm(sw=10, ab=18, el=30), dict(sw=120, ab=40, el=40), dict(sw=10, ab=50, el=30), dict(sw=0, ab=40, el=30), dict(sw=10, ab=20, el=30))
    dy, dz = step_keys(9, 13, -0.18, 25, 28)
    s["legs"]["l"] = leg(dy=T(dy, f), dz=T(dz, f))
    s["legs"]["r"] = leg(fp=T([(1, 0, "s"), (12, 0, "s"), (15, 20, "o"), (24, 12, "s"), (29, 0, "s")], f), tp=0)
    s["w"] = 0.24
    return s

# Gargoyle (Shoot .20, N21 -> f5): hunches, wings flare, forehead beam with a head-snap
@sig("Lookout", 21, dict(pop=[(5, "lr", 0.2)]))
def _(f):
    B = Beats(dict(a0=1, A=3, S=[4], I=5, F=9, R=15, N=21))
    s = spec()
    s["hip"] = chan(B, f, [0, 0, -0.15], [0, 0.04, -0.20], [0, -0.05, -0.12], [0, -0.06, -0.11], [0, -0.02, -0.14])
    s["sp"] = chan(B, f, [26, 0, 0], [34, 0, 0], [16, 0, 0], [14, 0, 0], [24, 0, 0])
    s["ch"] = chan(B, f, [12, 0, 0], [20, 0, 0], [-6, 0, 0], [-8, 0, 0], [10, 0, 0])
    s["hd"] = chan(B, f, [-16, 0, 0], [10, 0, 0], [-26, 0, 0], [-28, 0, 0], [-16, 0, 0])
    n = arm(sw=40, ab=30, el=70, fin=20)
    for sd in "lr":
        s["arms"][sd] = arm_b(B, f, n, dict(sw=30, ab=20, el=90), dict(sw=20, ab=80, el=40), dict(sw=18, ab=85, el=40), dict(sw=34, ab=40, el=60))
    return s

# Bog Hag (Raise .45, N30 -> f14): lantern swung low then hoisted high - the light burst
@sig("Keeper", 30, dict(pop=[(14, "r", 0.22)]))
def _(f):
    B = Beats(dict(a0=3, A=10, S=[12, 13], I=14, F=18, R=24, N=30))
    s = spec()
    s["hip"] = chan(B, f, [0, 0, -0.07], [0, 0.03, -0.14], [0, -0.03, -0.02], [0, -0.03, 0.0], [0, -0.01, -0.06])
    s["sp"] = chan(B, f, [18, 0, 0], [34, 0, -14], [-6, 0, 6], [-10, 0, 8], [14, 0, 0])
    s["ch"] = chan(B, f, [8, 0, 0], [20, 0, -12], [-10, 0, 6], [-14, 0, 8], [6, 0, 0])
    s["hd"] = chan(B, f, [-12, 0, 0], [0, 0, 10], [-30, 0, -6], [-34, 0, -8], [-12, 0, 0])
    n = arm(sw=46, ab=12, el=58)
    s["arms"]["r"] = arm_b(B, f, n, dict(sw=-20, ab=20, el=20), dict(sw=165, ab=12, el=8), dict(sw=172, ab=10, el=6), dict(sw=90, ab=12, el=30))
    s["arms"]["l"] = arm_b(B, f, arm(sw=14, ab=24, el=50, fin=20), dict(sw=30, ab=40, el=60), dict(sw=40, ab=70, el=30), dict(sw=36, ab=72, el=30), dict(sw=18, ab=30, el=50))
    return s

# Imp (Cast .30, N24 -> f8): jittery triple jab, the last one a big lunge (spark colour is random in code)
@sig("Intern", 24, dict(pop=[(4, "r", 0.12), (6, "r", 0.12), (8, "r", 0.25)]))
def _(f):
    B = Beats(dict(a0=1, A=3, S=[6], I=8, F=12, R=18, N=24))
    s = spec()
    jab = T([(1, 0, "s"), (3, 0.0, "s"), (4, 1, "x"), (5, 0.2, "s"), (6, 1, "x"), (7, 0.1, "s"), (8, 1.3, "x"), (12, 1.4, "o"), (18, 0.3, "s"), (24, 0, "s")], f)
    s["hip"] = [0, -0.10 * jab, -0.04 - 0.05 * jab]
    s["sp"] = [10 + 10 * jab, 0, -8 * jab]
    s["ch"] = [8 + 6 * jab, 0, -10 * jab]
    s["hd"] = [-6 - 6 * jab, 6 * S2(f / 8.0), 10 * jab]
    s["arms"]["r"] = arm(sw=40 + 50 * jab, ab=16 - 10 * jab, el=110 - 100 * min(jab, 1.0), tw=10)
    s["arms"]["l"] = arm(sw=48 - 40 * jab, ab=8 + 30 * jab, el=75 - 30 * jab, fin=55)
    dy, dz = step_keys(6, 8, -0.18, 18, 22)
    s["legs"]["l"] = leg(dy=T(dy, f), dz=T(dz, f))
    s["w"] = 0.14
    return s

# Scarecrow (Cast .30, N24 -> f8): arms snap forward from the pole, crows burst off them; floppy recoil
@sig("Sweep", 24, dict(pop=[(8, "lr", 0.2)], lag={"el": 1.5, "hd": 1.5}))
def _(f):
    B = Beats(dict(a0=1, A=5, S=[7], I=8, F=12, R=18, N=24))
    s = spec()
    s["hip"] = chan(B, f, [0, 0, -0.05], [0, 0.05, -0.08], [0, -0.07, -0.06], [0, -0.08, -0.06], [0, -0.03, -0.05])
    s["sp"] = chan(B, f, [4, 0, 0], [-12, 0, 0], [16, 0, 0], [18, 0, 0], [6, 0, 0])
    s["hd"] = chan(B, f, [-8, -14, 0], [-20, -10, 0], [4, 16, 0], [8, 20, 0], [-6, -10, 0])
    n = arm(sw=6, ab=72, el=8)
    for sd in "lr":
        s["arms"][sd] = arm_b(B, f, n, dict(sw=-30, ab=80, el=40), dict(sw=95, ab=20, el=4), dict(sw=100, ab=16, el=2), dict(sw=40, ab=60, el=20))
    return s

# Reanimator (Cast .30, N24 -> f8): paddles rubbed together, raised, "CLEAR!" - thrust forward with a full-body jolt
@sig("Solace", 24, dict(pop=[(8, "lr", 0.25)]))
def _(f):
    B = Beats(dict(a0=1, A=5, S=[7], I=8, F=11, R=18, N=24))
    s = spec()
    s["hip"] = chan(B, f, [0, 0, -0.04], [0, 0.04, -0.02], [0, -0.10, -0.08], [0, -0.11, -0.08], [0, -0.04, -0.05])
    s["sp"] = chan(B, f, [4, 0, 0], [-12, 0, 0], [16, 0, 0], [18, 0, 0], [6, 0, 0])
    jolt = T([(1, 0, "s"), (8, 0, "s"), (9, 1, "x"), (10, -0.6, "x"), (11, 0.4, "x"), (13, 0, "s"), (24, 0, "s")], f)
    s["ch"] = [2 + 6 * jolt, 4 * jolt, 0]
    s["hd"] = [-2 + 10 * jolt, -8 * jolt, 0]
    n = arm(sw=60, ab=14, el=100)
    for sd in "lr":
        s["arms"][sd] = arm_b(B, f, n, dict(sw=150, ab=24, el=60), dict(sw=92, ab=2, el=4), dict(sw=96, ab=0, el=2), dict(sw=70, ab=12, el=60))
    dy, dz = step_keys(4, 8, -0.16, 18, 22)
    s["legs"]["l"] = leg(dy=T(dy, f), dz=T(dz, f))
    return s

# Ghoul courier (Throw .30, N24 -> f8): hop, overhand bone boomerang throw, lands crouched watching it come back
@sig("Mort", 24, dict(pop=[(8, "r", 0.22)]))
def _(f):
    B = Beats(dict(a0=1, A=5, S=[7], I=8, F=12, R=18, N=24))
    s = spec()
    hop = T([(1, 0, "s"), (3, 0, "s"), (5, 0.10, "o"), (8, 0, "c"), (24, 0, "s")], f)
    s["hip"] = chan(B, f, [0, 0, -0.10], [0, 0.06, -0.10], [0, -0.10, -0.16], [0, -0.12, -0.17], [0, -0.04, -0.12])
    s["hip"][2] += hop
    s["sp"] = chan(B, f, [26, 0, 0], [8, 0, -18], [38, 0, 10], [42, 0, 14], [28, 0, 0])
    s["ch"] = chan(B, f, [12, 0, 0], [-10, 0, -22], [20, 0, 14], [24, 0, 18], [12, 0, 0])
    s["hd"] = chan(B, f, [-18, 0, 0], [0, 0, 16], [-20, 0, -8], [-14, 0, -12], [-18, 20, 0])
    n = arm(sw=-4, ab=22, el=14)
    s["arms"]["r"] = arm_b(B, f, n, dict(sw=190, ab=30, el=90, tw=30), dict(sw=80, ab=6, el=4), dict(sw=40, ab=8, el=10), dict(sw=10, ab=20, el=20))
    s["arms"]["l"] = arm_b(B, f, n, dict(sw=60, ab=20, el=20), dict(sw=-30, ab=20, el=10), dict(sw=-40, ab=20, el=10), dict(sw=-10, ab=20, el=14))
    dy, dz = step_keys(5, 8, -0.18, 18, 22)
    s["legs"]["l"] = leg(dy=T(dy, f), dz=T(dz, f))
    return s

# Moth Auditor (Slam .50, N31 -> f16): flutter up off the ground, stamp held high, slam it down - PAID
@sig("Actuary", 31, dict(pop=[(16, "r", 0.3)], nod=40.0))
def _(f):
    B = Beats(dict(a0=3, A=10, S=[13, 15], I=16, F=20, R=26, N=31))
    s = spec()
    up = T([(1, 0, "s"), (5, 0, "s"), (11, 0.22, "s"), (14, 0.20, "s"), (16, 0, "c"), (31, 0, "s")], f)
    s["hip"] = chan(B, f, [0, 0, -0.03], [0, 0.03, -0.03], [0, -0.06, -0.14], [0, -0.06, -0.15], [0, -0.03, -0.07])
    s["hip"][2] += up
    s["sp"] = chan(B, f, [-2, 0, 0], [-16, 0, 0], [24, 0, 0], [28, 0, 0], [6, 0, 0])
    s["ch"] = chan(B, f, [-4, 0, 0], [-14, 0, 0], [16, 0, 0], [18, 0, 0], [2, 0, 0])
    s["hd"] = chan(B, f, [4, 0, 0], [-20, 0, 0], [6, 0, 0], [10, 0, 0], [4, 0, 0])
    n = arm(sw=20, ab=10, el=60)
    s["arms"]["r"] = arm_b(B, f, n, dict(sw=175, ab=10, el=30, tw=20), dict(sw=60, ab=8, el=6), dict(sw=45, ab=8, el=10), dict(sw=24, ab=12, el=40))
    s["arms"]["l"] = arm_b(B, f, arm(sw=28, ab=18, el=80, fin=40), dict(sw=40, ab=40, el=80, fin=40), dict(sw=20, ab=30, el=80, fin=40), dict(sw=20, ab=30, el=80, fin=40), dict(sw=28, ab=18, el=80, fin=40))
    for sd in "lr":
        s["legs"][sd] = leg(dz=0.6 * up, fp=30 * up / 0.22, fy=8)
    return s

# Puppeteer (Cast .30, N24 -> f8): crossbar jerked up - the marionettes lunge - then a graceful flourish
@sig("Ectoplasm", 24, dict(pop=[(8, "lr", 0.2)], lag={"arm": 1.0}))
def _(f):
    B = Beats(dict(a0=1, A=5, S=[7], I=8, F=12, R=18, N=24))
    s = floater(0.0, W["Ectoplasm"]["float_idle"])
    s["hip"] = chan(B, f, [0, 0, 0.06], [0, 0.03, 0.03], [0, -0.06, 0.10], [0, -0.07, 0.11], [0, -0.02, 0.07])
    s["hr"] = chan(B, f, [0, 0, 0], [8, 0, -10], [-6, 0, 8], [-8, 0, 10], [0, 0, 0])
    s["ch"] = chan(B, f, [4, 0, 0], [10, 0, -10], [-10, 0, 10], [-12, 0, 12], [2, 0, 0])
    s["hd"] = chan(B, f, [-6, 0, 0], [6, 0, 10], [-16, 0, -6], [-18, 0, -8], [-6, 0, 0])
    n = arm(sw=70, ab=26, el=70)
    s["arms"]["r"] = arm_b(B, f, n, dict(sw=40, ab=20, el=100), dict(sw=165, ab=10, el=10), dict(sw=170, ab=10, el=6), dict(sw=100, ab=20, el=50))
    s["arms"]["l"] = arm_b(B, f, n, dict(sw=50, ab=40, el=90), dict(sw=110, ab=60, el=20), dict(sw=115, ab=65, el=15), dict(sw=80, ab=40, el=50))
    return s

def gen_sigs():
    for rid, (N, fn, cfg) in SIG.items():
        take("SQ_Sig_" + rid, N, False, fn, cfg)

gen_locomotion()
gen_sigs()
if not ONLY or "SQ_Marker_Remaster4" in ONLY:
    idle = ACT["Idle"]
    act = write_action("SQ_Marker_Remaster4", idle, [eval_pose(idle, 1), eval_pose(idle, 2)])
    act["sq_loop"] = False
    DONE4.append("SQ_Marker_Remaster4")
set_action(ACT["Idle"])
sc.frame_set(1)
bpy.ops.wm.save_as_mainfile(filepath=BLEND)
print("DONE4", len(DONE4), DONE4)
print("NACTIONS", len(bpy.data.actions), "SQ", len([a for a in bpy.data.actions if a.name.startswith("SQ_")]))
