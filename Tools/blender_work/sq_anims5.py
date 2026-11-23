# Soul Quota - Remaster 5 (2026-11-10): character-select ENTRANCES. Every monster gets SQ_Intro_<Id> (played when picked on the
# character screen, with its themed entrance in SQFxTheme / ASQMenuShowroom) and SQ_Flourish_<Id> (its hero pose; ASQRiseFx
# plays it after rising at the start of a mission). Run AFTER sq_anims4.py, then sq_export.py.
#   python3 sq_anims5.py [--only A,B]
# Entrance modes (C++ SQTheme::Entrance): Rise = mesh climbs out of the ground while the take claws up; Poof = hidden, puff, pops in;
# Drop = falls/swoops in from above and lands in the take's crouch; Materialize = spirals in from nothing; Grow = sprouts from the
# ground; Jolt = slumped and lifeless until a lightning strike jolts it alive.
import bpy, math, sys
src3 = open("/home/claude/blender_work/sq_anims3.py").read()
exec(compile(src3[:src3.index("# ============================================================ 1. RISE")], "sq_anims3_helpers", "exec"))
DONE5 = []
INTRO_N = 55
FLOURISH_FROM = 30     # SQ_Flourish_<Id> = frames 30..55 of the intro (the hero beat)

CH = ["hip", "hr", "sp", "ch", "hd"]
AK = ["sw", "ab", "el", "fin", "tw"]
LK = ["dx", "dy", "dz", "fp", "fy"]

def base_pose():
    s = spec()
    d = {}
    for c in CH:
        for i in range(3):
            d["%s%d" % (c, i)] = s[c][i]
    for sd in "lr":
        for k in AK:
            d["a%s_%s" % (sd, k)] = s["arms"][sd][k]
        for k in LK:
            d["l%s_%s" % (sd, k)] = s["legs"][sd][k]
    d["w"] = 0.17
    return d

def P(f, e="s", **kw):
    """a key pose at frame f; channels: hip/hr/sp/ch/hd=(x,y,z); l/r = arm dict; ll/lr = leg dict; w = stance"""
    o = {}
    for c in CH:
        if c in kw and kw[c] is not None:
            for i, v in enumerate(kw[c]):
                if v is not None:
                    o["%s%d" % (c, i)] = v
    for sd in "lr":
        for k, v in (kw.get(sd) or {}).items():
            o["a%s_%s" % (sd, k)] = v
        for k, v in (kw.get("l" + sd) or {}).items():
            o["l%s_%s" % (sd, k)] = v
    if "w" in kw:
        o["w"] = kw["w"]
    return (f, e, o)

def bake_keys(keys):
    cur = base_pose()
    tracks = {k: [] for k in cur}
    for f, e, o in keys:
        cur = dict(cur)
        cur.update(o)
        for k, v in cur.items():
            tracks[k].append((f, v, e))
    return tracks

def pose_at(tracks, f, floater=False):
    s = spec()
    v = {k: track(t, f) for k, t in tracks.items()}
    for c in CH:
        s[c] = [v["%s%d" % (c, i)] for i in range(3)]
    for sd in "lr":
        s["arms"][sd] = arm(**{k: v["a%s_%s" % (sd, k)] for k in AK})
        s["legs"][sd] = leg(**{k: v["l%s_%s" % (sd, k)] for k in LK})
    s["w"] = v["w"]
    if floater:
        s.pop("legs")
        s["fk"] = {sd: dict(th=20, kn=30, ft=30, to=10, ab=4) for sd in "lr"}
    return s

def intro(rid, keys, floater=False, cfg=None):
    tr = bake_keys(keys)
    for name, f0, N, loop in (("SQ_Intro_" + rid, 1, INTRO_N, False), ("SQ_Flourish_" + rid, FLOURISH_FROM, INTRO_N - FLOURISH_FROM + 1, False)):
        if ONLY and name not in ONLY:
            continue
        act = make_core(name, "Idle", N, lambda f, f0=f0: pose_at(tr, f + f0 - 1, floater), loop, dict(cfg or {}))
        act["sq_remaster"] = 5
        DONE5.append(name)

