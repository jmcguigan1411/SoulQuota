# Monster roster batch B: Grim (Shade), Collector (Vampire), Monk (Mummy), Femur (Werewolf), Digby (Patchwork Brute),
# Lookout (Gargoyle), Keeper (Bog Hag). Original designs.
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mr_lib import *
from mr_a import base, pumpkin
H = hexc
PAL, BUILD, WEAP = {}, {}, {}

# ============================================================ THE SHADE - hooded reaper, void face (Grim)
PAL["Grim"] = base(cloth="1E1830", cloth_dark="120E1E", cloth_inner="040308", cloth2="2A2240", cloth2_dark="1A1428", bone="1A1626", bone_shade="0E0C16",
                   socket="020104", glow="5CF2D6", glow2="B8FFF2", metal="CFC6B0", metal_dark="8A8070", extra1="3A2F55", gold="9AD9C8")
def b_grim():
    zc = ZC
    g, rings, gw = floating_gown([(1.28, .16, .15), (1.18, .29, .24), (1.02, .30, .25), (0.80, .30, .25), (0.56, .36, .31), (0.30, .44, .38), (0.06, .52, .46)],
                                 "cloth", "cloth_dark", "cloth_inner", "extra1", 16, 0.05)
    # smoke curling off the hem (the robe dissolves into smoke)
    w_ = Part("smoke", gw)
    for k in range(16):
        co = rings[-1][k]
        a = Vector((co.x * .9, co.y * .9, co.z + 0.05))
        ell(w_, a + Vector((co.x * .08, co.y * .08, -0.02)), (.07, .07, .05), 6, 3, "extra1" if k % 2 else "cloth_dark")
    for x, ht in ((0.25, 0.5), (-0.28, 0.4), (0.0, 0.6)):
        tube(w_, [(x, 0.35, 0.10), (x + 0.08, 0.48, ht * 0.6), (x - 0.02, 0.50, ht)], [.07, .045, .015], 6, "extra1")
    # rope belt with a soul lantern charm
    b = Part("cord", rigid("hips"))
    loft(b, [hring(0.68, .29, .24, 16), hring(0.71, .29, .24, 16)], "cloth2", cap0=False, cap1=False)
    tube(b, [(0.12, -0.24, 0.69), (0.14, -0.27, 0.52)], .015, 4, "cloth2")
    ell(b, (0.14, -0.27, 0.48), (.04, .04, .05), 6, 4, "glow")
    # shoulder mantle with a ragged edge
    m = Part("mantle", rigid("chest"))
    r0, r1 = hring(1.30, .17, .16, 16), hring(1.08, .40, .33, 16)
    r1 = [co - Vector((0, 0, 0.06 if k % 2 else 0)) for k, co in enumerate(r1)]
    gm, faces, caps = loft(m, [r0, r1], "cloth2", cap0=False, cap1=False)
    shell(m, faces, lambda co: Vector((co.x * .95, co.y * .95, co.z - 0.01)), "cloth_inner", "cloth2_dark")
    # wide sleeves; shadow hands with teal claw tips
    for side, s in (("l", 1), ("r", -1)):
        up, lo = "upperarm." + side, "lowerarm." + side
        a = Part("sl_" + side, segw(["chest", up, lo], k=5, bias={"chest": 1.6}))
        g2, faces, caps = loft(a, [ring((x * s, 0, zc - dz), (0, -1, 0), (0, 0, 1), r, r, 10) for x, r, dz in ((0.16, .11, 0), (0.42, .11, 0.0), (0.58, .14, 0.01), (0.72, .18, 0.04))], "cloth", cap0=True, cap1=False)
        shell(a, faces[-10:], lambda co, s=s: Vector((co.x, co.y * .8, zc + (co.z - zc) * .8)), "cloth_inner", "cloth_dark")
        w = Part("wr_" + side, segw([lo, "wrist." + side, "hand." + side], k=6))
        tube(w, [(0.62 * s, 0, zc), (0.82 * s, 0, zc)], .035, 5, "bone")
        claw_hand(side, s, "bone", "glow", 1.15)
    neck(0.06, 1.40, "cloth_inner")
    # deep hood with nothing inside but two eyes
    hd = Part("hood", rigid("head"))
    hc = Vector((0, 0.0, 1.62))
    n = 16
    rings_h = []
    for z, rx, ry, cy in ((1.30, .20, .19, 0.02), (1.46, .27, .26, 0.03), (1.64, .29, .29, 0.04), (1.82, .25, .26, 0.06), (1.96, .14, .16, 0.10), (2.04, .03, .04, 0.16)):
        rings_h.append(hring(z, rx, ry, n, cy=cy))
    gh, faces, caps = loft(hd, rings_h, "cloth", cap0=False, cap1=True)
    for f in list(faces):
        c = f.calc_center_median()
        if c.y < -0.10 and abs(c.x) < 0.17 and 1.40 < c.z < 1.86:
            hd.bm.faces.remove(f); faces.remove(f)
    shell(hd, faces, lambda co: Vector((co.x * .92, co.y * .92 + 0.01, co.z)), "cloth_inner", "cloth_dark")
    ell(hd, (0, 0.04, 1.63), (.19, .16, .21), 10, 6, "socket")          # the void
    for sx in (1, -1):
        ell(hd, (0.07 * sx, -0.11, 1.65), (.035, .02, .022), 6, 4, "glow")
        ell(hd, (0.07 * sx, -0.115, 1.65), (.06, .01, .04), 6, 4, "glow2")
WEAP["Grim"] = None   # keeps the imported Reaper scythe (Weapon = Scythe)

# ============================================================ COUNT LEDGERDRAIN - Vampire (Collector)
PAL["Collector"] = base(bone="DCD6E4", bone_shade="AEA6BC", cloth="1A1A22", cloth_dark="0E0E14", cloth_inner="5A1020", cloth2="22222C", cloth2_dark="141418",
                        shirt="F0ECE4", shirt_dark="C0B8AC", accent="B22234", accent_dark="7A1622", hair="141418", gold="FFD26A", gold_dark="B97A22",
                        glow="FF5050", glow2="FFD26A", shoe="0E0E12", extra1="3A0E18", extra2="5A1020")
