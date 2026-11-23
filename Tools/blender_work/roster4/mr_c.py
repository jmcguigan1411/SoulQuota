# Monster roster batch C: Intern (Imp), Sweep (Scarecrow), Solace (Reanimator), Mort (Ghoul Courier), Actuary (Moth Auditor),
# Ectoplasm (Puppeteer), Vexa (Butcher). Original designs.
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mr_lib import *
from mr_a import base
sys.path.insert(0, "/home/claude/blender_work/reapers3")
H = hexc
PAL, BUILD, WEAP = {}, {}, {}

# ============================================================ SNIG - Imp (Intern)
PAL["Intern"] = base(bone="E0503F", bone_shade="B03A2C", shirt="F2EBDD", shirt_dark="C8C0B0", accent="3A5A9A", accent_dark="26407A", cloth2="3A2F55", cloth2_dark="261E3A",
                     extra5="F2E3C0", glow="FFF3A0", glow2="FFE08A", paper="FFF8E8", metal="9A8F80", metal_dark="6A6050", wood="7A5236", shoe="2A2030", extra1="8A5A3A")
def b_intern():
    zc = ZC
    legs("cloth2", r=(.075, .07, .065, .06), shoe="bone", sole="bone_shade", scale=1.05, x=0.14)
    pelvis("cloth2", rx=.22, ry=.18, z0=0.48, z1=0.66)
    torso([(0.62, .22, .18), (0.78, .24, .19), (0.92, .25, .20), (1.04, .24, .19), (1.12, .19, .16), (1.18, .09, .08)], "shirt", "shirt_dark")
    tie = Part("tie", rigid("chest"))
    slab(tie, [(-0.03, -0.19, 1.13), (0.03, -0.19, 1.13), (0.07, -0.21, 0.88), (0.0, -0.215, 0.82), (-0.02, -0.205, 0.95)], (0, -0.015, 0), "accent")   # loose, askew
    box(tie, (0.11, -0.20, 1.00), (.06, .015, .08), "accent_dark", 0.005)     # name badge
    box(tie, (0.11, -0.21, 1.01), (.045, .005, .02), "paper", 0)
    # untucked shirt-tail
    st = Part("shirttail", skirt_w(0.66, 0.50, leg_max=0.4))
    loft(st, [hring(0.66, .23, .19, 12), hring(0.54, .25, .21, 12)], "shirt", cap0=False, cap1=False)
    tl = Part("itail", rigid("hips"))
    tube(tl, [(0, 0.17, 0.56), (0, 0.32, 0.48), (0.08, 0.42, 0.30), (0.04, 0.50, 0.16), (-0.04, 0.54, 0.20)], [.035, .03, .025, .02, .015], 5, "bone")
    slab(tl, [(-0.04, 0.54, 0.20), (-0.13, 0.56, 0.25), (-0.06, 0.60, 0.13)], (0, 0, 0.015), "bone_shade")
    for side, s in (("l", 1), ("r", -1)):
        arm(side, s, rad=(.065, .06, .058, .055, .052), sleeve_sw="shirt", hand_sw="bone", hand_scale=1.15, bony_wrist=False, x1=0.76, cuff_sw="shirt_dark", cuff_r=.06)
    # coffee cup in the left hand
    cp = Part("coffee", rigid("hand.l"))
    cc = Vector((0.88, -0.10, zc + 0.06))
    cyl(cp, cc - Vector((0, 0, 0.06)), cc + Vector((0, 0, 0.06)), .045, .055, 8, "paper")
    cyl(cp, cc + Vector((0, 0, 0.06)), cc + Vector((0, 0, 0.075)), .058, .055, 8, "extra1")
    box(cp, cc + Vector((0, -0.05, 0.0)), (.06, .01, .04), "accent", 0)
    neck(0.05, 1.22, "bone")
    hc = Vector((0, -0.02, 1.44))
    hd = Part("ihead", rigid("head"))
    sbox(hd, hc, (.46, .42, .40), 12, 8, "bone", power=0.65)
    hd.recolor(lambda cc_, n_, cur: n_.z < -0.6, "bone_shade")
    fy = hc.y - 0.21
    for sx in (1, -1):
        eye(hd, (0.10 * sx, fy, 1.47), 0.06, "glow", "ink", 0.35, look=(0.3, 0.2))
        cone(hd, (0.20 * sx, -0.02, 1.48), (0.34 * sx, 0.02, 1.56), .06, 4, "bone")          # bat ears
        tube(hd, [(0.12 * sx, -0.05, 1.60), (0.18 * sx, -0.02, 1.70), (0.16 * sx, 0.06, 1.80)], [.05, .035, .01], 6, "extra5")   # curled horns
    # huge grin with tiny teeth
    gp = [(-0.14, fy, 1.38), (0.14, fy, 1.38), (0.08, fy, 1.32), (0.0, fy, 1.30), (-0.08, fy, 1.32)]
    slab(hd, gp, (0, 0.02, 0), "ink")
    for k in range(6):
        cone(hd, (-0.10 + 0.04 * k, fy - 0.004, 1.38), (-0.10 + 0.04 * k, fy - 0.004, 1.355), .01, 3, "paper")
    ell(hd, (0, fy - 0.01, 1.42), (.025, .02, .02), 5, 3, "bone_shade")
WEAP["Intern"] = lambda: _pitchfork()
def _pitchfork():
    p = wpart("pitchfork")
    tube(p, [(0, 0, -0.30), (0, 0, 0.75)], [.02, .02], 6, "wood")
    box(p, (0, 0, 0.77), (.04, .26, .035), "metal", 0.008)
    for y in (-0.11, 0.0, 0.11):
        cone(p, (0, y, 0.78), (0, y, 1.05 if y == 0 else 0.98), .022, 4, "metal")
    for y in (-0.11, 0.11):
        slab(p, [(0, y, 0.96), (0, y + 0.05 * (1 if y > 0 else -1), 0.93), (0, y, 0.90)], (0.01, 0, 0), "metal_dark")
    return p

