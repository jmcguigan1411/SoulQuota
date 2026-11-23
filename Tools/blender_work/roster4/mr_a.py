# Monster roster batch A: Neckless (Headless Horseman), Tallow (Pumpkin Man), Bonesmith (Zombie Warrior),
# Pallbearers (Undertaker), Widow (Ghost), Beak (Witch), Tickwell (Wizard). Original designs.
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mr_lib import *
H = hexc
PAL, BUILD, WEAP = {}, {}, {}
def base(**kw):
    d = dict(bone=H("C8C0B0"), bone_shade=H("9A9284"), socket=H("0E0A12"), glow=H("7CF0B8"), glow2=H("FFD26A"),
             cloth=H("4A4458"), cloth_dark=H("302C3A"), cloth_inner=H("16121C"), cloth2=H("3A3442"), cloth2_dark=H("26222C"),
             shirt=H("8A8496"), shirt_dark=H("5E5868"), accent=H("C84A3A"), accent_dark=H("8A2E24"),
             metal=H("A4AAB4"), metal_dark=H("6A707C"), gold=H("E2B85A"), gold_dark=H("9A7A34"), wood=H("7A5236"), wood_dark=H("4E3220"),
             leather=H("5A3A24"), shoe=H("2A2224"), sole=H("100C0E"), hair=H("2A2226"), ink=H("120E14"), paper=H("F4EEE0"),
             glass=H("BFE8FF"), extra1=H("3A6B2A"), extra2=H("5A9A3A"), extra3=H("6A4A8A"), extra4=H("E07A1F"), extra5=H("F2EBDD"))
    d.update({k: H(v) if isinstance(v, str) else v for k, v in kw.items()})
    return d

def pumpkin(p, c, r, lobes=8, sw="extra4", dark="accent_dark", stem="extra1", leaf="extra2", face=True, glow="glow2", sq=1.0, rot=0):
    c = Vector(c)
    for k in range(lobes):
        a = 2 * math.pi * k / lobes
        ell(p, c + Vector((r * 0.55 * math.cos(a), r * 0.55 * math.sin(a), 0)), (r * 0.62, r * 0.62, r * 0.85 * sq), 8, 6,
            sw if k % 2 else dark, ROT(math.degrees(a), 'Z'))
    cyl(p, c + Vector((0, 0, r * 0.7 * sq)), c + Vector((r * 0.12, 0.02, r * 1.15 * sq)), r * 0.13, r * 0.09, 5, stem)
    slab(p, [c + Vector((r * .1, 0, r * .95 * sq)), c + Vector((r * .7, -r * .15, r * 1.1 * sq)), c + Vector((r * .45, r * .2, r * .9 * sq))], (0, 0, 0.012), leaf)
    if face:
        fz = c + Vector((0, -r * 1.0, 0))
        for sx in (1, -1):
            slab(p, [fz + Vector((r * .18 * sx, 0, r * .12)), fz + Vector((r * .52 * sx, r * .08, r * .14)), fz + Vector((r * .36 * sx, 0, r * .45))], (0, r * .12, 0), glow)
        slab(p, [fz + Vector((-r * .55, r * .08, -r * .18)), fz + Vector((-r * .3, 0, -r * .3)), fz + Vector((-r * .12, 0, -r * .2)), fz + Vector((r * .1, 0, -r * .32)),
                 fz + Vector((r * .3, 0, -r * .2)), fz + Vector((r * .55, r * .08, -r * .18)), fz + Vector((r * .3, 0, -r * .52)), fz + Vector((-r * .3, 0, -r * .52))], (0, r * .12, 0), glow)

# ============================================================ SIR HOLLOWHEAD - Headless Horseman (Neckless)
PAL["Neckless"] = base(cloth="5E2A2A", cloth_dark="3E1A1A", cloth_inner="160C0C", cloth2="5A5E66", cloth2_dark="3E4248",
                       shirt="2A2234", shirt_dark="1A1424", metal="8A7E6E", metal_dark="5A4E42", accent="9A5A2A", accent_dark="B85A14",
                       extra4="F08A24", glow="7CF0B8", glow2="B8FF6A", shoe="6A5E50", sole="2E2620", leather="4A2E1C", gold="B8924A")