def b_collector():
    zc = ZC
    legs("cloth2", r=(.085, .08, .075, .07), shoe="shoe", scale=1.1, x=0.15)
    pelvis("cloth2", rx=.24, ry=.20, z0=0.46, z1=0.66)
    torso([(0.60, .24, .20), (0.80, .26, .21), (0.98, .29, .22), (1.12, .30, .22), (1.22, .24, .19), (1.30, .10, .09)], "cloth", "cloth_dark")
    # waistcoat front + shirt + cravat jewel
    w = Part("vest", TORSO_W)
    slab(w, [(-0.10, -0.205, 1.20), (0.10, -0.205, 1.20), (0.12, -0.235, 0.66), (0.0, -0.245, 0.60), (-0.12, -0.235, 0.66)], (0, 0.012, 0), "accent")
    slab(w, [(-0.06, -0.218, 1.21), (0.06, -0.218, 1.21), (0.0, -0.228, 1.00)], (0, 0.008, 0), "shirt")
    ell(w, (0, -0.235, 1.12), (.03, .015, .03), 6, 4, "glow")
    for z in (0.80, 0.88, 0.96):
        ell(w, (0, -0.25, z), (.016, .01, .016), 5, 3, "gold")
    # sweeping high-collared cape, red lining
    cp = Part("vcape", TORSO_W)
    n = 13
    rows = []
    for z, rx, ry, cy in ((1.28, .26, .20, 0.02), (1.00, .40, .30, 0.06), (0.60, .50, .38, 0.10), (0.16, .58, .44, 0.14)):
        rows.append([Vector((rx * math.cos(math.pi * (0.06 + 0.88 * k / (n - 1))), cy + ry * math.sin(math.pi * (0.06 + 0.88 * k / (n - 1))) * 1.0, z)) for k in range(n)])
    rows[-1] = [co - Vector((0, 0, 0.07 if k % 2 else 0)) for k, co in enumerate(rows[-1])]
    def cape_w(co):
        return TORSO_W(co) if co.z > 0.62 else skirt_w(0.62, 0.16, leg_max=0.4)(co)
    cp.weights = cape_w
    g, faces, caps = loft(cp, rows, "cloth", closed=False)
    shell(cp, faces, lambda co: Vector((co.x * .97, co.y - 0.02, co.z)), "accent", "accent_dark")
    # standing collar behind the head (two tall points)
    cl = Part("collar", rigid("chest"))
    for sx in (1, -1):
        slab(cl, [(0.08 * sx, 0.02, 1.28), (0.30 * sx, 0.02, 1.34), (0.34 * sx, 0.10, 1.72), (0.12 * sx, 0.12, 1.50)], (0, 0.025, 0), "accent")
        slab(cl, [(0.08 * sx, 0.045, 1.28), (0.30 * sx, 0.045, 1.34), (0.34 * sx, 0.125, 1.72), (0.12 * sx, 0.145, 1.50)], (0, 0.02, 0), "cloth")
    for side, s in (("l", 1), ("r", -1)):
        arm(side, s, rad=(.08, .076, .073, .07, .068), sleeve_sw="cloth", hand_sw="bone", hand_scale=1.2, cuff_sw="shirt", cuff_r=.085, bony_wrist=False, x1=0.76)
        rg = Part("ring_" + side, rigid("hand." + side))
        ell(rg, (0.93 * s, -0.05, zc + 0.03), (.02, .02, .02), 5, 3, "gold")
    neck(0.05, 1.40, "bone")
    hc = Vector((0, -0.02, 1.62))
    hd = Part("vhead", rigid("head"))
    sbox(hd, hc, (.32, .34, .42), 12, 8, "bone", power=0.7, taper=lambda zn: (1.0 - 0.22 * max(0, -zn), 1.0))
    hd.recolor(lambda cc, n_, cur: n_.z < -0.6, "bone_shade")
    fy = hc.y - 0.17
    for sx in (1, -1):
        ell(hd, (0.07 * sx, fy, 1.66), (.04, .02, .025), 6, 4, "glow")
        box(hd, (0.07 * sx, fy - 0.005, 1.70), (.08, .02, .016), "hair", 0, ROT(-16 * sx, 'Y'))
        cone(hd, (0.035 * sx, fy - 0.005, 1.505), (0.035 * sx, fy - 0.01, 1.46), .012, 4, "paper")   # fangs
        cone(hd, (0.17 * sx, 0.0, 1.66), (0.24 * sx, 0.03, 1.74), .03, 4, "bone")                    # pointed ears
    box(hd, (0, fy - 0.003, 1.51), (.09, .01, .012), "accent_dark", 0)
    cone(hd, (0, fy, 1.62), (0, fy - 0.05, 1.575), .022, 4, "bone_shade")
    # monocle on the right eye
    torus(hd, (-0.07, fy - 0.02, 1.66), .045, .008, 12, 3, "gold", normal=Vector((0, 1, 0)))
    tube(hd, [(-0.115, fy - 0.02, 1.64), (-0.15, fy, 1.50), (-0.13, -0.18, 1.36)], .004, 3, "gold")
    # slicked-back hair
    hr = Part("vhair", rigid("head"))
    loft(hr, [hring(1.70, .17, .18, 14, cy=0.0), hring(1.80, .165, .18, 14, cy=0.0), hring(1.86, .11, .13, 14, cy=0.02)], "hair", cap0=False)
    tube(hr, [(0, -0.17, 1.80), (0, -0.19, 1.76), (0, -0.17, 1.72)], [.03, .03, .01], 4, "hair")          # widow's... no: a short forelock