IDLE_END = lambda f=INTRO_N: P(f, "s")     # carries the last pose; every intro ends near its idle stance

# ---------------------------------------------------------------- RISE (climb out of the ground: arms claw up, then push out)
def rise_keys(hero_l, hero_r, hero_body, settle_l=None, settle_r=None, extra=()):
    return [
        P(1, hip=(0, 0.05, -0.42), sp=(46, 0, 0), ch=(20, 0, 0), hd=(-10, 0, 0), l=dict(sw=160, ab=30, el=40, fin=60), r=dict(sw=150, ab=20, el=50, fin=60),
          ll=dict(dz=0.0, fy=14), lr=dict(dy=0.06, fy=14), w=0.22),
        P(9, "s", hip=(0.03, 0.03, -0.36), sp=(40, 8, 6), l=dict(sw=175, ab=34, el=20, fin=10), r=dict(sw=120, ab=40, el=70, fin=70)),       # left hand grabs the rim
        P(16, "s", hip=(-0.03, 0.02, -0.30), sp=(36, -8, -6), l=dict(sw=95, ab=50, el=90, fin=70), r=dict(sw=172, ab=30, el=20, fin=10)),   # right hand over
        P(24, "s", hip=(0, -0.02, -0.22), sp=(30, 0, 0), ch=(14, 0, 0), l=dict(sw=40, ab=40, el=40), r=dict(sw=40, ab=40, el=40)),           # push up out
        P(31, "o", **hero_body, l=hero_l, r=hero_r),
        *extra,
        P(46, "s"),
        P(INTRO_N, "s", hip=(0, 0, -0.04), sp=(6, 0, 0), ch=(2, 0, 0), hd=(-4, 0, 0), l=settle_l or dict(sw=8, ab=14, el=20, fin=6), r=settle_r or dict(sw=8, ab=14, el=20, fin=6),
          ll=dict(dz=0, dy=0, fy=8), lr=dict(dy=-0.04, fy=12), w=0.18),
    ]

# Gravus (zombie warrior): claws out of his grave, then the shield-up war bellow, sword aloft
intro("Bonesmith", rise_keys(dict(sw=60, ab=24, el=72, fin=30), dict(sw=175, ab=24, el=12, tw=30),
                             dict(hip=(0, -0.02, -0.08), sp=(-10, 0, 8), ch=(-14, 0, 10), hd=(-24, 0, 0)),
                             settle_l=dict(sw=50, ab=20, el=72, fin=30), settle_r=dict(sw=-6, ab=16, el=12)),
      cfg=dict(pop=[(31, "r", 0.3)], nod=40.0))
# Gutsy Gus (ghoul courier): scrabbles out, pats down his satchel, grins and thumbs-up
intro("Mort", rise_keys(dict(sw=30, ab=40, el=110, fin=0), dict(sw=110, ab=20, el=80, fin=55, tw=-40),
                        dict(hip=(0, 0, -0.10), sp=(20, 0, 10), ch=(8, 0, 6), hd=(-20, 14, -10)),
                        extra=(P(38, "s", l=dict(sw=-10, ab=50, el=90, fin=40)),)),
      cfg=dict(pop=[(31, "r", 0.2)]))
# Imhotet (mummy): bursts out of sand with arms locked forward, bandage ends whipping, then a slow regal head turn
intro("Monk", rise_keys(dict(sw=88, ab=8, el=4), dict(sw=88, ab=8, el=4),
                        dict(hip=(0, 0, -0.04), sp=(-6, 0, 0), ch=(-4, 0, 0), hd=(-14, 0, 0)),
                        settle_l=dict(sw=40, ab=10, el=20), settle_r=dict(sw=40, ab=10, el=20),
                        extra=(P(39, "s", hd=(-8, 0, 30)), P(44, "s", hd=(-8, 0, -20)))),
      cfg=dict(lag={"hd": 2.0}))