# ============================================================ HAYWARD CROWE - Scarecrow (Sweep)
PAL["Sweep"] = base(bone="D8C8A0", bone_shade="B8A47A", cloth="8A6A3A", cloth_dark="6A4E28", cloth_inner="2A1E10", cloth2="5A6A8A", cloth2_dark="3A4A6A",
                    shirt="B84A3A", shirt_dark="8A3428", hair="E0C070", wood="6A5A3A", wood_dark="4A3E28", extra1="141418", extra2="FFB43A", extra3="2A2A30",
                    socket="2A1E1A", glow="FFD26A", glow2="FFF0A0", accent="5A8A3A", shoe="4A3A28")
def crow(c, facing, wfn, name, k=1.0):
    q = Part(name, wfn)
    c = Vector(c)
    f = Vector((facing[0], facing[1], 0)).normalized()
    side = Vector((-f.y, f.x, 0))
    R = Matrix((side, f, Vector((0, 0, 1)))).transposed()
    ell(q, c, (.08 * k, .12 * k, .09 * k), 8, 5, "extra1", R)
    hcn = c + f * 0.09 * k + Vector((0, 0, 0.09 * k))
    ell(q, hcn, (.065 * k, .07 * k, .065 * k), 8, 5, "extra1")
    cone(q, hcn + f * 0.05 * k, hcn + f * 0.15 * k + Vector((0, 0, -0.02)), .024 * k, 4, "extra2")
    for sx in (1, -1):
        ell(q, hcn + f * 0.04 * k + side * (0.048 * sx * k) + Vector((0, 0, 0.02)), (.016, .016, .016), 4, 3, "glow")
        slab(q, [c + side * (0.065 * sx * k) + f * 0.06 * k, c + side * (0.07 * sx * k) - f * 0.13 * k, c + side * (0.055 * sx * k) + Vector((0, 0, -0.045 * k))], side * (0.012 * sx), "extra3")
    slab(q, [c - f * 0.07 * k, c - f * 0.20 * k + side * 0.045 * k + Vector((0, 0, -0.045)), c - f * 0.20 * k - side * 0.045 * k + Vector((0, 0, -0.045))], (0, 0, 0.014), "extra1")
def b_sweep():
    zc = ZC
    legs("cloth2", r=(.075, .07, .068, .066), shoe="shoe", scale=1.05)
    for side, s in (("l", 1), ("r", -1)):
        sw = Part("straw_leg_" + side, rigid("lowerleg." + side))
        for k in range(6):
            a = 2 * math.pi * k / 6
            cone(sw, (0.171 * s + 0.05 * math.cos(a), 0.05 * math.sin(a), 0.20), (0.171 * s + 0.09 * math.cos(a), 0.09 * math.sin(a), 0.10), .02, 3, "hair")
        pt = Part("knee_patch_" + side, rigid("upperleg." + side))
        box(pt, (0.171 * s, -0.075, 0.40), (.08, .02, .08), "shirt", 0.005)
    pelvis("cloth2", rx=.24, ry=.20, z0=0.46, z1=0.66)
    # baggy patched shirt + rope belt + straw poking out
    torso([(0.58, .27, .22), (0.78, .29, .23), (0.96, .30, .24), (1.10, .29, .23), (1.20, .23, .19), (1.27, .10, .09)], "cloth", "cloth_dark")
    pc = Part("patches", TORSO_W)
    box(pc, (0.14, -0.245, 0.96), (.10, .02, .10), "shirt", 0.005, ROT(8, 'Y'))
    stitch_line(pc, Vector((0.09, -0.257, 1.01)), Vector((0.19, -0.257, 1.02)), 4, "extra1", Vector((0, -1, 0)))
    box(pc, (-0.12, -0.245, 0.74), (.09, .02, .08), "accent", 0.005, ROT(-6, 'Y'))
    rb = Part("rope", rigid("hips"))
    loft(rb, [hring(0.62, .28, .23, 14), hring(0.66, .28, .23, 14)], "hair", cap0=False, cap1=False)
    for k in range(12):
        a = 2 * math.pi * k / 12
        cone(rb, (0.26 * math.cos(a), 0.22 * math.sin(a), 0.60), (0.33 * math.cos(a), 0.28 * math.sin(a), 0.50), .025, 3, "hair")
    # arms on a crossbar pole (arms stiffly out), straw hands
    for side, s in (("l", 1), ("r", -1)):
        arm(side, s, rad=(.085, .082, .08, .08, .085), sleeve_sw="cloth", hand_sw="bone", hand_scale=1.0, bony_wrist=False, x1=0.74)
        sh = Part("straw_hand_" + side, rigid("hand." + side))
        for k in range(7):
            a = 2 * math.pi * k / 7
            cone(sh, (0.74 * s, 0.05 * math.cos(a), zc + 0.05 * math.sin(a)), (0.94 * s, 0.09 * math.cos(a), zc + 0.09 * math.sin(a)), .025, 3, "hair")
    # crows riding the shoulders
    crow((0.32, -0.02, 1.26), (0.3, -1), rigid("chest"), "crow_l", 0.9)
    crow((-0.30, 0.02, 1.25), (-0.6, -1), rigid("chest"), "crow_r", 0.8)
    neck(0.05, 1.40, "wood")
    hc = Vector((0, -0.02, 1.62))
    hd = Part("sack", rigid("head"))
    sbox(hd, hc, (.40, .38, .42), 12, 8, "bone", power=0.8, taper=lambda zn: (1.0 + 0.05 * zn, 1.0))
    hd.recolor(lambda cc, n_, cur: n_.z < -0.6, "bone_shade")
    fy = hc.y - 0.19
    for sx in (1, -1):
        cx = 0.085 * sx
        box(hd, (cx, fy, 1.67), (.07, .015, .014), "socket", 0, ROT(45, 'Y'))
        box(hd, (cx, fy, 1.67), (.07, .015, .014), "socket", 0, ROT(-45, 'Y'))
    gp = [Vector((x, fy, 1.52 + 0.04 * (x / 0.13) ** 2)) for x in (-0.13, -0.065, 0.0, 0.065, 0.13)]
    for k in range(4):
        tube(hd, [gp[k], gp[k + 1]], .008, 3, "socket")
    for k in range(5):
        box(hd, (gp[k].x, fy - 0.003, gp[k].z), (.006, .01, .04), "socket", 0)
    tube(hd, [(-0.16, -0.10, 1.42), (0.0, -0.21, 1.41), (0.16, -0.10, 1.42)], .015, 3, "hair")      # tied neck
    for k in range(8):
        a = math.pi * k / 7 - math.pi
        cone(hd, (0.18 * math.cos(a), -0.02 + 0.17 * math.sin(a), 1.42), (0.24 * math.cos(a), -0.02 + 0.22 * math.sin(a), 1.33), .025, 3, "hair")
    ht = Part("shat", rigid("head"))
    disc(ht, (0.0, -0.02, 1.80), .36, .34, 16, "wood", normal=Vector((0, 0, 1)))
    disc(ht, (0.0, -0.02, 1.795), .36, .34, 16, "wood_dark", normal=Vector((0, 0, -1)))
    tube(ht, [(0, -0.02, 1.80), (0.0, 0.0, 1.98), (0.06, 0.04, 2.12), (0.14, 0.06, 2.12)], [.19, .14, .07, .02], 10, "wood")
    loft(ht, [hring(1.81, .195, .185, 14, cy=-0.02), hring(1.86, .18, .17, 14, cy=-0.018)], "shirt", cap0=False, cap1=False)
    for k in range(5):
        a = 2 * math.pi * k / 5 + 0.3
        cone(ht, (0.20 * math.cos(a), 0.18 * math.sin(a), 1.84), (0.30 * math.cos(a), 0.26 * math.sin(a), 1.90), .02, 3, "hair")