def b_neckless():
    zc = ZC
    for side, s in (("l", 1), ("r", -1)):
        ul, ll = "upperleg." + side, "lowerleg." + side
        leg(side, s, "metal", r=(.12, .115, .105, .10), cuff_z=0.16, shoe_sw="shoe", sole_sw="sole", shoe_scale=1.25, ankle=False)
        kp = Part("knee_" + side, segw([ul, ll], k=6))
        ell(kp, (0.171 * s, -0.10, 0.30), (.085, .06, .075), 8, 4, "metal_dark")
        cone(kp, (0.171 * s, -0.145, 0.30), (0.171 * s, -0.21, 0.30), .03, 4, "metal_dark")
        ts = Part("tasset_" + side, lambda co, ul=ul: {"hips": .45, ul: .55})
        for k, z in enumerate((0.56, 0.47)):
            box(ts, (0.19 * s, -0.08 + 0.01 * k, z), (.20, .20, .10), "metal" if k == 0 else "metal_dark", 0.025, ROT(-6 * s, 'Y'))
        # rust patches
        ell(ts, (0.22 * s, -0.18, 0.55), (.04, .012, .03), 5, 3, "accent")
    pelvis("cloth2", rx=.28, ry=.23, z0=0.46, z1=0.66)
    t, _ = torso([(0.60, .29, .24), (0.78, .32, .26), (0.96, .335, .27), (1.10, .32, .26), (1.20, .26, .22), (1.27, .16, .15)], "metal", "metal_dark", 14, "cuirass")
    for (x, z) in ((0.16, 1.02), (-0.10, 0.80), (0.05, 0.70)):
        ell(t, (x, -0.255, z), (.05, .02, .04), 5, 3, "accent")
    # tattered crimson tabard
    for sy in (-1, 1):
        fl = Part("tabard_%d" % sy, skirt_w(0.64, 0.30, leg_max=0.5))
        y0 = 0.27 * sy
        top = [Vector((x, y0 * math.sqrt(max(0.0, 1 - (x / 0.32) ** 2)), 1.18)) for x in (-0.16, -0.08, 0.0, 0.08, 0.16)]
        mid = [Vector((x, (y0 + 0.012 * sy) * math.sqrt(max(0.0, 1 - (x / 0.34) ** 2)), 0.66)) for x in (-0.17, -0.085, 0.0, 0.085, 0.17)]
        bot = [Vector((x, (y0 + 0.06 * sy) * math.sqrt(max(0.0, 1 - (x / 0.36) ** 2)), 0.32 + (0.07 if k % 2 else 0.0))) for k, x in enumerate((-0.18, -0.09, 0.0, 0.09, 0.18))]
        def tw(co):
            return TORSO_W(co) if co.z > 0.64 else skirt_w(0.64, 0.30, leg_max=0.5)(co)
        fl.weights = tw
        g, faces, caps = loft(fl, [top, mid, bot], "cloth", closed=False)
        shell(fl, faces, lambda co, sy=sy: Vector((co.x, co.y - 0.02 * sy, co.z)), "cloth_inner", "cloth_dark")
    b = Part("belt", rigid("hips"))
    loft(b, [hring(0.62, .30, .26, 14), hring(0.68, .30, .26, 14)], "leather", cap0=False, cap1=False)
    box(b, (0, -0.29, 0.65), (.09, .03, .075), "gold", 0.01)
    cape(1.26, 0.40, .24, .42, "shirt", "shirt_dark", jag=0.09, n=11)
    # gorget + green soul flame where the head should be
    gk = Part("gorget", rigid("chest"))
    loft(gk, [hring(1.22, .22, .19, 14), hring(1.30, .19, .17, 14), hring(1.36, .15, .14, 14)], "metal", cap0=False, cap1=False)
    disc(gk, (0, 0, 1.31), .13, .12, 12, "cloth_inner", normal=Vector((0, 0, 1)))
    ell(gk, (0, 0, 1.33), (.10, .09, .03), 8, 3, "glow")
    for k in range(7):
        a = 2 * math.pi * k / 7
        b0 = Vector((0.06 * math.cos(a), 0.06 * math.sin(a), 1.33))
        cone(gk, b0, b0 + Vector((0.03 * math.cos(a), 0.03 * math.sin(a) + 0.03, 0.12 + 0.07 * (k % 2))), .04, 4, "glow")
    cone(gk, (0, 0, 1.33), (0.0, 0.05, 1.62), .07, 6, "glow2")
    cone(gk, (0.03, 0.0, 1.40), (0.06, 0.08, 1.56), .035, 5, "glow")
    for side, s in (("l", 1), ("r", -1)):
        up, lo = "upperarm." + side, "lowerarm." + side
        arm(side, s, rad=(.09, .085, .082, .08, .076), sleeve_sw="cloth2", bony_wrist=False, hand_sw="metal", hand_scale=1.4)
        vb = Part("vambrace_" + side, rigid(lo))
        tube(vb, [(0.52 * s, 0, zc), (0.66 * s, 0, zc), (0.74 * s, 0, zc)], [.092, .10, .118], 8, "metal")
        pd = Part("pauldron_" + side, lambda co, up=up: {"chest": .55, up: .45})
        sbox(pd, (0.29 * s, 0, 1.18), (.32, .33, .18), 10, 6, "metal", power=0.6)
        sbox(pd, (0.34 * s, 0, 1.08), (.25, .29, .07), 8, 4, "metal_dark", power=0.6)
        for k in range(3):
            cone(pd, (0.25 * s + 0.05 * k * s, 0.02, 1.26), (0.25 * s + 0.05 * k * s, 0.04, 1.36), .025, 4, "metal_dark")
    # his own jack-o'-lantern head under the left arm, green-lit
    pk = Part("head_lantern", rigid("lowerarm.l"))
    pumpkin(pk, (0.57, -0.22, zc + 0.03), 0.15, glow="glow")
WEAP["Neckless"] = lambda: _sabre()
def _sabre():
    p = wpart("sabre")
    cyl(p, (0, 0, -0.10), (0, 0, 0.12), .025, .025, 6, "leather")
    ell(p, (0, 0, -0.12), (.04, .04, .04), 6, 4, "gold")
    box(p, (0, 0, 0.14), (.05, .26, .04), "gold", 0.01)
    tube(p, [(0, 0.0, 0.14), (0, 0.04, 0.20), (0.0, -0.02, 0.22)], .012, 4, "gold")       # knuckle guard
    pts = [(0, -0.03, 0.16), (0, 0.035, 0.16), (0, 0.06, 0.55), (0, 0.10, 0.90), (0, 0.04, 1.02), (0, 0.01, 0.80), (0, -0.03, 0.45)]
    slab(p, [Vector(q) - Vector((0.008, 0, 0)) for q in pts], (0.016, 0, 0), "metal")
    for k in range(5):
        z = 0.30 + 0.13 * k
        box(p, (0, 0.045 + 0.012 * k, z), (.02, .03, .04), "glow", 0)   # green fire notches
    return p

# ============================================================ JACK GOURDLING - Pumpkin Man (Tallow)
PAL["Tallow"] = base(bone="4E7A2A", bone_shade="345A1A", extra1="3A5A1E", extra2="6AAA3A", extra4="F08A24", accent_dark="C0600E",
                     cloth="5A7A2A", cloth_dark="3A5A1A", cloth_inner="1E2A0E", glow2="FFE08A", glow="FFB347", shoe="4A3A1E", sole="2A1E0E")
def vines(p, a, b, r, wig=0.04, n=5, sw="cloth", seed=0):
    a, b = Vector(a), Vector(b)
    pts = []
    for k in range(n):
        t = k / (n - 1)
        off = Vector((math.sin(t * 7 + seed) * wig, math.cos(t * 5 + seed) * wig, 0)) if 0 < k < n - 1 else Vector()
        pts.append(a.lerp(b, t) + off)
    tube(p, pts, r, 5, sw)
