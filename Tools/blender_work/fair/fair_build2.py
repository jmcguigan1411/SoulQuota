# Soul Quota: "Midnight Fairground" set pieces, second batch (map quality pass, iterations 6-10, Nov 19 2026).
# Original work; same pipeline and palette as fair_build.py (T_Fair / MI_Fair). usage: python3 fair_build2.py
import sys, os, math, json
sys.path.insert(0, "/home/claude/blender_work/fair")
import bpy
from mathutils import Vector
import fair_build as fb
pb, sb = fb.pb, fb.sb
Piece, box, frustum, ico, tube, F, V = fb.Piece, fb.box, fb.frustum, fb.ico, fb.tube, fb.F, fb.V
cyl, disc_x, gable, pyramid, hbox, gear = fb.cyl, fb.disc_x, fb.gable, fb.pyramid, fb.hbox, fb.gear
lantern, pennants, ring, coffin, stripes_drum, stripes_cone = fb.lantern, fb.pennants, fb.ring, fb.coffin, fb.stripes_drum, fb.stripes_cone
TAU = fb.TAU


def buoy(P, x, y, z, s=1.0, sw="crimson", glow=False):
    """a net float / buoy: a ball with a band and a rope loop"""
    ico(P, (x, y, z), 26 * s, "glow_cool" if glow else sw)
    box(P, (x, y, z), (54 * s, 54 * s, 8 * s), "cream" if not glow else "iron")
    box(P, (x, y, z + 30 * s), (4, 4, 16 * s), "rope")


def buoy_arch():
    """Drowned Harbour gate: a tarred timber gantry with a ship's lantern, net swags and glass floats"""
    P = Piece("Fair_BuoyArch")
    for s in (-1, 1):
        y = s * 430
        box(P, (0, y, 26), (190, 170, 52), "stone_dark", "barnacle")
        for dx in (-64, 64):
            tube(P, [(dx, y + s * 20, 40), (dx * 0.35, y - s * 30, 600)], [26, 18], 6, "hull_dark")
        tube(P, [(0, y, 40), (0, y - s * 36, 640)], [30, 20], 6, "hull")
        for z in (190, 380):
            tube(P, [(-52 + z * 0.04, y, z), (52 - z * 0.04, y, z)], 9, 5, "wood_dark")
        ring(P, (0, y + s * 38, 300), 44, 9, 10, "cream", "y")          # a life ring on the pier
        box(P, (0, y + s * 38, 300), (8, 6, 92), "crimson")
        lantern(P, 70, y - s * 10, 430, 0, 1.4)
        box(P, (44, y - s * 10, 436), (60, 6, 6), "iron")
        P.hull([(x, y + dy, z) for x in (-90, 90) for dy in (-80, 80) for z in (0, 620)])
    tube(P, [(0, -470, 640), (0, 470, 640)], [26, 26], 6, "hull")
    tube(P, [(0, -410, 590), (0, 410, 590)], 12, 5, "wood_dark")
    for y in (-300, -150, 0, 150, 300):
        box(P, (0, y, 615), (16, 12, 40), "iron")
    # the crest: a ship's wheel under a gull-wing gable
    ring(P, (14, 0, 720), 64, 8, 12, "wood_light")
    for k in range(8):
        a = TAU * k / 8
        tube(P, [(14, 0, 720), (14, 84 * math.cos(a), 720 + 84 * math.sin(a))], 5, 4, "wood")
    ico(P, (14, 0, 720), 16, "brass")
    gable(P, (0, 0, 800), (60, 330), 54, "slate", "hull_dark", 14)
    # net swags with floats, a big ship's lantern in the middle
    pts = [(0, -400 + k * 80, 584 - 150 * math.sin(math.pi * k / 10)) for k in range(11)]
    tube(P, pts, 3.5, 4, "rope", cap=False)
    for k in range(1, 10):
        tube(P, [pts[k], (pts[k][0], pts[k][1] + 40, 590)], 2.2, 3, "net", cap=False)
        tube(P, [pts[k], (pts[k][0], pts[k][1] - 40, 590)], 2.2, 3, "net", cap=False)
    for k, sw in ((2, "crimson"), (4, "cream"), (6, "crimson"), (8, "cream")):
        buoy(P, 0, pts[k][1], pts[k][2] - 30, 1.0, sw)
    lantern(P, 0, 0, 580, 120, 2.1, "glow_cool")
    pennants(P, (6, -430, 690), (6, 430, 690), 36, 9, ["crimson", "cream", "slate"], 38)
    return P


