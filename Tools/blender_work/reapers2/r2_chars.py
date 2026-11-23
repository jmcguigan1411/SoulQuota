# Soul Quota - reaper batch 2 characters (original designs from the approved roster sketches).
# Femur, Digby, Collector, Beak, Tallow, Lookout, Vexa. World space: Z up, facing -Y, left = +X.
import math
from mathutils import Vector, Matrix
from r2_lib import *

PALETTES = {}
BUILDERS = {}

def both():
    return (("l", 1), ("r", -1))

# ================================================================ FEMUR THE HOUND
PALETTES["Femur"] = pal(bone="#F2EBDD", bone_shade="#C9BFA8", cavity="#1A1210", glow="#FFB347", glow2="#FFE08A",
                        fur="#5A4632", fur_dark="#3E3022", skin="#C8A27A", skin_dark="#A3815C", skin_light="#E0C29C",
                        red="#D8434B", red_dark="#9C2A33", metal="#B8BCC4", gold="#FFD26A", gold_dark="#C89B32",
                        tooth="#FFF8E6", nail="#3A2E26", tongue="#E07A86", mouth="#2A1414", leather="#7A5236", sole="#3A2E26")

def build_femur():
    # --- pelvis / shaggy shorts (fur)
    p = Part("pelvis", rigid("hips"))
    loft(p, [hring(0.40, .20, .17, 10), hring(0.50, .245, .20, 10), hring(0.62, .25, .205, 10)], "fur")
    # --- torso: lean, hunched (upper body pushed forward, hump on the back), tan belly
    p = Part("torso", TORSO_W)
    spec = [(0.58, .235, .19, 0.0), (0.74, .25, .20, -0.02), (0.90, .275, .225, -0.05), (1.04, .30, .25, -0.08),
            (1.16, .26, .23, -0.10), (1.25, .14, .13, -0.12)]
    rings = [hring(z, rx, ry, 12, cy=cy) for z, rx, ry, cy in spec]
    g, faces, caps = loft(p, rings, "fur", cap0=False, cap1=True)
    p.recolor(lambda c, n, cur: n.y < -0.55 and c.z < 1.08 and abs(c.x) < 0.17, "skin_light")
    # shaggy fur hem over the pelvis
    hem = [0.0, 0.06, 0.02, 0.07, 0.0, 0.05, 0.02, 0.08, 0.0, 0.06, 0.01, 0.07]
    r0 = hring(0.70, .265, .215, 12)
    r1 = [co - Vector((0, 0, hem[k])) for k, co in enumerate(hring(0.55, .285, .235, 12))]
    g, faces, caps = loft(p, [r0, r1], "fur", cap0=False, cap1=False)
    shell(p, faces, lambda co: Vector((co.x * .9, co.y * .9, co.z)), "fur_dark", "fur_dark")
    # hump + scruff on the upper back
    q = Part("hump", zblend([(0.95, {"spine": .5, "chest": .5}), (1.1, {"chest": 1})]))
    ell(q, (0, 0.07, 1.13), (.27, .21, .19), 10, 6, "fur")
    for k, (x, z) in enumerate(((-0.12, 1.22), (0.0, 1.25), (0.12, 1.22), (-0.06, 1.14), (0.07, 1.15))):
        cone(q, (x, 0.19, z + 0.03), (x * 1.2, 0.33, z + 0.12), 0.07, 4, "fur_dark" if k % 2 else "fur")
    for z in (0.98, 1.10, 1.21):  # spine knobs on the hump
        ell(q, (0, 0.275 - abs(z - 1.12) * 0.45, z + 0.02), (.04, .035, .04), 5, 3, "bone")
    # --- spiked collar with a tag
    c = Part("collar", NECK_W)
    loft(c, [hring(1.20, .19, .175, 12, cy=-0.09), hring(1.31, .185, .17, 12, cy=-0.10)], "red", cap0=False, cap1=False)
    for k in range(12):
        a = -math.pi / 2 + 2 * math.pi * (k + 0.5) / 12
        if k in (11, 0):
            continue
        base = Vector((0.185 * math.cos(a), -0.095 + 0.17 * math.sin(a), 1.255))
        out = Vector((math.cos(a), math.sin(a), 0))
        cone(c, base, base + out * 0.08, 0.03, 4, "metal")
    t = Part("tag", rigid("chest"))
    tube(t, [(0, -0.27, 1.21), (0, -0.28, 1.17)], 0.014, 4, "metal")
    cyl(t, (0, -0.305, 1.10), (0, -0.275, 1.10), 0.075, 0.075, 8, "gold")
    box(t, (0, -0.307, 1.10), (.07, .008, .016), "gold_dark")
    neck("fur_dark", r=0.085, top=1.36, fwd=0.13)
    # --- arms: bony arms with fur shoulder tufts, big paws
    for side, s in both():
        arm_tube(side, s, [.085, .062, .056, .06, .053, .055], "bone", "bone_shade")
        q = Part("elbow_" + side, rigid("lowerarm." + side))
        ell(q, (0.46 * s, 0.01, ZC), (.065, .065, .065), 6, 4, "bone")
        sh = Part("tuft_" + side, ablend(0, [(0.20, {"chest": .6, "upperarm." + side: .4}), (0.32, {"upperarm." + side: 1})], s))
        ell(sh, (0.25 * s, 0.0, ZC + 0.02), (.13, .13, .12), 8, 5, "fur")
        for k in range(3):
            cone(sh, (0.31 * s, (k - 1) * 0.07, ZC - 0.06), (0.36 * s, (k - 1) * 0.09, ZC - 0.15), 0.04, 4, "fur_dark")
        # paw: round tan mitt with dark pads and claws
        h = Part("paw_" + side, rigid("hand." + side))
        sbox(h, (0.85 * s, 0, ZC - 0.01), (.17, .19, .12), 8, 5, "skin", power=0.6)
        for k, y in enumerate((-0.055, 0.0, 0.055)):
            ell(h, (0.94 * s, y, ZC - 0.02), (.04, .03, .04), 6, 4, "skin")
            cone(h, (0.96 * s, y, ZC - 0.04), (0.985 * s, y, ZC - 0.08), 0.015, 4, "nail")
        ell(h, (0.85 * s, 0, ZC - 0.07), (.06, .065, .02), 6, 3, "nail")
        tube(h, [(0.82 * s, -0.08, ZC), (0.87 * s, -0.12, ZC - 0.02)], [0.03, 0.026], 5, "skin")
        # legs: fur thigh, bony shin, big paw feet
        leg_tube(side, s, [.11, .10, .085, .062, .056, .056], [0.56, 0.42, 0.32, 0.27, 0.18, 0.11], "bone", "bone_shade")
        f = Part("thighfur_" + side, rigid("upperleg." + side))
        jag = [0, .05, .01, .04, 0, .06, .02, .03]
        r0 = hring(0.52, .13, .125, 8, cx=0.171 * s)
        r1 = [co - Vector((0, 0, jag[k])) for k, co in enumerate(hring(0.36, .125, .12, 8, cx=0.171 * s))]
        g, faces, caps = loft(f, [r0, r1], "fur", cap0=True, cap1=False)
        shell(f, faces, lambda co, s=s: Vector((0.171 * s + (co.x - 0.171 * s) * .85, co.y * .85, co.z)), "fur_dark", "fur_dark")
        k = Part("knee_" + side, rigid("lowerleg." + side))
        ell(k, (0.171 * s, -0.04, 0.29), (.055, .05, .055), 6, 4, "bone")
        ft = Part("pawfoot_" + side, rigid("foot." + side))
        sbox(ft, (0.171 * s, -0.02, 0.075), (.20, .24, .14), 8, 5, "skin", power=0.6)
        ell(ft, (0.171 * s, -0.02, 0.012), (.10, .12, .015), 6, 3, "sole")
        tq = Part("pawtoe_" + side, rigid("toes." + side))
        for dx in (-0.06, 0.0, 0.06):
            ell(tq, (0.171 * s + dx, -0.17, 0.05), (.045, .06, .045), 6, 4, "skin")
            cone(tq, (0.171 * s + dx, -0.215, 0.04), (0.171 * s + dx, -0.26, 0.02), 0.015, 4, "nail")
    # --- bone tail: vertebrae curling up and back, ending in a cartoon dog-bone
    tl = Part("tail", rigid("hips"))
    pts = [Vector((0, 0.20, 0.56)), Vector((0, 0.30, 0.58)), Vector((0, 0.39, 0.64)), Vector((0, 0.45, 0.73)),
           Vector((0, 0.48, 0.83)), Vector((0, 0.47, 0.93))]
    for i, pt in enumerate(pts):
        r = 0.05 - i * 0.004
        ell(tl, pt, (r, r, r), 6, 4, "bone")
        if i:
            tube(tl, [pts[i - 1], pt], 0.022, 4, "bone_shade", )
    tip = Vector((0, 0.44, 1.03))
    tube(tl, [pts[-1], tip], 0.028, 5, "bone")
    for dx in (-0.035, 0.035):
        ell(tl, tip + Vector((dx, 0, 0.03)), (.035, .035, .035), 6, 4, "bone")
        ell(tl, pts[-1] + Vector((dx, 0.01, -0.0)), (.03, .03, .03), 6, 4, "bone")
    # --- hound skull head: domed cranium, long snout, fangs, glowing amber sockets, big swept-back ears
    hc = Vector((0, -0.16, 1.54))
    h = Part("skull", rigid("head"))
    sbox(h, hc, (.52, .50, .46), 12, 8, "bone", power=0.78)
    h.recolor(lambda c, n, cur: n.z < -0.5, "bone_shade")
    # snout: tapered soft box going forward and a little down
    sy, sz = -0.50, 1.465
    sn = sbox(h, (0, sy, sz), (.30, .42, .21), 8, 6, "bone", power=0.5)
    for v in sn:
        f = max(0.0, min(1.0, (sy - v.co.y) / 0.21 + 0.5))  # 0 at the back, 1 at the nose
        v.co.x *= 1.0 - 0.28 * f
        v.co.z = sz + (v.co.z - sz) * (1.0 - 0.2 * f) - 0.02 * f
    h.recolor(lambda c, n, cur: cur == "bone" and n.z < -0.5 and c.y < -0.3, "bone_shade")
    ell(h, (0, -0.715, 1.49), (.075, .045, .05), 6, 4, "cavity")  # nose
    for sx in (1, -1):
        ell(h, (0.125 * sx, -0.385, 1.60), (.085, .06, .08), 8, 5, "cavity", ROT(-22 * sx, 'Z'))
        ell(h, (0.13 * sx, -0.44, 1.60), (.037, .025, .037), 6, 4, "glow", ROT(-22 * sx, 'Z'))
        ell(h, (0.15 * sx, -0.462, 1.618), (.016, .01, .016), 4, 3, "glow2")
        # big ears swept back, dark inner
        a0 = Vector((0.17 * sx, -0.10, 1.70))
        tip = Vector((0.33 * sx, 0.16, 1.92))
        cone(h, a0, tip, 0.09, 4, "bone")
        cone(h, a0 + Vector((0, -0.03, 0.01)), tip + Vector((-0.02 * sx, -0.035, -0.03)), 0.06, 4, "cavity")
        # fangs
        cone(h, (0.085 * sx, -0.64, 1.395), (0.085 * sx, -0.65, 1.33), 0.024, 4, "tooth")
    for x in (-0.045, 0.0, 0.045):
        cone(h, (x, -0.67, 1.39), (x, -0.675, 1.355), 0.015, 4, "tooth")
    # lower jaw, slightly open, with a tongue
    sbox(h, (0, -0.44, 1.345), (.26, .40, .07), 8, 4, "bone_shade", power=0.5)
    box(h, (0, -0.58, 1.375), (.10, .12, .02), "tongue", 0.01)
    box(h, (0, -0.53, 1.39), (.20, .17, .02), "mouth", 0.0)
    for x in (-0.07, 0.07):
        cone(h, (x, -0.60, 1.375), (x, -0.605, 1.425), 0.017, 4, "tooth")
    # crack on the dome
    stitch_line(h, (-0.06, -0.26, 1.765), (0.08, 0.0, 1.775), 0, "cavity", (0, 0, 1), w=0.018)