def b_tallow():
    zc = ZC
    # root legs: twisted vine bundles ending in gnarled root feet
    for side, s in (("l", 1), ("r", -1)):
        ul, ll = "upperleg." + side, "lowerleg." + side
        p = Part("vleg_" + side, segw([ul, ll], k=6))
        for j, sd in enumerate((0, 2, 4)):
            vines(p, (0.17 * s + 0.03 * math.cos(sd), 0.03 * math.sin(sd), 0.62), (0.17 * s + 0.025 * math.cos(sd + 1), 0.025 * math.sin(sd + 1), 0.14), .045, 0.025, 6, "cloth", sd)
        f = Part("root_" + side, rigid("foot." + side))
        ell(f, (0.17 * s, 0.0, 0.07), (.11, .13, .08), 7, 4, "wood")
        t_ = Part("rtoe_" + side, rigid("toes." + side))
        for dx in (-0.05, 0.0, 0.05):
            cone(t_, (0.17 * s + dx, -0.10, 0.06), (0.17 * s + dx * 1.6, -0.27, 0.02), .035, 4, "wood_dark")
    pelvis("cloth_dark", rx=.22, ry=.18, z0=0.50, z1=0.66)
    # a body of tangled vines with a leaf collar
    t = Part("vbody", TORSO_W)
    for k in range(7):
        a = 2 * math.pi * k / 7
        vines(t, (0.16 * math.cos(a), 0.13 * math.sin(a), 0.62), (0.19 * math.cos(a + 0.6), 0.15 * math.sin(a + 0.6), 1.18), .055, 0.03, 6, "cloth" if k % 2 else "cloth_dark", k)
    ell(t, (0, 0, 0.90), (.17, .14, .28), 8, 6, "cloth_inner")
    lc = Part("leaves", rigid("chest"))
    for k in range(9):
        a = 2 * math.pi * k / 9
        c = Vector((0.22 * math.cos(a), 0.19 * math.sin(a), 1.20))
        slab(lc, [c, c + Vector((0.20 * math.cos(a - 0.3), 0.18 * math.sin(a - 0.3), -0.10)), c + Vector((0.24 * math.cos(a + 0.2), 0.21 * math.sin(a + 0.2), -0.02))], (0, 0, 0.015), "extra2" if k % 2 else "extra1")
    # vine arms with leafy fingers
    for side, s in (("l", 1), ("r", -1)):
        p = Part("varm_" + side, segw(["chest", "upperarm." + side, "lowerarm." + side, "wrist." + side], k=5, bias={"chest": 1.6}))
        vines(p, (0.18 * s, 0, zc), (0.80 * s, 0, zc), .05, 0.03, 7, "cloth", 3 if s > 0 else 1)
        vines(p, (0.22 * s, 0.03, zc + 0.02), (0.74 * s, -0.02, zc - 0.03), .03, 0.03, 6, "cloth_dark", 5)
        h = Part("vhand_" + side, rigid("hand." + side))
        ell(h, (0.86 * s, 0, zc), (.07, .07, .06), 6, 4, "cloth")
        for y in (-0.05, 0.0, 0.05):
            tube(h, [(0.88 * s, y, zc), (0.97 * s, y * 1.3, zc - 0.01), (1.02 * s, y * 1.5, zc + 0.03)], [.025, .018, .008], 4, "cloth")
    neck(0.07, 1.36, "cloth_dark")
    # the big carved pumpkin head
    hp = Part("pumpkin_head", rigid("head"))
    pumpkin(hp, (0, -0.02, 1.66), 0.33, lobes=10, sw="extra4", dark="accent_dark", glow="glow2", sq=0.82)
    # candle glow inside (seen through the cuts)
    ell(hp, (0, -0.05, 1.66), (.12, .12, .14), 6, 4, "glow")

# ============================================================ GRAVUS THE UNFALLEN - Zombie Warrior (Bonesmith)
PAL["Bonesmith"] = base(bone="7E9468", bone_shade="5A6E48", cloth="8A6A4A", cloth_dark="5A4632", cloth_inner="2A1E14", metal="B08D57",
                        metal_dark="7A6038", gold="C9A24A", leather="5A3A24", glow="FF5A4A", glow2="FFB0A0", accent="7A2A2A",
                        extra5="E8E0C8", cloth2="4A3A2A", cloth2_dark="2E2418", shoe="5A3A24", sole="2A1A10", extra1="8A8F96", extra2="5A5E66")