WEAP["Collector"] = lambda: _cane()
def _cane():
    p = wpart("cane")
    tube(p, [(0, 0, -0.25), (0, 0, 0.50), (0, 0, 0.82)], [.02, .02, .022], 6, "cloth")
    cone(p, (0, 0, -0.25), (0, 0, -0.30), .022, 6, "gold")
    hc = Vector((0, 0.02, 0.90))
    ell(p, hc, (.06, .07, .06), 8, 5, "gold")
    for sx in (1, -1):
        slab(p, [hc + Vector((0.03 * sx, 0, 0.0)), hc + Vector((0.16 * sx, 0.0, 0.06)), hc + Vector((0.13 * sx, 0, -0.03)), hc + Vector((0.08 * sx, 0, -0.01))], (0, 0.015, 0), "gold_dark")
        ell(p, hc + Vector((0.025 * sx, -0.06, 0.02)), (.012, .01, .012), 4, 3, "glow")
    for k in range(3):
        cyl(p, (0.0, 0.05 + 0.04 * k, 0.60 - 0.05 * k), (0.0, 0.05 + 0.04 * k, 0.605 - 0.05 * k), .03, .03, 8, "gold")   # coins stacked on the shaft? small rings
    return p

# ============================================================ IMHOTET - Mummy (Monk)
PAL["Monk"] = base(bone="D8C8A0", bone_shade="B8A47A", cloth="E0D2AE", cloth_dark="B8A47A", cloth_inner="5A4A30", shirt="C8B48A", gold="E2B85A", gold_dark="9A7A34",
                   accent="2E6AB0", accent_dark="1E4A80", socket="0E0B16", glow="5CF2D6", glow2="9AD9C8", shoe="B8A47A", sole="8A7A5A")
def bandage(p, z0, z1, rx, ry, cy=0.0, n=14, step=0.045, sws=("cloth", "cloth_dark")):
    z = z0
    k = 0
    while z < z1:
        dz = step * (0.9 + 0.2 * ((k * 37) % 5) / 5)
        tilt = 0.02 * (1 if k % 2 else -1)
        r0 = [co + Vector((0, 0, tilt * co.x / max(rx, 0.01))) for co in hring(z, rx, ry, n, cy=cy)]
        r1 = [co + Vector((0, 0, tilt * co.x / max(rx, 0.01))) for co in hring(min(z1, z + dz), rx, ry, n, cy=cy)]
        loft(p, [r0, r1], sws[k % 2], cap0=False, cap1=False)
        z += dz * 0.92
        k += 1
def b_monk():
    zc = ZC
    for side, s in (("l", 1), ("r", -1)):
        lg = Part("mleg_" + side, segw(["upperleg." + side, "lowerleg." + side], k=6))
        tube(lg, [(0.16 * s, 0, 0.60), (0.16 * s, 0, 0.36), (0.16 * s, 0, 0.14)], [.10, .09, .08], 8, "cloth")
        for z in (0.55, 0.45, 0.35, 0.25, 0.17):
            tube(lg, [(0.16 * s, 0, z), (0.16 * s, 0, z - 0.035)], [.101 - (0.55 - z) * 0.05] * 2, 8, "cloth_dark")
        f = Part("mfoot_" + side, rigid("foot." + side))
        box(f, (0.16 * s, 0, 0.06), (.15, .19, .11), "cloth", 0.03)
        t = Part("mtoe_" + side, rigid("toes." + side))
        box(t, (0.16 * s, -0.15, 0.05), (.15, .14, .09), "cloth_dark", 0.03)
    pelvis("cloth", rx=.24, ry=.20, z0=0.46, z1=0.66)
    t = Part("mbody", TORSO_W)
    bandage(t, 0.58, 1.26, .27, .21)
    loft(t, [hring(0.58, .26, .20, 14), hring(1.25, .26, .20, 14)], "cloth_inner")
    # gold shendyt belt + loincloth with blue stripes
    lc = Part("shendyt", rigid("hips"))
    loft(lc, [hring(0.62, .27, .225, 14), hring(0.70, .27, .225, 14)], "gold", cap0=False, cap1=False)
    slab(lc, [(-0.10, -0.23, 0.63), (0.10, -0.23, 0.63), (0.08, -0.25, 0.38), (-0.08, -0.25, 0.38)], (0, -0.015, 0), "gold")
    for x in (-0.05, 0.0, 0.05):
        box(lc, (x, -0.268, 0.50), (.016, .006, .22), "accent", 0)
    # broad gold collar (usekh) in rings of gold and blue
    cl = Part("usekh", rigid("chest"))
    for i, (z, r) in enumerate(((1.25, .30), (1.21, .33), (1.17, .35))):
        loft(cl, [hring(z, r, r * 0.82, 16), hring(z - 0.045, r + 0.02, (r + 0.02) * 0.82, 16)], "gold" if i % 2 == 0 else "accent", cap0=False, cap1=False)
    # loose bandage ends fluttering from the back and arms
    fl = Part("ends", TORSO_W)
    for x, z in ((0.10, 0.95), (-0.14, 0.80)):
        slab(fl, [(x, 0.21, z), (x + 0.05, 0.21, z), (x + 0.10, 0.40, z - 0.25), (x + 0.06, 0.42, z - 0.27)], (0.0, 0.0, 0.012), "cloth")
    for side, s in (("l", 1), ("r", -1)):
        a = Part("marm_" + side, segw(["chest", "upperarm." + side, "lowerarm." + side, "wrist." + side], k=5, bias={"chest": 1.6}))
        tube(a, [(x * s, 0, zc) for x in (0.18, 0.40, 0.60, 0.78)], [.09, .085, .08, .075], 8, "cloth")
        for x in (0.26, 0.38, 0.50, 0.62, 0.72):
            tube(a, [(x * s, 0, zc), ((x + 0.04) * s, 0, zc)], [.091 - x * 0.02] * 2, 8, "cloth_dark")
        hand(side, s, "cloth", 1.25)
        br = Part("mbr_" + side, rigid("lowerarm." + side))
        tube(br, [(0.64 * s, 0, zc), (0.70 * s, 0, zc)], [.09, .09], 8, "gold")
        e = Part("mend_" + side, rigid("wrist." + side))
        tube(e, [(0.78 * s, 0.02, zc - 0.05), (0.74 * s, 0.10, zc - 0.20), (0.80 * s, 0.14, zc - 0.34)], [.02, .02, .01], 3, "cloth")
    neck(0.07, 1.40, "cloth_dark")
    hc = Vector((0, -0.01, 1.60))
    hd = Part("mhead", rigid("head"))
    bandage(hd, 1.42, 1.80, .19, .19, cy=-0.01, n=14, step=0.04)
    ell(hd, hc, (.18, .18, .20), 10, 6, "cloth_inner")
    # eye gap with two teal eyes
    box(hd, (0, -0.19, 1.64), (.22, .03, .05), "socket", 0.01)
    for sx in (1, -1):
        ell(hd, (0.055 * sx, -0.205, 1.64), (.022, .01, .016), 5, 3, "glow")
    # pharaoh nemes headcloth: striped gold/blue lappets down to the chest, a rearing cobra on the brow
    nm = Part("nemes", rigid("head"))
    loft(nm, [hring(1.70, .21, .21, 14, cy=0.0), hring(1.80, .22, .22, 14, cy=0.0), hring(1.88, .17, .18, 14, cy=0.01), hring(1.92, .06, .08, 14, cy=0.03)], "gold")
    for sx in (1, -1):
        for k in range(4):
            box(nm, (0.22 * sx, -0.06, 1.66 - 0.07 * k), (.04, .14, .065), "gold" if k % 2 == 0 else "accent", 0.01)
        slab(nm, [(0.20 * sx, 0.0, 1.78), (0.26 * sx, 0.04, 1.70), (0.24 * sx, -0.06, 1.42), (0.18 * sx, -0.10, 1.44)], (0.02 * sx, 0, 0), "accent")
    tube(nm, [(0, -0.20, 1.80), (0, -0.25, 1.86), (0, -0.26, 1.93)], [.018, .02, .025], 5, "gold")
    ell(nm, (0, -0.27, 1.95), (.03, .02, .025), 5, 3, "accent")