# Old Mother Murk (bog hag): wades up out of the swamp, lantern hoisted high, frog croaks
intro("Keeper", rise_keys(dict(sw=14, ab=30, el=60, fin=30), dict(sw=168, ab=12, el=10),
                          dict(hip=(0, 0, -0.06), sp=(10, 0, -6), ch=(-8, 0, -6), hd=(-24, 0, -10)),
                          settle_r=dict(sw=46, ab=12, el=58)),
      cfg=dict(pop=[(31, "r", 0.2)]))
# Mortimer Crepe (undertaker): rises stiffly from the grave (no clawing - he levitates up, arms crossed), then tips his hat
intro("Pallbearers", [
    P(1, hip=(0, 0, -0.06), sp=(-4, 0, 0), ch=(-4, 0, 0), hd=(8, 0, 0), l=dict(sw=60, ab=-30, el=110, fin=40), r=dict(sw=60, ab=-30, el=110, fin=40)),
    P(24, "s", hd=(4, 0, 0)),
    P(30, "s", hd=(10, 0, 0), l=dict(sw=60, ab=-30, el=110, fin=40), r=dict(sw=150, ab=20, el=120, fin=40)),     # hand to the hat brim
    P(36, "o", hd=(22, 0, 0), sp=(14, 0, 0), ch=(10, 0, 0), r=dict(sw=120, ab=40, el=70, fin=10)),               # the bow, hat lifted
    P(44, "s", hd=(4, 0, 0), sp=(-2, 0, 0), ch=(-4, 0, 0), r=dict(sw=150, ab=20, el=120, fin=40)),
    P(INTRO_N, "s", hd=(6, 0, 0), l=dict(sw=-28, ab=24, el=60, fin=45), r=dict(sw=-28, ab=24, el=60, fin=45)),
])
# The Shade: rises out of a pool of shadow, hood bowed, then lifts the head - eyes flare - scythe swept wide
intro("Grim", [
    P(1, hip=(0, 0, -0.30), sp=(40, 0, 0), ch=(20, 0, 0), hd=(-30, 0, 0), l=dict(sw=-20, ab=10, el=10), r=dict(sw=-20, ab=10, el=10)),
    P(24, "s", hip=(0, 0, -0.10), sp=(20, 0, 0), hd=(-30, 0, 0)),
    P(30, "x", hd=(-6, 0, 0)),
    P(35, "o", sp=(-4, 0, -14), ch=(-6, 0, -16), r=dict(sw=60, ab=95, el=10, tw=20), l=dict(sw=30, ab=40, el=40)),
    P(46, "s"),
    P(INTRO_N, "s", hip=(0, 0, 0.08), sp=(6, 0, 0), ch=(4, 0, 0), hd=(-12, 0, 0), l=dict(sw=8, ab=20, el=40), r=dict(sw=8, ab=20, el=40)),
], floater=True)