BUILDERS["Femur"] = build_femur

# ================================================================ DIGBY SIXFEET
PALETTES["Digby"] = pal(bone="#F2EBDD", bone_shade="#CFC4AC", cavity="#15110E", glow="#9FE8FF", tooth="#FFF8E6",
                        fabric="#4A5568", fabric_dark="#353D4C", shirt="#C9A35E", shirt_dark="#A07E42",
                        hat="#8A7D6B", hat_dark="#6B604F", light="#FFE27A", lens="#FFF6C8", metal="#9AA0A8",
                        gold="#D9A441", leather="#6B4A2E", leather_dark="#4A3220", mud="#6E5238", mud_dark="#4E3824",
                        sole="#2A2420", red="#C8443C", red_dark="#8E2A26", stitch="#2A2E38")

def build_digby():
    # --- stout body: overalls below, mustard shirt above, broad shoulders
    p = Part("torso", TORSO_W)
    spec = [(0.58, .30, .25), (0.72, .33, .28), (0.88, .345, .29), (1.02, .33, .27), (1.15, .29, .235), (1.25, .15, .13)]
    g, faces, caps = loft(p, [hring(z, rx, ry, 12) for z, rx, ry in spec], "shirt", cap0=False, cap1=True)
    p.recolor(lambda c, n, cur: c.z < 0.95, "fabric")
    pv = Part("pelvis", rigid("hips"))
    loft(pv, [hring(0.38, .27, .23, 12), hring(0.48, .31, .26, 12), hring(0.62, .315, .265, 12)], "fabric")
    pv.recolor(lambda c, n, cur: n.z < -0.6, "fabric_dark")
    # bib + pocket + buttons + straps over the shoulders
    b = Part("bib", TORSO_W)
    box(b, (0, -0.285, 0.99), (.36, .05, .30), "fabric", 0.02)
    box(b, (0, -0.315, 0.95), (.18, .02, .12), "fabric_dark", 0.01)
    stitch_line(b, (-0.09, -0.327, 1.012), (0.09, -0.327, 1.012), 0, "stitch", (0, -1, 0), w=0.012)
    for sx in (1, -1):
        cyl(b, (0.13 * sx, -0.31, 1.10), (0.13 * sx, -0.33, 1.10), 0.03, 0.03, 6, "gold")
        tube(b, [(0.13 * sx, -0.29, 1.12), (0.15 * sx, -0.25, 1.21), (0.15 * sx, -0.05, 1.27), (0.15 * sx, 0.18, 1.20),
                 (0.13 * sx, 0.27, 1.02), (0.10 * sx, 0.28, 0.92)], [(0.045, 0.014)] * 6, 4, "fabric_dark")
    # red rag hanging from the back pocket
    rg = Part("rag", rigid("hips"))
    box(rg, (-0.14, 0.265, 0.58), (.12, .03, .10), "fabric_dark", 0.01)
    slab(rg, ((-0.20, 0.285, 0.60), (-0.08, 0.285, 0.60), (-0.10, 0.30, 0.44), (-0.17, 0.295, 0.47)), (0, 0.015, 0), "red")
    # mud splats on the belly
    for x, z, r in ((0.17, 0.72, .05), (-0.21, 0.66, .06), (0.05, 0.62, .04)):
        ell(b, (x, -0.255 - (0.02 if abs(x) < 0.1 else 0.0), z), (r, .02, r * 0.8), 6, 3, "mud")
    neck("bone_shade", r=0.07, top=1.36)
    for side, s in both():
        # rolled-up sleeve to the elbow, bare bony forearm, leather work glove
        arm_tube(side, s, [.075, .06, .055, .06, .055, .058], "bone", "bone_shade")
        sl = Part("sleeve_" + side, segw(["chest", "upperarm." + side, "lowerarm." + side], k=5, bias={"chest": 1.6}))
        tube(sl, [(0.17 * s, 0, ZC), (0.30 * s, 0, ZC), (0.42 * s, 0, ZC)], [.13, .12, .11], 8, "shirt")
        sl.recolor(lambda c, n, cur: n.z < -0.6, "shirt_dark")
        cuff(side, s, 0.44, 0.115, "shirt_dark", w=0.06, bone="upperarm")
        shoulder_pad(side, s, 0.14, "shirt")
        q = Part("elbow_" + side, rigid("lowerarm." + side))
        ell(q, (0.47 * s, 0.01, ZC), (.06, .06, .06), 6, 4, "bone")
        mitt(side, s, "leather", scale=1.25, sw_dark="leather_dark")
        cuff(side, s, 0.78, 0.085, "leather_dark", w=0.08, bone="hand")
        # wide trouser legs with muddy knees, big boots
        lp, lf = leg_loft(side, s, [(0.56, .14, 0.0), (0.42, .135, 0.0), (0.30, .13, -0.005), (0.20, .135, 0.0), (0.16, .14, 0.0)], "fabric")
        lp.recolor(lambda c, n, cur, s=s: (0.24 < c.z < 0.38 and n.y < -0.3) or (c.z < 0.2), "mud")
        boot(side, s, "leather", "sole", size=1.15, cuff_sw="mud_dark", h=0.17)
        # a patch on the right knee
        if s < 0:
            pt = Part("patch", leg_weights(side))
            box(pt, (0.171 * s, -0.135, 0.42), (.10, .02, .09), "red_dark", 0.0, ROT(8, 'Y'))
    # --- head: chunky skull, hard hat with a big glowing head-lamp
    hc = Vector((0, -0.05, 1.58))
    skull(hc, size=(.60, .54, .52), eye_r=0.078, eye_dx=0.125, teeth=5, power=0.6)
    h = Part("hat", rigid("head"))
    hat_brim(h, (0, -0.04, 1.73), .40, .38, 0.035, 14, "hat", "hat_dark")
    dome(h, (0, -0.03, 1.76), .32, .31, .22, 12, 3, "hat")
    box(h, (0, -0.03, 1.94), (.08, .50, .04), "hat_dark", 0.015)  # ridge
    # head-lamp: housing + glowing lens + strap band
    loft(h, [hring(1.775, .325, .315, 14, cy=-0.03), hring(1.83, .31, .30, 14, cy=-0.03)], "leather_dark", cap0=False, cap1=False)
    cyl(h, (0, -0.30, 1.84), (0, -0.40, 1.84), 0.085, 0.10, 10, "metal")
    disc(h, (0, -0.405, 1.84), 0.08, 0.08, 10, "light", normal=Vector((0, -1, 0)))
    ell(h, (0, -0.40, 1.84), (.05, .012, .05), 8, 3, "lens")
    # mud smear on the hat
    ell(h, (0.18, -0.15, 1.88), (.07, .05, .04), 6, 3, "mud")