WEAP["Monk"] = lambda: _bandage_whip()
def _bandage_whip():
    p = wpart("bandage_whip")
    cyl(p, (0, 0, -0.12), (0, 0, 0.14), .03, .03, 6, "gold")
    ell(p, (0, 0, 0.16), (.045, .045, .03), 6, 4, "accent")
    pts = [(0, 0.0, 0.16), (0, 0.06, 0.35), (0, -0.04, 0.55), (0, 0.07, 0.75), (0, -0.02, 0.95), (0, 0.06, 1.10)]
    tube(p, pts, [.035, .032, .03, .027, .024, .02], 5, "cloth")
    ell(p, (0, 0.07, 1.15), (.05, .05, .06), 6, 4, "gold")     # weighted scarab tip
    return p

# ============================================================ RUFUS MOONFANG - Werewolf (Femur)
PAL["Femur"] = base(bone="7A6250", bone_shade="5A4636", cloth="6B5544", cloth_dark="4A3A2E", cloth_inner="2A1E1A", shirt="E8E2D0", shirt_dark="B8B0A0",
                    cloth2="3A4A6A", cloth2_dark="26324A", hair="5A4636", socket="1A1210", glow="FFD26A", glow2="FFF3B0", extra5="F2EBDD", extra1="A08670", accent="7A2A2A",
                    shoe="7A6250", sole="3A2E26")