# ---------------------------------------------------------------- POOF (hidden ~0.25 s, coloured puff, pops in mid-flourish)
# Granny Nettle (witch): pops out of purple smoke mid-cackle, broom twirled overhead then planted
intro("Beak", [
    P(1, hip=(0, 0, 0.0), sp=(-10, 0, 0), ch=(-16, 0, 0), hd=(-36, 0, 0), l=dict(sw=150, ab=60, el=30, fin=20), r=dict(sw=170, ab=20, el=20, tw=60)),
    P(10, "o", hd=(-40, 10, 0), r=dict(sw=165, ab=40, el=20, tw=-40)),
    P(18, "s", hd=(-34, -10, 0), r=dict(sw=170, ab=20, el=20, tw=60)),
    P(26, "s", hip=(0, 0, -0.10), sp=(30, 0, 0), ch=(12, 0, 0), hd=(-20, 0, 0), l=dict(sw=20, ab=30, el=60), r=dict(sw=60, ab=12, el=40)),
    P(32, "o", hd=(-34, 0, 0)),                                                            # cackle (head thrown back)
    P(38, "s", hd=(-22, 0, 0)), P(42, "s", hd=(-34, 0, 0)),
    P(INTRO_N, "s", hip=(0, 0, -0.09), sp=(28, 0, 0), ch=(12, 0, 0), hd=(-20, 0, 0), l=dict(sw=12, ab=20, el=50, fin=20), r=dict(sw=40, ab=12, el=60)),
], cfg=dict(lag={"hd": 1.0}))
# Archmage Tickwell (wizard): blinks in through a clock rune, staff raised, slams it down - the tick ring
intro("Tickwell", [
    P(1, hip=(0, 0, -0.02), sp=(-8, 0, 0), ch=(-10, 0, 0), hd=(-24, 0, 0), r=dict(sw=175, ab=30, el=10, tw=40), l=dict(sw=120, ab=60, el=30)),
    P(14, "s", hip=(0, 0, 0.02)),
    P(20, "x", hip=(0, -0.04, -0.12), sp=(18, 0, 0), ch=(8, 0, 0), hd=(-6, 0, 0), r=dict(sw=60, ab=10, el=12), l=dict(sw=30, ab=40, el=40)),   # slam
    P(28, "s", hip=(0, -0.03, -0.09)),
    P(38, "s", hd=(-14, 0, 20), l=dict(sw=40, ab=30, el=90, fin=40)),                       # strokes the beard
    P(INTRO_N, "s", hip=(0, 0, -0.07), sp=(18, 0, 0), ch=(8, 0, 0), hd=(-10, 0, 0), r=dict(sw=48, ab=10, el=58, fin=10), l=dict(sw=20, ab=20, el=70, fin=40)),
], cfg=dict(pop=[(20, "r", 0.3)], nod=40.0))
# Count Ledgerdrain (vampire): a swarm of bats folds into him; he unfurls from behind his cape
intro("Collector", [
    P(1, hip=(0, 0, -0.08), sp=(20, 0, 0), ch=(10, 0, 0), hd=(-20, 0, 30), l=dict(sw=100, ab=-50, el=140, fin=40), r=dict(sw=90, ab=-40, el=130, fin=40)),   # cape held across the face
    P(16, "s", hd=(-14, 0, 10)),
    P(24, "o", hip=(0, 0, -0.02), sp=(-8, 0, 0), ch=(-10, 0, 0), hd=(4, 0, 0), l=dict(sw=40, ab=95, el=10, fin=0), r=dict(sw=40, ab=95, el=10, fin=0)),        # flings the cape wide
    P(34, "s"),
    P(40, "s", hd=(8, 0, -14), r=dict(sw=80, ab=20, el=110, fin=50)),                       # adjusts the monocle
    P(INTRO_N, "s", hip=(0, 0, -0.03), sp=(-6, 0, 0), ch=(-7, 0, 0), hd=(6, 0, 0), l=dict(sw=-24, ab=30, el=70, fin=40), r=dict(sw=36, ab=10, el=70, tw=30)),
])
# Snig (imp): explodes out of a red firework, tumbles, sticks the landing - TA-DA
intro("Intern", [
    P(1, hip=(0, 0, 0.10), sp=(40, 0, 0), ch=(30, 0, 0), hd=(-30, 0, 0), l=dict(sw=150, ab=40, el=90), r=dict(sw=150, ab=40, el=90), ll=dict(dz=0.25, fp=60), lr=dict(dz=0.22, fp=60)),
    P(10, "s", hip=(0, 0, 0.18), sp=(80, 0, 0), ch=(40, 0, 0)),                              # mid-somersault (forward roll)
    P(16, "x", hip=(0, -0.05, -0.20), sp=(30, 0, 0), ch=(14, 0, 0), hd=(-10, 0, 0), ll=dict(dz=0, fp=0), lr=dict(dz=0, fp=0), l=dict(sw=40, ab=60, el=40), r=dict(sw=40, ab=60, el=40)),
    P(24, "o", hip=(0, 0, 0.0), sp=(-14, 0, 0), ch=(-14, 0, 0), hd=(-20, 0, 0), l=dict(sw=150, ab=50, el=10, fin=0), r=dict(sw=150, ab=50, el=10, fin=0)),     # TA-DA
    P(36, "s"),
    P(INTRO_N, "s", hip=(0, 0, -0.04), sp=(10, 0, 0), ch=(8, 0, 0), hd=(0, 0, 0), l=dict(sw=48, ab=8, el=75, fin=55), r=dict(sw=20, ab=16, el=40)),
], cfg=dict(pop=[(16, "lr", 0.3)]))
# Hayward Crowe (scarecrow): the crows gather and he flops together, then shakes himself out, head lolling
def crow_post(s, f):
    if f < 30:
        k = 1 - f / 30.0
        s["hd"][1] += 30 * k * math.sin(f * 0.9)
        s["sp"][1] += 10 * k * math.sin(f * 1.1)
