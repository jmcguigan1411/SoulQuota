# Soul Quota - reapers batch 3 (original meshes on the Reaper rig, mesh only).
# usage: python3 rq_chars.py [Actuary Sweep Solace Pallbearers Ectoplasm Neckless Tickwell]
import bpy, math, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mathutils import Vector, Matrix
from rq_lib import *

H = hexc
BONE = dict(bone=H("F2EBDD"), bone_shade=H("CFC6B2"), socket=H("120E18"))

# ================================================================ THE ACTUARY
PAL = {}
PAL["Actuary"] = dict(BONE, glow=H("FF6A4A"), glow2=H("FFD26A"),
                      cloth=H("5E4464"), cloth_dark=H("3E2C42"), cloth_inner=H("1E1820"),     # plum waistcoat
                      cloth2=H("3A2F3A"), cloth2_dark=H("261E26"),                           # trousers
                      shirt=H("F4F1EA"), shirt_dark=H("C9C4BA"), accent=H("B22234"), accent_dark=H("7A1622"),
                      metal=H("9A9AA4"), metal_dark=H("55555E"), gold=H("FFC94A"), gold_dark=H("B8862A"),
                      wood=H("8A5E38"), wood_dark=H("5A3A22"), leather=H("6A4428"), shoe=H("1C1A1E"), sole=H("0C0B0D"),
                      hair=H("E8C040"), ink=H("1A1A22"), paper=H("F8F2DE"), glass=H("2F8A4A"),
                      extra1=H("1F6A38"), extra2=H("E05A6A"))

def build_actuary():
    # trousers + shoes
    pelvis("cloth2", rx=.27, ry=.22, z0=0.46, z1=0.66)
    for side, s in (("l", 1), ("r", -1)):
        leg(side, s, "cloth2", r=(.11, .104, .094, .09), cuff_z=0.17, shoe_scale=1.12)
    # torso: shirt with a plum waistcoat on top, red tie
    t = Part("shirt", TORSO_W)
    spec = [(0.60, .265, .215), (0.78, .29, .228), (0.95, .295, .232), (1.08, .285, .225), (1.18, .23, .19), (1.25, .11, .10)]
    loft(t, [hring(z, rx, ry, 12) for z, rx, ry in spec], "shirt")
    v = Part("waistcoat", TORSO_W)
    vs = [(0.57, .278, .228), (0.78, .303, .241), (0.95, .307, .245), (1.10, .296, .236)]
    rings = [hring(z, rx, ry, 12) for z, rx, ry in vs]
    # pointed front hem
    rings[0][0] = rings[0][0] - Vector((0, 0, 0.05))
    g, faces, caps = loft(v, rings, "cloth", cap0=False, cap1=False)
    # open V at the front top: drop the top-front quads so the shirt and tie show
    for f in list(faces):
        c = f.calc_center_median()
        if c.y < -0.12 and abs(c.x) < 0.09 and c.z > 0.94:
            v.bm.faces.remove(f)
    v.recolor(lambda c, n, cur: n.z < -0.5, "cloth_dark")
    for z in (0.70, 0.79, 0.88):
        ell(v, (0, -0.245, z), (.02, .014, .02), 5, 3, "gold")
    # watch-chain across the waistcoat
    tube(v, [(-0.17, -0.225, 0.82), (-0.07, -0.248, 0.77), (0.0, -0.248, 0.80)], .008, 3, "gold")
    # tie
    q = Part("tie", rigid("chest"))
    box(q, (0, -0.205, 1.18), (.07, .04, .06), "accent_dark", 0.012)
    slab(q, [(-0.035, -0.215, 1.16), (0.035, -0.215, 1.16), (0.05, -0.232, 0.99), (0.0, -0.236, 0.95), (-0.05, -0.232, 0.99)],
         (0, -0.02, 0), "accent")
    # collar points
    for sx in (1, -1):
        slab(q, [(0.0, -0.19, 1.21), (0.11 * sx, -0.16, 1.235), (0.09 * sx, -0.19, 1.15)], (0, -0.02, 0), "shirt")
    # belt (on hips) + stamp holster on the right hip + abacus on the left hip
    b = Part("belt", rigid("hips"))
    loft(b, [hring(0.58, .285, .235, 12), hring(0.635, .285, .235, 12)], "leather", cap0=False, cap1=False)
    box(b, (0, -0.238, 0.607), (.07, .02, .05), "gold", 0.008)
    # holster: leather pouch with three stamps (red knobs)
    hx, hy = -0.25, -0.16
    box(b, (hx, hy, 0.52), (.10, .13, .16), "leather", 0.02, ROT(-28, 'Z'))
    for k, (dx, dy) in enumerate(((-0.01, -0.04), (0.012, 0.0), (-0.01, 0.04))):
        c = Vector((hx + dx, hy + dy, 0.62))
        cyl(b, c, c + Vector((0, 0, 0.07)), .014, .014, 5, "wood_dark")
        ell(b, c + Vector((0, 0, 0.09)), (.03, .03, .028), 6, 4, "accent")
    # abacus: wooden frame, three rods, gold beads; hangs off the belt on the left hip, angled to the front
    a = Part("abacus", rigid("hips"))
    R = ROT(-38, 'Z')
    ac = Vector((0.27, -0.20, 0.50))
    W_, H_ = 0.36, 0.26
    for dz in (H_ / 2, -H_ / 2):
        box(a, ac + R @ Vector((0, 0, dz)), (W_ + 0.03, .04, .035), "wood", 0.008, R)
    for dx in (W_ / 2, -W_ / 2):
        box(a, ac + R @ Vector((dx, 0, 0)), (.035, .04, H_ + 0.03), "wood", 0.008, R)
    for i, dz in enumerate((-0.06, 0.0, 0.06)):
        cyl(a, ac + R @ Vector((-W_ / 2, 0, dz)), ac + R @ Vector((W_ / 2, 0, dz)), .006, .006, 4, "wood_dark")
        for j, dx in enumerate((-0.10, -0.06, 0.03, 0.07, 0.11)[i % 2:i % 2 + 4]):
            ell(a, ac + R @ Vector((dx, 0, dz)), (.02, .022, .022), 6, 3, "gold" if j % 2 == 0 else "accent")
    tube(a, [(0.22, -0.21, 0.60), ac + R @ Vector((-W_ / 2 + 0.02, 0, H_ / 2))], .01, 3, "leather")
    # arms: white shirt sleeves, red sleeve garters, rolled cuffs
    for side, s in (("l", 1), ("r", -1)):
        arm(side, s, rad=(.09, .086, .082, .08, .075), sleeve_sw="shirt", cuff_sw="shirt_dark", cuff_r=.088)
        g_ = Part("garter_" + side, rigid("upperarm." + side))
        tube(g_, [(0.33 * s, 0, ZC), (0.38 * s, 0, ZC)], [.097, .097], 8, "accent")
        # shoulder caps (waistcoat edge)
        c = Part("shoulder_" + side, ablend(0, [(0.16, {"chest": 1}), (0.30, {"chest": .4, "upperarm." + side: .6})], s))
        ell(c, (0.21 * s, 0, 1.13), (.12, .14, .10), 8, 4, "shirt")
    neck(0.05)
    # ---- head: big skull, green eyeshade visor, pencil tucked behind the band, little round spectacles
    hc = Vector((0, -0.02, 1.62))
    p, fy = skull(hc, size=(.58, .52, .52), eye_r=.088, pupil_sw="glow")
    h = Part("visor", rigid("head"))
    # band around the head
    loft(h, [hring(1.74, .30, .27, 14, cy=-0.02), hring(1.80, .295, .265, 14, cy=-0.02)], "extra1", cap0=False, cap1=False)
    # translucent-looking green brim angled down over the eyes
    brim = [Vector((x, y, 0)) for x, y in ((-0.30, -0.18), (-0.24, -0.40), (0.0, -0.47), (0.24, -0.40), (0.30, -0.18))]
    pts = [Vector((v.x, v.y, 1.785 + (v.y + 0.18) * 0.35)) for v in brim]
    slab(h, pts, (0, 0, 0.018), "glass")
    # pencil
    cyl(h, (-0.33, -0.05, 1.72), (-0.30, 0.18, 1.80), .016, .016, 6, "hair")
    cone(h, (-0.33, -0.05, 1.72), (-0.338, -0.11, 1.70), .016, 6, "wood")
    # spectacles: two thin gold rings
    for sx in (1, -1):
        torus(h, (0.12 * sx, fy - 0.03, 1.63), .072, .009, 10, 4, "gold", normal=Vector((0, 1, 0)))
    box(h, (0, fy - 0.035, 1.645), (.08, .012, .012), "gold", 0)