def b_femur():
    zc = ZC
    # digitigrade-looking furry legs in torn trousers, clawed paws
    for side, s in (("l", 1), ("r", -1)):
        leg(side, s, "cloth2", r=(.12, .115, .10, .09), cuff_z=0.30, shoe_sw="bone", sole_sw="bone_shade", shoe_scale=1.25, ankle=False)
        lw = Part("shin_" + side, segw(["lowerleg." + side], k=6))
        tube(lw, [(0.171 * s, 0.01, 0.32), (0.171 * s, 0.02, 0.12)], [.085, .07], 7, "bone")
        for k in range(3):
            cone(lw, (0.171 * s + 0.04 * (k - 1), 0.0, 0.33), (0.171 * s + 0.05 * (k - 1), 0.0, 0.25), .03, 3, "cloth2")   # ragged cuff
        tc = Part("tclaw_" + side, rigid("toes." + side))
        for dx in (-0.05, 0.0, 0.05):
            cone(tc, (0.171 * s + dx, -0.23, 0.04), (0.171 * s + dx, -0.30, 0.01), .02, 4, "extra5")
    pelvis("cloth2", rx=.27, ry=.22, z0=0.46, z1=0.66)
    t, faces = torso([(0.60, .26, .21), (0.80, .30, .25), (1.00, .36, .29), (1.14, .37, .29), (1.24, .30, .24), (1.32, .16, .15)], "bone", "bone_shade", 14)
    # torn open shirt showing a furry chest
    sh = Part("shirt", TORSO_W)
    for sx in (1, -1):
        slab(sh, [(0.06 * sx, -0.29, 1.22), (0.30 * sx, -0.23, 1.20), (0.30 * sx, -0.25, 0.70), (0.12 * sx, -0.265, 0.66), (0.10 * sx, -0.30, 0.85)], (0, 0.03, 0), "shirt")
    for k in range(5):
        cone(sh, (0.12 + 0.03 * k, -0.27, 0.68), (0.13 + 0.03 * k, -0.27, 0.60 - 0.02 * (k % 2)), .02, 3, "shirt_dark")
    fur = Part("fur", TORSO_W)
    for k in range(7):
        z = 0.80 + 0.06 * k
        cone(fur, (0.0, -0.31, z), (0.0, -0.34, z - 0.07), .045, 4, "extra1")
    # back mane
    mn = Part("mane", rigid("chest"))
    for k in range(10):
        a = math.pi * (0.1 + 0.8 * k / 9)
        c = Vector((0.24 * math.cos(a), 0.10 + 0.16 * math.sin(a), 1.30))
        cone(mn, c, c + Vector((0.08 * math.cos(a), 0.10, -0.16)), .06, 4, "hair")
    # tail
    tl = Part("tail", rigid("hips"))
    tube(tl, [(0, 0.20, 0.58), (0, 0.38, 0.50), (0, 0.52, 0.40), (0.04, 0.60, 0.26)], [.06, .085, .07, .02], 7, "hair")
    for side, s in (("l", 1), ("r", -1)):
        arm(side, s, rad=(.11, .105, .10, .095, .09), sleeve_sw="bone", hand_sw="bone", hand_scale=1.45, bony_wrist=False, x1=0.76)
        claw_hand(side, s, "bone", "extra5", 1.45)
        tf = Part("tuft_" + side, rigid("lowerarm." + side))
        for k in range(3):
            cone(tf, ((0.56 + 0.05 * k) * s, 0.06, zc + 0.05), ((0.58 + 0.05 * k) * s, 0.12, zc + 0.10), .03, 3, "hair")
    neck(0.10, 1.40, "bone")
    # wolf head: skull box + long snout, ears (one bent), yellow eyes, teeth
    hd = Part("wolf", rigid("head"))
    hc = Vector((0, 0.0, 1.62))
    sbox(hd, hc, (.38, .36, .34), 12, 8, "bone", power=0.7)
    sbox(hd, (0, -0.24, 1.55), (.22, .26, .16), 10, 6, "bone", power=0.6)
    sbox(hd, (0, -0.24, 1.49), (.18, .22, .06), 8, 4, "bone_shade", power=0.6)                       # jaw
    ell(hd, (0, -0.375, 1.58), (.05, .03, .035), 6, 4, "socket")                                    # nose
    for sx in (1, -1):
        eye(hd, (0.085 * sx, -0.16, 1.67), 0.035, "glow", "socket", 0.4)
        box(hd, (0.09 * sx, -0.17, 1.71), (.09, .03, .025), "hair", 0, ROT(-18 * sx, 'Y'))
        for k in range(3):
            cone(hd, (0.06 * sx, -0.30 + 0.06 * k, 1.515), (0.06 * sx, -0.30 + 0.06 * k, 1.48), .012, 3, "extra5")
    cone(hd, (0.10, 0.02, 1.76), (0.14, 0.04, 1.98), .07, 4, "bone")
    tube(hd, [(-0.10, 0.02, 1.76), (-0.15, 0.03, 1.90), (-0.24, 0.0, 1.94)], [.07, .05, .01], 4, "bone")   # bent ear
    for k in range(5):
        a = math.pi * (0.15 + 0.7 * k / 4)
        cone(hd, (0.18 * math.cos(a), 0.0 + 0.05 * math.sin(a), 1.52), (0.24 * math.cos(a), -0.02, 1.44), .04, 3, "extra1")   # cheek ruff
WEAP["Femur"] = lambda: _bone_claws()
def _bone_claws():
    # a big gnawed thigh bone club (his StartWeapon BuriedBone throws these); grip at the narrow end
    p = wpart("bone_club")
    tube(p, [(0, 0, -0.08), (0, 0, 0.40), (0, 0.01, 0.72)], [.045, .04, .05], 7, "extra5")
    for sx in (1, -1):
        ell(p, (0.04 * sx, 0, 0.78), (.07, .07, .08), 8, 5, "extra5")
        ell(p, (0.035 * sx, 0, -0.10), (.05, .05, .06), 7, 4, "extra5")
    for z in (0.25, 0.50):
        box(p, (0, -0.04, z), (.03, .02, .02), "socket", 0)    # bite marks
    return p

# ============================================================ BOLT PATCHWORK - Patchwork Brute (Digby)
PAL["Digby"] = base(bone="8A9A7A", bone_shade="6A7A5E", shirt="A8B090", shirt_dark="7A8468", cloth="4A5568", cloth_dark="2E3646", cloth_inner="161A22",
                    cloth2="5A4A3A", cloth2_dark="3A2E24", accent="C8A07A", accent_dark="9A7A5A", metal="5A6470", metal_dark="3A424C",
                    gold="B9B9C0", glow="7FA8FF", glow2="DDEBFF", wood="7A5A3A", shoe="2A2420", extra1="3A2F2A", extra2="D8C8A0", extra3="B8E0B0")