intro("Sweep", [
    P(1, hip=(0, 0, -0.30), sp=(50, 0, 0), ch=(30, 0, 0), hd=(-40, 20, 0), l=dict(sw=-10, ab=20, el=20), r=dict(sw=-10, ab=20, el=20)),
    P(14, "s", hip=(0, 0, -0.12), sp=(20, 0, 0), hd=(-20, -20, 0), l=dict(sw=10, ab=50, el=30), r=dict(sw=10, ab=50, el=30)),
    P(24, "o", hip=(0, 0, -0.02), sp=(0, 0, 0), ch=(0, 0, 0), hd=(-6, 14, 0), l=dict(sw=6, ab=80, el=6), r=dict(sw=6, ab=80, el=6)),                             # arms snap out on the pole
    P(32, "s", hd=(-8, -14, 0)), P(38, "s", hd=(-6, 10, 0)),
    P(INTRO_N, "s", hip=(0, 0, -0.05), sp=(4, 0, 0), hd=(-8, -14, 0), l=dict(sw=6, ab=72, el=8), r=dict(sw=6, ab=72, el=8)),
])
# Doc Stitches (reanimator): appears in a crack of lightning, paddles raised - "IT'S ALIVE!"
intro("Solace", [
    P(1, hip=(0, 0, -0.10), sp=(24, 0, 0), ch=(12, 0, 0), hd=(-10, 0, 0), l=dict(sw=90, ab=10, el=100), r=dict(sw=90, ab=10, el=100)),
    P(12, "s", hip=(0, 0, -0.12)),
    P(18, "o", hip=(0, 0, 0.02), sp=(-14, 0, 0), ch=(-18, 0, 0), hd=(-30, 0, 0), l=dict(sw=165, ab=30, el=20), r=dict(sw=165, ab=30, el=20)),                   # arms flung up
    P(24, "s", hd=(-34, 8, 0)), P(28, "s", hd=(-34, -8, 0)), P(32, "s", hd=(-34, 8, 0)),
    P(40, "s", hd=(-10, 0, 0), l=dict(sw=60, ab=14, el=100), r=dict(sw=60, ab=14, el=100)),
    P(INTRO_N, "s", hip=(0, 0, -0.04), sp=(4, 0, 0), ch=(2, 0, 0), hd=(-2, 0, 0)),
], cfg=dict(pop=[(18, "lr", 0.3)]))

# ---------------------------------------------------------------- DROP (falls / swoops in, lands in a crouch, then the hero beat)
def drop_keys(land_body, hero_l, hero_r, hero_body, settle_l, settle_r, settle_body, air_l=None, air_r=None, land=10, hero=24):
    return [
        P(1, hip=(0, 0.04, 0.10), sp=(-6, 0, 0), ch=(-8, 0, 0), hd=(-10, 0, 0), l=air_l or dict(sw=40, ab=80, el=30), r=air_r or dict(sw=40, ab=80, el=30),
          ll=dict(dz=0.20, fp=30, dy=0.04), lr=dict(dz=0.14, fp=30, dy=-0.06)),
        P(land, "x", **land_body, ll=dict(dz=0, fp=0, dy=0.04, fy=14), lr=dict(dz=0, fp=0, dy=-0.08, fy=14), w=0.24),
        P(land + 5, "s"),
        P(hero, "o", **hero_body, l=hero_l, r=hero_r),
        P(hero + 14, "s"),
        P(INTRO_N, "s", **settle_body, l=settle_l, r=settle_r, ll=dict(dy=0, fy=8), lr=dict(dy=-0.04, fy=12), w=0.18),
    ]