def buoy_line():
    """Drowned Harbour lane span: mooring posts with a rope of floats, signal pennants and storm lanterns"""
    P = Piece("Fair_BuoyLine")
    for s in (-1, 1):
        y = s * 470
        cyl(P, (0, y, 0), 34, 26, 60, 8, "barnacle")
        cyl(P, (0, y, 50), 17, 12, 520, 6, "hull_dark", hull=False)
        for z in (170, 330):
            cyl(P, (0, y, z), 20, 20, 20, 6, "rope", hull=False)
        tube(P, [(0, y, 420), (0, y - s * 120, 560)], 7, 4, "hull")             # a yard arm reaching in
        ico(P, (0, y, 590), 22, "glow_cool")
        box(P, (0, y, 566), (30, 30, 8), "iron")
        P.hull([(x, y + dy, z) for x in (-18, 18) for dy in (-18, 18) for z in (0, 570)])
    pts = [Vector((0, -350 + k * 35, 556 - 130 * 4 * (k / 20) * (1 - k / 20))) for k in range(21)]
    tube(P, [tuple(p) for p in pts], 3.2, 4, "rope", cap=False)
    for k in range(2, 19, 2):
        if k in (6, 10, 14):
            lantern(P, 0, pts[k].y, pts[k].z, 40, 1.25, "glow" if k != 10 else "glow_cool")
        else:
            buoy(P, 0, pts[k].y, pts[k].z - 34, 0.85, "crimson" if k % 4 == 0 else "cream")
    pennants(P, (4, -350, 470), (4, 350, 470), 50, 8, ["slate", "cream", "crimson"], 30)
    return P


def cog_line():
    """Clocktower lane span: brass standards with a chain of small cogs and hanging clock-lamps"""
    P = Piece("Fair_CogLine")
    for s in (-1, 1):
        y = s * 470
        box(P, (0, y, 20), (80, 80, 40), "stone_dark")
        cyl(P, (0, y, 40), 14, 10, 520, 6, "brass_dark", hull=False)
        for z in (160, 320, 470):
            cyl(P, (0, y, z), 17, 17, 18, 6, "brass", hull=False)
        gear(P, (0, y, 590), 44, 22, 8, "brass", axis="x")
        P.hull([(x, y + dy, z) for x in (-20, 20) for dy in (-20, 20) for z in (0, 560)])
    pts = [Vector((0, -470 + k * 47, 556 - 110 * 4 * (k / 20) * (1 - k / 20))) for k in range(21)]
    tube(P, [tuple(p) for p in pts], 3.4, 4, "iron", cap=False)
    for k in range(2, 19, 2):
        if k in (6, 10, 14):
            p = pts[k]
            box(P, (0, p.y, p.z - 24), (3, 3, 48), "iron")
            disc_x(P, (0, p.y, p.z - 78), 34, 16, 10, "brass_dark", "brass")       # a little clock-lamp
            disc_x(P, (9, p.y, p.z - 78), 26, 4, 10, "glow_hot")
            disc_x(P, (-9, p.y, p.z - 78), 26, 4, 10, "glow_hot")
        else:
            gear(P, (0, pts[k].y, pts[k].z - 30), 22 if k % 4 else 30, 12, 8, "copper" if k % 4 else "brass", axis="x")
    return P