def b_digby():
    zc = ZC
    legs("cloth", r=(.15, .14, .13, .12), shoe="shoe", scale=1.45, boot=True, x=0.19)
    pelvis("cloth", rx=.33, ry=.27, z0=0.46, z1=0.70)
    t, faces = torso([(0.64, .34, .28), (0.84, .40, .32), (1.04, .46, .34), (1.18, .45, .33), (1.28, .36, .28), (1.34, .18, .16)], "bone", "bone_shade", 14)
    # patchwork skin panels in mismatched tones with stitches
    pt = Part("patches", TORSO_W)
    for (x, z, w, h, col) in ((0.18, 1.02, .16, .18, "accent"), (-0.20, 0.86, .18, .14, "shirt"), (0.02, 0.74, .14, .12, "extra3")):
        y = -0.33 if z > 0.8 else -0.30
        box(pt, (x, y, z), (w, .02, h), col, 0.005)
        stitch_line(pt, Vector((x - w / 2, y - 0.012, z + h / 2)), Vector((x + w / 2, y - 0.012, z + h / 2)), 5, "extra1", Vector((0, -1, 0)))
        stitch_line(pt, Vector((x - w / 2, y - 0.012, z - h / 2)), Vector((x + w / 2, y - 0.012, z - h / 2)), 5, "extra1", Vector((0, -1, 0)))
    # overalls: bib + straps
    ov = Part("overalls", TORSO_W)
    slab(ov, [(-0.20, -0.33, 1.02), (0.20, -0.33, 1.02), (0.24, -0.31, 0.66), (-0.24, -0.31, 0.66)], (0, -0.02, 0), "cloth")
    for sx in (1, -1):
        tube(ov, [(0.16 * sx, -0.34, 1.01), (0.22 * sx, -0.30, 1.28), (0.18 * sx, 0.20, 1.26), (0.16 * sx, 0.31, 0.90)], .025, 4, "cloth_dark")
        ell(ov, (0.16 * sx, -0.355, 1.00), (.03, .015, .03), 5, 3, "gold")
    loft(ov, [hring(0.62, .36, .30, 14), hring(0.72, .36, .30, 14)], "cloth", cap0=False, cap1=False)
    # mismatched arms: a huge right arm, a normal left arm
    for side, s, big in (("l", 1, False), ("r", -1, True)):
        rad = (.15, .16, .17, .17, .16) if big else (.10, .095, .09, .088, .085)
        arm(side, s, rad=rad, sleeve_sw="shirt" if big else "bone", hand_sw="shirt" if big else "bone", hand_scale=1.9 if big else 1.35, bony_wrist=False, x1=0.76)
        st = Part("armstitch_" + side, rigid("upperarm." + side))
        torus(st, (0.30 * s, 0, zc), (rad[1] + 0.005), .008, 14, 3, "extra1", normal=Vector((1, 0, 0)))
        if big:
            for k in range(4):
                box(st, (0.36 * s, -0.04 + 0.03 * k, zc + 0.16), (.01, .01, .03), "extra1", 0)
    neck(0.11, 1.42, "bone")
    hc = Vector((0, -0.02, 1.62))
    hd = Part("phead", rigid("head"))
    sbox(hd, hc, (.38, .38, .40), 12, 8, "bone", power=0.6, taper=lambda zn: (1.0 + 0.06 * zn, 1.0))
    # one side a different skin tone
    hd.recolor(lambda cc, n_, cur: cc.x < -0.04 and cur == "bone", "shirt")
    stitch_line(hd, Vector((-0.04, -0.21, 1.80)), Vector((-0.04, -0.21, 1.46)), 6, "extra1", Vector((0, -1, 0)))
    fy = hc.y - 0.19
    eye(hd, (0.08, fy, 1.66), 0.045, "extra2", "ink")
    eye(hd, (-0.08, fy, 1.65), 0.032, "extra2", "glow", 0.5)
    box(hd, (0, fy, 1.52), (.15, .02, .03), "socket", 0.005)
    for k in range(4):
        box(hd, (-0.05 + 0.033 * k, fy - 0.004, 1.52), (.008, .01, .045), "extra2", 0)
    box(hd, (0.0, fy - 0.03, 1.59), (.05, .05, .05), "bone_shade", 0.01)
    # steel head brace with two tesla coils + sparks (original: a brace, not a flat head with neck bolts)
    br = Part("brace", rigid("head"))
    torus(br, (0, -0.02, 1.76), .21, .025, 16, 4, "metal")
    for sx in (1, -1):
        cyl(br, (0.12 * sx, -0.02, 1.78), (0.12 * sx, -0.02, 1.98), .03, .022, 6, "metal_dark")
        for k in range(3):
            torus(br, (0.12 * sx, -0.02, 1.84 + 0.04 * k), .035, .01, 8, 3, "gold")
        ell(br, (0.12 * sx, -0.02, 2.0), (.035, .035, .035), 6, 4, "glow")
    tube(br, [(0.12, -0.02, 2.0), (0.05, -0.04, 2.06), (-0.02, 0.0, 2.02), (-0.12, -0.02, 2.0)], .01, 3, "glow2")
WEAP["Digby"] = lambda: _shovel()
def _shovel():
    p = wpart("shovel")
    tube(p, [(0, 0, -0.15), (0, 0, 0.95)], [.03, .03], 6, "wood")
    tube(p, [(-0.07, 0, -0.18), (0.07, 0, -0.18)], .025, 5, "metal_dark")
    pts = [(0, -0.14, 0.95), (0, 0.14, 0.95), (0, 0.13, 1.25), (0, 0.0, 1.38), (0, -0.13, 1.25)]
    slab(p, [Vector(q) - Vector((0.012, 0, 0)) for q in pts], (0.024, 0, 0), "metal")
    for z in (1.02, 1.12, 1.22):
        box(p, (0, 0.0, z), (.03, .22, .012), "glow", 0)     # charged copper bands
    return p

# ============================================================ GRANITE GIL - Gargoyle (Lookout)
PAL["Lookout"] = base(bone="8A909C", bone_shade="5E6470", cloth="6A7080", cloth_dark="4A5060", cloth_inner="2A2E38", shirt="9AA0AC", shirt_dark="70768A",
                      extra1="3E7A3A", extra5="E8E2D0", glow="FFC24A", glow2="FFF3B0", accent="E07A1F", socket="1A1A22", shoe="7A808C", sole="4A5060")