# Rufus Moonfang (werewolf): leaps in, lands on all fours, rears up and HOWLS at the moon
intro("Femur", drop_keys(dict(hip=(0, -0.06, -0.34), sp=(50, 0, 0), ch=(20, 0, 0), hd=(-30, 0, 0)),
                         dict(sw=40, ab=40, el=70, fin=10), dict(sw=40, ab=40, el=70, fin=10),
                         dict(hip=(0, 0, -0.06), sp=(-14, 0, 0), ch=(-20, 0, 0), hd=(-50, 0, 0)),
                         dict(sw=24, ab=30, el=50, fin=10), dict(sw=24, ab=30, el=50, fin=10), dict(hip=(0, 0, -0.10), sp=(20, 0, 0), ch=(10, 0, 0), hd=(-8, 0, 0)),
                         air_l=dict(sw=120, ab=30, el=40), air_r=dict(sw=120, ab=30, el=40), land=9, hero=22),
      cfg=dict(pop=[(9, "lr", 0.3)], nod=40.0))
# Granite Gil (gargoyle): swoops down, lands crouched like a rooftop statue, wings flare and the eye ignites
intro("Lookout", drop_keys(dict(hip=(0, -0.04, -0.30), sp=(40, 0, 0), ch=(16, 0, 0), hd=(-30, 0, 0)),
                           dict(sw=20, ab=85, el=40), dict(sw=20, ab=85, el=40),
                           dict(hip=(0, 0, -0.14), sp=(20, 0, 0), ch=(-10, 0, 0), hd=(-28, 0, 0)),
                           dict(sw=40, ab=30, el=70, fin=20), dict(sw=40, ab=30, el=70, fin=20), dict(hip=(0, 0, -0.16), sp=(26, 0, 0), ch=(12, 0, 0), hd=(-16, 0, 0)),
                           air_l=dict(sw=10, ab=90, el=20), air_r=dict(sw=10, ab=90, el=20)),
      cfg=dict(pop=[(10, "lr", 0.3)]))
# Big Brisket (butcher): drops in with a ground-shaking stomp, bangs the cleavers together over his head
intro("Vexa", drop_keys(dict(hip=(0, -0.04, -0.26), sp=(24, 0, 0), ch=(10, 0, 0), hd=(-10, 0, 0)),
                        dict(sw=150, ab=-10, el=40), dict(sw=150, ab=-10, el=40),
                        dict(hip=(0, 0, -0.04), sp=(-10, 0, 0), ch=(-12, 0, 0), hd=(-20, 0, 0)),
                        dict(sw=24, ab=34, el=70, tw=-20), dict(sw=24, ab=34, el=70, tw=-20), dict(hip=(0, 0, -0.05), sp=(0, 0, 0), ch=(-6, 0, 0), hd=(-2, 0, 0))),
      cfg=dict(pop=[(10, "lr", 0.35), (24, "lr", 0.2)], nod=40.0))