BUILDERS["Digby"] = build_digby

# ================================================================ THE DEBT COLLECTOR
PALETTES["Collector"] = pal(bone="#F2EBDD", bone_shade="#CFC4AC", cavity="#101410", glow="#FFD26A", glow2="#FFF1B8",
                            tooth="#FFF8E6", fabric="#1E3326", fabric_dark="#14241A", stripe="#5E8F6C",
                            hat="#141414", hat_dark="#050505", white="#F4F1E8", white_dark="#C9C4B6",
                            red="#B22234", red_dark="#7E1824", gold="#FFD26A", gold_dark="#B88A2E",
                            leather="#2F6B3A", leather_dark="#1F4A27", paper="#F2E7C9", shoe="#1A1A1A", sole="#0E0E0E",
                            chain="#D9B04A", ink="#20201C", skin="#E9E4D6")

def build_collector():
    # --- pinstripe suit jacket (fitted, broad at the shoulders) over a waistcoat
    nst = 9
    p = Part("jacket", TORSO_W)
    spec = [(0.60, .25, .20), (0.76, .25, .20), (0.92, .275, .215), (1.06, .30, .23), (1.17, .27, .21), (1.25, .14, .12)]
    rings = [striped_hring(z, rx, ry, nst) for z, rx, ry in spec]
    g, faces, caps = loft(p, rings, "fabric", cap0=False, cap1=True)
    stripe_faces(p, faces, 2 * nst, "stripe", rows=range(0, 4))
    # shirt front V + red tie + lapels
    f = Part("shirtfront", TORSO_W)
    slab(f, ((-0.10, -0.205, 1.24), (0.10, -0.205, 1.24), (0.0, -0.225, 0.92)), (0, -0.02, 0), "white")
    tube(f, [(0, -0.235, 1.20), (0, -0.245, 1.12), (0, -0.25, 1.00)], [0.035, 0.04, 0.045], 4, "red")
    cone(f, (0, -0.25, 1.00), (0, -0.25, 0.94), 0.045, 4, "red")
    box(f, (0, -0.235, 1.215), (.06, .04, .05), "red_dark", 0.012)
    for sx in (1, -1):
        slab(f, ((0.10 * sx, -0.21, 1.25), (0.20 * sx, -0.18, 1.18), (0.05 * sx, -0.245, 0.93), (0.02 * sx, -0.245, 0.98)), (0, 0.03, 0), "fabric_dark")
    # gold watch chain across the waistcoat + buttons
    tube(f, [(-0.17, -0.24, 0.84), (-0.08, -0.265, 0.80), (0.02, -0.265, 0.82), (0.14, -0.245, 0.86)], 0.011, 4, "chain")
    for z in (0.74, 0.82):
        ell(f, (0.04, -0.265, z), (.02, .015, .02), 4, 3, "gold")
    box(f, (-0.18, -0.25, 0.83), (.06, .03, .045), "fabric_dark", 0.01)  # pocket
    # jacket tails (short, split) below the waist
    split_skirt("tails", [(0.66, .262, .212), (0.54, .28, .23), (0.44, .29, .245)], 6, "fabric", "fabric_dark", "fabric_dark",
                gap_deg=12, front_gap_deg=22, shell_k=0.92, top_w=TORSO_W)
    pv = Part("pelvis", rigid("hips"))
    loft(pv, [hring(0.40, .21, .18, 10), hring(0.50, .245, .205, 10), hring(0.64, .25, .205, 10)], "fabric_dark")
    # --- chained ledger hanging on the left hip
    lg = Part("ledger", rigid("hips"))
    for k in range(4):
        box(lg, (0.27, -0.06 + 0.005 * k, 0.60 - 0.045 * k), (.018, .035, .04), "chain", 0.006, ROT(90 * (k % 2), 'Z'))
    box(lg, (0.30, -0.05, 0.36), (.07, .20, .25), "leather", 0.015)
    box(lg, (0.30, -0.05, 0.36), (.075, .18, .23), "paper", 0.0)
    box(lg, (0.338, -0.05, 0.36), (.012, .08, .08), "gold", 0.0)
    for zz in (0.465, 0.255):
        box(lg, (0.30, -0.05, zz), (.08, .21, .02), "gold_dark", 0.0)
    neck("white", r=0.075, top=1.36)
    collar = Part("collar", NECK_W)
    loft(collar, [hring(1.21, .15, .14, 10), hring(1.29, .14, .13, 10)], "white", cap0=False, cap1=False)
    for side, s in both():
        # pinstripe sleeves (whole arm, smooth weights), white cuffs, white skull hands
        arm_tube(side, s, [.05, .045, .042, .045, .04, .04], "bone", x0=0.5, x1=0.82, name="wristbone")
        sl = Part("sleeve_" + side, segw(["chest", "upperarm." + side, "lowerarm." + side, "wrist." + side], k=5, bias={"chest": 1.6}))
        rings = []
        for x, r in ((0.17, .11), (0.30, .10), (0.45, .095), (0.60, .09), (0.70, .095)):
            ring_ = striped_hring(0, r, r, 5)
            rings.append([Vector((x * s, v.y, ZC + v.x)) for v in ring_])
        g, faces, caps = loft(sl, rings, "fabric", cap0=True, cap1=True)
        stripe_faces(sl, faces, 10, "stripe")
        shoulder_pad(side, s, 0.12, "fabric", x=0.24)
        cuff(side, s, 0.725, 0.08, "white", w=0.04)
        mitt(side, s, "bone", scale=1.05, sw_dark="bone_shade")
        # pinstripe trousers, white spats over black shoes
        leg_loft(side, s, [(0.56, .11, 0.0), (0.42, .10, 0.0), (0.30, .095, -0.005), (0.18, .09, 0.0)], "fabric", stripe_sw="stripe", nst=5)
        boot(side, s, "shoe", "sole", size=1.0, h=0.12, cuff_sw="white")
        sp = Part("spat_" + side, rigid("foot." + side))
        box(sp, (0.171 * s, -0.05, 0.10), (.20, .16, .10), "white", 0.03)
        ell(sp, (0.171 * s + 0.1 * s, -0.05, 0.10), (.012, .012, .012), 4, 3, "gold")
    # --- head: skull, bowler hat, gold monocle
    hc = Vector((0, -0.04, 1.57))
    skull(hc, size=(.56, .52, .50), eye_r=0.074, eye_dx=0.12, teeth=4, power=0.62, eye_shape="slit")
    h = Part("bowler", rigid("head"))
    hat_brim(h, (0, -0.03, 1.775), .36, .35, 0.03, 14, "hat", "hat_dark")
    for v in h.bm.verts:  # curl the brim up at the sides
        d = abs(v.co.x)
        if d > 0.25:
            v.co.z += (d - 0.25) * 0.5
    dome(h, (0, -0.03, 1.80), .25, .245, .21, 12, 3, "hat")
    loft(h, [hring(1.80, .255, .25, 12, cy=-0.03), hring(1.85, .25, .245, 12, cy=-0.03)], "red_dark", cap0=False, cap1=False)
    m = Part("monocle", rigid("head"))
    ring_pts = ring((-0.12, -0.335, 1.58), (1, 0, 0), (0, 0, 1), 0.075, 0.075, 10)
    tube(m, ring_pts + [ring_pts[0]], 0.011, 4, "gold", cap0=False, cap1=False)
    tube(m, [(-0.19, -0.33, 1.55), (-0.22, -0.30, 1.42), (-0.20, -0.26, 1.30)], 0.007, 3, "chain")