WEAP["Sweep"] = None

# ============================================================ DOC STITCHES - Reanimator (Solace)
PAL["Solace"] = base(bone="B8E0B0", bone_shade="8AB884", shirt="E8EEF2", shirt_dark="B8C4CC", cloth="3A4458", cloth_dark="262E3E", cloth2="2A2A30", cloth2_dark="18181C",
                     hair="F2EBDD", metal="5A6470", metal_dark="3A424C", gold="C9A24A", glow="7FA8FF", glow2="DDEBFF", glass="5CF2D6", accent="C8323A", extra1="6A4A2A",
                     extra2="FFD26A", shoe="2A2420")
def b_solace():
    zc = ZC
    legs("cloth", r=(.08, .075, .07, .065), shoe="shoe", scale=1.1)
    pelvis("cloth", rx=.25, ry=.20, z0=0.46, z1=0.66)
    torso([(0.60, .25, .20), (0.80, .27, .21), (0.96, .28, .22), (1.10, .28, .22), (1.20, .23, .19), (1.28, .10, .09)], "cloth2", "cloth2_dark")
    # long lab coat, open front, scorch marks
    fc = Part("labcoat", lambda co: TORSO_W(co) if co.z > 0.62 else skirt_w(0.62, 0.22, leg_max=0.6)(co))
    n = 14
    g, faces, caps = loft(fc, [hring(z, rx, ry, n) for z, rx, ry in ((1.24, .25, .205), (1.10, .30, .235), (0.92, .30, .235), (0.70, .31, .245), (0.44, .35, .29), (0.22, .38, .32))], "shirt", cap0=False, cap1=False)
    for f in list(faces):
        cc = f.calc_center_median()
        if cc.y < -0.14 and abs(cc.x) < (0.09 if cc.z > 0.7 else 0.20):
            fc.bm.faces.remove(f); faces.remove(f)
    shell(fc, faces, lambda co: Vector((co.x * .95, co.y * .95, co.z)), "shirt_dark", "shirt_dark")
    fc.recolor(lambda cc, n_, cur: cur == "shirt" and cc.z < 0.32, "shirt_dark")
    sc = Part("scorch", TORSO_W)
    for x, z in ((0.20, 0.82), (-0.24, 1.02)):
        ell(sc, (x, -0.24, z), (.05, .02, .04), 6, 3, "cloth2_dark")
    # pocket with pens + rubber gloves, battery pack on the back with coils
    pk = Part("pocket", TORSO_W)
    box(pk, (0.18, -0.255, 1.02), (.09, .02, .08), "shirt_dark", 0.005)
    for k, col in enumerate(("accent", "glow", "extra2")):
        cyl(pk, (0.15 + 0.025 * k, -0.255, 1.05), (0.15 + 0.025 * k, -0.255, 1.11), .008, .008, 4, col)
    bp = Part("battery", rigid("chest"))
    box(bp, (0, 0.30, 0.98), (.36, .16, .40), "metal", 0.03)
    box(bp, (0, 0.385, 0.98), (.26, .02, .18), "metal_dark", 0.01)
    for sx in (1, -1):
        cyl(bp, (0.10 * sx, 0.30, 1.18), (0.10 * sx, 0.30, 1.42), .025, .02, 6, "metal_dark")
        for k in range(3):
            torus(bp, (0.10 * sx, 0.30, 1.24 + 0.05 * k), .04, .01, 8, 3, "gold")
        ell(bp, (0.10 * sx, 0.30, 1.44), (.04, .04, .04), 6, 4, "glow")
        tube(bp, [(0.14 * sx, 0.24, 0.85), (0.30 * sx, 0.10, 0.80), (0.40 * sx, 0.05, 0.98)], .012, 3, "extra1")   # cables
    tube(bp, [(0.10, 0.30, 1.46), (0.02, 0.32, 1.52), (-0.04, 0.29, 1.49), (-0.10, 0.30, 1.46)], .01, 3, "glow2")
    for side, s in (("l", 1), ("r", -1)):
        arm(side, s, rad=(.085, .08, .078, .076, .074), sleeve_sw="shirt", hand_sw="extra2", hand_scale=1.2, bony_wrist=False, x1=0.76, cuff_sw="shirt_dark", cuff_r=.085)
    # left-hand defibrillator paddle (the right one is the weapon)
    pd = Part("paddle_l", rigid("hand.l"))
    box(pd, (0.92, -0.04, zc - 0.10), (.12, .10, .03), "metal", 0.01)
    cyl(pd, (0.92, -0.04, zc - 0.08), (0.92, -0.04, zc + 0.02), .025, .025, 6, "accent")
    neck(0.05, 1.40, "bone")
    hc = Vector((0, -0.02, 1.60))
    hd = Part("dhead", rigid("head"))
    sbox(hd, hc, (.36, .36, .40), 12, 8, "bone", power=0.7)
    hd.recolor(lambda cc, n_, cur: n_.z < -0.6, "bone_shade")
    fy = hc.y - 0.18
    # brass goggles over the eyes
    box(hd, (0, fy + 0.03, 1.65), (.40, .30, .035), "extra1", 0.01)
    for sx in (1, -1):
        cyl(hd, (0.075 * sx, fy + 0.01, 1.65), (0.075 * sx, fy - 0.04, 1.65), .06, .06, 10, "gold")
        disc(hd, (0.075 * sx, fy - 0.041, 1.65), .048, .048, 10, "glass")
        ell(hd, (0.075 * sx, fy - 0.045, 1.65), (.018, .008, .018), 5, 3, "ink")
    mouth_slab(hd, (0, fy - 0.002, 1.50), .14, .045, "socket", "paper", 4)
    box(hd, (0, fy - 0.035, 1.57), (.04, .045, .045), "bone_shade", 0.01)
    hr = Part("wild_hair", rigid("head"))
    for k in range(14):
        a = math.pi * (-0.15 + 1.3 * k / 13)
        c = Vector((0.19 * math.cos(a), 0.0 + 0.17 * math.sin(a), 1.76))
        cone(hr, c, c + Vector((0.14 * math.cos(a), 0.10 * math.sin(a) + 0.04, 0.10 + 0.06 * (k % 3))), .06, 4, "hair")
    ell(hr, (0, 0.02, 1.80), (.18, .17, .06), 8, 4, "hair")