# ================================================================ THE CHIMNEY SWEEP
PAL["Sweep"] = dict(BONE, bone=H("D8D0C2"), bone_shade=H("A8A094"), glow=H("FF9A3A"), glow2=H("FFD26A"),
                    cloth=H("34343C"), cloth_dark=H("22222A"), cloth_inner=H("0E0E10"),      # coat
                    cloth2=H("55555E"), cloth2_dark=H("3A3A42"),                             # trousers
                    shirt=H("6A6A72"), shirt_dark=H("4A4A52"), accent=H("C8323A"), accent_dark=H("8A1E26"),
                    metal=H("9A9AA4"), metal_dark=H("55555E"), gold=H("D8A84A"), gold_dark=H("8A6A2A"),
                    wood=H("7A5A3A"), wood_dark=H("4A3424"), leather=H("4A3628"), shoe=H("2A2220"), sole=H("0E0C0C"),
                    hair=H("141418"), ink=H("0A0A0C"), paper=H("F8F2DE"), glass=H("1A1A1E"),
                    extra1=H("1A1A1E"), extra2=H("FFB43A"), extra3=H("3A3A42"), extra4=H("5A5A62"))

def crow(c, facing, wfn, name, k=1.0):
    """little crow sitting on a shoulder; facing = unit XY vector its beak points to"""
    q = Part(name, wfn)
    c = Vector(c)
    f = Vector((facing[0], facing[1], 0)).normalized()
    side = Vector((-f.y, f.x, 0))
    R = Matrix((side, f, Vector((0, 0, 1)))).transposed()
    S = Matrix.Diagonal((k, k, k))
    _q0 = len(q.bm.verts)
    body = ell(q, c, (.09, .13, .10), 8, 5, "extra1", R)
    ell(q, c + f * 0.10 + Vector((0, 0, 0.10)), (.075, .08, .075), 8, 5, "extra1")
    hcn = c + f * 0.10 + Vector((0, 0, 0.10))
    cone(q, hcn + f * 0.06, hcn + f * 0.17 + Vector((0, 0, -0.02)), .028, 4, "extra2")
    for sx in (1, -1):
        ell(q, hcn + f * 0.045 + side * (0.055 * sx) + Vector((0, 0, 0.02)), (.018, .018, .018), 4, 3, "glow2")
        # folded wings
        slab(q, [c + side * (0.07 * sx) + f * 0.07 + Vector((0, 0, 0.03)), c + side * (0.075 * sx) - f * 0.14 + Vector((0, 0, 0.0)),
                 c + side * (0.06 * sx) - f * 0.02 + Vector((0, 0, -0.05))], side * (0.012 * sx), "extra3")
    # tail
    slab(q, [c - f * 0.08 + Vector((0, 0, 0.0)), c - f * 0.22 + side * 0.05 + Vector((0, 0, -0.05)), c - f * 0.22 - side * 0.05 + Vector((0, 0, -0.05))],
         (0, 0, 0.015), "extra1")
    for sx in (1, -1):
        cyl(q, c + side * (0.03 * sx) + Vector((0, 0, -0.07)), c + side * (0.03 * sx) + f * 0.02 + Vector((0, 0, -0.11)), .008, .008, 3, "extra2")
    for v in q.bm.verts:
        v.co = c + (v.co - c) * k

def build_sweep():
    pelvis("cloth2", rx=.21, ry=.18, z0=0.46, z1=0.66)
    for side, s in (("l", 1), ("r", -1)):
        # rolled-up trousers, big clumpy boots
        leg(side, s, "cloth2", r=(.095, .09, .085, .095), cuff_z=0.24, shoe_scale=1.18, boot=True)
        rc = Part("roll_" + side, segw(["upperleg." + side, "lowerleg." + side], k=6))
        tube(rc, [(0.171 * s, 0, 0.27), (0.171 * s, 0, 0.21)], [.105, .105], 7, "cloth2_dark")
    # lean torso, sooty grey shirt, black tailcoat with tails
    t = Part("shirt", TORSO_W)
    spec = [(0.60, .20, .17), (0.80, .215, .175), (0.98, .23, .185), (1.12, .225, .18), (1.21, .17, .15), (1.27, .09, .085)]
    loft(t, [hring(z, rx, ry, 12) for z, rx, ry in spec], "shirt")
    c_ = Part("coat", TORSO_W)
    cs = [(0.62, .218, .19), (0.82, .232, .195), (1.0, .247, .202), (1.14, .24, .197), (1.22, .19, .165)]
    rings = [hring(z, rx, ry, 12) for z, rx, ry in cs]
    g, faces, caps = loft(c_, rings, "cloth", cap0=False, cap1=False)
    for f in list(faces):
        cc = f.calc_center_median()
        if cc.y < -0.10 and abs(cc.x) < 0.08:
            c_.bm.faces.remove(f)
    c_.recolor(lambda c, n, cur: n.z < -0.5, "cloth_dark")
    # patches + soot smudges
    box(c_, (0.15, 0.18, 0.95), (.10, .03, .10), "extra4", 0.0, ROT(8, 'Y'))
    for z in (0.75, 0.88):
        ell(c_, (-0.085, -0.19, z), (.017, .012, .017), 5, 3, "gold_dark")
    # coat tails (split, each follows its leg a little)
    for sx in (1, -1):
        tl = Part("tail_%d" % sx, skirt_w(0.62, 0.30, leg_max=0.55))
        pts = [(0.02 * sx, 0.19, 0.64), (0.20 * sx, 0.16, 0.64), (0.19 * sx, 0.19, 0.30), (0.04 * sx, 0.21, 0.36)]
        slab(tl, pts, (0, 0.025, 0), "cloth", "cloth_dark")
    # red neckerchief
    q = Part("scarf", rigid("chest"))
    loft(q, [hring(1.20, .17, .15, 10), hring(1.27, .14, .13, 10)], "accent", cap0=False, cap1=False)
    slab(q, [(-0.02, -0.15, 1.24), (0.10, -0.14, 1.22), (0.04, -0.20, 1.08)], (0, -0.02, 0), "accent")
    # crows on both shoulders (sit on the chest, just inside the shoulder joint)
    crow((0.33, 0.06, 1.33), (0.45, -0.89), lambda co: {"chest": .5, "upperarm.l": .5}, "crow_l", k=1.3)
    crow((-0.33, 0.06, 1.33), (-0.45, -0.89), lambda co: {"chest": .5, "upperarm.r": .5}, "crow_r", k=1.3)
    for side, s in (("l", 1), ("r", -1)):
        arm(side, s, rad=(.08, .076, .074, .072, .068), sleeve_sw="cloth", cuff_sw="cloth2_dark", cuff_r=.085)
        c = Part("shoulder_" + side, ablend(0, [(0.16, {"chest": 1}), (0.30, {"chest": .4, "upperarm." + side: .6})], s))
        ell(c, (0.19 * s, 0, 1.13), (.09, .11, .08), 8, 4, "cloth")
    neck(0.05)
    # ---- head: sooty skull + tall battered top hat (kinked crown, patch, red band)
    hc = Vector((0, -0.02, 1.60))
    p, fy = skull(hc, size=(.54, .50, .50), eye_r=.085, pupil_sw="glow")
    # soot smudges on the cheeks
    for sx in (1, -1):
        ell(p, (0.16 * sx, fy + 0.01, 1.53), (.05, .015, .03), 5, 3, "shirt")
    h = Part("hat", rigid("head"))
    R = ROT(-9, 'Y')
    o = Vector((0.02, -0.0, 1.80))
    loft(h, [[o + R @ (v - Vector((0, 0, 0))) for v in hring(0, .34, .31, 14)],
             [o + R @ v for v in hring(0.035, .34, .31, 14)]], "hair")      # brim
    crown = []
    for i, (z, r, dx) in enumerate(((0.03, .22, 0.0), (0.18, .225, 0.015), (0.27, .205, 0.07), (0.36, .215, 0.03), (0.47, .235, -0.04))):
        crown.append([o + R @ (v + Vector((dx, 0, 0))) for v in hring(z, r, r * 0.92, 12)])
    g, faces, caps = loft(h, crown, "hair")
    h.tag(faces[:12], "accent")
    h.tag(caps[-1:], "extra3")
    # dent on top and a patch
    box(h, o + R @ Vector((0.0, -0.205, 0.30)), (.10, .03, .09), "extra4", 0.0, R)
    # loose brush bristles tucked in the band
    for k, a in enumerate((-0.4, -0.2, 0.0)):
        cone(h, o + R @ Vector((0.20, 0.06 + a * 0.15, 0.08)), o + R @ Vector((0.30, 0.10 + a * 0.3, 0.30 + k * 0.03)), .02, 3, "wood_dark")