def b_bonesmith():
    zc = ZC
    legs("bone", r=(.12, .11, .10, .095), shoe="shoe", scale=1.2, boot=True)
    for side, s in (("l", 1), ("r", -1)):
        gr = Part("greave_" + side, rigid("lowerleg." + side))
        tube(gr, [(0.171 * s, -0.01, 0.38), (0.171 * s, -0.01, 0.22)], [.11, .10], 8, "metal")
        # torn flesh patch
        ell(gr, (0.20 * s, -0.08, 0.48), (.04, .015, .05), 5, 3, "accent")
    pelvis("cloth2", rx=.29, ry=.24, z0=0.46, z1=0.68)
    # pteruges: leather strips skirt
    for k in range(12):
        a = 2 * math.pi * (k + 0.5) / 12
        side = "upperleg.l" if math.cos(a) > 0 else "upperleg.r"
        st = Part("strip_%d" % k, lambda co, side=side: {"hips": max(0.4, min(1, (co.z - 0.32) / 0.3)), side: 1 - max(0.4, min(1, (co.z - 0.32) / 0.3))})
        c = Vector((0.30 * math.cos(a), 0.25 * math.sin(a), 0.48))
        box(st, c, (.10, .03, .26), "leather" if k % 2 else "cloth", 0.01, ROT(math.degrees(a) + 90, 'Z'))
        ell(st, c + Vector((0, 0, 0.11)), (.02, .02, .02), 4, 3, "gold")
    t, _ = torso([(0.62, .29, .24), (0.80, .31, .25), (0.98, .34, .27), (1.12, .33, .26), (1.22, .26, .21), (1.28, .14, .13)], "bone", "bone_shade", 14)
    # bronze muscle cuirass, dented, with a big gash showing ribs
    c = Part("cuirass", TORSO_W)
    loft(c, [hring(z, rx + 0.02, ry + 0.02, 14) for z, rx, ry in ((0.66, .29, .24), (0.82, .31, .25), (0.98, .34, .27), (1.12, .33, .26), (1.20, .27, .22))], "metal", cap0=False, cap1=False)
    c.recolor(lambda cc, n, cur: n.z < -0.4, "metal_dark")
    for sx in (1, -1):
        ell(c, (0.11 * sx, -0.27, 1.04), (.10, .03, .07), 7, 4, "metal")
    slab(c, [(0.04, -0.29, 0.86), (0.18, -0.27, 0.92), (0.16, -0.27, 0.78)], (0, 0.01, 0), "cloth_inner")
    for k in range(3):
        box(c, (0.12, -0.288, 0.81 + 0.04 * k), (.10, .01, .014), "extra5", 0)
    sh = Part("strap", rigid("chest"))
    tube(sh, [(0.22, -0.26, 1.20), (0.0, -0.31, 0.95), (-0.24, -0.27, 0.70)], .025, 4, "leather")
    for side, s in (("l", 1), ("r", -1)):
        arm(side, s, rad=(.10, .095, .09, .088, .085), sleeve_sw="bone", hand_sw="bone", hand_scale=1.45, bony_wrist=False, x1=0.76)
        br = Part("bracer_" + side, rigid("lowerarm." + side))
        tube(br, [(0.56 * s, 0, zc), (0.74 * s, 0, zc)], [.10, .11], 8, "leather")
        pd = Part("pauldron_" + side, lambda co, s=s, side=side: {"chest": .5, "upperarm." + side: .5})
        sbox(pd, (0.30 * s, 0, 1.19), (.30, .30, .16), 10, 6, "metal", power=0.6)
    # round shield strapped to the left forearm, facing outward
    sd = Part("shield", rigid("lowerarm.l"))
    sc = Vector((0.62, 0.0, zc - 0.02))
    cyl(sd, sc + Vector((0.06, 0, 0)), sc + Vector((0.12, 0, 0)), .30, .30, 18, "wood")
    torus(sd, sc + Vector((0.12, 0, 0)), .29, .025, 18, 4, "gold", normal=Vector((1, 0, 0)))
    ell(sd, sc + Vector((0.15, 0, 0)), (.05, .08, .08), 8, 5, "gold")
    for k in range(4):
        a = math.pi / 2 * k + 0.4
        box(sd, sc + Vector((0.125, 0.17 * math.cos(a), 0.17 * math.sin(a))), (.01, .04, .04), "metal_dark", 0)
    neck(0.08, 1.40, "bone")
    hc = Vector((0, -0.02, 1.58))
    hd = Part("zhead", rigid("head"))
    sbox(hd, hc, (.42, .40, .40), 12, 8, "bone", power=0.65)
    hd.recolor(lambda cc, n, cur: n.z < -0.6, "bone_shade")
    fy = hc.y - 0.20
    ell(hd, (0.085, fy, 1.58), (.05, .02, .045), 6, 4, "socket")
    ell(hd, (0.085, fy - 0.012, 1.58), (.025, .01, .025), 5, 3, "glow")       # one glowing red eye
    box(hd, (-0.085, fy - 0.005, 1.58), (.08, .015, .02), "socket", 0)         # the other stitched shut
    for k in range(3):
        box(hd, (-0.11 + 0.025 * k, fy - 0.01, 1.58), (.008, .01, .05), "extra5", 0)
    mouth_slab(hd, (0, fy - 0.005, 1.47), .16, .06, "socket", "extra5", 5)
    box(hd, (0, fy - 0.03, 1.52), (.045, .05, .06), "bone_shade", 0.01)    # nose
    # horned bronze helm with cheek guards
    hm = Part("helm", rigid("head"))
    loft(hm, [hring(1.66, .235, .225, 14, cy=-0.0), hring(1.74, .24, .23, 14, cy=-0.0), hring(1.84, .20, .19, 14, cy=-0.0), hring(1.90, .11, .10, 14, cy=-0.0)], "metal")
    for sx in (1, -1):
        box(hm, (0.215 * sx, -0.02, 1.56), (.04, .14, .16), "metal_dark", 0.02)
        tube(hm, [(0.22 * sx, -0.02, 1.76), (0.36 * sx, -0.04, 1.86), (0.42 * sx, -0.08, 2.02), (0.40 * sx, -0.12, 2.10)], [.06, .05, .03, .01], 6, "extra5")
    box(hm, (0, -0.235, 1.66), (.035, .03, .10), "metal_dark", 0.01)
WEAP["Bonesmith"] = lambda: _greatsword()
def _greatsword():
    p = wpart("greatsword")
    cyl(p, (0, 0, -0.18), (0, 0, 0.10), .03, .03, 6, "leather")
    ell(p, (0, 0, -0.21), (.05, .05, .05), 6, 4, "gold")
    box(p, (0, 0, 0.12), (.07, .40, .06), "gold", 0.015)
    pts = [(0, -0.07, 0.15), (0, 0.07, 0.15), (0, 0.065, 1.05), (0, 0.04, 1.12), (0, 0.0, 1.18), (0, -0.05, 1.10), (0, -0.065, 0.70)]
    slab(p, [Vector(q) - Vector((0.012, 0, 0)) for q in pts], (0.024, 0, 0), "metal")
    # chips in the edge
    for z, y in ((0.55, 0.068), (0.82, -0.066), (0.35, -0.068)):
        box(p, (0, y, z), (.03, .03, .03), "cloth_inner", 0)
    box(p, (0, 0, 0.55), (.03, .02, .70), "metal_dark", 0)
    return p

# ============================================================ MORTIMER CREPE - Undead Undertaker (Pallbearers)
PAL["Pallbearers"] = base(bone="B8BCC8", bone_shade="8A8E9A", cloth="1E1C24", cloth_dark="121016", cloth_inner="08070A", cloth2="2A2832", cloth2_dark="1A1820",
                          shirt="F0ECE4", shirt_dark="BEB8AC", accent="8A1A2A", accent_dark="5A1018", wood="6A4A33", wood_dark="3A2A1E",
                          gold="C9A24A", glow="7CF0B8", hair="141216", extra1="E8D888", extra2="2A2A30", shoe="141216")