WEAP["Solace"] = lambda: _paddle()
def _paddle():
    p = wpart("defib_paddle")
    cyl(p, (0, 0, -0.08), (0, 0, 0.16), .03, .03, 6, "accent")
    box(p, (0, 0.02, 0.22), (.05, .20, .14), "metal", 0.015)
    box(p, (0, 0.02, 0.30), (.07, .24, .02), "metal_dark", 0.005)
    tube(p, [(0, 0, -0.08), (0, 0.06, -0.20), (0, 0.16, -0.22)], .01, 3, "extra1")
    for k in range(3):
        z = 0.34 + 0.06 * k
        tube(p, [(0, -0.08 + 0.02 * k, z), (0, 0.0, z + 0.03), (0, 0.10 - 0.02 * k, z)], .008, 3, "glow2")   # crackle
    return p

# ============================================================ GUTSY GUS - Ghoul Courier (Mort)
PAL["Mort"] = base(bone="8FAE74", bone_shade="6E8A5A", cloth="4E6B3E", cloth_dark="34482A", cloth_inner="1A2414", cloth2="5A4A6A", cloth2_dark="3A2F48",
                   shirt="6A5A8A", shirt_dark="4A3E64", leather="8A6A4A", wood="5A4632", glow="5CF2D6", glow2="FFF3A0", accent="C8A040", extra5="F2EBDD",
                   socket="1A1A14", shoe="8FAE74", sole="6E8A5A")
def b_mort():
    zc = ZC
    # short ragged trousers, bare long-toed feet
    for side, s in (("l", 1), ("r", -1)):
        leg(side, s, "cloth2", r=(.085, .08, .075, .07), cuff_z=0.34, shoe_sw="bone", sole_sw="bone_shade", shoe_scale=1.25, ankle=False)
        sn = Part("gshin_" + side, rigid("lowerleg." + side))
        tube(sn, [(0.171 * s, 0.0, 0.36), (0.171 * s, 0.0, 0.10)], [.05, .045], 6, "bone")
        tc = Part("gtoe_" + side, rigid("toes." + side))
        for dx in (-0.05, 0.0, 0.05):
            cone(tc, (0.171 * s + dx, -0.24, 0.03), (0.171 * s + dx, -0.29, 0.01), .015, 3, "extra5")
    pelvis("cloth2", rx=.24, ry=.20, z0=0.46, z1=0.66)
    torso([(0.60, .24, .20), (0.80, .27, .23), (0.98, .30, .26), (1.12, .31, .27), (1.22, .26, .22), (1.28, .12, .11)], "shirt", "shirt_dark")
    # courier vest + big satchel of souls on the right hip, strap across
    vs = Part("vest", TORSO_W)
    loft(vs, [hring(z, rx + 0.015, ry + 0.015, 14) for z, rx, ry in ((0.66, .24, .20), (0.82, .27, .23), (1.00, .30, .26), (1.14, .31, .27))], "cloth", cap0=False, cap1=False)
    box(vs, (0.15, -0.28, 1.04), (.07, .02, .05), "accent", 0.005)      # courier badge
    st = Part("strap", rigid("chest"))
    tube(st, [(0.26, -0.24, 1.22), (0.0, -0.30, 0.98), (-0.26, -0.24, 0.72)], .03, 4, "leather")
    sb = Part("satchel", rigid("hips"))
    sc = Vector((-0.34, -0.06, 0.58))
    box(sb, sc, (.14, .30, .26), "leather", 0.04)
    box(sb, sc + Vector((-0.01, 0, 0.10)), (.15, .31, .08), "wood", 0.02)
    for k, (dy, dz) in enumerate(((-0.08, 0.16), (0.02, 0.18), (0.10, 0.15))):
        ell(sb, sc + Vector((0, dy, dz)), (.05, .05, .05), 6, 4, "glow")
    for side, s in (("l", 1), ("r", -1)):
        arm(side, s, rad=(.075, .07, .068, .066, .064), sleeve_sw="bone", hand_sw="bone", hand_scale=1.4, bony_wrist=False, x1=0.80)
        claw_hand(side, s, "bone", "extra5", 1.3)
    neck(0.06, 1.36, "bone")
    hc = Vector((0, -0.06, 1.56))
    hd = Part("ghhead", rigid("head"))
    sbox(hd, hc, (.40, .38, .36), 12, 8, "bone", power=0.65, taper=lambda zn: (1.0 - 0.12 * max(0, -zn), 1.0))
    hd.recolor(lambda cc, n_, cur: n_.z < -0.6, "bone_shade")
    fy = hc.y - 0.19
    for sx in (1, -1):
        eye(hd, (0.085 * sx, fy, 1.60), 0.05, "glow2", "ink", 0.35)
        cone(hd, (0.18 * sx, -0.06, 1.58), (0.30 * sx, -0.02, 1.66), .04, 4, "bone")
    # wide grin of needle teeth
    slab(hd, [(-0.13, fy, 1.49), (0.13, fy, 1.49), (0.08, fy, 1.43), (-0.08, fy, 1.43)], (0, 0.02, 0), "socket")
    for k in range(7):
        x = -0.11 + 0.037 * k
        cone(hd, (x, fy - 0.004, 1.49), (x, fy - 0.004, 1.455), .009, 3, "extra5")
    # postman's cap
    cp = Part("cap", rigid("head"))
    loft(cp, [hring(1.68, .20, .19, 14, cy=-0.06), hring(1.76, .22, .21, 14, cy=-0.06), hring(1.80, .20, .19, 14, cy=-0.06)], "cloth")
    slab(cp, [(-0.15, -0.20, 1.69), (0.15, -0.20, 1.69), (0.12, -0.33, 1.66), (-0.12, -0.33, 1.66)], (0, 0, 0.02), "cloth_dark")
    box(cp, (0, -0.255, 1.73), (.06, .02, .04), "accent", 0.005)