# ================================================================ SISTER SOLACE
PAL["Solace"] = dict(BONE, glow=H("C9A2FF"), glow2=H("FFD86A"),
                     cloth=H("EDE6F5"), cloth_dark=H("C4B8D8"), cloth_inner=H("3A2E4A"),      # habit
                     cloth2=H("D2BCEC"), cloth2_dark=H("A88CCC"),                             # veil (lavender)
                     shirt=H("FFFFFF"), shirt_dark=H("DCD6E4"), accent=H("B898DC"), accent_dark=H("8460B4"),
                     metal=H("A8A0B8"), metal_dark=H("6A6278"), gold=H("FFC94A"), gold_dark=H("B8862A"),
                     wood=H("8A6A4A"), wood_dark=H("5A4430"), leather=H("D8C090"), shoe=H("3A2E4A"), sole=H("1A1420"),
                     hair=H("2A2030"), ink=H("1A1420"), paper=H("F8F2DE"), glass=H("C9A2FF"),
                     extra1=H("5A3A8A"), extra2=H("FFE9A8"))

def build_solace():
    # stockinged legs + little shoes (mostly hidden by the habit)
    for side, s in (("l", 1), ("r", -1)):
        leg(side, s, "cloth_inner", r=(.07, .066, .06, .056), cuff_z=0.14, ankle=False, shoe_scale=1.0)
    # bodice
    t = Part("bodice", TORSO_W)
    spec = [(0.60, .25, .21), (0.80, .27, .22), (0.98, .275, .225), (1.12, .265, .215), (1.22, .21, .18), (1.28, .11, .10)]
    loft(t, [hring(z, rx, ry, 12) for z, rx, ry in spec], "cloth")
    # long habit skirt: hips at the waist -> each half follows its leg toward the hem (the shin pulls the hem)
    sk = Part("skirt", skirt_w(0.62, 0.18, leg_max=0.75, low_leg=0.35))
    n = 16
    sspec = [(0.70, .262, .218), (0.52, .31, .26), (0.34, .36, .30), (0.17, .40, .34)]
    rings = [hring(z, rx, ry, n) for z, rx, ry in sspec]
    g, faces, caps = loft(sk, rings, "cloth", cap0=False, cap1=False)
    sk.tag(faces[-n:], "accent")      # hem band
    shell(sk, faces[-2 * n:], lambda co: Vector((co.x * .9, co.y * .9, co.z + 0.01)), "cloth_inner", "accent_dark")
    # rope belt + soul locket
    b = Part("belt", rigid("hips"))
    loft(b, [hring(0.64, .27, .225, 14), hring(0.68, .272, .227, 14)], "leather", cap0=False, cap1=False)
    tube(b, [(0.10, -0.21, 0.64), (0.12, -0.25, 0.52), (0.11, -0.26, 0.42)], .015, 4, "leather")
    tube(b, [(0.13, -0.20, 0.64), (0.17, -0.235, 0.53), (0.18, -0.25, 0.45)], .015, 4, "leather")
    # capelet over the shoulders (lavender trim)
    c_ = Part("capelet", rigid("chest"))
    hem = [0, .03, 0, .03, 0, .03, 0, .03, 0, .03, 0, .03, 0, .03, 0, .03]
    r0 = hring(1.30, .15, .14, 16)
    r1 = hring(1.18, .32, .27, 16)
    r2 = [co - Vector((0, 0, hem[k])) for k, co in enumerate(hring(1.00, .37, .30, 16))]
    g, faces, caps = loft(c_, [r0, r1, r2], "cloth2", cap0=False, cap1=False)
    c_.tag(faces[16:], "cloth2")
    shell(c_, faces, lambda co: Vector((co.x * .92, co.y * .92, co.z - 0.01)), "cloth_inner", "accent")
    # locket on the capelet
    torus(c_, (0, -0.315, 1.09), .045, .012, 10, 4, "gold", normal=Vector((0, 1, 0)))
    ell(c_, (0, -0.315, 1.09), (.035, .015, .035), 6, 4, "glow")
    # arms: bell sleeves + hands, the hymn book held in the left hand
    for side, s in (("l", 1), ("r", -1)):
        up, lo = "upperarm." + side, "lowerarm." + side
        a = Part("sleeve_up_" + side, ablend(0, [(0.20, {"chest": .5, up: .5}), (0.30, {up: 1})], s))
        loft(a, [ring((x * s, 0, ZC), (0, -1, 0), (0, 0, 1), r, r, 8) for x, r in ((0.16, .11), (0.30, .11), (0.46, .105))], "cloth")
        bb = Part("sleeve_lo_" + side, rigid(lo))
        r0 = ring((0.43 * s, 0, ZC), (0, -1, 0), (0, 0, 1), .105, .105, 10)
        r1 = ring((0.58 * s, 0, ZC - 0.01), (0, -1, 0), (0, 0, 1), .14, .14, 10)
        r2 = ring((0.70 * s, 0, ZC - 0.03), (0, -1, 0), (0, 0, 1), .17, .17, 10)
        g, faces, caps = loft(bb, [r0, r1, r2], "cloth", cap0=True, cap1=False)
        bb.tag(faces[-10:], "accent")
        shell(bb, faces, lambda co, s=s: Vector((co.x, co.y * .8, ZC + (co.z - ZC) * .8)), "cloth_inner", "accent_dark")
        w = Part("wrist_" + side, segw([lo, "wrist." + side, "hand." + side], k=6))
        tube(w, [(0.64 * s, 0, ZC), (0.82 * s, 0, ZC)], .034, 5, "bone")
        hand(side, s)
    bk = Part("book", rigid("hand.l"))
    bc = Vector((0.87, -0.15, ZC - 0.01))
    box(bk, bc, (.24, .07, .18), "extra1", 0.012)
    box(bk, bc + Vector((0.006, -0.0, 0.0)), (.225, .075, .165), "glow", 0.0)
    box(bk, bc + Vector((0, -0.04, 0)), (.07, .012, .07), "gold", 0.01)
    for dx in (-0.1, 0.1):
        for dz in (-0.07, 0.07):
            box(bk, bc + Vector((dx, -0.036, dz)), (.035, .01, .035), "gold", 0.0)
    neck(0.05)
    # ---- head: big rounded veil, white wimple framing a round bone face, floating halo
    hc = Vector((0, -0.01, 1.63))
    v = Part("veil", rigid("head"))
    sbox(v, hc + Vector((0, 0.035, 0.0)), (.68, .60, .58), 12, 8, "cloth2", power=0.75)
    v.recolor(lambda c, n, cur: n.z < -0.6, "cloth2_dark")
    # drape down the back to the shoulders
    d = Part("drape", zblend([(1.24, {"chest": 1}), (1.36, {"chest": .5, "head": .5}), (1.46, {"head": 1})]))
    rings = [hring(1.50, .325, .29, 14, cy=0.06), hring(1.34, .33, .27, 14, cy=0.09), hring(1.16, .35, .22, 14, cy=0.13)]
    g, faces, caps = loft(d, rings, "cloth2", cap0=False, cap1=False)
    for f in list(faces):
        if f.calc_center_median().y < -0.0:
            d.bm.faces.remove(f)
            faces.remove(f)
    shell(d, faces, lambda co: Vector((co.x * .9, co.y * .9 + 0.01, co.z)), "cloth2_dark", "cloth2_dark")
    # wimple + face
    fy = hc.y - 0.30
    torus(v, (0, fy - 0.015, 1.60), .225, .05, 14, 5, "shirt", normal=Vector((0, 1, 0)))
    f_ = Part("face", rigid("head"))
    ell(f_, (0, fy + 0.0, 1.60), (.225, .08, .235), 10, 6, "bone")
    for sx in (1, -1):
        ell(f_, (0.085 * sx, fy - 0.06, 1.625), (.05, .02, .062), 6, 4, "socket")
        ell(f_, (0.085 * sx, fy - 0.076, 1.63), (.019, .01, .024), 4, 3, "glow")
        ell(f_, (0.14 * sx, fy - 0.05, 1.54), (.034, .012, .02), 5, 3, "accent")   # rosy cheeks
    ell(f_, (0, fy - 0.07, 1.565), (.016, .01, .02), 4, 3, "socket")
    box(f_, (0, fy - 0.065, 1.51), (.08, .01, .016), "socket", 0.004)
    # halo floating over the veil
    hl = Part("halo", rigid("head"))
    o = Vector((0, 0.03, 2.02))
    torus(hl, o, .22, .03, 16, 5, "glow2", normal=ROT(-12, 'X') @ Vector((0, 0, 1)))