def b_lookout():
    zc = ZC
    legs("bone", r=(.13, .12, .11, .10), shoe="bone", sole="bone_shade", scale=1.25)
    for side, s in (("l", 1), ("r", -1)):
        tc = Part("gclaw_" + side, rigid("toes." + side))
        for dx in (-0.06, 0.0, 0.06):
            cone(tc, (0.171 * s + dx, -0.24, 0.05), (0.171 * s + dx * 1.3, -0.33, 0.01), .03, 4, "shirt_dark")
        lc = Part("kneespike_" + side, rigid("lowerleg." + side))
        cone(lc, (0.171 * s, -0.09, 0.34), (0.171 * s, -0.20, 0.38), .04, 4, "bone_shade")
    pelvis("bone_shade", rx=.28, ry=.23, z0=0.46, z1=0.66)
    t, faces = torso([(0.60, .28, .23), (0.80, .32, .26), (1.00, .38, .30), (1.14, .40, .30), (1.24, .32, .25), (1.32, .16, .15)], "bone", "bone_shade", 14)
    # cracks and moss
    ck = Part("cracks", TORSO_W)
    for pts in (((0.12, -0.30, 1.10), (0.06, -0.31, 0.98), (0.10, -0.30, 0.88)), ((-0.18, -0.28, 0.82), (-0.10, -0.30, 0.76))):
        tube(ck, list(pts), .008, 3, "glow")
    for x, z in ((0.25, 1.15), (-0.30, 1.02), (0.0, 0.62)):
        ell(ck, (x, -0.22 if z > 0.7 else -0.26, z), (.06, .03, .04), 6, 3, "extra1")
    # big bat wings on the back (stone membranes with ribs), rigid to the chest
    wg = Part("wings", rigid("chest"))
    for sx in (1, -1):
        root = Vector((0.12 * sx, 0.24, 1.18))
        tip = Vector((0.85 * sx, 0.40, 1.58))
        elbow = Vector((0.45 * sx, 0.36, 1.50))
        tube(wg, [root, elbow, tip], [.05, .04, .02], 5, "bone_shade")
        fingers = [Vector((0.80 * sx, 0.42, 1.10)), Vector((0.62 * sx, 0.40, 0.90)), Vector((0.38 * sx, 0.34, 0.86))]
        for f in fingers:
            tube(wg, [elbow, f], [.025, .012], 4, "bone_shade")
        poly = [root, elbow, tip, fingers[0], fingers[1], fingers[2], Vector((0.16 * sx, 0.28, 0.90))]
        for k in range(len(poly) - 2):
            slab(wg, [poly[0], poly[k + 1], poly[k + 2]], (0, 0.015, 0), "cloth")
        cone(wg, elbow, elbow + Vector((0.02 * sx, 0.02, 0.12)), .03, 4, "bone_shade")
    tl = Part("gtail", rigid("hips"))
    tube(tl, [(0, 0.20, 0.56), (0, 0.40, 0.44), (0.06, 0.55, 0.30), (0.14, 0.62, 0.22)], [.07, .06, .04, .02], 6, "bone")
    slab(tl, [(0.14, 0.62, 0.22), (0.24, 0.66, 0.20), (0.16, 0.74, 0.20)], (0, 0, 0.02), "bone_shade")
    for side, s in (("l", 1), ("r", -1)):
        arm(side, s, rad=(.11, .10, .095, .09, .088), sleeve_sw="bone", hand_sw="bone", hand_scale=1.45, bony_wrist=False, x1=0.76)
        claw_hand(side, s, "bone", "shirt_dark", 1.45)
    neck(0.10, 1.40, "bone")
    hd = Part("ghead", rigid("head"))
    hc = Vector((0, -0.02, 1.62))
    sbox(hd, hc, (.42, .38, .36), 12, 8, "bone", power=0.6)
    sbox(hd, (0, -0.20, 1.53), (.30, .16, .14), 10, 6, "bone", power=0.6)                         # big jaw
    box(hd, (0, -0.28, 1.53), (.24, .02, .07), "socket", 0.01)
    for k in range(6):
        x = (k - 2.5) * 0.04
        cone(hd, (x, -0.29, 1.565), (x, -0.30, 1.52 if k % 2 else 1.50), .014, 3, "extra5")
        cone(hd, (x + 0.02, -0.29, 1.495), (x + 0.02, -0.30, 1.54), .012, 3, "extra5")
    # the glowing forehead eye, small side eyes, heavy brow, horns, pointed ears
    ell(hd, (0, -0.21, 1.70), (.06, .03, .055), 8, 5, "glow2")
    ell(hd, (0, -0.235, 1.70), (.025, .012, .03), 6, 4, "accent")
    for sx in (1, -1):
        ell(hd, (0.12 * sx, -0.19, 1.64), (.025, .012, .018), 5, 3, "glow")
        box(hd, (0.10 * sx, -0.21, 1.755), (.12, .05, .04), "bone_shade", 0.01, ROT(-15 * sx, 'Y'))
        tube(hd, [(0.14 * sx, -0.02, 1.78), (0.22 * sx, 0.02, 1.92), (0.20 * sx, 0.10, 2.02)], [.05, .035, .01], 5, "shirt_dark")
        cone(hd, (0.21 * sx, 0.0, 1.66), (0.34 * sx, 0.04, 1.74), .04, 4, "bone")
WEAP["Lookout"] = None

# ============================================================ OLD MOTHER MURK - Bog Hag (Keeper)
PAL["Keeper"] = base(bone="8FA06A", bone_shade="6A7A4A", cloth="4E5A3A", cloth_dark="343E26", cloth_inner="161C10", cloth2="6E3A4A", cloth2_dark="4A2632",
                     hair="5E7A3A", shirt="7A8A4A", extra1="2E2A20", extra2="5E8A3A", glow="FFD27A", glow2="FFF0C0", wood="5A4632", wood_dark="3A2E20",
                     metal="4A4A42", metal_dark="2A2A24", shoe="3A2E20", accent="9AC05A")