WEAP["Mort"] = lambda: _bone_boomerang()
def _bone_boomerang():
    p = wpart("bone_boomerang")
    tube(p, [(0, 0, -0.05), (0, 0.02, 0.30), (0, 0.18, 0.52), (0, 0.38, 0.58)], [.035, .035, .033, .03], 6, "extra5")
    for sx in (1, -1):
        ell(p, (0.025 * sx, 0.38, 0.59), (.04, .045, .04), 6, 4, "extra5")
        ell(p, (0.025 * sx, 0.0, -0.06), (.035, .04, .04), 6, 4, "extra5")
    torus(p, (0, 0.01, 0.10), .04, .012, 8, 3, "leather", normal=Vector((0, 0, 1)))
    return p

# ============================================================ LEDGER MOTHWICK - Moth Auditor (Actuary)
PAL["Actuary"] = base(bone="C9B48A", bone_shade="9A8660", cloth="4A3A4A", cloth_dark="2E242E", cloth_inner="161016", cloth2="3A2F3A", cloth2_dark="261E26",
                      shirt="F4F1EA", shirt_dark="C9C4BA", accent="D8434B", accent_dark="9A2A30", hair="E8DCC0", glass="2F8A4A", gold="FFC94A", gold_dark="B8862A",
                      extra1="B9A27A", extra2="8A7652", extra3="5A4A6A", socket="1A1426", glow="FFD26A", glow2="F2E3C0", wood="7A5A3A", paper="F8F2DE", shoe="2A2228")