# Ledger Mothwick (moth auditor): flutters down slowly, lands on tiptoe, stamps the ledger - PAID
intro("Actuary", drop_keys(dict(hip=(0, 0, -0.10), sp=(10, 0, 0), ch=(4, 0, 0), hd=(-6, 0, 0)),
                           dict(sw=28, ab=18, el=80, fin=40), dict(sw=60, ab=8, el=6),
                           dict(hip=(0, -0.04, -0.12), sp=(26, 0, 0), ch=(16, 0, 0), hd=(6, 0, 0)),
                           dict(sw=28, ab=18, el=80, fin=40), dict(sw=20, ab=10, el=60), dict(hip=(0, 0, -0.03), sp=(-2, 0, 0), ch=(-4, 0, 0), hd=(4, 0, 0)),
                           air_l=dict(sw=60, ab=60, el=60), air_r=dict(sw=60, ab=60, el=60), land=18, hero=30),
      cfg=dict(pop=[(30, "r", 0.3)]))
# Sir Hollowhead (headless horseman): vaults off his phantom steed, lands, raises the lantern head high and salutes with the sabre
intro("Neckless", drop_keys(dict(hip=(0, -0.06, -0.24), sp=(24, 0, 0), ch=(10, 0, 0), hd=(0, 0, 0)),
                            dict(sw=170, ab=20, el=10, fin=20), dict(sw=110, ab=30, el=110, tw=30),
                            dict(hip=(0, 0, -0.03), sp=(-8, 0, 0), ch=(-10, 0, 0), hd=(0, 0, 0)),
                            dict(sw=30, ab=22, el=80, fin=30), dict(sw=10, ab=14, el=24), dict(hip=(0, 0, -0.03), sp=(-6, 0, 0), ch=(-8, 0, 0), hd=(0, 0, 0)),
                            air_l=dict(sw=40, ab=40, el=90, fin=30), air_r=dict(sw=100, ab=60, el=30)),
      cfg=dict(pop=[(10, "lr", 0.3)]))
# Madame Marionette (puppeteer): lowered from above on glowing strings, limp like a puppet, then she takes the strings herself - a curtsey
def puppet_post(s, f):
    if f < 22:
        k = 1 - f / 22.0
        s["hd"][1] += 18 * k * math.sin(f * 0.7)
intro("Ectoplasm", [
    P(1, hip=(0, 0, 0.30), sp=(10, 0, 0), ch=(8, 0, 0), hd=(-30, 16, 0), l=dict(sw=-10, ab=30, el=40), r=dict(sw=-10, ab=30, el=40)),   # dangling
    P(18, "s", hip=(0, 0, 0.10)),
    P(24, "o", hip=(0, 0, 0.06), hd=(-10, 0, 0), l=dict(sw=150, ab=30, el=40), r=dict(sw=150, ab=30, el=40)),      # takes her own strings
    P(33, "s", hip=(0, 0, 0.0), sp=(18, 0, 0), ch=(10, 0, 0), hd=(14, 0, 0), l=dict(sw=-20, ab=40, el=20), r=dict(sw=-20, ab=40, el=20)),  # curtsey
    P(42, "s", sp=(0, 0, 0), ch=(4, 0, 0), hd=(-6, 0, 0)),
    P(INTRO_N, "s", hip=(0, 0, 0.06), l=dict(sw=70, ab=26, el=70), r=dict(sw=70, ab=26, el=70)),
], floater=True)