def carousel():
    """Graveyard signature: the Hearse-Go-Round. A striped carousel whose riders are upright coffins on brass poles."""
    P = Piece("Fair_Carousel")
    r = 430
    cyl(P, (0, 0, 0), r + 40, r + 40, 34, 14, "stone_dark", "stone")
    cyl(P, (0, 0, 34), r, r, 26, 14, "plum_dark", "wood_light")
    cyl(P, (0, 0, 60), 70, 56, 520, 10, "cream", hull=False)
    for z in (150, 300, 450):
        cyl(P, (0, 0, z), 74, 74, 26, 10, "plum", hull=False)
    for k in range(4):   # mirror panels on the hub that glow
        a = TAU * k / 4 + 0.4
        box(P, (62 * math.cos(a), 62 * math.sin(a), 230), (16 if abs(math.cos(a)) > 0.5 else 44, 44 if abs(math.cos(a)) > 0.5 else 16, 110), "glow")
    stripes_drum(P, (0, 0, 560), r + 30, 46, 14, ["cream", "plum"], r + 10)      # scalloped eave
    stripes_cone(P, (0, 0, 606), r + 30, 250, 14, ["plum", "cream"], 40)
    cyl(P, (0, 0, 856), 40, 20, 50, 8, "brass", hull=False)
    box(P, (0, 0, 950), (6, 6, 110), "iron")
    ico(P, (0, 0, 1010), 20, "glow_hot")
    for k in range(7):
        a = TAU * k / 7 + 0.15
        x, y = (r - 90) * math.cos(a), (r - 90) * math.sin(a)
        cyl(P, (x, y, 60), 7, 7, 500, 5, "brass", hull=False)
        bob = 40 * math.sin(k * 2.3)
        # upright coffin rider on the pole (built facing +X then turned to face outward)
        w = [(-24, 0), (-38, -56), (-22, -180), (22, -180), (38, -56), (24, 0)]
        bm = P.part()
        ux, uy = math.cos(a), math.sin(a); tx, ty = -uy, ux
        top = 330 + bob
        fr = [V(bm, (x + ux * 20 + tx * yy, y + uy * 20 + ty * yy, top + zz)) for yy, zz in w]
        bk = [V(bm, (x - ux * 20 + tx * yy, y - uy * 20 + ty * yy, top + zz)) for yy, zz in w]
        F(bm, fr, "wood_light"); F(bm, bk[::-1], "wood")
        for j in range(6):
            F(bm, [fr[j], bk[j], bk[(j + 1) % 6], fr[(j + 1) % 6]], "wood_dark" if k % 2 else "plum_dark")
        box(P, (x + ux * 22, y + uy * 22, top - 78), (8, 8, 86), "bone")
        box(P, (x + ux * 22 + tx * 0, y + uy * 22, top - 58), (8 + abs(tx) * 30, 8 + abs(ty) * 30, 8), "bone")
        lantern(P, (r + 6) * math.cos(a + 0.22), (r + 6) * math.sin(a + 0.22), 556, 26, 1.1, "glow" if k % 3 else "glow_cool")
    for k in range(14):   # bulbs round the eave
        a = TAU * (k + 0.5) / 14
        ico(P, ((r + 34) * math.cos(a), (r + 34) * math.sin(a), 584), 12, "glow_hot")
    P.hull([((r + 30) * math.cos(TAU * k / 12), (r + 30) * math.sin(TAU * k / 12), z) for k in range(12) for z in (0, 620)])
    return P