def b_actuary():
    zc = ZC
    legs("cloth2", r=(.075, .07, .065, .06), shoe="shoe", scale=1.05)
    pelvis("cloth2", rx=.24, ry=.20, z0=0.46, z1=0.66)
    torso([(0.60, .25, .20), (0.80, .28, .23), (0.98, .29, .24), (1.12, .28, .23), (1.22, .22, .19), (1.28, .10, .09)], "cloth", "cloth_dark")
    # fluffy moth ruff over the collar + starched shirt front + red tie
    rf = Part("ruff", rigid("chest"))
    for k in range(16):
        a = 2 * math.pi * k / 16
        ell(rf, (0.22 * math.cos(a), 0.18 * math.sin(a), 1.24), (.07, .07, .06), 6, 4, "hair")
    sf = Part("shirtfront", TORSO_W)
    slab(sf, [(-0.07, -0.225, 1.18), (0.07, -0.225, 1.18), (0.0, -0.245, 0.86)], (0, 0.01, 0), "shirt")
    slab(sf, [(-0.025, -0.24, 1.14), (0.025, -0.24, 1.14), (0.035, -0.25, 0.94), (0.0, -0.255, 0.90), (-0.035, -0.25, 0.94)], (0, -0.01, 0), "accent")
    # dusty moth wings on the back, eye-spots
    wg = Part("mwings", rigid("chest"))
    for sx in (1, -1):
        up = [Vector((0.08 * sx, 0.24, 1.10)), Vector((0.40 * sx, 0.34, 1.62)), Vector((0.78 * sx, 0.36, 1.58)), Vector((0.70 * sx, 0.34, 1.20)), Vector((0.30 * sx, 0.28, 1.00))]
        lo = [Vector((0.08 * sx, 0.24, 1.00)), Vector((0.30 * sx, 0.28, 0.98)), Vector((0.52 * sx, 0.32, 0.80)), Vector((0.36 * sx, 0.30, 0.60)), Vector((0.12 * sx, 0.25, 0.72))]
        for poly, col in ((up, "extra1"), (lo, "extra2")):
            c = sum(poly, Vector()) / len(poly)
            for k in range(len(poly)):
                slab(wg, [c, poly[k], poly[(k + 1) % len(poly)]], (0, 0.015, 0), col)
        ell(wg, (0.48 * sx, 0.355, 1.36), (.09, .01, .09), 8, 4, "extra3")
        ell(wg, (0.48 * sx, 0.365, 1.36), (.04, .01, .04), 6, 3, "glow2")
        ell(wg, (0.30 * sx, 0.31, 0.80), (.05, .01, .05), 6, 3, "extra3")
    for side, s in (("l", 1), ("r", -1)):
        arm(side, s, rad=(.075, .07, .068, .066, .064), sleeve_sw="shirt", hand_sw="bone", hand_scale=1.15, bony_wrist=False, x1=0.76, cuff_sw="shirt_dark", cuff_r=.075)
        gt = Part("garter_" + side, rigid("upperarm." + side))
        tube(gt, [(0.33 * s, 0, zc), (0.37 * s, 0, zc)], [.08, .08], 8, "accent")
    # ledger under the left arm
    lg = Part("ledger", rigid("lowerarm.l"))
    box(lg, (0.55, -0.16, zc - 0.06), (.24, .05, .30), "accent_dark", 0.01)
    box(lg, (0.55, -0.16, zc - 0.06), (.22, .055, .28), "paper", 0.0)
    neck(0.05, 1.40, "hair")
    hc = Vector((0, -0.02, 1.60))
    hd = Part("mhead", rigid("head"))
    sbox(hd, hc, (.38, .34, .36), 12, 8, "hair", power=0.7)
    fy = hc.y - 0.17
    for sx in (1, -1):
        ell(hd, (0.10 * sx, fy + 0.02, 1.62), (.085, .05, .10), 10, 6, "socket")     # huge black compound eyes
        ell(hd, (0.08 * sx, fy - 0.025, 1.66), (.02, .01, .025), 5, 3, "paper")
        tube(hd, [(0.06 * sx, -0.10, 1.76), (0.12 * sx, -0.14, 1.92), (0.22 * sx, -0.12, 2.02)], [.012, .01, .008], 4, "extra2")
        for k in range(5):
            t_ = k / 4.0
            c = Vector((0.06 * sx, -0.10, 1.76)).lerp(Vector((0.22 * sx, -0.12, 2.02)), t_)
            cone(hd, c, c + Vector((0.05 * sx, -0.02, 0.0)), .01, 3, "extra2")       # feathery antennae
    ell(hd, (0, fy - 0.01, 1.50), (.03, .015, .02), 5, 3, "bone_shade")
    # green accountant's visor
    vz = Part("visor", rigid("head"))
    loft(vz, [hring(1.72, .20, .18, 14, cy=-0.02), hring(1.76, .20, .18, 14, cy=-0.02)], "accent_dark", cap0=False, cap1=False)
    brim = [Vector((x, y, 1.74 + (y + 0.15) * 0.4)) for x, y in ((-0.20, -0.12), (-0.15, -0.32), (0.0, -0.37), (0.15, -0.32), (0.20, -0.12))]
    slab(vz, brim, (0, 0, 0.015), "glass")
WEAP["Actuary"] = lambda: _stamp()
def _stamp():
    p = wpart("stamp")
    cyl(p, (0, 0, -0.06), (0, 0, 0.30), .03, .03, 8, "wood")
    ell(p, (0, 0, -0.09), (.06, .06, .06), 8, 5, "accent")
    cyl(p, (0, 0, 0.30), (0, 0, 0.36), .06, .09, 8, "gold")
    box(p, (0, 0, 0.42), (.24, .24, .10), "wood", 0.02)
    box(p, (0, 0, 0.485), (.22, .22, .03), "accent", 0.005)
    return p

# ============================================================ MADAME MARIONETTE - Puppeteer (Ectoplasm)
PAL["Ectoplasm"] = base(bone="E8DCEC", bone_shade="B8A8C0", cloth="5A2E6A", cloth_dark="3A1E48", cloth_inner="1A0E24", cloth2="7A2A5A", cloth2_dark="4E1A3A",
                        shirt="F0E8F4", shirt_dark="C8B8D0", gold="FFC94A", gold_dark="B8862A", glow="7CF0B8", glow2="CFFFE8", hair="2A1E30", accent="E0304A",
                        socket="0E0B16", wood="7A5A3A", wood_dark="4E3420", extra1="F2EBDD", extra2="8A4FC0", paper="F8F2DE")