def b_keeper():
    zc = ZC
    legs("cloth_dark", r=(.08, .075, .07, .065), shoe="shoe", scale=1.1)
    pelvis("cloth", rx=.27, ry=.22, z0=0.46, z1=0.66)
    torso([(0.60, .28, .23), (0.80, .31, .25), (0.96, .32, .26), (1.10, .30, .24), (1.20, .25, .20), (1.28, .12, .11)], "cloth", "cloth_dark")
    sk = skirt([(0.68, .31, .26), (0.46, .38, .32), (0.24, .45, .38), (0.10, .48, .41)], "cloth", "cloth_dark", "cloth_inner", 16, 0.6, 0.05)
    # muddy wet hem + reeds and weeds caught in the skirt
    wd = Part("weeds", skirt_w(0.68, 0.10, leg_max=0.6))
    for k in range(10):
        a = 2 * math.pi * k / 10 + 0.2
        c = Vector((0.46 * math.cos(a), 0.40 * math.sin(a), 0.14))
        tube(wd, [c, c + Vector((0.02, 0.0, 0.14)), c + Vector((0.05, 0.0, 0.22))], [.012, .01, .004], 3, "extra2" if k % 2 else "accent")
    # shawl of moss
    sh = Part("moss", rigid("chest"))
    r1 = [co - Vector((0, 0, 0.07 if k % 2 else 0)) for k, co in enumerate(hring(1.04, .36, .30, 16))]
    g, faces, caps = loft(sh, [hring(1.28, .15, .14, 16), hring(1.17, .33, .28, 16), r1], "hair", cap0=False, cap1=False)
    shell(sh, faces, lambda co: Vector((co.x * .94, co.y * .94, co.z)), "cloth_inner", "extra2")
    # apron with pockets, a pouch of soul-candles
    ap = Part("apron", TORSO_W)
    slab(ap, [(-0.16, -0.27, 0.92), (0.16, -0.27, 0.92), (0.20, -0.32, 0.50), (-0.20, -0.32, 0.50)], (0, -0.015, 0), "cloth2")
    box(ap, (0.08, -0.33, 0.62), (.12, .02, .10), "cloth2_dark", 0.01)
    for side, s in (("l", 1), ("r", -1)):
        arm(side, s, rad=(.085, .08, .08, .09, .10), sleeve_sw="cloth", hand_sw="bone", hand_scale=1.2, bony_wrist=False, x1=0.76)
        claw_hand(side, s, "bone", "extra1", 1.15)
    # a fat frog riding the left shoulder
    fr = Part("frog", rigid("chest"))
    fc = Vector((0.26, -0.02, 1.32))
    ell(fr, fc, (.09, .10, .07), 8, 5, "extra2")
    for sx in (1, -1):
        ell(fr, fc + Vector((0.04 * sx, -0.06, 0.06)), (.03, .03, .03), 6, 4, "glow2")
        ell(fr, fc + Vector((0.04 * sx, -0.085, 0.065)), (.012, .01, .015), 4, 3, "ink")
        ell(fr, fc + Vector((0.07 * sx, -0.04, -0.05)), (.04, .05, .02), 5, 3, "extra2")
    box(fr, fc + Vector((0, -0.10, 0.0)), (.08, .01, .01), "ink", 0)
    neck(0.05, 1.40, "bone")
    hc = Vector((0, -0.03, 1.58))
    hd = Part("hhead", rigid("head"))
    sbox(hd, hc, (.38, .36, .38), 12, 8, "bone", power=0.7)
    hd.recolor(lambda cc, n_, cur: n_.z < -0.6, "bone_shade")
    fy = hc.y - 0.18
    eye(hd, (0.08, fy, 1.63), 0.045, "glow2", "ink")
    eye(hd, (-0.08, fy, 1.61), 0.032, "glow2", "ink")
    ell(hd, (0.03, fy - 0.05, 1.57), (.05, .06, .045), 6, 4, "bone_shade")   # bulbous nose
    for (x, z) in ((0.10, 1.53), (-0.06, 1.68)):
        ell(hd, (x, fy - 0.005, z), (.015, .012, .015), 4, 3, "extra2")       # warts
    mouth_slab(hd, (0, fy + 0.01, 1.48), .16, .04, "socket", "paper", 3)
    # dripping moss hair
    hr = Part("mhair", rigid("head"))
    for k in range(13):
        a = math.pi * (-0.1 + 1.2 * k / 12)
        c = Vector((0.20 * math.cos(a), 0.06 + 0.14 * math.sin(a) - 0.04, 1.68))
        tube(hr, [c, c + Vector((0.04 * math.cos(a), 0.04, -0.22)), c + Vector((0.05 * math.cos(a), 0.06, -0.42))], [.035, .028, .01], 4, "hair" if k % 2 else "extra2")
    # wide sagging hat
    ht = Part("bhat", rigid("head"))
    brim = [co - Vector((0, 0, 0.05 * abs(math.sin(2 * math.atan2(co.y + 0.03, co.x))))) for co in hring(1.76, .44, .42, 18, cy=-0.03)]
    loft(ht, [hring(1.76, .22, .21, 18, cy=-0.03), brim], "extra1", cap0=False, cap1=False)
    loft(ht, [hring(1.75, .22, .21, 18, cy=-0.03), hring(1.92, .19, .18, 18, cy=-0.01), hring(1.98, .12, .12, 18, cy=0.02)], "extra1", cap0=False)
    loft(ht, [hring(1.77, .225, .215, 18, cy=-0.03), hring(1.82, .215, .205, 18, cy=-0.025)], "cloth2", cap0=False, cap1=False)
WEAP["Keeper"] = lambda: _lantern_pole()
def _lantern_pole():
    p = wpart("lantern_pole")
    tube(p, [(0, 0, -0.40), (0.02, 0, 0.40), (-0.02, 0.0, 1.00), (0.0, 0.06, 1.18), (0.0, 0.20, 1.22)], [.03, .028, .026, .024, .02], 6, "wood")
    hook = Vector((0, 0.22, 1.20))
    tube(p, [hook, hook + Vector((0, 0.0, -0.10))], .008, 3, "metal")
    lc = hook + Vector((0, 0, -0.24))
    cyl(p, lc + Vector((0, 0, 0.11)), lc + Vector((0, 0, 0.14)), .07, .03, 6, "metal")
    cyl(p, lc + Vector((0, 0, -0.11)), lc + Vector((0, 0, -0.08)), .07, .07, 6, "metal")
    ell(p, lc, (.07, .07, .09), 8, 5, "glow")
    for k in range(4):
        a = math.pi / 2 * k + math.pi / 4
        cyl(p, lc + Vector((0.07 * math.cos(a), 0.07 * math.sin(a), -0.09)), lc + Vector((0.07 * math.cos(a), 0.07 * math.sin(a), 0.11)), .008, .008, 3, "metal_dark")
    return p

BUILD.update(Grim=b_grim, Collector=b_collector, Monk=b_monk, Femur=b_femur, Digby=b_digby, Lookout=b_lookout, Keeper=b_keeper)

if __name__ == "__main__":
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    for m in (args or list(BUILD)):
        build_monster(m, PAL[m], BUILD[m], WEAP.get(m))