BUILDERS["Collector"] = build_collector

# ================================================================ DOCTOR BEAK
PALETTES["Beak"] = pal(fabric="#3B4232", fabric_dark="#272C21", fabric2="#7E8C69", fabric2_dark="#5D6A4C",
                       hat="#1A1A1A", hat_dark="#0A0A0A", bone="#C9C2B0", bone_shade="#A39B87", leather="#7A5A3A",
                       leather_dark="#553C25", skin="#2A2A2A", skin_dark="#1A1A1A", lens="#5CF2D6", glow="#5CF2D6",
                       gold="#C9A04A", gold_dark="#8E6E2E", metal="#8F9499", shoe="#211C18", sole="#0E0C0A",
                       red="#B53A3A", white="#E8E4D8", cavity="#0A0A0A", glass="#9FD8C8")

def build_beak():
    # --- long waxed coat: fitted chest, belt, long split skirt to the shins
    p = Part("coat", TORSO_W)
    spec = [(0.60, .26, .215), (0.76, .26, .21), (0.92, .275, .22), (1.06, .29, .235), (1.17, .26, .215), (1.25, .14, .13)]
    g, faces, caps = loft(p, [hring(z, rx, ry, 12) for z, rx, ry in spec], "fabric", cap0=False, cap1=True)
    # buttons down the front
    for z in (0.78, 0.88, 0.98, 1.08):
        ell(p, (0.06, -0.225 if z < 1.0 else -0.24, z), (.022, .015, .022), 4, 3, "gold")
    box(p, (-0.02, -0.226, 0.93), (.012, .01, .34), "fabric_dark", 0.0)
    hem = [0.0, 0.03, 0.0, 0.02, 0.0, 0.03, 0.0]
    split_skirt("skirt", [(0.66, .27, .22), (0.42, .33, .29), (0.20, .38, .34)], 7, "fabric", "fabric_dark", "fabric2_dark",
                gap_deg=6, front_gap_deg=10, shell_k=0.93, hem=hem, top_w=TORSO_W)
    pv = Part("pelvis", rigid("hips"))
    loft(pv, [hring(0.40, .21, .18, 10), hring(0.50, .245, .205, 10), hring(0.64, .25, .205, 10)], "fabric_dark")
    bt = Part("belt", rigid("hips"))
    loft(bt, [hring(0.62, .285, .235, 12), hring(0.68, .285, .235, 12)], "leather_dark", cap0=False, cap1=False)
    box(bt, (0, -0.24, 0.65), (.08, .03, .07), "gold", 0.01)
    # medicine satchel on the left hip + strap across the chest (right shoulder -> left hip)
    sb = Part("satchel", rigid("hips"))
    box(sb, (0.30, -0.02, 0.52), (.10, .24, .19), "leather", 0.03)
    box(sb, (0.31, -0.03, 0.59), (.11, .25, .07), "leather_dark", 0.02)
    box(sb, (0.36, -0.03, 0.52), (.012, .06, .06), "white", 0.0)
    box(sb, (0.367, -0.03, 0.52), (.006, .02, .045), "red", 0.0)
    box(sb, (0.367, -0.03, 0.52), (.006, .045, .02), "red", 0.0)
    st = Part("strap", TORSO_W)
    tube(st, [(-0.19, -0.18, 1.20), (-0.05, -0.255, 1.02), (0.12, -0.265, 0.82), (0.25, -0.24, 0.68)], [(0.035, 0.012)] * 4, 4, "leather_dark")
    tube(st, [(-0.19, 0.17, 1.20), (-0.02, 0.245, 1.00), (0.15, 0.25, 0.80), (0.26, 0.20, 0.68)], [(0.035, 0.012)] * 4, 4, "leather_dark")
    # little flasks on the belt
    for x, col in ((-0.17, "glass"), (-0.24, "glow")):
        fl = cyl(bt, (x, -0.20 if x > -0.2 else -0.15, 0.55), (x, -0.20 if x > -0.2 else -0.15, 0.62), 0.03, 0.03, 6, col)
    # capelet over the shoulders
    cp = Part("capelet", rigid("chest"))
    hemc = [0.0, 0.04, 0.0, 0.04, 0.0, 0.04, 0.0, 0.04, 0.0, 0.04, 0.0, 0.04, 0.0, 0.04]
    r0 = hring(1.30, .17, .16, 14)
    r1 = hring(1.21, .33, .27, 14)
    r2 = [co - Vector((0, 0, hemc[k])) for k, co in enumerate(hring(1.06, .39, .32, 14))]
    g, faces, caps = loft(cp, [r0, r1, r2], "fabric2", cap0=False, cap1=False)
    shell(cp, faces, lambda co: Vector((co.x * .9, co.y * .9, co.z - 0.01)), "fabric_dark", "fabric2_dark")
    neck("skin", r=0.08, top=1.36)
    for side, s in both():
        # long coat sleeves with turned-back cuffs, black leather gloves
        sl = Part("sleeve_" + side, segw(["chest", "upperarm." + side, "lowerarm." + side, "wrist." + side], k=5, bias={"chest": 1.6}))
        tube(sl, [(x * s, 0, ZC) for x in (0.17, 0.30, 0.45, 0.60, 0.70)], [.11, .10, .095, .095, .10], 8, "fabric")
        sl.recolor(lambda c, n, cur: n.z < -0.6, "fabric_dark")
        cuff(side, s, 0.70, 0.11, "fabric2", w=0.07)
        arm_tube(side, s, [.05, .045, .045], "skin", x0=0.70, x1=0.80, name="wrist")
        mitt(side, s, "skin", scale=1.1, sw_dark="skin_dark")
        leg_loft(side, s, [(0.56, .10, 0.0), (0.42, .095, 0.0), (0.30, .09, -0.005), (0.18, .085, 0.0)], "fabric_dark")
        boot(side, s, "shoe", "sole", size=1.0, h=0.20, cuff_sw="leather_dark")
    # --- head: dark leather hood, wide-brim hat, long curved beak, round glowing lenses
    hc = Vector((0, -0.02, 1.57))
    h = Part("hood", rigid("head"))
    sbox(h, hc, (.52, .50, .50), 12, 8, "skin", power=0.75)
    h.recolor(lambda c, n, cur: n.z < -0.5, "skin_dark")
    # beak: lofted, curving down, from the face to a point
    bk = Part("beak", rigid("head"))
    pts = [Vector((0, -0.20, 1.51)), Vector((0, -0.36, 1.48)), Vector((0, -0.52, 1.43)), Vector((0, -0.66, 1.36)), Vector((0, -0.75, 1.28))]
    rad = [(0.14, 0.12), (0.11, 0.095), (0.08, 0.07), (0.05, 0.045), (0.02, 0.02)]
    g, faces, caps = tube(bk, pts, rad, 8, "bone", up=Vector((0, 0, 1)))
    bk.recolor(lambda c, n, cur: n.z < -0.6, "bone_shade")
    for sx in (1, -1):
        ell(bk, (0.04 * sx, -0.40, 1.505), (.015, .02, .01), 4, 3, "cavity")  # nostrils
        stitch_line(bk, (0.10 * sx, -0.27, 1.50), (0.06 * sx, -0.50, 1.44), 3, "leather_dark", (sx, -0.2, 0.6), w=0.01, cross=0.03)
        # round lenses in brass rims
        c = Vector((0.12 * sx, -0.255, 1.64))
        cyl(h, c + Vector((0, 0.05, 0)), c + Vector((0, -0.03, 0)), 0.085, 0.085, 10, "gold")
        disc(h, c + Vector((0, -0.032, 0)), 0.065, 0.065, 10, "lens", normal=Vector((0, -1, 0)))
        ell(h, c + Vector((0.02 * sx, -0.036, 0.02)), (.018, .006, .018), 4, 3, "white")
    # wide-brim hat with a flat-ish crown and a band
    ht = Part("hat", rigid("head"))
    hat_brim(ht, (0, -0.01, 1.76), .52, .50, 0.03, 16, "hat", "hat_dark")
    for v in ht.bm.verts:  # droop the brim at front and back
        d = abs(v.co.y + 0.01)
        if d > 0.32:
            v.co.z -= (d - 0.32) * 0.35
    loft(ht, [hring(1.78, .28, .27, 12, cy=-0.01), hring(1.93, .25, .245, 12, cy=-0.01), hring(1.99, .23, .225, 12, cy=-0.01)], "hat", cap0=False, cap1=True)
    loft(ht, [hring(1.785, .29, .28, 12, cy=-0.01), hring(1.84, .275, .265, 12, cy=-0.01)], "fabric2_dark", cap0=False, cap1=False)