def b_ectoplasm():
    zc = ZC
    floating_gown([(1.27, .12, .11), (1.20, .24, .20), (1.05, .265, .215), (0.86, .25, .205), (0.68, .27, .225), (0.46, .37, .32), (0.24, .46, .40), (0.08, .52, .46)],
                  "cloth", "cloth_dark", "cloth_inner", "gold", 16, 0.035)
    # diamond harlequin panel down the front
    hp = Part("harlequin", TORSO_W)
    for k, (z, col) in enumerate(((1.10, "cloth2"), (0.96, "extra1"), (0.82, "cloth2"), (0.68, "extra1"))):
        slab(hp, [(0, -0.235, z + 0.07), (0.06, -0.235, z), (0, -0.235, z - 0.07), (-0.06, -0.235, z)], (0, -0.01, 0), col)
    rf = Part("ruffcollar", rigid("chest"))
    for k in range(14):
        a = 2 * math.pi * k / 14
        ell(rf, (0.17 * math.cos(a), 0.15 * math.sin(a), 1.27), (.06, .06, .035), 6, 3, "shirt" if k % 2 else "shirt_dark")
    for side, s in (("l", 1), ("r", -1)):
        up, lo = "upperarm." + side, "lowerarm." + side
        a = Part("pslu_" + side, ablend(0, [(0.20, {"chest": .5, up: .5}), (0.30, {up: 1})], s))
        loft(a, [ring((x * s, 0, zc), (0, -1, 0), (0, 0, 1), r, r, 8) for x, r in ((0.16, .10), (0.30, .11), (0.46, .09))], "cloth2")
        bb = Part("psll_" + side, rigid(lo))
        g, faces, caps = loft(bb, [ring((x * s, 0, zc - dz), (0, -1, 0), (0, 0, 1), r, r, 10) for x, r, dz in ((0.43, .085, 0), (0.62, .085, 0.0), (0.72, .12, 0.02))], "cloth", cap0=True, cap1=False)
        shell(bb, faces, lambda co, s=s: Vector((co.x, co.y * .8, zc + (co.z - zc) * .8)), "cloth_inner", "gold")
        w = Part("pwr_" + side, segw([lo, "wrist." + side, "hand." + side], k=6))
        tube(w, [(0.66 * s, 0, zc), (0.80 * s, 0, zc)], .035, 5, "shirt")
        hand(side, s, "shirt", 1.15)
    # a little ghost marionette dangling from her left hand on glowing strings
    pm = Part("puppet", rigid("hand.l"))
    pc = Vector((0.90, -0.05, zc - 0.42))
    ell(pm, pc, (.08, .07, .10), 8, 5, "glow")
    cone(pm, pc + Vector((0, 0, -0.06)), pc + Vector((0, 0.02, -0.18)), .07, 6, "glow")
    for sx in (1, -1):
        ell(pm, pc + Vector((0.03 * sx, -0.065, 0.02)), (.012, .006, .016), 4, 3, "socket")
        tube(pm, [(0.90 + 0.02 * sx, -0.05, zc - 0.05), pc + Vector((0.05 * sx, 0, 0.08))], .004, 3, "glow2")
    neck(0.045, 1.40, "bone")
    hc = Vector((0, -0.02, 1.60))
    hd = Part("phead", rigid("head"))
    sbox(hd, hc, (.32, .32, .40), 12, 8, "bone", power=0.7, taper=lambda zn: (1.0 - 0.2 * max(0, -zn), 1.0))
    fy = hc.y - 0.16
    # porcelain half-mask (gold, crescent eye holes) + red lips
    mk = Part("mask", rigid("head"))
    sl = [Vector((x, fy - 0.01 - 0.03 * (1 - abs(x) / 0.17), z)) for x, z in ((-0.17, 1.70), (0.17, 1.70), (0.17, 1.60), (0.0, 1.57), (-0.17, 1.60))]
    slab(mk, sl, (0, 0.012, 0), "gold")
    for sx in (1, -1):
        slab(mk, [Vector((0.03 * sx, fy - 0.04, 1.65)), Vector((0.12 * sx, fy - 0.035, 1.65)), Vector((0.08 * sx, fy - 0.04, 1.62))], (0, -0.008, 0), "socket")
        ell(mk, (0.075 * sx, fy - 0.05, 1.637), (.012, .006, .01), 4, 3, "glow")
    ell(hd, (0, fy - 0.005, 1.50), (.04, .015, .015), 6, 3, "accent")
    # tall feathered hat
    ht = Part("phat", rigid("head"))
    loft(ht, [hring(1.74, .19, .19, 14, cy=-0.02), hring(1.92, .15, .15, 14, cy=-0.0), hring(2.02, .17, .17, 14, cy=0.0)], "cloth")
    disc(ht, (0, -0.02, 1.74), .26, .26, 14, "cloth_dark", normal=Vector((0, 0, -1)))
    disc(ht, (0, -0.02, 1.745), .26, .26, 14, "cloth", normal=Vector((0, 0, 1)))
    loft(ht, [hring(1.76, .185, .185, 14, cy=-0.02), hring(1.81, .18, .18, 14, cy=-0.015)], "gold", cap0=False, cap1=False)
    for k, (dx, h_) in enumerate(((0.10, 0.36), (0.14, 0.30), (0.06, 0.32))):
        tube(ht, [(dx, 0.04, 1.86), (dx + 0.05, 0.08, 1.86 + h_ * 0.5), (dx + 0.12, 0.14, 1.86 + h_)], [.025, .045, .012], 5, "extra2" if k else "accent")
    hr = Part("phair", rigid("head"))
    tube(hr, [(0.0, 0.14, 1.70), (0.0, 0.20, 1.55), (0.0, 0.18, 1.40)], [.10, .09, .05], 8, "hair")
WEAP["Ectoplasm"] = lambda: _crossbar()
def _crossbar():
    p = wpart("crossbar")
    cyl(p, (0, 0, -0.05), (0, 0, 0.30), .022, .022, 6, "wood")
    box(p, (0, 0, 0.32), (.04, .50, .04), "wood", 0.008)
    box(p, (0, 0, 0.32), (.36, .04, .04), "wood_dark", 0.008)
    for y in (-0.24, 0.24):
        tube(p, [(0, y, 0.32), (0, y, 0.05)], .004, 3, "glow2")
        ell(p, (0, y, 0.0), (.05, .045, .065), 6, 4, "glow")
        cone(p, (0, y, -0.03), (0, y, -0.12), .045, 5, "glow")
    return p

# ============================================================ BIG BRISKET - The Butcher (Vexa)
PAL["Vexa"] = base(bone="E8C8A8", bone_shade="C0A080", shirt="F2EBDD", shirt_dark="C8C0B0", cloth="5A4A5A", cloth_dark="3A3040", cloth_inner="1A141E", accent="D8434B",
                   accent_dark="9A2A30", metal="B9C0CC", metal_dark="7A808C", leather="5A4632", glow="5CF2D6", glow2="FFB0A0", hair="3A2A22", extra1="C86A6A", extra2="8A5A3A",
                   socket="1A0E10", shoe="2A2224", wood="5A4632")