# ================================================================ THE PALLBEARERS
PAL["Pallbearers"] = dict(BONE, glow=H("FFD26A"), glow2=H("FFE9A8"),
                          cloth=H("26232A"), cloth_dark=H("17151A"), cloth_inner=H("0C0B0E"),   # suits
                          cloth2=H("1E1C22"), cloth2_dark=H("121115"),
                          shirt=H("F4F1EA"), shirt_dark=H("CFC9BF"), accent=H("8A1E2A"), accent_dark=H("5A121A"),
                          metal=H("A8A8B0"), metal_dark=H("5A5A62"), gold=H("E2B04A"), gold_dark=H("9A7428"),
                          wood=H("8A603C"), wood_dark=H("5A3C26"), leather=H("8A6A48"), shoe=H("151417"), sole=H("070708"),
                          hair=H("141218"), ink=H("0A0A0C"), paper=H("F8F2DE"), glass=H("2A2A30"),
                          extra1=H("F6F2E6"), extra2=H("3E7A34"), extra3=H("C2303C"), extra4=H("FFFFFF"))

FIG_X = 0.245
# the coffin crew's upper bodies ride a steadier blend than the bare chest bone, so hunched idles and leans
# tip the coffin a little instead of standing it on end
UPPER = {"hips": 0.45, "spine": 0.2, "chest": 0.35}

def chest_remap(fn):
    def g(co):
        w = dict(fn(co))
        c = w.pop("chest", 0.0)
        for b, v in UPPER.items():
            w[b] = w.get(b, 0) + c * v
        return w
    return g

def build_pallbearers():
    for side, s in (("l", 1), ("r", -1)):
        ul, ll = "upperleg." + side, "lowerleg." + side
        cx = FIG_X * s
        # two little legs per figure, both on this side's leg chain (the pair marches in step)
        for k, dx in enumerate((-0.07, 0.07)):
            leg(side, s, "cloth2", r=(.062, .058, .054, .052), cuff_z=0.17, x=abs(cx + dx * s), shoe_scale=0.72,
                name="leg%d" % k, top_z=0.56)
        # pelvis + jacket: torso rides hips -> spine -> chest
        p = Part("fig_pelvis_" + side, rigid("hips"))
        loft(p, [hring(z, rx, ry, 10, cx=cx) for z, rx, ry in ((0.48, .13, .11), (0.56, .14, .12), (0.64, .14, .12))], "cloth2")
        j = Part("jacket_" + side, chest_remap(TORSO_W))
        spec = [(0.50, .145, .125), (0.66, .15, .13), (0.84, .155, .135), (1.00, .16, .135), (1.10, .15, .125), (1.16, .10, .09)]
        rings = [hring(z, rx, ry, 10, cx=cx) for z, rx, ry in spec]
        hem = [0, .02, .05, .02, 0, 0, 0, .02, .05, .02]
        rings[0] = [co - Vector((0, 0, hem[i])) for i, co in enumerate(rings[0])]
        g, faces, caps = loft(j, rings, "cloth", cap0=True, cap1=True)
        j.recolor(lambda c, n, cur: n.z < -0.5, "cloth_dark")
        # white shirt front + black tie + white carnation
        slab(j, [(cx - 0.05, -0.128, 1.14), (cx + 0.05, -0.128, 1.14), (cx + 0.02, -0.135, 0.86), (cx - 0.02, -0.135, 0.86)], (0, -0.012, 0), "shirt")
        slab(j, [(cx - 0.018, -0.145, 1.12), (cx + 0.018, -0.145, 1.12), (cx + 0.022, -0.148, 0.92), (cx, -0.15, 0.89), (cx - 0.022, -0.148, 0.92)], (0, -0.01, 0), "ink")
        ell(j, (cx + 0.09 * s, -0.13, 1.06), (.025, .015, .025), 5, 3, "extra1")
        for z in (0.80, 0.70):
            ell(j, (cx, -0.137, z), (.012, .01, .012), 4, 3, "ink")
        # little skull, chest-rigid, leaning out from under the coffin
        hc = Vector((0.35 * s, -0.03, 1.35))
        sk, fy = skull(hc, size=(.36, .33, .32), eye_r=.058, tilt=14 * s, wfn=chest_remap(rigid("chest")), name="skull_" + side,
                       useg=10, vseg=6, teeth=True, jaw=True)
        nk = Part("neck_" + side, chest_remap(rigid("chest")))
        tube(nk, [(0.28 * s, 0.0, 1.14), (0.32 * s, -0.01, 1.22)], .035, 5, "bone")
        # outer arm: follows the rig arm (swings while marching, the right one holds the weapon)
        arm(side, s, rad=(.06, .057, .055, .052, .05), sleeve_sw="cloth", x0=0.25, x1=0.73, cuff_sw="shirt", cuff_r=.058,
            hand_scale=1.0, name="oarm", wfn=chest_remap(segw(["chest", "upperarm." + side, "lowerarm." + side, "wrist." + side], k=5, bias={"chest": 1.6})))
        # inner arm: rigid to the chest, hand up on the coffin handle
        ia = Part("inner_arm_" + side, chest_remap(rigid("chest")))
        sh = Vector((0.13 * s, -0.02, 1.10))
        el = Vector((0.14 * s, -0.17, 0.98))
        hd = Vector((0.20 * s, -0.24, 1.20))
        tube(ia, [sh, el, hd], [.05, .048, .045], 6, "cloth")
        ell(ia, hd + Vector((0.0, -0.01, 0.03)), (.05, .05, .055), 6, 4, "extra4")
    # coffin on their shoulders (rigid to the chest so the walk makes it bob)
    cf = Part("coffin", chest_remap(rigid("chest")))
    outline = [(0.0, -0.66), (0.17, -0.54), (0.20, -0.30), (0.155, 0.62), (-0.155, 0.62), (-0.20, -0.30), (-0.17, -0.54)]
    zb, zt = 1.17, 1.37
    def ol(z, k):
        return [Vector((x * k, y * (0.96 + 0.04 * k), z)) for x, y in outline]
    g, faces, caps = loft(cf, [ol(zb, 0.94), ol(zb + 0.03, 1.0), ol(zt - 0.04, 1.0), ol(zt, 0.88)], "wood", sw_cap="wood")
    cf.recolor(lambda c, n, cur: n.z > 0.9, "wood_dark")
    # lid trim
    loft(cf, [ol(zt - 0.06, 1.035), ol(zt - 0.03, 1.035)], "wood_dark", cap0=False, cap1=False)
    # gold handles along the sides, plaque, lilies
    for sx in (1, -1):
        for y in (-0.38, 0.0, 0.36):
            xx = 0.20 * sx if y < -0.2 else (0.20 - (y + 0.30) * 0.045 / 0.92) * sx
            tube(cf, [(xx, y - 0.07, 1.25), (xx + 0.035 * sx, y - 0.05, 1.235), (xx + 0.035 * sx, y + 0.05, 1.235), (xx, y + 0.07, 1.25)], .012, 4, "gold")
    box(cf, (0, 0.18, zt + 0.004), (.14, .09, .012), "gold", 0.0)
    # wreath of lilies near the head end
    wc = Vector((0, -0.30, zt + 0.02))
    torus(cf, wc, .09, .028, 10, 4, "extra2")
    for k in range(5):
        a = 2 * math.pi * k / 5 + 0.3
        pc = wc + Vector((0.09 * math.cos(a), 0.09 * math.sin(a), 0.03))
        ell(cf, pc, (.035, .035, .025), 6, 3, "extra1" if k % 2 == 0 else "extra3")
        ell(cf, pc + Vector((0, 0, 0.02)), (.012, .012, .012), 4, 3, "glow2")