BUILDERS["Beak"] = build_beak

# ================================================================ TALLOW (a living candle)
PALETTES["Tallow"] = pal(wax="#F2E3C0", wax_dark="#D9C597", wax_drip="#FFB347", skin="#E8D5A8", skin_dark="#CDB683",
                         flame="#FFB347", flame_core="#FFF1A8", ink="#2A1E14", pupil="#1A120C", mouth="#5A2A18",
                         gold="#C99A3E", gold_dark="#8E6A26", metal="#B08A3A", cavity="#3A2614", tongue="#E07A5A",
                         sole="#8E6A26", red="#D2683A")

def build_tallow():
    # one wax column from the hips to the top of the head; weights blend up the spine so it bends like soft wax
    COL_W = zblend([(0.55, {"hips": 1}), (0.70, {"hips": .5, "spine": .5}), (0.88, {"spine": 1}),
                    (1.04, {"chest": .7, "spine": .3}), (1.18, {"chest": 1}), (1.30, {"chest": .5, "head": .5}), (1.42, {"head": 1})])
    p = Part("column", COL_W)
    n = 14
    spec = [(0.42, .24, .22), (0.55, .25, .23), (0.72, .255, .235), (0.90, .26, .24), (1.06, .265, .245),
            (1.20, .27, .25), (1.34, .275, .255), (1.50, .28, .26), (1.66, .28, .26), (1.78, .275, .255)]
    rings = [hring(z, rx, ry, n) for z, rx, ry in spec]
    top = [Vector((v.x * 0.86, v.y * 0.86, v.z + (0.03 if k % 3 else 0.0))) for k, v in enumerate(rings[-1])]
    rings.append(top)
    g, faces, caps = loft(p, rings, "wax", cap0=True, cap1=True)
    p.recolor(lambda c, nn, cur: nn.z > 0.6, "wax_dark")
    # melted rim lumps at the top
    for k in range(0, n, 2):
        a = -math.pi / 2 + 2 * math.pi * k / n
        ell(p, (0.25 * math.cos(a), 0.235 * math.sin(a), 1.80), (.06, .06, .045), 6, 3, "wax")
    # drips running down the body (cream and orange)
    for k, (a, z0, L, sw) in enumerate(((-70, 1.79, 0.38, "wax"), (-115, 1.78, 0.22, "wax_drip"), (20, 1.79, 0.55, "wax"),
                                        (160, 1.79, 0.45, "wax_drip"), (-30, 1.15, 0.30, "wax_drip"), (-150, 1.10, 0.35, "wax"),
                                        (90, 1.75, 0.70, "wax"), (230, 0.95, 0.30, "wax_drip"))):
        ar = math.radians(a)
        nrm = Vector((math.cos(ar), math.sin(ar) * 0.93, 0))
        base = Vector((0.27 * math.cos(ar), 0.25 * math.sin(ar), 0))
        pts = [base + nrm * 0.012 + Vector((0, 0, z0)), base + nrm * 0.018 + Vector((0, 0, z0 - L * 0.6)), base + nrm * 0.016 + Vector((0, 0, z0 - L))]
        tube(p, pts, [(0.035, 0.02), (0.03, 0.02), (0.028, 0.018)], 5, sw, up=nrm)
        ell(p, pts[-1] + nrm * 0.005 - Vector((0, 0, 0.01)), (.04, .04, .045), 5, 3, sw)
    # face: big dark eyes, little worried mouth, rosy cheeks
    f = Part("face", rigid("head"))
    for sx in (1, -1):
        ell(f, (0.10 * sx, -0.245, 1.57), (.05, .03, .065), 6, 4, "pupil")
        ell(f, (0.115 * sx, -0.27, 1.59), (.016, .01, .018), 4, 3, "wax")
        ell(f, (0.17 * sx, -0.225, 1.49), (.04, .02, .025), 5, 3, "red")
        box(f, (0.10 * sx, -0.262, 1.66), (.09, .02, .022), "ink", 0.005, ROT(-12 * sx, 'Y'))
    ell(f, (0, -0.255, 1.46), (.05, .02, .035), 6, 3, "mouth")
    # wick + flame (glow slot)
    w = Part("wick", rigid("head"))
    tube(w, [(0, 0, 1.80), (0, 0, 1.88), (0.02, -0.01, 1.95)], [0.018, 0.016, 0.014], 4, "ink")
    fl = ell(w, (0.02, -0.01, 2.06), (.075, .075, .13), 8, 6, "flame")
    for v in fl:  # teardrop: pinch the top
        t = max(0.0, (v.co.z - 2.06) / 0.13)
        v.co.x = 0.02 + (v.co.x - 0.02) * (1 - 0.75 * t)
        v.co.y = -0.01 + (v.co.y + 0.01) * (1 - 0.75 * t)
        if t > 0.9:
            v.co.z += 0.06
    ell(w, (0.02, -0.01, 2.03), (.04, .04, .065), 6, 4, "flame_core")
    # brass drip tray around the hips (it stands in its own candlestick) with a finger loop
    tr = Part("tray", rigid("hips"))
    g, faces, caps = loft(tr, [hring(0.44, .28, .26, 14), hring(0.50, .38, .36, 14), hring(0.53, .39, .37, 14)], "gold", cap0=True, cap1=False)
    shell(tr, faces[14:], lambda co: Vector((co.x * .93, co.y * .93, co.z - 0.01)), "gold_dark", "gold_dark")
    lp = ring((0.44, 0.0, 0.47), (1, 0, 0), (0, 0, 1), 0.065, 0.065, 8)
    tube(tr, lp + [lp[0]], 0.016, 4, "gold", cap0=False, cap1=False)
    for k in range(3):  # wax spilt over the tray rim
        a = math.radians(-60 + 110 * k)
        ell(tr, (0.36 * math.cos(a), 0.34 * math.sin(a), 0.52), (.06, .06, .02), 6, 3, "wax_drip" if k == 1 else "wax")
    for side, s in both():
        # soft wax arms, drippy blob hands
        arm_tube(side, s, [.075, .068, .062, .062, .058, .06], "skin", "skin_dark", x0=0.20)
        h = Part("hand_" + side, rigid("hand." + side))
        sbox(h, (0.85 * s, 0, ZC - 0.01), (.16, .16, .12), 8, 5, "skin", power=0.7)
        for y in (-0.05, 0.0, 0.05):
            tube(h, [(0.92 * s, y, ZC), (0.96 * s, y, ZC - 0.03), (0.965 * s, y, ZC - 0.07)], [0.03, 0.028, 0.024], 5, "skin")
        tube(h, [(0.82 * s, -0.07, ZC), (0.86 * s, -0.11, ZC - 0.03)], [0.03, 0.026], 5, "skin")
        ell(h, (0.80 * s, 0, ZC - 0.07), (.03, .03, .05), 5, 3, "wax_drip")
        # short wax legs and melted puddle feet
        leg_tube(side, s, [.10, .095, .085, .08, .085, .09], [0.50, 0.40, 0.31, 0.25, 0.17, 0.11], "skin", "skin_dark")
        ft = Part("foot_" + side, rigid("foot." + side))
        sbox(ft, (0.171 * s, -0.03, 0.07), (.20, .24, .12), 8, 5, "skin", power=0.7)
        ell(ft, (0.171 * s, -0.03, 0.015), (.13, .15, .02), 8, 3, "wax")
        tq = Part("toe_" + side, rigid("toes." + side))
        sbox(tq, (0.171 * s, -0.17, 0.05), (.19, .14, .09), 8, 4, "skin", power=0.7)
        ell(tq, (0.171 * s, -0.19, 0.012), (.11, .09, .015), 8, 3, "wax")