def b_vexa():
    zc = ZC
    legs("cloth", r=(.13, .125, .115, .105), shoe="shoe", scale=1.3, boot=True, x=0.18)
    pelvis("cloth", rx=.34, ry=.28, z0=0.46, z1=0.70)
    t, faces = torso([(0.62, .36, .30), (0.80, .44, .36), (0.96, .46, .38), (1.10, .42, .34), (1.22, .33, .27), (1.30, .16, .15)], "shirt", "shirt_dark", 14)
    # striped butcher's apron front, strings, stains
    ap = Part("apron", lambda co: TORSO_W(co) if co.z > 0.64 else skirt_w(0.64, 0.30, leg_max=0.5)(co))
    rows = []
    for z, w, y in ((1.12, .20, -0.36), (0.96, .34, -0.40), (0.78, .36, -0.40), (0.60, .36, -0.36), (0.32, .34, -0.34)):
        rows.append([Vector((w * (k / 4 - 0.5) * 2 * 0.5, y - 0.03 * (1 - abs(k / 4 - 0.5) * 2), z)) for k in range(5)])
    g, faces, caps = loft(ap, rows, "paper", closed=False)
    shell(ap, faces, lambda co: Vector((co.x, co.y + 0.012, co.z)), "shirt_dark", "shirt_dark")
    for k in range(4):
        x = -0.12 + 0.08 * k
        tube(ap, [(x, -0.43, 0.95), (x, -0.44, 0.78), (x, -0.40, 0.55), (x, -0.37, 0.34)], .012, 3, "accent")
    for x, z in ((0.10, 0.70), (-0.08, 0.86)):
        ell(ap, (x, -0.43, z), (.05, .012, .04), 6, 3, "extra1")
    tube(ap, [(-0.10, -0.36, 1.12), (-0.14, -0.20, 1.30), (0.14, -0.20, 1.30), (0.10, -0.36, 1.12)], .012, 3, "paper")
    b = Part("belt", rigid("hips"))
    loft(b, [hring(0.64, .37, .31, 14), hring(0.70, .37, .31, 14)], "leather", cap0=False, cap1=False)
    for k in range(3):
        a = math.pi * (0.2 + 0.3 * k)
        c = Vector((0.37 * math.cos(-a), 0.31 * math.sin(-a), 0.62))
        tube(b, [c, c + Vector((0, 0, -0.10)), c + Vector((0.04, -0.03, -0.14))], .01, 3, "metal_dark")   # hooks
    # sausage links over the shoulder
    for k in range(6):
        t_ = k / 5.0
        c = Vector((-0.30, -0.10, 1.26)).lerp(Vector((0.28, -0.40, 0.80)), t_) + Vector((0, -0.03 * math.sin(t_ * math.pi), 0))
        ell(b, c, (.045, .045, .06), 6, 4, "extra2")
    for side, s in (("l", 1), ("r", -1)):
        arm(side, s, rad=(.13, .125, .12, .115, .11), sleeve_sw="bone", hand_sw="bone", hand_scale=1.55, bony_wrist=False, x1=0.76)
        sl = Part("rolled_" + side, rigid("upperarm." + side))
        tube(sl, [(0.18 * s, 0, zc), (0.36 * s, 0, zc)], [.14, .14], 10, "shirt")
        tube(sl, [(0.34 * s, 0, zc), (0.39 * s, 0, zc)], [.15, .15], 10, "shirt_dark")
    # second cleaver held in the left hand (the right one is the weapon)
    cl = Part("cleaver_l", rigid("hand.l"))
    cyl(cl, (0.90, -0.03, zc - 0.12), (0.90, -0.03, zc + 0.06), .025, .025, 6, "wood")
    box(cl, (0.90, -0.10, zc - 0.24), (.02, .20, .16), "metal", 0.01)
    neck(0.10, 1.40, "bone")
    hc = Vector((0, -0.02, 1.62))
    hd = Part("bhead", rigid("head"))
    sbox(hd, hc, (.42, .40, .40), 12, 8, "bone", power=0.65, taper=lambda zn: (1.0 + 0.08 * max(0, -zn), 1.0))
    hd.recolor(lambda cc, n_, cur: n_.z < -0.65, "bone_shade")
    fy = hc.y - 0.20
    # steel eye mask with two teal eye lights
    box(hd, (0, fy - 0.005, 1.66), (.36, .03, .08), "metal_dark", 0.015)
    for sx in (1, -1):
        ell(hd, (0.075 * sx, fy - 0.02, 1.66), (.025, .01, .02), 5, 3, "glow")
    ell(hd, (0, fy - 0.04, 1.58), (.05, .05, .045), 6, 4, "bone_shade")
    # thick handlebar moustache + grin
    for sx in (1, -1):
        tube(hd, [(0.02 * sx, fy - 0.03, 1.54), (0.12 * sx, fy - 0.02, 1.53), (0.18 * sx, fy, 1.57)], [.03, .025, .012], 5, "hair")
    box(hd, (0, fy, 1.49), (.12, .01, .02), "socket", 0)
    # tall chef's toque
    tq = Part("toque", rigid("head"))
    loft(tq, [hring(1.78, .20, .19, 14, cy=-0.02), hring(1.86, .20, .19, 14, cy=-0.02)], "shirt_dark", cap0=False, cap1=False)
    for k in range(7):
        a = 2 * math.pi * k / 7
        ell(tq, (0.12 * math.cos(a), -0.02 + 0.11 * math.sin(a), 2.00), (.10, .10, .14), 8, 5, "paper")
    ell(tq, (0, -0.02, 2.04), (.14, .14, .12), 8, 5, "paper")
WEAP["Vexa"] = lambda: _cleaver()
def _cleaver():
    p = wpart("cleaver")
    cyl(p, (0, 0, -0.06), (0, 0, 0.22), .03, .03, 6, "wood")
    for z in (0.04, 0.14):
        ell(p, (0.0, 0, z), (.035, .035, .012), 6, 3, "metal_dark")
    pts = [(0, -0.04, 0.22), (0, 0.24, 0.22), (0, 0.26, 0.60), (0, -0.04, 0.62)]
    slab(p, [Vector(q) - Vector((0.012, 0, 0)) for q in pts], (0.024, 0, 0), "metal")
    ell(p, (0, 0.0, 0.56), (.016, .02, .02), 5, 3, "cloth_inner")    # hang hole
    box(p, (0, 0.25, 0.42), (.03, .02, .38), "glow2", 0)              # edge (glows red as rage fills)
    return p

BUILD.update(Intern=b_intern, Sweep=b_sweep, Solace=b_solace, Mort=b_mort, Actuary=b_actuary, Ectoplasm=b_ectoplasm, Vexa=b_vexa)

if __name__ == "__main__":
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    for m in (args or list(BUILD)):
        build_monster(m, PAL[m], BUILD[m], WEAP.get(m))