# ================================================================ MADAME ECTOPLASM
PAL["Ectoplasm"] = dict(BONE, glow=H("7CF0B8"), glow2=H("9CFFD0"),
                        cloth=H("5A3670"), cloth_dark=H("3E2450"), cloth_inner=H("1A0E24"),    # gown
                        cloth2=H("B0306A"), cloth2_dark=H("7A1E48"),                           # shawl
                        shirt=H("4FB88A"), shirt_dark=H("348A64"), accent=H("FFC94A"), accent_dark=H("B8862A"),  # turban
                        metal=H("A8A0B8"), metal_dark=H("6A6278"), gold=H("FFC94A"), gold_dark=H("B8862A"),
                        wood=H("6A4A30"), wood_dark=H("4A3424"), leather=H("6A4428"), shoe=H("2A1E30"), sole=H("120C16"),
                        hair=H("8A4FC0"), ink=H("1A1420"), paper=H("F8F2DE"), glass=H("7CF0B8"),
                        extra1=H("E0304A"), extra2=H("6A3AA0"))

def build_ectoplasm():
    zc = ZC
    # floating gown (no legs, like the Wraith): rigid to the hips, the lower hem takes a little of each upper leg
    def gown_w(co):
        base = TORSO_W(co) if co.z > 0.60 else {"hips": 1.0}
        t = max(0.0, min(1.0, (0.42 - co.z) / 0.34))
        if t <= 0:
            return base
        side = "upperleg.l" if co.x > 0 else "upperleg.r"
        k = min(1.0, abs(co.x) / 0.12)
        w = {b: v * (1 - 0.35 * t * k) for b, v in base.items()}
        w[side] = w.get(side, 0) + 0.35 * t * k
        return w
    g_ = Part("gown", gown_w)
    n = 16
    spec = [(1.27, .12, .11), (1.20, .24, .20), (1.05, .265, .215), (0.86, .25, .205), (0.68, .27, .225), (0.46, .37, .32), (0.24, .46, .40), (0.10, .50, .44)]
    rings = [hring(z, rx, ry, n) for z, rx, ry in spec]
    wave = [0.0, 0.035, 0.0, -0.025] * 4
    rings[-1] = [co + Vector((0, 0, wave[k])) for k, co in enumerate(rings[-1])]
    g, faces, caps = loft(g_, rings, "cloth", cap0=True, cap1=False)
    g_.tag(faces[-n:], "cloth_dark")
    shell(g_, faces[-3 * n:], lambda co: Vector((co.x * .9, co.y * .9, co.z + 0.02)), "cloth_inner", "gold")
    # gold sash with tassels
    s_ = Part("sash", rigid("hips"))
    loft(s_, [hring(0.66, .275, .23, 16), hring(0.72, .265, .22, 16)], "gold", cap0=False, cap1=False)
    for x in (0.10, 0.15):
        tube(s_, [(x, -0.22, 0.66), (x + 0.02, -0.25, 0.50)], .014, 4, "gold")
        ell(s_, (x + 0.02, -0.25, 0.48), (.025, .025, .035), 5, 3, "gold_dark")
    # ectoplasm wisps trailing from the hem (glow) - three curling tails behind, small drips around
    w_ = Part("wisps", gown_w)
    for k, (x, curl, ht) in enumerate(((0.22, 1, 1.0), (-0.02, -1, 1.25), (-0.25, 1, 0.9))):
        pts = [(x, 0.30, 0.20), (x + 0.06 * curl, 0.46, 0.34 * ht), (x + 0.14 * curl, 0.52, 0.62 * ht), (x + 0.04 * curl, 0.48, 0.86 * ht), (x - 0.06 * curl, 0.40, 0.98 * ht)]
        tube(w_, pts, [.075, .06, .045, .03, .012], 6, "glow2")
    for k in range(0, n, 2):
        co = rings[-1][k]
        a = Vector((co.x * .93, co.y * .93, co.z + 0.06))
        cone(w_, a, a + Vector((co.x * 0.12, co.y * 0.12, -0.10)), 0.04, 4, "glow2")
    # fringed shawl over the shoulders
    sh = Part("shawl", rigid("chest"))
    r0 = hring(1.29, .15, .14, 16)
    r1 = hring(1.18, .33, .28, 16)
    r2 = hring(0.98, .37, .31, 16)
    g, faces, caps = loft(sh, [r0, r1, r2], "cloth2", cap0=False, cap1=False)
    for f in list(faces):
        c = f.calc_center_median()
        if c.y < -0.15 and abs(c.x) < 0.10 and c.z < 1.16:
            sh.bm.faces.remove(f)
            faces.remove(f)
    shell(sh, faces, lambda co: Vector((co.x * .93, co.y * .93, co.z - 0.01)), "cloth_inner", "cloth2_dark")
    for k in range(16):
        co = r2[k]
        if co.y < -0.15 and abs(co.x) < 0.10:
            continue
        cone(sh, co + Vector((0, 0, 0.01)), co * 1.0 + Vector((co.x * 0.04, co.y * 0.04, -0.08)), .02, 3, "gold")
    # bead necklace
    for k in range(7):
        a = math.pi * (0.25 + 0.5 * k / 6)
        ell(sh, (0.16 * math.cos(a + math.pi / 2) * -1, -0.215 - 0.03 * math.sin(a), 1.15 - 0.06 * math.sin(math.pi * k / 6)),
            (.02, .02, .02), 5, 3, "gold" if k % 2 else "extra1")
    # crystal ball floating in front of the waist (glows), with a gold cradle ring
    cb = Part("crystal", lambda co: {"spine": 0.7, "hips": 0.3})
    bc = Vector((0.0, -0.46, 0.84))
    ell(cb, bc, (.17, .17, .17), 12, 8, "glow2")
    torus(cb, bc + Vector((0, 0, -0.15)), .095, .024, 10, 4, "gold")
    for k in range(3):
        a = 2 * math.pi * k / 3
        cone(cb, bc + Vector((0.08 * math.cos(a), 0.08 * math.sin(a), -0.17)), bc + Vector((0.05 * math.cos(a), 0.05 * math.sin(a), -0.26)), .015, 3, "gold")
    # a spirit swirling up out of the ball
    tube(cb, [bc + Vector((0.04, 0, 0.16)), bc + Vector((0.10, -0.02, 0.26)), bc + Vector((0.04, -0.03, 0.35)), bc + Vector((-0.04, -0.02, 0.38))],
         [.03, .025, .018, .01], 5, "glow")
    # arms: bell sleeves, bony hands with gold rings
    for side, s in (("l", 1), ("r", -1)):
        up, lo = "upperarm." + side, "lowerarm." + side
        a = Part("sleeve_up_" + side, ablend(0, [(0.20, {"chest": .5, up: .5}), (0.30, {up: 1})], s))
        loft(a, [ring((x * s, 0, zc), (0, -1, 0), (0, 0, 1), r, r, 8) for x, r in ((0.16, .10), (0.30, .10), (0.46, .095))], "cloth")
        bb = Part("sleeve_lo_" + side, rigid(lo))
        r0 = ring((0.43 * s, 0, zc), (0, -1, 0), (0, 0, 1), .095, .095, 10)
        r1 = ring((0.58 * s, 0, zc - 0.01), (0, -1, 0), (0, 0, 1), .13, .13, 10)
        r2 = ring((0.69 * s, 0, zc - 0.03), (0, -1, 0), (0, 0, 1), .16, .16, 10)
        g, faces, caps = loft(bb, [r0, r1, r2], "cloth", cap0=True, cap1=False)
        bb.tag(faces[-10:], "gold")
        shell(bb, faces, lambda co, s=s: Vector((co.x, co.y * .8, zc + (co.z - zc) * .8)), "cloth_inner", "gold_dark")
        w = Part("wrist_" + side, segw([lo, "wrist." + side, "hand." + side], k=6))
        tube(w, [(0.63 * s, 0, zc), (0.82 * s, 0, zc)], .034, 5, "bone")
        tube(w, [(0.76 * s, 0, zc), (0.79 * s, 0, zc)], .045, 6, "gold")   # bangle
        hand(side, s)
    neck(0.05)
    # ---- head: skull, big jewelled turban with a feather, hoop earrings
    hc = Vector((0, -0.02, 1.60))
    p, fy = skull(hc, size=(.54, .50, .50), eye_r=.085, pupil_sw="glow")
    # eyeshadow arcs
    for sx in (1, -1):
        ell(p, (0.115 * sx, fy + 0.005, 1.70), (.08, .02, .03), 6, 3, "extra2")
    t = Part("turban", rigid("head"))
    o = Vector((0, 0.0, 1.76))
    for i, (z, r, tw) in enumerate(((0.0, .31, 0), (0.07, .33, 0.02), (0.14, .32, -0.02), (0.21, .28, 0.02))):
        torus(t, o + Vector((0, 0, z)), r - 0.04, 0.06, 14, 5, "shirt" if i % 2 == 0 else "shirt_dark",
              normal=ROT(8 * (1 if i % 2 else -1), 'Y') @ Vector((0, 0, 1)))
    ell(t, o + Vector((0, 0.01, 0.20)), (.27, .26, .17), 12, 6, "shirt")
    ell(t, o + Vector((0, 0.0, 0.34)), (.08, .08, .07), 8, 4, "shirt_dark")      # top knot
    # jewel + feather plume
    jc = Vector((0, -0.325, 1.86))
    torus(t, jc, .05, .016, 10, 4, "gold", normal=Vector((0, 1, 0)))
    ell(t, jc + Vector((0, -0.01, 0)), (.04, .025, .045), 6, 4, "glow")
    for k, (dx, h_) in enumerate(((0.0, 0.40), (0.05, 0.33), (-0.05, 0.32))):
        tube(t, [jc + Vector((dx * 0.3, 0.02, 0.04)), jc + Vector((dx, 0.0, h_ * 0.5)), jc + Vector((dx * 1.6, 0.05, h_))], [.025, .04, .015], 5, "hair")
    for sx in (1, -1):
        torus(t, (0.275 * sx, -0.02, 1.48), .045, .01, 10, 4, "gold", normal=Vector((1, 0, 0)))