# ---------------------------------------------------------------- MATERIALIZE / GROW / JOLT
# Ondine (ghost): spirals in from nothing, flings her arms wide and WAILS, then the veil settles
intro("Widow", [
    P(1, hip=(0, 0, 0.10), hr=(0, 0, -60), sp=(10, 0, 0), hd=(-10, 0, 0), l=dict(sw=-40, ab=20, el=30), r=dict(sw=-40, ab=20, el=30)),
    P(18, "s", hip=(0, 0, 0.20), hr=(0, 0, 10)),
    P(26, "o", hip=(0, -0.06, 0.26), hr=(-10, 0, 0), ch=(-16, 0, 0), hd=(-30, 0, 0), l=dict(sw=-50, ab=70, el=20, tw=40), r=dict(sw=-50, ab=70, el=20, tw=40)),
    P(32, "o", hip=(0, -0.10, 0.18), hr=(14, 0, 0), ch=(12, 0, 0), hd=(10, 0, 0), l=dict(sw=40, ab=85, el=8), r=dict(sw=40, ab=85, el=8)),                     # WAIL
    P(44, "s"),
    P(INTRO_N, "s", hip=(0, 0, 0.20), hr=(2, 0, 0), ch=(4, 0, 0), hd=(-12, 0, 0), l=dict(sw=10, ab=40, el=40), r=dict(sw=10, ab=40, el=40)),
], floater=True, cfg=dict(lag={"arm": 1.5}))
# Jack Gourdling (pumpkin man): sprouts up from the vine, unfurls, the head tilts back and lights up - a gout of flame skyward
intro("Tallow", [
    P(1, hip=(0, 0, -0.40), sp=(60, 0, 0), ch=(30, 0, 0), hd=(-40, 0, 0), l=dict(sw=-20, ab=60, el=120), r=dict(sw=-20, ab=60, el=120), w=0.22),
    P(16, "s", hip=(0, 0, -0.20), sp=(30, 0, 0), ch=(14, 0, 0), hd=(-20, 0, 0), l=dict(sw=40, ab=80, el=60), r=dict(sw=40, ab=80, el=60)),
    P(24, "o", hip=(0, 0, -0.02), sp=(-14, 0, 0), ch=(-14, 0, 0), hd=(-44, 0, 0), l=dict(sw=-40, ab=60, el=10), r=dict(sw=-40, ab=60, el=10)),               # head back, the gout of flame
    P(34, "s", hd=(-40, 0, 0)),
    P(42, "s", hip=(0, 0, -0.06), sp=(8, 0, 0), ch=(2, 0, 0), hd=(-6, 0, 0)),
    P(INTRO_N, "s", hip=(0, 0, -0.05), sp=(6, 0, 0), hd=(-6, 0, -10), l=dict(sw=8, ab=28, el=30), r=dict(sw=8, ab=28, el=30), w=0.22),
], cfg=dict(lag={"hd": 2.0}))
# Bolt Patchwork (patchwork brute): slumped lifeless on his feet - lightning strikes the brace - he jolts alive and flexes the big arm
intro("Digby", [
    P(1, hip=(0, 0, -0.10), sp=(30, 0, 0), ch=(20, 0, 0), hd=(-40, 0, 10), l=dict(sw=-6, ab=8, el=4, fin=0), r=dict(sw=-6, ab=8, el=4, fin=0)),
    P(12, "s"),
    P(14, "x", hip=(0, 0, 0.02), sp=(-14, 0, 0), ch=(-20, 0, 0), hd=(-30, 0, 0), l=dict(sw=40, ab=80, el=10, fin=0), r=dict(sw=40, ab=80, el=10, fin=0)),      # JOLT
    P(17, "x", hd=(-24, 10, 0)), P(20, "x", hd=(-30, -10, 0)),
    P(30, "o", hip=(0, 0, -0.08), sp=(14, 0, -10), ch=(10, 0, -16), hd=(-10, 0, -20), r=dict(sw=90, ab=70, el=120, fin=60), l=dict(sw=20, ab=30, el=40)),      # big-arm flex
    P(42, "s"),
    P(INTRO_N, "s", hip=(0, 0, -0.06), sp=(12, 0, 0), ch=(6, 0, 0), hd=(-4, 0, 0), r=dict(sw=4, ab=24, el=14), l=dict(sw=10, ab=18, el=30), w=0.24),
], cfg=dict(pop=[(14, "lr", 0.35)]))

if not ONLY or "SQ_Marker_Remaster5" in ONLY:
    idle = ACT["Idle"]
    act = write_action("SQ_Marker_Remaster5", idle, [eval_pose(idle, 1), eval_pose(idle, 2)])
    act["sq_loop"] = False
    DONE5.append("SQ_Marker_Remaster5")
set_action(ACT["Idle"])
sc.frame_set(1)
bpy.ops.wm.save_as_mainfile(filepath=BLEND)
print("DONE5", len(DONE5))
print("NACTIONS", len(bpy.data.actions))