def diving_bell():
    """Drowned Harbour signature: a salvage gantry with a great brass diving bell hanging over a tide pool"""
    P = Piece("Fair_DivingBell")
    box(P, (0, 0, 24), (520, 760, 48), "stone_dark", "barnacle")
    hbox(P, (0, 0, 80), (520, 760, 160))
    for sy in (-1, 1):
        for sx in (-1, 1):
            tube(P, [(sx * 200, sy * 320, 40), (sx * 40, sy * 150, 900)], [28, 18], 6, "hull_dark")
        tube(P, [(-150, sy * 290, 330), (150, sy * 290, 330)], 10, 5, "wood_dark")
        tube(P, [(-110, sy * 240, 560), (110, sy * 240, 560)], 9, 5, "wood_dark")
    tube(P, [(0, -200, 900), (0, 200, 900)], [30, 30], 6, "hull")
    P.hull([(x, y, z) for x in (-60, 60) for y in (-180, 180) for z in (160, 920)])
    gear(P, (60, 120, 900), 70, 30, 8, "brass_dark", axis="x")            # the winch
    tube(P, [(60, 120, 900), (120, 120, 820)], 8, 4, "iron")
    tube(P, [(0, 0, 900), (0, 0, 650)], 6, 4, "iron")                     # chain
    # the bell (lathe by stacked frusta), portholes that glow
    prof = [(130, 250), (160, 330), (150, 520), (110, 610), (50, 650)]
    for (r0, z0), (r1, z1) in zip(prof[:-1], prof[1:]):
        bm = P.part()
        n = 12
        a_ = [V(bm, (r0 * math.cos(TAU * k / n), r0 * math.sin(TAU * k / n), z0)) for k in range(n)]
        b_ = [V(bm, (r1 * math.cos(TAU * k / n), r1 * math.sin(TAU * k / n), z1)) for k in range(n)]
        for k in range(n):
            F(bm, [a_[k], a_[(k + 1) % n], b_[(k + 1) % n], b_[k]], "brass" if (z0 // 80) % 2 == 0 else "brass_dark")
    cyl(P, (0, 0, 244), 168, 168, 16, 12, "patina", hull=False)
    cyl(P, (0, 0, 420), 158, 158, 14, 12, "patina", hull=False)
    for k in range(4):
        a = TAU * k / 4 + 0.3
        disc = (152 * math.cos(a), 152 * math.sin(a), 470)
        ico(P, disc, 30, "glow_cool")
        ico(P, (160 * math.cos(a), 160 * math.sin(a), 470), 22, "iron", sq=(0.3, 0.3, 1.25)) if False else None
    for sy in (-1, 1):
        lantern(P, 0, sy * 150, 880, 60, 1.4)
    pennants(P, (0, -200, 930), (0, -330, 60), 20, 5, ["crimson", "cream", "slate"], 34)
    return P


def orrery():
    """Clocktower signature: a great brass orrery. Tilted rings round a lit sun, planets on arms, on a clockwork drum."""
    P = Piece("Fair_Orrery")
    cyl(P, (0, 0, 0), 330, 330, 44, 12, "stone_dark", "stone")
    cyl(P, (0, 0, 44), 250, 230, 150, 12, "brass_dark", "brass")
    for k in range(6):
        a = TAU * k / 6
        box(P, (238 * math.cos(a), 238 * math.sin(a), 120), (30, 30, 90), "glow" if k % 2 == 0 else "copper")
    gear(P, (0, 0, 194), 240, 26, 14, "brass", axis="z")
    cyl(P, (0, 0, 220), 40, 30, 520, 8, "brass_dark", hull=False)
    hbox(P, (0, 0, 110), (560, 560, 220))
    P.hull([(x, y, z) for x in (-40, 40) for y in (-40, 40) for z in (200, 760)])
    c = (0, 0, 780)
    ico(P, c, 86, "glow_hot", subd=2)
    ring(P, c, 300, 12, 20, "brass", "x")
    ring(P, c, 360, 10, 22, "copper", "y")
    # a tilted third ring
    pts = []
    for k in range(25):
        a = TAU * k / 24
        pts.append((c[0] + 430 * math.cos(a), c[1] + 430 * math.sin(a) * 0.86, c[2] + 430 * math.sin(a) * 0.5))
    tube(P, pts, 11, 6, "brass_dark", cap=False)
    for k, (rr, sw, s) in enumerate(((300, "patina", 34), (360, "copper", 26), (430, "cream", 44), (430, "crimson", 22))):
        a = 0.7 + k * 1.9
        if k < 2:
            p = (c[0], c[1] + rr * math.cos(a), c[2] + rr * math.sin(a)) if k == 0 else (c[0] + rr * math.cos(a), c[1], c[2] + rr * math.sin(a))
        else:
            p = (c[0] + rr * math.cos(a), c[1] + rr * math.sin(a) * 0.86, c[2] + rr * math.sin(a) * 0.5)
        ico(P, p, s, sw)
        tube(P, [c, p], 4, 4, "iron")
    for k in range(3):
        a = TAU * k / 3 + 0.5
        lantern(P, 270 * math.cos(a), 270 * math.sin(a), 330, 0, 1.3)
        box(P, (255 * math.cos(a), 255 * math.sin(a), 290), (12, 12, 90), "iron")
    return P


def stairs():
    """A broad flight of stone steps with lantern newels: faces a terrace edge (front +X = the low side)"""
    P = Piece("Fair_Stairs")
    n = 8
    for k in range(n):
        box(P, (-200 + k * 50 + 25, 0, (n - k) * 28 - (n - k) * 14), (50, 520, (n - k) * 28), "stone_light" if k % 2 == 0 else "stone")
    box(P, (-250, 0, n * 14), (100, 560, n * 28), "stone", "stone_light")
    for s in (-1, 1):
        frustum(P, (-300, 200, s * 290 - 34, s * 290 + 34), (-300, -180, s * 290 - 34, s * 290 + 34), 0, n * 28 + 40, "stone_dark", "stone_dark", hull=False)
        box(P, (210, s * 290, 60), (80, 80, 120), "stone_dark")
        box(P, (210, s * 290, 126), (96, 96, 12), "stone_light")
        lantern(P, 210, s * 290, 200, 0, 1.5)
        box(P, (-270, s * 290, n * 28 + 60), (70, 70, 120), "stone_dark")
        lantern(P, -270, s * 290, n * 28 + 190, 0, 1.3)
    # ride hulls: a ramp wedge so reapers and the horde walk it
    P.hull([(200, -260, 0), (200, 260, 0), (-200, -260, n * 28), (-200, 260, n * 28), (-300, -260, n * 28), (-300, 260, n * 28), (-300, -260, 0), (-300, 260, 0)])
    return P


def chimney():
    """Clocktower: a foundry smokestack (the brass town's "tree"): brick-banded copper stack, glowing vents, a cowl"""
    P = Piece("Fair_Chimney")
    box(P, (0, 0, 40), (190, 190, 80), "stone_dark", "stone")
    cyl(P, (0, 0, 80), 74, 52, 760, 8, "copper_dark", hull=False)
    for k, z in enumerate((150, 330, 510, 690)):
        cyl(P, (0, 0, z), 76 - k * 5.5, 76 - k * 5.5, 26, 8, "brass_dark" if k % 2 else "brass", hull=False)
    for a in (0.4, 2.5, 4.6):
        box(P, (62 * math.cos(a), 62 * math.sin(a), 240), (26, 26, 60), "glow")
    cyl(P, (0, 0, 840), 60, 66, 40, 8, "iron", hull=False)
    cyl(P, (0, 0, 880), 30, 30, 50, 6, "iron", hull=False)
    pyramid(P, (0, 0, 930), 150, 50, "slate")
    tube(P, [(40, 0, 420), (150, 0, 470), (150, 0, 640)], 14, 6, "copper")          # a side pipe with a lamp
    lantern(P, 150, 0, 640, 0, 1.3)
    P.hull([(x, y, z) for x in (-78, 78) for y in (-78, 78) for z in (0, 860)])
    return P


def gear_post():
    """Clocktower: a signal post. A turning-cog head on a brass standard with two clock-lamps (the avenue piece)"""
    P = Piece("Fair_GearPost")
    box(P, (0, 0, 22), (110, 110, 44), "stone_dark", "stone")
    cyl(P, (0, 0, 44), 20, 14, 420, 6, "brass_dark", hull=False)
    for z in (150, 300):
        cyl(P, (0, 0, z), 24, 24, 20, 6, "brass", hull=False)
    gear(P, (0, 0, 540), 78, 26, 10, "brass", axis="x")
    gear(P, (16, 70, 452), 40, 18, 8, "copper", axis="x")
    for s in (-1, 1):
        tube(P, [(0, 0, 400), (0, s * 90, 430), (0, s * 110, 400)], 6, 5, "iron")
        lantern(P, 0, s * 110, 400, 20, 1.2)
    P.hull([(x, y, z) for x in (-30, 30) for y in (-30, 30) for z in (0, 600)])
    return P


def mast():
    """Drowned Harbour: a salvaged ship's mast stepped ashore as a signal mast: yard, furled sail, lantern, pennants"""
    P = Piece("Fair_Mast")
    cyl(P, (0, 0, 0), 60, 60, 50, 8, "barnacle", "stone")
    cyl(P, (0, 0, 50), 22, 12, 980, 6, "hull", hull=False)
    tube(P, [(0, -260, 700), (0, 260, 700)], 10, 5, "hull_dark")
    tube(P, [(0, -170, 880), (0, 170, 880)], 8, 5, "hull_dark")
    for y0, y1 in ((-240, -20), (20, 240)):   # furled sail bundles on the yard
        tube(P, [(0, y0, 678), (0, y1, 678)], 22, 6, "canvas")
        for y in (y0 + 50, y1 - 50):
            tube(P, [(0, y - 4, 678), (0, y + 4, 678)], 25, 6, "rope")
    for s in (-1, 1):   # stays
        tube(P, [(0, s * 250, 700), (0, s * 40, 40)], 2.5, 3, "rope", cap=False)
        lantern(P, 0, s * 250, 692, 40, 1.2, "glow" if s > 0 else "glow_cool")
    box(P, (0, 0, 560), (90, 90, 14), "wood_dark")         # the top (crow's nest floor)
    cyl(P, (0, 0, 566), 50, 58, 70, 8, "wood", hull=False)
    pennants(P, (0, 0, 1020), (0, 150, 960), 10, 2, ["crimson", "cream"], 40)
    P.hull([(x, y, z) for x in (-26, 26) for y in (-26, 26) for z in (0, 900)])
    return P


PIECES = [buoy_arch, buoy_line, cog_line, carousel, diving_bell, orrery, stairs, chimney, gear_post, mast]

if __name__ == "__main__":
    only = sys.argv[1:] if len(sys.argv) > 1 else None
    report = json.load(open("/home/claude/blender_work/fair/report.json"))
    for fn in PIECES:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        img = bpy.data.images.load(fb.PAL)
        mat = pb.make_material(img)
        P = fn()
        if only and P.name not in only:
            continue
        ob, hulls, off = pb.build_objects(P, mat)
        fb.glow_slot(ob, fb.LAYOUT)
        pb.export(ob, hulls, fb.OUT + P.name + ".fbx")
        pb.P_NAME[0] = P.name
        st = pb.stats(ob, hulls)
        report[P.name] = st
        print("PIECE", P.name, st["size_cm"], st["tris"], st["hulls"], [round(off.x, 1), round(off.y, 1)])
    json.dump(report, open("/home/claude/blender_work/fair/report.json", "w"), indent=1)