def b_pallbearers():
    zc = ZC
    legs("cloth2", r=(.085, .08, .075, .07), shoe="shoe", scale=1.15, x=0.15)
    pelvis("cloth2", rx=.24, ry=.19, z0=0.46, z1=0.66)
    torso([(0.60, .24, .19), (0.80, .25, .20), (0.98, .27, .21), (1.12, .27, .21), (1.22, .22, .18), (1.30, .10, .09)], "shirt", "shirt_dark")
    # frock coat: long tails behind, open front
    fc = Part("frock", lambda co: TORSO_W(co) if co.z > 0.62 else skirt_w(0.62, 0.20, leg_max=0.6)(co))
    n = 14
    rings = [hring(z, rx, ry, n) for z, rx, ry in ((1.24, .245, .20), (1.10, .29, .225), (0.92, .285, .22), (0.70, .29, .23), (0.45, .33, .27), (0.18, .36, .30))]
    g, faces, caps = loft(fc, rings, "cloth", cap0=False, cap1=False)
    for f in list(faces):
        cc = f.calc_center_median()
        if cc.y < -0.14 and abs(cc.x) < (0.11 if cc.z > 0.66 else 0.28):
            fc.bm.faces.remove(f); faces.remove(f)
    shell(fc, faces, lambda co: Vector((co.x * .95, co.y * .95, co.z)), "cloth_inner", "cloth_dark")
    for z in (0.74, 0.86, 0.98):
        for sx in (1, -1):
            ell(fc, (0.12 * sx, -0.235, z), (.018, .012, .018), 5, 3, "extra2")
    # cravat + measuring tape draped round the neck
    q = Part("cravat", rigid("chest"))
    box(q, (0, -0.19, 1.22), (.10, .05, .06), "accent", 0.015)
    slab(q, [(-0.04, -0.20, 1.20), (0.04, -0.20, 1.20), (0.05, -0.22, 1.06), (-0.05, -0.22, 1.06)], (0, -0.02, 0), "accent_dark")
    for sx in (1, -1):
        tube(q, [(0.10 * sx, -0.10, 1.29), (0.14 * sx, -0.22, 1.16), (0.13 * sx, -0.25, 0.90)], [.016, .016, .016], 3, "extra1")
    for side, s in (("l", 1), ("r", -1)):
        arm(side, s, rad=(.08, .075, .072, .07, .068), sleeve_sw="cloth", hand_sw="bone", hand_scale=1.25, cuff_sw="shirt", cuff_r=.08, bony_wrist=False, x1=0.76)
        ell(Part("sh_" + side, ablend(0, [(0.16, {"chest": 1}), (0.30, {"chest": .4, "upperarm." + side: .6})], s)), (0.21 * s, 0, 1.15), (.12, .13, .09), 8, 4, "cloth")
    neck(0.05, 1.40, "bone")
    hc = Vector((0, -0.02, 1.62))
    hd = Part("uhead", rigid("head"))
    sbox(hd, hc, (.32, .34, .46), 12, 8, "bone", power=0.7, taper=lambda zn: (1.0 - 0.18 * max(0, -zn), 1.0))
    hd.recolor(lambda cc, n, cur: n.z < -0.6, "bone_shade")
    fy = hc.y - 0.17
    for sx in (1, -1):
        ell(hd, (0.07 * sx, fy + 0.01, 1.66), (.05, .02, .04), 6, 4, "socket")
        ell(hd, (0.07 * sx, fy - 0.004, 1.66), (.02, .01, .02), 5, 3, "glow")
        box(hd, (0.075 * sx, fy - 0.01, 1.715), (.08, .02, .016), "hair", 0, ROT(10 * sx, 'Y'))   # stern brows
    cone(hd, (0, fy, 1.62), (0, fy - 0.07, 1.57), .025, 4, "bone_shade")   # long nose
    box(hd, (0, fy - 0.005, 1.50), (.10, .01, .012), "socket", 0)          # thin frown
    for sx in (1, -1):
        tube(hd, [(0.09 * sx, fy, 1.49), (0.17 * sx, fy + 0.02, 1.47)], .012, 3, "hair")   # long thin moustache
    # sky-high top hat
    th = Part("tophat", rigid("head"))
    disc(th, (0, -0.02, 1.80), .27, .25, 16, "cloth", normal=Vector((0, 0, 1)))
    disc(th, (0, -0.02, 1.79), .27, .25, 16, "cloth_dark", normal=Vector((0, 0, -1)))
    loft(th, [hring(1.80, .17, .16, 14, cy=-0.02), hring(2.20, .18, .17, 14, cy=-0.02), hring(2.24, .18, .17, 14, cy=-0.02)], "cloth")
    loft(th, [hring(1.83, .175, .165, 14, cy=-0.02), hring(1.90, .178, .168, 14, cy=-0.02)], "accent", cap0=False, cap1=False)
WEAP["Pallbearers"] = lambda: _coffin()
def _coffin():
    p = wpart("coffin")
    # grip = a brass handle at the foot end; the coffin extends up (+Z)
    tube(p, [(0, -0.06, -0.02), (0, -0.06, 0.08), (0, 0.06, 0.08), (0, 0.06, -0.02)], .02, 5, "gold")
    pts = [(0, -0.13, 0.10), (0, 0.13, 0.10), (0, 0.24, 0.80), (0, 0.17, 1.10), (0, -0.17, 1.10), (0, -0.24, 0.80)]
    slab(p, [Vector(q) - Vector((0.10, 0, 0)) for q in pts], (0.20, 0, 0), "wood", "wood_dark")
    for xx in (-0.103, 0.103):
        box(p, (xx, 0, 0.78), (.01, .04, .30), "gold", 0)
        box(p, (xx, 0, 0.84), (.01, .18, .04), "gold", 0)
    for z in (0.40, 0.95):
        for sy in (-1, 1):
            tube(p, [(0, 0.20 * sy if z < 0.6 else 0.19 * sy, z), (0, 0.25 * sy, z)], .02, 4, "gold")
    return p

# ============================================================ ONDINE THE WAILING - Ghost (Widow)
PAL["Widow"] = base(cloth="DDEAFF", cloth_dark="A8B8D8", cloth_inner="5A6A8A", shirt="F4F8FF", shirt_dark="C8D4EC", bone="E8F0FF",
                    socket="1E1638", glow="B98CFF", glow2="E8DCFF", metal="8A90A0", metal_dark="5A6070", accent="C8B8FF")