# ================================================================ SIR NECKLESS
PAL["Neckless"] = dict(BONE, glow=H("FFD86A"), glow2=H("FF8A2A"),
                       cloth=H("E07A1F"), cloth_dark=H("A8540E"), cloth_inner=H("2A1E18"),     # tabard
                       cloth2=H("6A6E78"), cloth2_dark=H("4A4E58"),                            # mail / undersuit
                       shirt=H("3A2E44"), shirt_dark=H("241C2C"),                              # cape
                       accent=H("F08A24"), accent_dark=H("B85A14"),                            # pumpkin
                       metal=H("A4AAB4"), metal_dark=H("6A707C"), gold=H("D8A84A"), gold_dark=H("8A6A2A"),
                       wood=H("6A4A30"), wood_dark=H("4A3424"), leather=H("5A3A24"), shoe=H("7A808C"), sole=H("3A3E46"),
                       hair=H("2E2A26"), ink=H("120C0A"), paper=H("F8F2DE"), glass=H("C8CED8"),
                       extra1=H("3A6B2A"), extra2=H("5A9A3A"))

def build_neckless():
    zc = ZC
    # armoured legs: cuisses, knee cops, greaves, big sabatons
    for side, s in (("l", 1), ("r", -1)):
        ul, ll = "upperleg." + side, "lowerleg." + side
        leg(side, s, "metal", r=(.12, .115, .105, .10), cuff_z=0.16, shoe_sw="shoe", sole_sw="sole", shoe_scale=1.25, ankle=False)
        kp = Part("knee_" + side, segw([ul, ll], k=6))
        ell(kp, (0.171 * s, -0.10, 0.30), (.085, .06, .075), 8, 4, "metal_dark")
        cone(kp, (0.171 * s, -0.145, 0.30), (0.171 * s, -0.20, 0.30), .03, 4, "metal_dark")
        # tassets
        ts = Part("tasset_" + side, lambda co, ul=ul: {"hips": .45, ul: .55})
        for k, z in enumerate((0.56, 0.47)):
            box(ts, (0.19 * s, -0.08 + 0.01 * k, z), (.20, .20, .10), "metal" if k == 0 else "metal_dark", 0.025, ROT(-6 * s, 'Y'))
    pelvis("cloth2", rx=.28, ry=.23, z0=0.46, z1=0.66)
    # barrel breastplate
    t = Part("cuirass", TORSO_W)
    spec = [(0.60, .29, .24), (0.78, .32, .26), (0.96, .335, .27), (1.10, .32, .26), (1.20, .26, .22), (1.27, .16, .15)]
    g, faces, caps = loft(t, [hring(z, rx, ry, 14) for z, rx, ry in spec], "metal")
    t.recolor(lambda c, n, cur: n.z < -0.5, "metal_dark")
    # dents
    for (x, z) in ((0.16, 1.02), (-0.10, 0.80)):
        ell(t, (x, -0.24, z), (.045, .02, .035), 5, 3, "metal_dark")
    # orange surcoat fitted over the cuirass (open at the sides), ragged flaps front and back over the hips
    sc = Part("surcoat", TORSO_W)
    n = 14
    rings = [hring(z, rx + 0.012, ry + 0.012, n) for z, rx, ry in ((0.64, .30, .25), (0.78, .32, .26), (0.96, .335, .27), (1.10, .32, .26), (1.19, .262, .222))]
    g, faces, caps = loft(sc, rings, "cloth", cap0=False, cap1=False)
    for f in list(faces):
        cc = f.calc_center_median()
        if abs(cc.y) < 0.13 and abs(cc.x) > 0.2:
            sc.bm.faces.remove(f)
            faces.remove(f)
    for sy in (-1, 1):
        fl = Part("flap_%d" % sy, skirt_w(0.62, 0.36, leg_max=0.5))
        y0 = 0.262 * sy
        top = [Vector((x, y0 * math.sqrt(max(0.0, 1 - (x / 0.31) ** 2)) * 1.0, 0.66)) for x in (-0.20, -0.10, 0.0, 0.10, 0.20)]
        bot = [Vector((x, (y0 + 0.05 * sy) * math.sqrt(max(0.0, 1 - (x / 0.34) ** 2)), 0.40 + (0.05 if k % 2 else 0.0))) for k, x in enumerate((-0.20, -0.10, 0.0, 0.10, 0.20))]
        g, faces, caps = loft(fl, [top, bot], "cloth", closed=False)
        shell(fl, faces, lambda co, sy=sy: Vector((co.x, co.y - 0.02 * sy, co.z)), "cloth_inner", "cloth_dark")
    # dark pumpkin badge on the chest
    bcn = Vector((0, -0.29, 0.92))
    bdg = Part("badge", TORSO_W)
    ell(bdg, bcn, (.11, .025, .095), 8, 4, "cloth_inner")
    box(bdg, bcn + Vector((0, 0.0, 0.10)), (.025, .02, .05), "extra1", 0.0)
    for sx in (1, -1):
        slab(bdg, [bcn + Vector((0.03 * sx, -0.026, 0.01)), bcn + Vector((0.07 * sx, -0.026, 0.01)), bcn + Vector((0.05 * sx, -0.026, 0.045))], (0, -0.004, 0), "glow")
    slab(bdg, [bcn + Vector((-0.06, -0.026, -0.03)), bcn + Vector((0.06, -0.026, -0.03)), bcn + Vector((0.0, -0.026, -0.06))], (0, -0.004, 0), "glow")
    # belt
    b = Part("belt", rigid("hips"))
    loft(b, [hring(0.62, .30, .26, 14), hring(0.68, .30, .26, 14)], "leather", cap0=False, cap1=False)
    box(b, (0, -0.285, 0.65), (.09, .03, .075), "gold", 0.01)
    # short cape behind
    cp = Part("cape", TORSO_W)
    n = 9
    rows = []
    for z, rx, ry, cy in ((1.24, .24, .18, 0.08), (0.95, .36, .28, 0.10), (0.62, .38, .30, 0.12)):
        rows.append([Vector((rx * math.cos(math.pi * k / (n - 1)), cy + ry * math.sin(math.pi * k / (n - 1)) + 0.12, z)) for k in range(n)])
    rows[-1] = [co - Vector((0, 0, (0.06 if k % 2 else 0.0))) for k, co in enumerate(rows[-1])]
    g, faces, caps = loft(cp, rows, "shirt", closed=False)
    shell(cp, faces, lambda co: Vector((co.x * .97, co.y - 0.02, co.z)), "shirt_dark", "shirt_dark")
    # gorget + neck stub: hollow ring with a glowing ember where the head should be (no mesh on the head bone)
    gk = Part("gorget", rigid("chest"))
    loft(gk, [hring(1.22, .22, .19, 14), hring(1.30, .19, .17, 14), hring(1.36, .15, .14, 14)], "metal", cap0=False, cap1=False)
    loft(gk, [hring(1.36, .15, .14, 14), hring(1.37, .12, .11, 14)], "metal_dark", cap0=False, cap1=False)
    disc(gk, (0, 0, 1.31), .12, .11, 12, "cloth_inner", normal=Vector((0, 0, 1)))
    ell(gk, (0, 0, 1.33), (.09, .085, .03), 8, 3, "glow2")
    for k in range(5):
        a = 2 * math.pi * k / 5
        b0 = Vector((0.05 * math.cos(a), 0.05 * math.sin(a), 1.33))
        cone(gk, b0, b0 + Vector((0.02 * math.cos(a), 0.02 * math.sin(a), 0.10 + 0.05 * (k % 2))), .035, 4, "glow2")
    cone(gk, (0, 0, 1.33), (0.01, 0.0, 1.52), .05, 5, "glow")
    # arms: mail sleeves, vambraces, elbow cops, big gauntlets; pauldrons
    for side, s in (("l", 1), ("r", -1)):
        up, lo = "upperarm." + side, "lowerarm." + side
        arm(side, s, rad=(.09, .085, .082, .08, .076), sleeve_sw="cloth2", bony_wrist=False, hand_sw="metal", hand_scale=1.4)
        vb = Part("vambrace_" + side, rigid(lo))
        tube(vb, [(0.52 * s, 0, zc), (0.66 * s, 0, zc), (0.74 * s, 0, zc)], [.092, .10, .115], 8, "metal")
        ec = Part("elbow_" + side, segw([up, lo], k=6))
        ell(ec, (0.455 * s, 0.07, zc), (.07, .06, .07), 6, 4, "metal_dark")
        pd = Part("pauldron_" + side, lambda co, up=up: {"chest": .55, up: .45})
        sbox(pd, (0.29 * s, 0, 1.18), (.30, .32, .17), 10, 6, "metal", power=0.6)
        sbox(pd, (0.33 * s, 0, 1.08), (.24, .28, .07), 8, 4, "metal_dark", power=0.6)
        ell(pd, (0.29 * s, 0, 1.27), (.03, .03, .02), 5, 3, "gold")
    # the pumpkin head, tucked under the left arm (rigid to the left forearm: in T-pose it sits in front of the forearm,
    # when the arm hangs it rests against the hip)
    pk = Part("pumpkin", rigid("lowerarm.l"))
    pc = Vector((0.57, -0.20, zc + 0.03))
    nl = 8
    for k in range(nl):
        a = 2 * math.pi * k / nl
        ell(pk, pc + Vector((0.085 * math.cos(a), 0.085 * math.sin(a), 0)), (.11, .11, .15), 8, 5, "accent" if k % 2 else "accent_dark",
            ROT(math.degrees(a), 'Z'))
    cyl(pk, pc + Vector((0, 0, 0.12)), pc + Vector((0.02, 0.01, 0.21)), .028, .02, 5, "extra1")
    slab(pk, [pc + Vector((0.02, 0, 0.17)), pc + Vector((0.11, -0.03, 0.20)), pc + Vector((0.08, 0.04, 0.16))], (0, 0, 0.01), "extra2")
    # carved face pointing forward (glow)
    fz = pc + Vector((0, -0.19, 0))
    for sx in (1, -1):
        slab(pk, [fz + Vector((0.035 * sx, 0, 0.02)), fz + Vector((0.10 * sx, 0.01, 0.025)), fz + Vector((0.07 * sx, 0.0, 0.08))], (0, 0.02, 0), "glow")
    slab(pk, [fz + Vector((-0.10, 0.01, -0.04)), fz + Vector((-0.05, 0, -0.06)), fz + Vector((-0.02, 0, -0.04)), fz + Vector((0.02, 0, -0.06)),
              fz + Vector((0.05, 0, -0.04)), fz + Vector((0.10, 0.01, -0.04)), fz + Vector((0.06, 0.0, -0.10)), fz + Vector((-0.06, 0.0, -0.10))], (0, 0.02, 0), "glow")