BUILDERS["Tallow"] = build_tallow

# ================================================================ THE GRAVEYARD LOOKOUT
PALETTES["Lookout"] = pal(bone="#F2EBDD", bone_shade="#CFC4AC", cavity="#0E1018", glow="#8FD3FF", tooth="#FFF8E6",
                          fabric="#26364F", fabric_dark="#18233A", fabric2="#3E5C8A", fabric2_dark="#2C4468",
                          hat="#1A2638", hat_dark="#0E1624", gold="#E0B84A", gold_dark="#A07E2A", metal="#C9C2B0", leather="#5A3E2A", leather_dark="#3E2A1C", light="#FFF3B0", lens="#FFF9DA",
                          shoe="#1C1A18", sole="#0C0B0A", glass="#9FC8E8", ink="#14161C", white="#E8E4D8")

def build_lookout():
    # --- trench coat: double-breasted, belted, knee length, split
    p = Part("coat", TORSO_W)
    spec = [(0.60, .265, .22), (0.76, .265, .215), (0.92, .28, .225), (1.06, .30, .24), (1.17, .27, .22), (1.25, .15, .13)]
    loft(p, [hring(z, rx, ry, 12) for z, rx, ry in spec], "fabric", cap0=False, cap1=True)
    for sx in (1, -1):
        for z in (0.82, 0.95, 1.08):
            ell(p, (0.09 * sx, -0.235 if z < 1.0 else -0.25, z), (.022, .015, .022), 4, 3, "gold")
    slab(p, ((-0.13, -0.24, 1.18), (0.20, -0.215, 1.14), (0.18, -0.235, 0.78), (-0.05, -0.245, 0.78)), (0, 0.01, 0), "fabric2_dark")
    split_skirt("skirt", [(0.66, .275, .225), (0.46, .32, .27), (0.30, .35, .30)], 7, "fabric", "fabric_dark", "fabric2_dark",
                gap_deg=8, front_gap_deg=14, shell_k=0.93, top_w=TORSO_W)
    pv = Part("pelvis", rigid("hips"))
    loft(pv, [hring(0.40, .21, .18, 10), hring(0.50, .245, .205, 10), hring(0.64, .25, .205, 10)], "fabric_dark")
    bt = Part("belt", rigid("hips"))
    loft(bt, [hring(0.62, .29, .24, 12), hring(0.69, .29, .24, 12)], "fabric2", cap0=False, cap1=False)
    box(bt, (0.0, -0.245, 0.655), (.09, .03, .08), "gold", 0.01)
    # spyglass on the left hip
    cyl(bt, (0.30, -0.02, 0.70), (0.30, -0.02, 0.48), 0.045, 0.035, 8, "gold")
    cyl(bt, (0.30, -0.02, 0.50), (0.30, -0.02, 0.36), 0.035, 0.028, 8, "gold_dark")
    box(bt, (0.29, -0.02, 0.62), (.06, .10, .04), "leather_dark", 0.01)
    # turned-up collar
    cl = Part("collar", rigid("chest"))
    g, faces, caps = loft(cl, [hring(1.20, .20, .18, 12, cy=0.01), hring(1.36, .22, .20, 12, cy=0.03)], "fabric", cap0=False, cap1=False)
    for f in list(faces):
        c = f.calc_center_median()
        if c.y < -0.12 and abs(c.x) < 0.10:
            cl.bm.faces.remove(f); faces.remove(f)
    shell(cl, faces, lambda co: Vector((co.x * .88, co.y * .88, co.z)), "fabric_dark", "fabric_dark")
    # --- searchlight backpack: frame + straps + big drum lamp over the right shoulder, lens facing forward
    sl = Part("searchlight", rigid("chest"))
    box(sl, (0, 0.27, 0.98), (.30, .08, .36), "leather", 0.02)
    tube(sl, [(-0.12, 0.29, 0.86), (-0.30, 0.29, 1.30), (-0.46, 0.22, 1.42)], 0.03, 5, "metal")
    tube(sl, [(0.12, 0.29, 0.86), (0.05, 0.30, 1.22)], 0.022, 5, "metal")
    for sx in (1, -1):
        tube(sl, [(0.12 * sx, 0.24, 1.14), (0.15 * sx, 0.05, 1.26), (0.15 * sx, -0.20, 1.12), (0.13 * sx, -0.25, 0.92)], [(0.04, 0.012)] * 4, 4, "leather_dark")
    lc = Vector((-0.46, 0.14, 1.60))
    cyl(sl, lc + Vector((0, 0.19, 0)), lc + Vector((0, -0.12, 0)), 0.14, 0.18, 12, "metal")
    loft(sl, [ring(lc + Vector((0, -0.12, 0)), (1, 0, 0), (0, 0, 1), .195, .195, 12), ring(lc + Vector((0, -0.17, 0)), (1, 0, 0), (0, 0, 1), .195, .195, 12)],
         "hat", cap0=False, cap1=False)
    disc(sl, lc + Vector((0, -0.16, 0)), 0.17, 0.17, 12, "light", normal=Vector((0, -1, 0)))
    ell(sl, lc + Vector((0, -0.165, 0)), (.075, .015, .075), 6, 3, "lens")
    cyl(sl, lc + Vector((0, 0.17, 0)), lc + Vector((0, 0.24, 0)), 0.06, 0.04, 8, "hat")
    tube(sl, [lc + Vector((0, 0.24, 0)), lc + Vector((0.15, 0.30, -0.15)), (0.0, 0.32, 1.10)], 0.015, 4, "ink")
    for sx in (1, -1):
        cyl(sl, lc + Vector((0.15 * sx, 0, 0)), lc + Vector((0.17 * sx, 0, 0)), 0.04, 0.04, 6, "hat")
    neck("bone_shade", r=0.07, top=1.36)
    for side, s in both():
        sv = Part("sleeve_" + side, segw(["chest", "upperarm." + side, "lowerarm." + side, "wrist." + side], k=5, bias={"chest": 1.6}))
        tube(sv, [(x * s, 0, ZC) for x in (0.17, 0.30, 0.45, 0.60, 0.71)], [.11, .10, .095, .09, .095], 8, "fabric")
        sv.recolor(lambda c, n, cur: n.z < -0.6, "fabric_dark")
        cuff(side, s, 0.69, 0.10, "fabric2", w=0.05)
        shoulder_pad(side, s, 0.12, "fabric2", x=0.25)
        arm_tube(side, s, [.045, .042, .042], "bone", x0=0.70, x1=0.80, name="wrist")
        mitt(side, s, "leather", scale=1.1, sw_dark="leather_dark")
        leg_loft(side, s, [(0.56, .10, 0.0), (0.42, .095, 0.0), (0.30, .09, -0.005), (0.18, .085, 0.0)], "fabric_dark")
        boot(side, s, "shoe", "sole", size=1.05, h=0.22, cuff_sw="leather_dark")
    # --- head: skull with a peaked watchman's cap and badge
    hc = Vector((0, -0.04, 1.57))
    skull(hc, size=(.56, .52, .50), eye_r=0.075, eye_dx=0.12, teeth=4, power=0.62)
    h = Part("cap", rigid("head"))
    loft(h, [hring(1.74, .30, .29, 14, cy=-0.03), hring(1.84, .31, .30, 14, cy=-0.03), hring(1.92, .33, .32, 14, cy=0.0),
             hring(1.95, .30, .29, 14, cy=0.0)], "hat", cap0=False, cap1=True)
    loft(h, [hring(1.735, .305, .295, 14, cy=-0.03), hring(1.79, .305, .295, 14, cy=-0.03)], "fabric2_dark", cap0=False, cap1=False)
    slab(h, ((-0.25, -0.20, 1.75), (0.25, -0.20, 1.75), (0.20, -0.46, 1.71), (-0.20, -0.46, 1.71)), (0, 0, 0.025), "hat_dark")
    box(h, (0, -0.33, 1.85), (.09, .03, .09), "gold", 0.02, ROT(45, 'Y'))