def b_widow():
    zc = ZC
    g, rings, gw = floating_gown([(1.30, .16, .15), (1.20, .26, .22), (1.02, .28, .24), (0.80, .30, .26), (0.56, .36, .31), (0.32, .44, .38), (0.10, .52, .46)],
                                 "cloth", "cloth_dark", "cloth_inner", "cloth_dark", 16, 0.06)
    # wispy torn hem tails
    w_ = Part("tails", gw)
    for k in range(0, 16, 2):
        co = rings[-1][k]
        a = Vector((co.x * .95, co.y * .95, co.z + 0.04))
        cone(w_, a, a + Vector((co.x * 0.18, co.y * 0.18 + 0.06, -0.12)), 0.06, 4, "cloth_dark")
    # sheet sleeves that taper into wisps (no hands showing - just pale fingers)
    for side, s in (("l", 1), ("r", -1)):
        up, lo = "upperarm." + side, "lowerarm." + side
        a = Part("sleeve_" + side, segw(["chest", up, lo, "wrist." + side], k=5, bias={"chest": 1.6}))
        loft(a, [ring((x * s, 0, zc - dz), (0, -1, 0), (0, 0, 1), r, r * rz, 10) for x, r, rz, dz in
                 ((0.16, .12, 1, 0), (0.34, .11, 1.1, 0.01), (0.54, .12, 1.4, 0.03), (0.72, .13, 1.8, 0.06))], "cloth")
        hand(side, s, "bone", 1.1)
        # trailing broken shackle chain
        ch = Part("chain_" + side, rigid("wrist." + side))
        torus(ch, (0.74 * s, 0, zc), .10, .02, 12, 4, "metal", normal=Vector((1, 0, 0)))
        for k in range(4):
            torus(ch, (0.74 * s, 0.02 * k, zc - 0.14 - 0.08 * k), .035, .012, 8, 3, "metal_dark", normal=Vector((1, 0, 0)) if k % 2 else Vector((0, 1, 0)))
    neck(0.08, 1.40, "cloth")
    # head: a sheet draped over a round head, with hollow eyes and a long open "O" mouth
    hc = Vector((0, -0.01, 1.62))
    hd = Part("ghead", rigid("head"))
    ell(hd, hc, (.27, .26, .30), 14, 10, "shirt")
    hd.recolor(lambda cc, n, cur: n.z < -0.5, "shirt_dark")
    fy = hc.y - 0.255
    for sx in (1, -1):
        ell(hd, (0.095 * sx, fy, 1.66), (.06, .03, .085), 8, 5, "socket")
        ell(hd, (0.095 * sx, fy - 0.01, 1.64), (.018, .01, .02), 5, 3, "glow")
    ell(hd, (0, fy, 1.50), (.05, .03, .08), 8, 5, "socket")
    # flowing bridal veil from the crown, down the back
    vl = Part("veil", lambda co: {"head": max(0.0, min(1.0, (co.z - 1.1) / 0.4))} | ({"chest": 1 - max(0.0, min(1.0, (co.z - 1.1) / 0.4))} if co.z < 1.5 else {}))
    rows = []
    n = 9
    for z, rx, cy in ((1.86, .20, 0.0), (1.70, .30, 0.12), (1.30, .36, 0.26), (0.80, .40, 0.34)):
        rows.append([Vector((rx * math.cos(math.pi * k / (n - 1)), cy + 0.22 * math.sin(math.pi * k / (n - 1)), z)) for k in range(n)])
    rows[-1] = [co - Vector((0, 0, 0.08 if k % 2 else 0)) for k, co in enumerate(rows[-1])]
    g, faces, caps = loft(vl, rows, "glow2", closed=False)
    shell(vl, faces, lambda co: Vector((co.x * .97, co.y + 0.015, co.z)), "accent", "accent")
    torus(vl, (0, 0.0, 1.86), .19, .025, 14, 4, "metal")   # tarnished tiara band

# ============================================================ GRANNY NETTLE - Witch (Beak)
PAL["Beak"] = base(bone="8FB86A", bone_shade="6A9A4A", cloth="4A2E5C", cloth_dark="2E1C3A", cloth_inner="140A1C", cloth2="6A3A2A", cloth2_dark="4A2618",
                   shirt="5E4A6E", shirt_dark="3E2E4A", hair="C8C8C8", accent="B98CFF", accent_dark="7A4AC0", glow="7CF0B8", glow2="C8FFE0",
                   extra1="1E1A24", extra2="2E2A34", extra3="3A6B2A", metal="5A5A62", metal_dark="2A2A30", wood="7A5A3A", gold="C9A24A")