# ================================================================ OLD TICKWELL
PAL["Tickwell"] = dict(BONE, glow=H("FFC24A"), glow2=H("BFF4FF"),
                       cloth=H("5A4630"), cloth_dark=H("3E3020"), cloth_inner=H("1E160E"),     # coat
                       cloth2=H("3A3040"), cloth2_dark=H("26202C"),                            # trousers
                       shirt=H("E8E0CC"), shirt_dark=H("BFB59E"), accent=H("B08D57"), accent_dark=H("7A6038"),
                       metal=H("B8BCC4"), metal_dark=H("6A6E78"), gold=H("E2B85A"), gold_dark=H("9A7A34"),
                       wood=H("7A4A28"), wood_dark=H("4E2E18"), leather=H("5A3A24"), shoe=H("2A2020"), sole=H("100C0C"),
                       hair=H("F4F2EE"), ink=H("1A1410"), paper=H("F6EEDA"), glass=H("2A3440"),
                       extra1=H("9A5E30"), extra2=H("C83A2A"))

def build_tickwell():
    zc = ZC
    for side, s in (("l", 1), ("r", -1)):
        leg(side, s, "cloth2", r=(.10, .095, .085, .08), cuff_z=0.17, shoe_scale=1.12)
        bk = Part("buckle_" + side, rigid("toes." + side))
        box(bk, (0.171 * s, -0.235, 0.10), (.08, .02, .05), "gold", 0.0)
    pelvis("wood_dark", rx=.27, ry=.22, z0=0.46, z1=0.62)
    # grandfather-clock case for a chest: plinth, case, hood with arched crown
    c = Part("case", TORSO_W)
    sbox(c, (0, 0, 0.88), (.54, .40, .62), 8, 6, "wood", power=0.25)
    c.recolor(lambda cc, n, cur: n.z < -0.5, "wood_dark")
    pl = Part("plinth", rigid("hips"))
    box(pl, (0, 0, 0.58), (.60, .45, .09), "wood_dark", 0.02)
    hd = Part("hood", rigid("chest"))
    box(hd, (0, 0, 1.20), (.58, .43, .08), "wood_dark", 0.02)
    cyl(hd, (0, -0.205, 1.24), (0, 0.205, 1.24), .20, .20, 12, "wood")      # arched crown (half buried)
    for sx in (1, -1):
        ell(hd, (0.25 * sx, -0.17, 1.28), (.04, .04, .05), 6, 4, "gold")
        cone(hd, (0.25 * sx, -0.17, 1.31), (0.25 * sx, -0.17, 1.38), .02, 4, "gold")
    # clock face
    fc = Vector((0, -0.215, 1.02))
    fcp = Part("clockface", TORSO_W)
    torus(fcp, fc, .145, .022, 16, 4, "gold", normal=Vector((0, 1, 0)))
    disc(fcp, fc + Vector((0, -0.005, 0)), .14, .14, 16, "paper", normal=Vector((0, -1, 0)))
    for k in range(12):
        a = 2 * math.pi * k / 12
        L = .03 if k % 3 == 0 else .015
        box(fcp, fc + Vector((0.11 * math.sin(a), -0.012, 0.11 * math.cos(a))), (.012 if k % 3 else .018, .006, L), "ink", 0.0, ROT(-math.degrees(a), 'Y'))
    box(fcp, fc + Vector((0.03, -0.016, 0.02)), (.08, .006, .016), "ink", 0.0, ROT(-30, 'Y'))
    box(fcp, fc + Vector((-0.005, -0.016, 0.05)), (.014, .006, .10), "ink", 0.0, ROT(8, 'Y'))
    ell(fcp, fc + Vector((0, -0.02, 0)), (.015, .008, .015), 5, 3, "gold")
    # pendulum window + bob
    pw = Vector((0, -0.205, 0.74))
    box(fcp, pw, (.22, .02, .22), "glass", 0.01)
    box(fcp, pw + Vector((0, 0.002, 0.115)), (.25, .025, .03), "gold_dark", 0.0)
    box(fcp, pw + Vector((0.006, -0.012, 0.03)), (.012, .006, .14), "gold", 0.0)
    ell(fcp, pw + Vector((0.01, -0.016, -0.05)), (.045, .012, .045), 8, 4, "gold")
    # coat tails behind, split per leg
    for sx in (1, -1):
        tl = Part("tail_%d" % sx, skirt_w(0.64, 0.30, leg_max=0.6))
        pts = [(0.02 * sx, 0.20, 1.10), (0.27 * sx, 0.17, 1.10), (0.29 * sx, 0.22, 0.35), (0.04 * sx, 0.25, 0.28)]
        slab(tl, pts, (0, 0.03, 0), "cloth", "cloth_dark")
    # pocket watches on chains from the plinth
    pwt = Part("watches", rigid("hips"))
    for (x, y, z, r) in ((0.27, -0.16, 0.42, .055), (-0.29, -0.10, 0.45, .05), (0.12, -0.25, 0.40, .045)):
        a = Vector((x * 0.85, y * 0.9, 0.58))
        cc = Vector((x, y - 0.02, z))
        tube(pwt, [a, (a + cc) / 2 + Vector((0, -0.015, 0)), cc + Vector((0, 0, r))], .007, 3, "gold")
        torus(pwt, cc, r, .012, 10, 4, "gold", normal=Vector((0, 1, 0)))
        disc(pwt, cc, r * 0.9, r * 0.9, 10, "paper", normal=Vector((0, -1, 0)))
        disc(pwt, cc + Vector((0, 0.004, 0)), r * 0.9, r * 0.9, 10, "gold_dark", normal=Vector((0, 1, 0)))
        box(pwt, cc + Vector((0, -0.006, 0.01)), (.006, .004, .03), "ink", 0)
    # arms: coat sleeves with brass cuffs
    for side, s in (("l", 1), ("r", -1)):
        arm(side, s, rad=(.088, .084, .08, .078, .074), sleeve_sw="cloth", cuff_sw="accent", cuff_r=.088)
        sh = Part("shoulder_" + side, ablend(0, [(0.16, {"chest": 1}), (0.30, {"chest": .4, "upperarm." + side: .6})], s))
        ell(sh, (0.23 * s, 0, 1.12), (.12, .14, .10), 8, 4, "cloth")
    neck(0.05)
    # ---- head: old skull, bushy white brows + moustache, monocle, floating gear halo
    hc = Vector((0, -0.02, 1.60))
    p, fy = skull(hc, size=(.56, .52, .50), eye_r=.08, pupil_sw="glow", teeth=False)
    h = Part("face_hair", rigid("head"))
    for sx in (1, -1):
        box(h, (0.12 * sx, fy - 0.01, 1.71), (.17, .05, .05), "hair", 0.02, ROT(-12 * sx, 'Y'))
        ell(h, (0.085 * sx, fy - 0.02, 1.50), (.10, .05, .045), 7, 4, "hair", ROT(14 * sx, 'Y'))
        ell(h, (0.25 * sx, -0.02, 1.66), (.06, .09, .08), 6, 4, "hair")     # tufts over the ears
    ell(h, (0, fy - 0.035, 1.43), (.06, .04, .07), 6, 4, "hair")           # goatee
    # monocle on the left eye with a chain
    mc = Vector((0.117, fy - 0.03, 1.61))
    torus(h, mc, .072, .013, 12, 4, "gold", normal=Vector((0, 1, 0)))
    disc(h, mc + Vector((0, 0.002, 0)), .065, .065, 12, "glow2", normal=Vector((0, -1, 0)))
    tube(h, [mc + Vector((0.06, 0, -0.04)), mc + Vector((0.10, -0.01, -0.15)), (0.16, -0.20, 1.40)], .006, 3, "gold")
    # gear halo
    gh = Part("gear_halo", rigid("head"))
    o = Vector((0, 0.02, 2.02))
    nr = ROT(-10, 'X') @ Vector((0, 0, 1))
    gear(gh, o, .22, 0, 12, .035, "gold", normal=nr, hole=.15, tooth_h=.045)
    gear(gh, o + ROT(-10, 'X') @ Vector((0.27, 0.06, 0.02)), .07, 0, 7, .03, "accent", normal=nr, hole=.025, tooth_h=.022)
    gear(gh, o + ROT(-10, 'X') @ Vector((-0.24, -0.12, 0.02)), .055, 0, 6, .03, "gold_dark", normal=nr, hole=.02, tooth_h=.02)

BUILDERS = {"Actuary": build_actuary, "Sweep": build_sweep, "Solace": build_solace, "Pallbearers": build_pallbearers,
            "Ectoplasm": build_ectoplasm, "Neckless": build_neckless, "Tickwell": build_tickwell}

if __name__ == "__main__":
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    for m in (args or list(BUILDERS)):
        build_reaper(m, PAL[m], BUILDERS[m])