BUILDERS["Lookout"] = build_lookout

# ================================================================ VEXA THE VEILED
PALETTES["Vexa"] = pal(bone="#F2EBDD", bone_shade="#CFC4AC", cavity="#140A10", glow="#FF4A5A", glow2="#FFB0B8", tooth="#FFF8E6",
                       fabric="#F4F0FF", fabric_dark="#CFC6E6", fabric2="#E9E3F7", fabric2_dark="#B9AED6",
                       white="#FBF9FF", white_dark="#D6D0EA", red="#B22234", red_dark="#7A1424",
                       hat="#5A1A24", hat_dark="#3A0E16", leather="#2E2236", leather_dark="#1C1424",
                       pants="#3A2F4A", pants_dark="#272033", shoe="#2A2230", sole="#120E16", gold="#D9B45A", metal="#C9C2D6")

def build_vexa():
    # --- fitted duelist doublet with a high collar, crimson sash, short flared coat tails
    p = Part("doublet", TORSO_W)
    spec = [(0.60, .23, .19), (0.76, .225, .185), (0.92, .25, .20), (1.06, .275, .22), (1.17, .25, .205), (1.25, .13, .12)]
    loft(p, [hring(z, rx, ry, 12) for z, rx, ry in spec], "fabric", cap0=False, cap1=True)
    for z in (0.80, 0.90, 1.00, 1.10):
        tube(p, [(-0.06, -0.21 if z < 1.0 else -0.225, z), (0.06, -0.21 if z < 1.0 else -0.225, z)], 0.012, 4, "red_dark")
    sa = Part("sash", TORSO_W)
    # diagonal sash: right shoulder -> left hip, front and back
    tube(sa, [(-0.17, -0.12, 1.24), (-0.05, -0.235, 1.06), (0.12, -0.235, 0.84), (0.25, -0.15, 0.68)], [(0.07, 0.016)] * 4, 4, "red")
    tube(sa, [(-0.17, 0.12, 1.24), (-0.03, 0.225, 1.04), (0.14, 0.215, 0.84), (0.25, 0.13, 0.68)], [(0.07, 0.016)] * 4, 4, "red")
    loft(sa, [hring(0.66, .245, .205, 12), hring(0.74, .24, .20, 12)], "red", cap0=False, cap1=False)
    kn = Part("sashknot", rigid("hips"))
    ell(kn, (0.22, -0.16, 0.68), (.06, .05, .06), 6, 4, "red_dark")
    split_skirt("tails", [(0.66, .25, .21), (0.52, .30, .26), (0.38, .34, .30)], 7, "fabric", "fabric2_dark", "fabric_dark",
                gap_deg=10, front_gap_deg=35, shell_k=0.92, hem=[0.0, 0.0, 0.02, 0.05, 0.08, 0.05, 0.02], top_w=TORSO_W)
    # sash tails hanging from the knot (follow the left leg a little)
    tw = lambda co: {"hips": 0.6, "upperleg.l": 0.4} if co.z < 0.55 else {"hips": 1.0}
    st = Part("sashtails", tw)
    tube(st, [(0.23, -0.19, 0.66), (0.27, -0.22, 0.52), (0.29, -0.20, 0.36)], [(0.05, 0.012), (0.05, 0.012), (0.06, 0.012)], 4, "red", up=Vector((0, -1, 0)))
    tube(st, [(0.20, -0.20, 0.65), (0.20, -0.235, 0.50), (0.19, -0.22, 0.42)], [(0.045, 0.012)] * 3, 4, "red_dark", up=Vector((0, -1, 0)))
    pv = Part("pelvis", rigid("hips"))
    loft(pv, [hring(0.40, .20, .17, 10), hring(0.50, .235, .195, 10), hring(0.64, .24, .195, 10)], "pants")
    cl = Part("collar", rigid("chest"))
    g, faces, caps = loft(cl, [hring(1.20, .17, .155, 12), hring(1.33, .20, .19, 12, cy=0.02)], "fabric2", cap0=False, cap1=False)
    shell(cl, faces, lambda co: Vector((co.x * .88, co.y * .88, co.z)), "fabric2_dark", "fabric_dark")
    neck("bone_shade", r=0.065, top=1.36)
    for side, s in both():
        sv = Part("sleeve_" + side, segw(["chest", "upperarm." + side, "lowerarm." + side, "wrist." + side], k=5, bias={"chest": 1.6}))
        tube(sv, [(x * s, 0, ZC) for x in (0.17, 0.30, 0.45, 0.60, 0.71)], [.095, .10, .085, .075, .08], 8, "fabric")
        sv.recolor(lambda c, n, cur: n.z < -0.6, "fabric_dark")
        shoulder_pad(side, s, 0.11, "fabric2" if s > 0 else "fabric", x=0.24)
        if s > 0:  # a single crimson epaulette on the off-hand shoulder
            ep = Part("epaulette", ablend(0, [(0.20, {"chest": .6, "upperarm.l": .4}), (0.32, {"upperarm.l": 1})], 1))
            sbox(ep, (0.26, 0, ZC + 0.10), (.18, .20, .05), 6, 4, "red", power=0.6)
            for k in range(4):
                tube(ep, [(0.33, -0.07 + 0.045 * k, ZC + 0.08), (0.355, -0.07 + 0.045 * k, ZC + 0.01)], 0.01, 3, "gold")
        # long fencing gloves with flared cuffs
        cuff(side, s, 0.66, 0.10, "leather", w=0.06)
        arm_tube(side, s, [.05, .047, .047], "leather", x0=0.68, x1=0.80, name="wrist")
        mitt(side, s, "leather", scale=1.0, sw_dark="leather_dark")
        leg_loft(side, s, [(0.56, .095, 0.0), (0.42, .085, 0.0), (0.30, .078, -0.005), (0.20, .075, 0.0)], "pants")
        boot(side, s, "shoe", "sole", size=0.95, h=0.30, cuff_sw="red_dark", round_toe=False)
        tq = Part("bootpoint_" + side, rigid("toes." + side))
        cone(tq, (0.171 * s, -0.22, 0.05), (0.171 * s, -0.30, 0.04), 0.045, 4, "shoe")
    # --- head: skull with crimson slit eyes, crown of thorns, white veil falling behind to the shoulders
    hc = Vector((0, -0.03, 1.57))
    skull(hc, size=(.54, .50, .50), eye_r=0.075, eye_dx=0.115, teeth=4, power=0.64, eye_shape="slit")
    VEIL_W = zblend([(1.22, {"chest": 1}), (1.40, {"chest": .35, "head": .65}), (1.50, {"head": 1})])
    vl = Part("veil", VEIL_W)
    n = 14
    rings = []
    for z, r, cy in ((1.86, .27, -0.01), (1.78, .31, 0.0), (1.62, .335, 0.02), (1.46, .34, 0.05), (1.30, .36, 0.08), (1.16, .40, 0.10)):
        rr = []
        for k in range(n + 1):  # open at the front: from left cheek around the back to right cheek
            a = math.radians(-35) + (math.radians(250)) * k / n
            rr.append(Vector((r * math.cos(a), cy + r * 0.95 * math.sin(a), z)))
        rings.append(rr)
    hemv = [0.0, 0.04, 0.0, 0.05, 0.0, 0.04, 0.0, 0.06, 0.0, 0.04, 0.0, 0.05, 0.0, 0.04, 0.0]
    rings[-1] = [v - Vector((0, 0, hemv[k])) for k, v in enumerate(rings[-1])]
    g, faces, caps = loft(vl, rings, "white", closed=False, cap0=False, cap1=False)
    shell(vl, faces, lambda co: Vector((co.x * .93, 0.02 + (co.y - 0.02) * .93, co.z)), "white_dark", "white_dark")
    # crown of thorns: twisted ring + crimson spikes
    cr = Part("crown", rigid("head"))
    pts = [Vector((0.29 * math.cos(2 * math.pi * k / 16), -0.02 + 0.28 * math.sin(2 * math.pi * k / 16), 1.84 + 0.02 * math.sin(4 * math.pi * k / 16))) for k in range(16)]
    tube(cr, pts + [pts[0]], 0.028, 4, "hat", cap0=False, cap1=False)
    pts2 = [Vector((0.295 * math.cos(2 * math.pi * k / 16 + 0.2), -0.02 + 0.285 * math.sin(2 * math.pi * k / 16 + 0.2), 1.84 - 0.02 * math.sin(4 * math.pi * k / 16))) for k in range(16)]
    tube(cr, pts2 + [pts2[0]], 0.018, 4, "red_dark", cap0=False, cap1=False)
    for k in range(9):
        a = 2 * math.pi * k / 9 - math.pi / 2
        b = Vector((0.29 * math.cos(a), -0.02 + 0.28 * math.sin(a), 1.85))
        out = Vector((math.cos(a), math.sin(a), 0))
        cone(cr, b, b + out * 0.05 + Vector((0, 0, 0.13 if k % 2 == 0 else 0.08)), 0.03, 4, "red")

BUILDERS["Vexa"] = build_vexa