def b_beak():
    zc = ZC
    legs("cloth2", r=(.08, .075, .07, .065), shoe="extra1", scale=1.15)
    for side, s in (("l", 1), ("r", -1)):
        tp = Part("ptoe_" + side, rigid("toes." + side))
        cone(tp, (0.171 * s, -0.22, 0.07), (0.171 * s, -0.36, 0.14), .05, 5, "extra1")    # curled pointy shoes
    pelvis("cloth", rx=.26, ry=.21, z0=0.46, z1=0.66)
    torso([(0.60, .27, .22), (0.80, .30, .25), (0.96, .31, .26), (1.10, .29, .24), (1.20, .24, .20), (1.28, .12, .11)], "cloth", "cloth_dark")
    skirt([(0.68, .30, .25), (0.48, .37, .31), (0.26, .44, .37), (0.12, .47, .40)], "cloth", "cloth_dark", "cloth_inner", 16, 0.6, 0.04)
    # patches on the robe
    pt = Part("patches", TORSO_W)
    box(pt, (0.14, -0.26, 0.86), (.10, .02, .09), "cloth2", 0.0, ROT(12, 'Y'))
    stitch_line(pt, Vector((0.09, -0.272, 0.86)), Vector((0.19, -0.272, 0.88)), 4, "hair", Vector((0, -1, 0)))
    # shawl + belt with potion flasks and a small cauldron on the left hip
    sh = Part("shawl", rigid("chest"))
    loft(sh, [hring(1.28, .15, .14, 14), hring(1.17, .33, .28, 14), hring(1.02, .35, .30, 14)], "shirt", cap0=False, cap1=False)
    b = Part("belt", rigid("hips"))
    loft(b, [hring(0.64, .30, .25, 14), hring(0.69, .30, .25, 14)], "cloth2", cap0=False, cap1=False)
    for k, (x, col) in enumerate(((-0.12, "glow"), (-0.22, "accent"), (0.02, "glow"))):
        c = Vector((x, -0.26 + 0.02 * k, 0.58))
        ell(b, c, (.04, .04, .05), 6, 4, col)
        cyl(b, c + Vector((0, 0, 0.04)), c + Vector((0, 0, 0.08)), .015, .015, 5, "wood")
    cb = Part("cauldron", rigid("hips"))
    cc = Vector((0.32, -0.08, 0.50))
    ell(cb, cc, (.13, .13, .11), 10, 6, "metal_dark")
    disc(cb, cc + Vector((0, 0, 0.07)), .10, .10, 10, "glow", normal=Vector((0, 0, 1)))
    torus(cb, cc + Vector((0, 0, 0.08)), .11, .02, 12, 4, "metal")
    for k in range(3):
        ell(cb, cc + Vector((0.03 * math.cos(k * 2), 0.03 * math.sin(k * 2), 0.11 + 0.03 * k)), (.02, .02, .02), 4, 3, "glow2")
    for side, s in (("l", 1), ("r", -1)):
        arm(side, s, rad=(.085, .08, .085, .10, .12), sleeve_sw="cloth", hand_sw="bone", hand_scale=1.15, bony_wrist=False, x1=0.76)
    neck(0.05, 1.40, "bone")
    hc = Vector((0, -0.03, 1.58))
    hd = Part("whead", rigid("head"))
    sbox(hd, hc, (.38, .36, .40), 12, 8, "bone", power=0.7, taper=lambda zn: (1.0 - 0.15 * max(0, -zn), 1.0))
    hd.recolor(lambda cc_, n, cur: n.z < -0.6, "bone_shade")
    fy = hc.y - 0.18
    for sx in (1, -1):
        eye(hd, (0.075 * sx, fy, 1.62), 0.04, "glow2", "ink", look=(0.3 * sx, 0))
    tube(hd, [(0, fy, 1.60), (0, fy - 0.10, 1.55), (0, fy - 0.15, 1.48), (0, fy - 0.13, 1.44)], [.045, .035, .022, .012], 6, "bone")   # hooked nose
    ell(hd, (0.02, fy - 0.10, 1.55), (.018, .018, .018), 4, 3, "bone_shade")     # wart
    mouth_slab(hd, (0, fy + 0.01, 1.46), .14, .04, "socket", "paper", 3)
    # stringy grey hair
    hr = Part("hair", rigid("head"))
    for k in range(9):
        a = math.pi * (0.1 + 0.8 * k / 8)
        c = Vector((0.19 * math.cos(a), 0.08 + 0.12 * math.sin(a) - 0.05, 1.68))
        tube(hr, [c, c + Vector((0.03 * math.cos(a), 0.05, -0.20)), c + Vector((0.05 * math.cos(a), 0.06, -0.40))], [.025, .02, .01], 4, "hair")
    # huge floppy pointed hat with a crooked tip and a buckle band
    ht = Part("hat", rigid("head"))
    disc(ht, (0, -0.03, 1.76), .40, .38, 18, "extra1", normal=Vector((0, 0, 1)))
    disc(ht, (0, -0.03, 1.755), .40, .38, 18, "extra2", normal=Vector((0, 0, -1)))
    tube(ht, [(0, -0.02, 1.76), (0, 0.0, 1.98), (0.03, 0.04, 2.16), (0.14, 0.10, 2.26), (0.24, 0.10, 2.22)], [.22, .16, .10, .05, .015], 12, "extra1")
    loft(ht, [hring(1.78, .215, .205, 14, cy=-0.02), hring(1.84, .20, .19, 14, cy=-0.018)], "accent_dark", cap0=False, cap1=False)
    box(ht, (0, -0.22, 1.81), (.08, .02, .06), "gold", 0.01)
WEAP["Beak"] = lambda: _broom()
def _broom():
    p = wpart("broom")
    tube(p, [(0, 0, -0.30), (0.01, 0.0, 0.30), (-0.01, 0.01, 0.80), (0.0, 0.0, 1.05)], [.025, .023, .022, .02], 6, "wood")
    ell(p, (0, 0, -0.32), (.035, .035, .04), 6, 4, "wood_dark")
    torus(p, (0, 0, 1.05), .045, .015, 10, 4, "cloth2")
    cone(p, (0, 0, 1.02), (0, 0, 1.45), .16, 12, "gold")
    for k in range(8):
        a = 2 * math.pi * k / 8
        cone(p, (0.06 * math.cos(a), 0.06 * math.sin(a), 1.1), (0.15 * math.cos(a), 0.15 * math.sin(a), 1.47), .025, 3, "wood_dark")
    return p

# ============================================================ ARCHMAGE TICKWELL - Wizard (Tickwell)
PAL["Tickwell"] = base(bone="E8C8A8", bone_shade="C0A080", cloth="2E3A7A", cloth_dark="1E2654", cloth_inner="0E1230", cloth2="3A4890", cloth2_dark="26306A",
                       hair="F4F2EE", extra1="DCD8D0", gold="FFD26A", gold_dark="B97A22", glow="B98CFF", glow2="FFE08A", paper="F2EBDD",
                       wood="7A5A3A", wood_dark="4E3420", glass="CFF4FF", shoe="2A2030", accent="5A3A9A")
def b_tickwell():
    zc = ZC
    legs("cloth2", r=(.08, .075, .07, .065), shoe="shoe", scale=1.1)
    pelvis("cloth", rx=.26, ry=.21, z0=0.46, z1=0.66)
    torso([(0.60, .26, .21), (0.80, .28, .23), (0.96, .29, .23), (1.10, .28, .22), (1.20, .23, .19), (1.28, .11, .10)], "cloth", "cloth_dark")
    skirt([(0.70, .29, .24), (0.48, .35, .29), (0.26, .41, .35), (0.08, .45, .39)], "cloth", "cloth_dark", "cloth_inner", 16, 0.6, 0.0)
    # stars and moons
    st = Part("stars", TORSO_W)
    for x, z in ((0.14, 0.95), (-0.10, 0.80), (0.05, 0.70), (-0.17, 1.05)):
        y = -0.235 if z > 0.68 else -0.30
        slab(st, [(x, y, z + 0.03), (x + 0.012, y, z + 0.005), (x + 0.035, y, z), (x + 0.012, y, z - 0.008), (x, y, z - 0.035), (x - 0.012, y, z - 0.008), (x - 0.035, y, z), (x - 0.012, y, z + 0.005)], (0, -0.01, 0), "gold")
    sk = Part("skirtstars", skirt_w(0.70, 0.08, leg_max=0.6))
    for k in range(8):
        a = 2 * math.pi * k / 8 + 0.3
        c = Vector((0.40 * math.cos(a), 0.34 * math.sin(a), 0.25))
        ell(sk, c * 1.02, (.025, .025, .025), 4, 3, "gold")
    sk_ = Part("sash", rigid("hips"))
    loft(sk_, [hring(0.66, .28, .23, 14), hring(0.72, .28, .23, 14)], "gold_dark", cap0=False, cap1=False)
    for side, s in (("l", 1), ("r", -1)):
        up, lo = "upperarm." + side, "lowerarm." + side
        a = Part("sl_up_" + side, ablend(0, [(0.20, {"chest": .5, up: .5}), (0.30, {up: 1})], s))
        loft(a, [ring((x * s, 0, zc), (0, -1, 0), (0, 0, 1), r, r, 8) for x, r in ((0.16, .10), (0.30, .10), (0.46, .095))], "cloth")
        bb = Part("sl_lo_" + side, rigid(lo))
        g, faces, caps = loft(bb, [ring((x * s, 0, zc - dz), (0, -1, 0), (0, 0, 1), r, r, 10) for x, r, dz in ((0.43, .095, 0), (0.58, .13, 0.01), (0.70, .17, 0.03))], "cloth", cap0=True, cap1=False)
        bb.tag(faces[-10:], "gold")
        shell(bb, faces, lambda co, s=s: Vector((co.x, co.y * .8, zc + (co.z - zc) * .8)), "cloth_inner", "gold_dark")
        w = Part("wr_" + side, segw([lo, "wrist." + side, "hand." + side], k=6))
        tube(w, [(0.63 * s, 0, zc), (0.80 * s, 0, zc)], .04, 5, "bone")
        hand(side, s, "bone", 1.2)
    neck(0.05, 1.40, "bone")
    hc = Vector((0, -0.03, 1.60))
    hd = Part("wzhead", rigid("head"))
    sbox(hd, hc, (.36, .36, .40), 12, 8, "bone", power=0.7)
    fy = hc.y - 0.18
    for sx in (1, -1):
        eye(hd, (0.07 * sx, fy, 1.64), 0.03, "paper", "accent")
        box(hd, (0.08 * sx, fy - 0.03, 1.69), (.11, .05, .05), "hair", 0.02, ROT(-14 * sx, 'Y'))   # bushy brows
    ell(hd, (0, fy - 0.03, 1.58), (.045, .05, .05), 6, 4, "bone_shade")
    # long beard to the knees, moustache
    bd = Part("beard", lambda co: {"head": max(0.0, min(1.0, (co.z - 1.0) / 0.42)), "chest": 1 - max(0.0, min(1.0, (co.z - 1.0) / 0.42))})
    tube(bd, [(0, fy + 0.02, 1.54), (0, fy - 0.03, 1.36), (0, fy - 0.07, 1.10), (0, fy - 0.09, 0.80), (0.03, fy - 0.07, 0.56)], [.15, .17, .14, .09, .02], 10, "hair")
    for sx in (1, -1):
        tube(bd, [(0.02 * sx, fy - 0.05, 1.54), (0.12 * sx, fy - 0.04, 1.52), (0.19 * sx, fy - 0.01, 1.47)], [.03, .025, .01], 5, "extra1")
    # tall star hat with a clock face that ticks, crooked tip
    ht = Part("whhat", rigid("head"))
    disc(ht, (0, -0.03, 1.77), .34, .33, 18, "cloth", normal=Vector((0, 0, 1)))
    disc(ht, (0, -0.03, 1.765), .34, .33, 18, "cloth_dark", normal=Vector((0, 0, -1)))
    tube(ht, [(0, -0.03, 1.77), (0, -0.01, 2.02), (0.02, 0.03, 2.25), (0.10, 0.08, 2.38), (0.18, 0.05, 2.36)], [.21, .15, .09, .04, .012], 12, "cloth")
    cc = Vector((0, -0.21, 1.93))
    disc(ht, cc, .07, .07, 12, "paper")
    torus(ht, cc + Vector((0, 0.005, 0)), .07, .012, 12, 4, "gold", normal=Vector((0, 1, 0)))
    box(ht, cc + Vector((0, -0.01, 0.02)), (.008, .006, .045), "ink", 0)
    box(ht, cc + Vector((0.016, -0.01, 0)), (.035, .006, .008), "ink", 0)
WEAP["Tickwell"] = lambda: _hourglass_staff()
def _hourglass_staff():
    p = wpart("hourglass_staff")
    tube(p, [(0, 0, -0.45), (0.01, 0, 0.20), (-0.01, 0.0, 0.80), (0, 0, 1.00)], [.025, .025, .023, .025], 6, "wood")
    ell(p, (0, 0, -0.47), (.035, .035, .035), 6, 4, "gold")
    hc = Vector((0, 0, 1.17))
    for dz in (-0.17, 0.17):
        cyl(p, hc + Vector((0, 0, dz - 0.015)), hc + Vector((0, 0, dz + 0.015)), .11, .11, 10, "gold")
    cone(p, hc + Vector((0, 0, -0.15)), hc, .09, 10, "glass")
    cone(p, hc + Vector((0, 0, 0.15)), hc, .09, 10, "glass")
    cone(p, hc + Vector((0, 0, -0.15)), hc + Vector((0, 0, -0.06)), .07, 8, "glow2")   # sand
    for k in range(3):
        a = 2 * math.pi * k / 3
        cyl(p, hc + Vector((0.10 * math.cos(a), 0.10 * math.sin(a), -0.16)), hc + Vector((0.10 * math.cos(a), 0.10 * math.sin(a), 0.16)), .012, .012, 4, "gold_dark")
    # orbiting rune ring
    torus(p, hc, .20, .012, 18, 3, "glow", normal=Vector((0.3, 0, 1)))
    return p

BUILD.update(Neckless=b_neckless, Tallow=b_tallow, Bonesmith=b_bonesmith, Pallbearers=b_pallbearers, Widow=b_widow, Beak=b_beak, Tickwell=b_tickwell)

if __name__ == "__main__":
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    for m in (args or list(BUILD)):
        build_monster(m, PAL[m], BUILD[m], WEAP.get(m))
