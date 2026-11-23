# Soul Quota reapers, batch 1 (Grim, Mort, Widow, Bonesmith, Keeper, Monk, Intern). Original work.
# usage: python3 rq_b1.py [Id ...]
import bpy, math, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rq_lib import *

PAL = {}
BUILD = {}

def reg(mid, pal):
    def deco(fn):
        PAL[mid] = pal
        BUILD[mid] = fn
        return fn
    return deco

# ================================================================ GRIM: the senior reaper
@reg("Grim", dict(bone="#EEE7D2", bone_shade="#C2B9A2", bone_dark="#8F8672", socket="#07060B", eye="#5CF2D6", eye_hi="#E8FFFA",
                  glow="#5CF2D6", glow2="#9AF8E6", c1="#4A3D66", c1_dark="#30283F", c1_light="#5E5080", c1_inner="#0B0912",
                  c2="#3A2F52", c2_dark="#251E34", trim="#8A7BA8", rope="#9C8C6A", wood="#5A4330", wood_dark="#3A2A1E",
                  metal="#A8A6B2", gold="#C9A55A", glass="#BFE9E2"))
def build_grim():
    # ---- long robe: torso + skirt to the ankles (follows the legs below the hips)
    p, g, f = torso([(0.56, .27, .24), (0.72, .29, .255), (0.88, .30, .26), (1.03, .29, .245), (1.17, .23, .20), (1.28, .13, .12)], "c1", 12)
    hem = [0.0, 0.05, 0.01, 0.07, 0.0, 0.04, 0.02, 0.08, 0.0, 0.05, 0.01, 0.07, 0.0, 0.06, 0.02, 0.05]
    skirt([(0.64, .285, .255, 0), (0.46, .31, .28, 0.0), (0.28, .35, .31, 0.01), (0.12, .40, .35, 0.02)], hem,
          "c1", "c1_inner", "c1_dark", hem_sw="c1_dark", wts=skirt_w(0.60, 0.26, 0.85, 0.45, 0.12))
    # front fold lines (darker strips) on the skirt
    q = Part("folds", skirt_w(0.60, 0.26, 0.85, 0.45, 0.12))
    for sx in (-0.13, 0.13):
        tube(q, [(sx, -0.262, 0.60), (sx * 1.15, -0.29, 0.40), (sx * 1.3, -0.33, 0.16)], [.016, .018, .02], 4, "c1_dark")
    # ---- big mantle over the shoulders, ragged edge
    q = Part("mantle", zblend([(1.0, {"chest": 1}), (1.3, {"chest": 1})]))
    hemm = [0.0, 0.08, 0.02, 0.10, 0.0, 0.07, 0.03, 0.09, 0.0, 0.08, 0.02, 0.10, 0.0, 0.06, 0.02, 0.09]
    r0 = hring(1.34, .19, .18, 16)
    r1 = hring(1.22, .38, .33, 16)
    r2 = [co - Vector((0, 0, hemm[k])) for k, co in enumerate(hring(1.03, .44, .38, 16))]
    gg, faces, caps = loft(q, [r0, r1, r2], "c2", cap0=False, cap1=False)
    shell(q, faces, lambda co: Vector((co.x * .9, co.y * .9, co.z - 0.015)), "c1_inner", "c2_dark")
    # soul clasp at the collar
    c = Part("clasp", rigid("chest"))
    ell(c, (0, -0.255, 1.235), (.05, .025, .05), 6, 4, "glow")
    box(c, (0, -0.24, 1.235), (.13, .02, .025), "metal", 0.005)
    # ---- rope belt + hourglass on the left hip
    belt(0.66, .30, .265, .026, "rope", 16, 4)
    h = Part("hourglass", rigid("hips"))
    hc = Vector((0.27, -0.16, 0.50))
    R = ROT(-30, 'Z')
    cyl(h, hc + Vector((0, 0, 0.09)), hc + Vector((0, 0, 0.11)), .065, .065, 6, "wood")
    cyl(h, hc + Vector((0, 0, -0.11)), hc + Vector((0, 0, -0.09)), .065, .065, 6, "wood")
    cyl(h, hc + Vector((0, 0, -0.09)), hc, .05, .012, 6, "glow")
    cyl(h, hc, hc + Vector((0, 0, 0.09)), .012, .05, 6, "glass")
    for k in range(3):
        a = 2 * math.pi * k / 3
        box(h, hc + Vector((.055 * math.cos(a), .055 * math.sin(a), 0)), (.014, .014, .2), "wood_dark")
    cyl(h, hc + Vector((0, 0, 0.11)), (0.26, -0.20, 0.66), .01, .01, 4, "rope")
    # ---- arms: big bell sleeves, long bony fingers
    for side, s in (("l", 1), ("r", -1)):
        bony_arm(side, s, 0.03, "claw", 1.25)
        sleeve(side, s, .105, .115, .19, 0.70, "c1", "c1_inner", "c1_dark", n=10, droop=0.03)
    # ---- legs (mostly hidden) + bony bare feet
    for side, s in (("l", 1), ("r", -1)):
        bony_leg(side, s, 0.042)
        boot(side, s, "bare", "bone", "bone_shade", sc=1.1)
    # ---- head: tall pointed hood over a skull with teal eyes
    hood((0, 0.02, 1.62), (0.43, 0.42, 0.44),
         tip=((0, .05, .12, .17), (0, .13, .25, .11), (0, .22, .34, .06)), tip_end=(0, .30, .40),
         open_deg=56, sw="c2", inner="c1_inner", rim="trim", rim_dark="c2_dark")
    ell(Part("void", rigid("head")), (0, 0.0, 1.60), (.34, .32, .34), 10, 7, "c1_inner")
    skull((0, -0.12, 1.57), (.25, .23, .25), 0.9, eyes=dict(r=.045, socket=.085, sx=0.098, hi=False), mouth="grin",
          eye_z=0.03, mouth_z=-0.13, mouth_w=0.10)


LEGS = (("l", 1), ("r", -1))

def wrap_rings(side, s, zs, r, sw, bone_name=None):
    """bandage wraps around the shin"""
    x = 0.171 * s
    p = Part("wrap_" + side, rigid(bone_name or "lowerleg." + side))
    for k, z in enumerate(zs):
        cyl(p, (x, 0.0, z - 0.022), (x, 0.0, z + 0.022), r + 0.004 * (k % 2), r, 6, sw)

# ================================================================ MORT: Swift the Hauler
@reg("Mort", dict(bone="#EEE7D2", bone_shade="#C4BBA3", socket="#07080D", eye="#7FD8FF", eye_hi="#F0FBFF",
                  glow="#5CF2D6", glow2="#B4FFF0", c1="#3A78AE", c1_dark="#28567E", c1_light="#8CC8F2", c1_inner="#0E1A26",
                  c2="#1E3F5C", c2_dark="#142C41", c2_light="#5AA9E6", c3_light="#5E4A3A", c3="#E9E4D6", c3_dark="#B9B3A3", trim="#5AA9E6",
                  leather="#8A6A4A", leather_dark="#5E452F", metal="#B8B6BE", boot="#5AA9E6", boot_dark="#2D5D86", sole="#F2EEE4",
                  accent="#F2B134", accent_dark="#B57A1E"))
def build_mort():
    # lean jacket
    torso([(0.58, .235, .21), (0.74, .25, .22), (0.90, .265, .225), (1.04, .26, .215), (1.17, .21, .18), (1.27, .12, .11)], "c1", 12)
    q = Part("jacket_hem", skirt_w(0.62, 0.40, 0.6, 0.0, 0.1))
    loft(q, [hring(0.64, .245, .22, 12), hring(0.52, .26, .235, 12)], "c1_dark", cap0=False, cap1=False)
    # zip + pockets
    z = Part("zip", TORSO_W)
    box(z, (0, -0.226, 0.90), (.025, .02, .58), "metal")
    for sx in (1, -1):
        box(z, (0.13 * sx, -0.222, 0.72), (.11, .02, .08), "c1_dark", 0.005)
    # shorts
    skirt([(0.60, .24, .215, 0), (0.48, .265, .235, 0), (0.38, .27, .24, 0)], [0, .02, 0, .015], "c2", "c1_inner", "c2_dark",
          n=12, wts=skirt_w(0.58, 0.40, 0.9, 0.0, 0.08), name="shorts")
    # short cape: open at the front, longer at the back
    q = Part("cape", zblend([(0.9, {"chest": .6, "spine": .4}), (1.1, {"chest": 1})]))
    n = 14
    r0 = hring(1.32, .17, .16, n)
    r1 = hring(1.22, .34, .29, n)
    drop = [0.10, 0.14, 0.22, 0.30, 0.36, 0.40, 0.42, 0.42, 0.42, 0.40, 0.36, 0.30, 0.22, 0.14]
    r2 = [Vector((co.x * 1.06, co.y * 1.06 + 0.02, 1.20 - drop[k])) for k, co in enumerate(hring(1.2, .36, .31, n))]
    gg, faces, caps = loft(q, [r0, r1, r2], "c2", cap0=False, cap1=False)
    shell(q, faces, lambda co: Vector((co.x * .9, co.y * .9, co.z - 0.01)), "c1_inner", "trim")
    # scarf ends flying back
    sc = Part("scarf", rigid("chest"))
    loft(sc, [hring(1.24, .15, .14, 10), hring(1.31, .14, .13, 10)], "accent", cap0=False, cap1=False)
    tube(sc, [(0.06, 0.12, 1.27), (0.12, 0.32, 1.24), (0.20, 0.50, 1.16)], [(.07, .018), (.07, .018), (.06, .014)], 4, "accent")
    tube(sc, [(0.0, 0.13, 1.25), (-0.02, 0.28, 1.16), (0.02, 0.38, 1.04)], [(.045, .015), (.045, .015), (.04, .012)], 4, "accent_dark")
    # messenger bag on the right hip, stuffed with glowing souls; strap over the left shoulder
    b = Part("bag", rigid("hips"))
    bc = Vector((-0.27, 0.33, 0.64))
    BR = ROT(-35, 'Z')
    box(b, bc, (.46, .18, .34), "leather", 0.04, BR)
    box(b, bc + BR @ Vector((0, 0.012, 0.09)), (.465, .185, .18), "leather_dark", 0.02, BR @ ROT(-8, 'X'))
    box(b, bc + BR @ Vector((0, 0.10, 0.03)), (.08, .012, .07), "metal", 0, BR)
    for k, (dx, dz, r) in enumerate(((-0.14, 0.22, .11), (0.0, 0.27, .13), (0.14, 0.21, .10))):
        ell(b, bc + BR @ Vector((dx, -0.01, dz)), (r, r, r), 6, 4, "glow" if k != 1 else "glow2")
    st = Part("strap", TORSO_W)
    tube(st, [(-0.27, 0.10, 0.70), (-0.24, -0.15, 0.80), (0.05, -0.26, 1.02), (0.19, -0.19, 1.20), (0.20, 0.02, 1.27),
              (0.12, 0.215, 1.10), (-0.06, 0.27, 0.88), (-0.14, 0.32, 0.80)], (.035, .008), 4, "leather_dark", up=Vector((0, 0, 1)))
    # arms: rolled jacket sleeves, bony forearms
    for side, s in LEGS:
        bony_arm(side, s, 0.033, "fist", 1.05)
        p = Part("sleeve_" + side, sleeve_up_w(side, s))
        loft(p, [ring((x * s, 0, ZC), (0, -1, 0), (0, 0, 1), r, r, 8, math.pi / 8) for x, r in ((0.16, .095), (0.30, .095), (0.42, .10))], "c1")
        loft(p, [ring((x * s, 0, ZC), (0, -1, 0), (0, 0, 1), r, r, 8, math.pi / 8) for x, r in ((0.40, .108), (0.45, .11))], "c1_light")
        g = Part("glovecuff_" + side, rigid("wrist." + side))
        cyl(g, (0.76 * s, 0, ZC), (0.81 * s, 0, ZC), .05, .055, 6, "c2")
    for side, s in LEGS:
        bony_leg(side, s, 0.04)
        wrap_rings(side, s, (0.19, 0.24), 0.05, "c3")
        boot(side, s, "sneaker", "boot", "boot_dark", "sole", sc=1.05)
    # head: skull + flat cap with a long brim
    skull((0, -0.09, 1.56), (.27, .25, .27), 0.88, eyes=dict(r=.07, socket=.085, sx=0.105), mouth="grin", eye_z=0.02)
    h = Part("cap", rigid("head"))
    vs = ell(h, (0, 0.0, 1.76), (.33, .33, .14), 12, 6, "c2_light")
    for v in vs:
        v.co.z = max(v.co.z, 1.72)
        if v.co.y < 0:
            v.co.z -= 0.03 * (-v.co.y / 0.29)
    shade_down(h, "c2_light", "c2", -0.5)
    brim = [Vector((0.30 * math.cos(a), -0.06 - 0.36 * math.sin(a), 1.72)) for a in [math.pi * k / 8 for k in range(9)]]
    slab(h, brim[::-1], (0, 0, 0.025), "c2_dark")
    transform_part(h, lambda co: co)  # keep
    box(h, (0, 0.0, 1.885), (.06, .06, .03), "c1_light", 0.01)
    box(h, (0, -0.26, 1.77), (.30, .02, .04), "c1_light", 0.005)

# ================================================================ WIDOW: the Wailing Widow (floats, no feet)
@reg("Widow", dict(bone="#E9DDF6", bone_shade="#BBA9D2", socket="#1A0F26", eye="#C77DFF", eye_hi="#F7EEFF",
                   glow="#C77DFF", glow2="#E3B8FF", glow3="#9B5BE0", c1="#54327A", c1_dark="#3A2356", c1_light="#7450A0", c1_inner="#0E0716",
                   c2="#1A1023", c2_dark="#0F0915", c2_light="#2C1C3C", lace="#8A6AB0", trim="#B58AE0", skin="#DCCDEE", skin_dark="#B2A0CC",
                   metal="#B8A8CC", gold="#C9A55A"))
def build_widow():
    float_w = skirt_w(0.60, 0.15, 0.30, 0.0, 0.18)
    # corset bodice
    torso([(0.60, .22, .20), (0.74, .205, .185), (0.90, .235, .205), (1.04, .25, .21), (1.17, .20, .17), (1.27, .11, .10)], "c1", 12)
    lc = Part("lacing", TORSO_W)
    for k in range(4):
        z = 0.70 + 0.07 * k
        box(lc, (0, -0.20, z), (.10, .015, .012), "trim", 0, ROT(20 * (1 if k % 2 else -1), 'Y'))
    # big bell gown to the floor, scalloped hem, layered ruffle
    hem = [0.0, 0.03, 0.05, 0.03] * 5
    skirt([(0.66, .23, .21, 0), (0.50, .33, .30, 0.01), (0.32, .44, .40, 0.02), (0.16, .52, .47, 0.03), (0.08, .56, .50, 0.03)],
          hem, "c1", "c1_inner", "c1_dark", hem_sw="c2_light", n=20, wts=float_w, name="gown")
    rf = Part("ruffle", float_w)
    hemr = [0.0, 0.04, 0.0, 0.05] * 5
    r0 = hring(0.46, .32, .29, 20, cy=0.01)
    r1 = [co - Vector((0, 0, hemr[k])) for k, co in enumerate(hring(0.30, .43, .39, 20, cy=0.02))]
    g, faces, caps = loft(rf, [r0, r1], "c2", cap0=False, cap1=False)
    shell(rf, faces, lambda co: Vector((co.x * .93, co.y * .93, co.z + 0.01)), "c1_inner", "lace")
    # spectral wisps around the hem (glow)
    wp = Part("wisps", float_w)
    for k in range(0, 20, 3):
        a = -math.pi / 2 + 2 * math.pi * k / 20
        c0 = Vector((0.55 * math.cos(a), 0.03 + 0.49 * math.sin(a), 0.10))
        cone(wp, c0, c0 + Vector((0.10 * math.cos(a), 0.10 * math.sin(a), -0.07)), 0.035, 4, "glow3")
    # high lace collar + glowing brooch
    cl = Part("collar", rigid("chest"))
    jag = [0.0, 0.03] * 6
    r0 = hring(1.22, .14, .13, 12)
    r1 = [co + Vector((0, 0, jag[k])) for k, co in enumerate(hring(1.40, .22, .20, 12))]
    g, faces, caps = loft(cl, [r0, r1], "lace", cap0=False, cap1=False)
    shell(cl, faces, lambda co: Vector((co.x * .85, co.y * .85, co.z)), "c2_dark", "c2")
    ell(cl, (0, -0.215, 1.15), (.045, .02, .055), 6, 4, "glow")
    box(cl, (0, -0.205, 1.15), (.09, .015, .1), "gold", 0.01)
    # long tight sleeves with lace bell cuffs, pale ghostly hands
    for side, s in LEGS:
        bony_arm(side, s, 0.03, "claw", 1.1, sw="skin", shade="skin_dark")
        p = Part("sleeve_up_" + side, sleeve_up_w(side, s))
        loft(p, [ring((x * s, 0, ZC), (0, -1, 0), (0, 0, 1), r, r, 8, math.pi / 8) for x, r in ((0.15, .09), (0.30, .08), (0.465, .075))], "c1")
        q = Part("sleeve_lo_" + side, rigid("lowerarm." + side))
        loft(q, [ring((x * s, 0, ZC), (0, -1, 0), (0, 0, 1), r, r, 8, math.pi / 8) for x, r in ((0.43, .075), (0.62, .065))], "c1", cap0=True, cap1=False)
        jg = [0.0, 0.04] * 5
        r0 = ring((0.60 * s, 0, ZC), (0, -1, 0), (0, 0, 1), .065, .065, 10)
        r1 = [co + Vector((jg[k] * s, 0, 0)) for k, co in enumerate(ring((0.72 * s, 0, ZC - 0.02), (0, -1, 0), (0, 0, 1), .14, .14, 10))]
        g, faces, caps = loft(q, [r0, r1], "lace", cap0=False, cap1=False)
        shell(q, faces, lambda co: Vector((co.x, co.y * .85, ZC + (co.z - ZC) * .85)), "c2_dark", "c2")
    # head: pale wailing face, little mourning hat, long black veil
    skull((0, -0.08, 1.58), (.23, .22, .26), 0.88, sw="skin", shade="skin_dark",
          eyes=dict(r=.06, socket=.08, sx=0.092, sh=1.25), mouth="o", nose=False, eye_z=0.03, mouth_z=-0.13)
    hood((0, 0.03, 1.62), (0.34, 0.33, 0.36), tip=(), tip_end=(0, 0.02, 0.06),
         lats=(-82, -60, -32, -4, 26, 52, 74), open_deg=62, open_rows=(0, 3), sw="c2", inner="c2_dark", rim="lace", rim_dark="c2_light", name="veil")
    vt = Part("veil_tail", zblend([(0.95, {"chest": .7, "spine": .3}), (1.25, {"chest": 1})]))
    n = 9
    cols = []
    for k in range(n):
        a = math.radians(-20 + 220 * k / (n - 1))  # back half
        cols.append(a)
    rows = []
    for z, rx, ry, cy in ((1.36, .26, .24, 0.06), (1.12, .34, .30, 0.08), (0.86, .38, .34, 0.10)):
        rows.append([Vector((rx * math.cos(a), cy + ry * math.sin(a), z - (0.06 if (k % 2 and z < 1) else 0))) for k, a in enumerate(cols)])
    g, faces, caps = loft(vt, rows, "c2", closed=False)
    shell(vt, faces, lambda co: Vector((co.x * .95, (co.y - 0.06) * .92 + 0.06, co.z)), "c2_dark", "lace")
    ht = Part("hat", rigid("head"))
    cyl(ht, (0.0, 0.04, 1.93), (0.0, 0.04, 2.01), .17, .16, 10, "c2_light")
    ell(ht, (0.12, -0.08, 2.0), (.06, .04, .05), 6, 4, "glow")  # glowing flower
    for k in range(3):
        a = 2 * math.pi * k / 3
        ell(ht, (0.12 + .06 * math.cos(a), -0.09, 2.0 + .06 * math.sin(a)), (.04, .02, .04), 5, 3, "lace")
    # lace fringe over the brow
    fr = Part("fringe", rigid("head"))
    pts = []
    for k in range(9):
        t = -1 + 2 * k / 8
        pts.append(Vector((0.24 * t, -0.30 + 0.09 * t * t, 1.76 - (0.05 if k % 2 else 0.0))))
    for a, b2 in zip(pts, pts[1:]):
        fr.face([fr.v(a), fr.v(b2), fr.v(Vector((b2.x, b2.y, 1.82))), fr.v(Vector((a.x, a.y, 1.82)))], "c2")
    fr.recalc = False
    for f in fr.bm.faces:
        f.normal_update()
        if f.normal.y > 0:
            f.normal_flip()
    shell(fr, list(fr.bm.faces), lambda co: co + Vector((0, 0.012, 0)), "c2_dark", "c2")

# ================================================================ BONESMITH: stocky skeleton blacksmith
@reg("Bonesmith", dict(bone="#EDE4CC", bone_shade="#C3B898", bone_dark="#8E846A", socket="#120A06", eye="#FF8A3D", eye_hi="#FFE2B0",
                       glow="#FF8A3D", glow2="#FFC266", glow3="#FF5A1F", c1="#6B4A2B", c1_dark="#4A3220", c1_light="#8A6440", c1_inner="#1E140C",
                       c2="#3B3A44", c2_dark="#26252D", leather="#6B4A2B", leather_dark="#4A3220", metal="#7C7882", metal_dark="#4E4B54",
                       gold="#F2B134", boot="#3A2A1E", boot_dark="#24190F", sole="#1A1410", glass="#FFC266", rope="#9C8C6A"))
def build_bonesmith():
    # barrel ribcage with a forge glowing inside
    p, g, f = torso([(0.58, .30, .25), (0.72, .32, .26), (0.88, .36, .29), (1.02, .37, .29), (1.15, .31, .25), (1.27, .15, .13)], "socket", 14)
    p.recolor(lambda c, n, cur: n.y < -0.3 and 0.80 < c.z < 1.20 and abs(c.x) < 0.27, "glow")
    p.recolor(lambda c, n, cur: cur == "socket" and n.y < -0.2 and 0.80 < c.z < 1.20, "glow3")
    rb = Part("ribs", TORSO_W)
    for k, z in enumerate((0.86, 0.95, 1.04, 1.13)):
        sc = (1.0, 1.04, 1.04, 0.92)[k]
        rx, ry = 0.355 * sc + 0.015, 0.285 * sc + 0.015
        pts = []
        for j in range(11):  # from the spine round the side to the sternum, both sides
            a = math.radians(90 + 165 * j / 10)
            pts.append((rx * math.cos(a), ry * math.sin(a) * -1, z + 0.03 * math.sin(math.pi * j / 10)))
        tube(rb, pts, .026, 4, "bone", up=Vector((0, 0, 1)))
        tube(rb, [(-x, y, zz) for x, y, zz in pts], .026, 4, "bone", up=Vector((0, 0, 1)))
    box(rb, (0, -0.31, 1.0), (.07, .05, .32), "bone", 0.015)  # sternum
    for k in range(6):  # spine at the back
        box(rb, (0, 0.29, 0.66 + 0.11 * k), (.09, .06, .07), "bone", 0.015)
    shade_down(rb, "bone", "bone_shade", -0.5)
    # big round shoulder bones
    for side, s in LEGS:
        sh = Part("shoulder_" + side, sleeve_up_w(side, s))
        ell(sh, (0.23 * s, 0, 1.13), (.13, .13, .12), 8, 5, "bone")
        shade_down(sh, "bone", "bone_shade", -0.5)
    # heavy trousers
    skirt([(0.62, .30, .25, 0), (0.48, .31, .265, 0), (0.30, .30, .26, 0)], [0, .02, 0, .01], "c2", "c1_inner", "c2_dark",
          n=14, wts=skirt_w(0.60, 0.40, 0.95, 0.25, 0.08), name="trousers")
    belt(0.64, .31, .265, .03, "leather_dark", 16, 4)
    bk = Part("buckle", rigid("hips"))
    box(bk, (0, -0.28, 0.64), (.11, .03, .09), "gold", 0.01)
    # leather apron: bib + long front panel that follows the legs
    ap_w = skirt_w(0.60, 0.25, 0.7, 0.3, 0.16)
    ap = Part("apron", ap_w)
    cols = 7
    rows = []
    for z, w, y in ((0.86, .22, -0.335), (0.80, .25, -0.335), (0.72, .27, -0.32), (0.62, .29, -0.30), (0.45, .30, -0.32), (0.28, .31, -0.34)):
        rows.append([Vector((w * (-1 + 2 * k / (cols - 1)), y + 0.04 * (abs(-1 + 2 * k / (cols - 1)) ** 2), z)) for k in range(cols)])
    g, faces, caps = loft(ap, rows, "leather", closed=False)
    shell(ap, faces, lambda co: co + Vector((0, 0.018, 0)), "leather_dark", "leather_dark")
    pk = Part("apron_pocket", ap_w)
    box(pk, (0.12, -0.335, 0.52), (.14, .03, .12), "leather_dark", 0.01)
    cyl(pk, (0.10, -0.335, 0.55), (0.08, -0.34, 0.70), .012, .012, 4, "metal")  # tongs
    cyl(pk, (0.14, -0.335, 0.55), (0.17, -0.34, 0.70), .012, .012, 4, "metal")
    ns = Part("apron_strap", rigid("chest"))
    tube(ns, [(-0.19, -0.33, 0.84), (-0.31, -0.20, 1.02), (-0.20, -0.05, 1.26), (0.0, 0.10, 1.29), (0.20, -0.05, 1.26), (0.31, -0.20, 1.02), (0.19, -0.33, 0.84)], (.03, .01), 4, "leather_dark")
    # thick bony arms, big leather gloves
    for side, s in LEGS:
        bony_arm(side, s, 0.048, "mitt", 1.25, hand_sw="leather")
    # legs: heavy steel-toed boots
    for side, s in LEGS:
        bony_leg(side, s, 0.055)
        boot(side, s, "boot", "boot", "boot_dark", "sole", sc=1.2, cuff_sw="leather")
        t = Part("toecap_" + side, rigid("toes." + side))
        box(t, (0.171 * s, -0.235, 0.07), (.22, .09, .12), "metal", 0.03)
    # head: big wide skull, heavy brow, toothy jaw, goggles on the forehead
    skull((0, -0.09, 1.55), (.30, .26, .27), 0.86, eyes=dict(r=.05, socket=.08, sx=0.115, sh=0.9, hi=False),
          mouth="teeth", brow=0.06, eye_z=0.0, mouth_z=-0.14, mouth_w=0.13)
    jw = Part("jaw", rigid("head"))
    box(jw, (0, -0.16, 1.37), (.36, .24, .10), "bone", 0.035)
    shade_down(jw, "bone", "bone_shade", -0.4)
    gg = belt(1.69, .29, .245, .022, "leather_dark", 14, 4, wts=rigid("head"), cy=-0.08, name="goggle_strap")
    gl = Part("goggles", rigid("head"))
    for sx in (1, -1):
        c = Vector((0.11 * sx, -0.33, 1.70))
        cyl(gl, c + Vector((0, 0.04, 0)), c, .075, .075, 8, "metal_dark")
        cyl(gl, c, c - Vector((0, 0.012, 0)), .058, .058, 8, "glass")
    box(gl, (0, -0.335, 1.70), (.06, .03, .03), "metal_dark")

# ================================================================ KEEPER: the Lantern Keeper
@reg("Keeper", dict(bone="#EFE6D0", bone_shade="#C6BBA1", socket="#0E0A06", eye="#FFD27A", eye_hi="#FFF6DE",
                    glow="#FFD27A", glow2="#FFB347", c1="#6E5640", c1_dark="#4C3B2A", c1_light="#8E7456", c1_inner="#160F09",
                    c2="#3E3022", c2_dark="#2A2016", c2_light="#5A4834", c3="#8A3E36", c3_dark="#5E2A24", white="#ECE8E0",
                    metal="#8C8478", metal_dark="#5A544C", gold="#D8A848", wood="#7A5A3A", wood_dark="#523C26", glass="#E8F0F0",
                    accent="#6B7A4A", accent_dark="#4A5634"))
def build_keeper():
    torso([(0.56, .27, .24), (0.72, .29, .255), (0.88, .30, .27, 0.02), (1.02, .29, .26, 0.04), (1.15, .23, .21, 0.03), (1.27, .13, .12)], "c1", 12)
    hem = [0.0, 0.04, 0.01, 0.05, 0.0, 0.03, 0.02, 0.05, 0.0, 0.04, 0.01, 0.06, 0.0, 0.03, 0.01, 0.05]
    sk, g, f = skirt([(0.64, .285, .255, 0), (0.46, .31, .28, 0.01), (0.30, .34, .30, 0.02), (0.16, .37, .33, 0.02)], hem,
                     "c1", "c1_inner", "c1_dark", hem_sw="c1_dark", wts=skirt_w(0.60, 0.26, 0.85, 0.45, 0.12), name="coat")
    pt = Part("patches", skirt_w(0.60, 0.26, 0.85, 0.45, 0.12))
    box(pt, (0.20, -0.255, 0.36), (.12, .02, .11), "accent", 0, ROT(-28, 'Z') @ ROT(8, 'Y'))
    box(pt, (-0.22, 0.26, 0.42), (.13, .02, .10), "c3", 0, ROT(-35, 'Z'))
    belt(0.64, .30, .265, .025, "c2", 16, 4)
    ky = Part("keys", rigid("hips"))
    c = Vector((-0.22, -0.20, 0.56))
    tube(ky, [c + Vector((0, 0, 0.06)), c + Vector((-0.04, -0.01, 0.0)), c + Vector((0, -0.02, -0.05)), c + Vector((0.04, -0.01, 0.0)), c + Vector((0, 0, 0.06))], .01, 4, "gold")
    for k, dx in enumerate((-0.04, 0.0, 0.04)):
        box(ky, c + Vector((dx, -0.03, -0.11)), (.016, .01, .09), "gold")
        box(ky, c + Vector((dx + 0.012, -0.03, -0.15)), (.02, .01, .02), "gold")
    # shawl + humped back
    q = Part("shawl", rigid("chest"))
    fr = [0.0, 0.06] * 8
    r0 = hring(1.33, .18, .18, 16, cy=0.02)
    r1 = hring(1.22, .36, .33, 16, cy=0.04)
    r2 = [co - Vector((0, 0, fr[k] + (0.10 if co.y > 0.1 else 0))) for k, co in enumerate(hring(1.06, .41, .38, 16, cy=0.06))]
    gg, faces, caps = loft(q, [r0, r1, r2], "c3", cap0=False, cap1=False)
    shell(q, faces, lambda co: Vector((co.x * .9, co.y * .9, co.z - 0.015)), "c1_inner", "c3_dark")
    hm = Part("hump", rigid("chest"))
    ell(hm, (0, 0.20, 1.10), (.27, .16, .20), 10, 6, "c1")
    # lantern hanging on a crooked hook above the hump
    ln = Part("back_lantern", rigid("chest"))
    tube(ln, [(0.05, 0.28, 1.10), (0.14, 0.38, 1.36), (0.26, 0.48, 1.50), (0.34, 0.52, 1.47)], .022, 5, "wood_dark")
    lantern(ln, (0.34, 0.52, 1.13), 0.30, 0.095, "metal_dark", "glow")
    # sleeves + thin bony hands
    for side, s in LEGS:
        bony_arm(side, s, 0.03, "claw", 1.15)
        sleeve(side, s, .10, .105, .15, 0.69, "c1", "c1_inner", "c1_dark", n=10, droop=0.02)
    for side, s in LEGS:
        bony_leg(side, s, 0.04)
        boot(side, s, "clog", "wood", "wood_dark", sc=1.05)
    # head pushed forward and low (hunched), spectacles, white hair tufts, wide-brim hat
    skull((0, -0.15, 1.52), (.25, .23, .25), 0.88, eyes=dict(r=.05, socket=.075, sx=0.10, hi=True), mouth="grin",
          eye_z=0.03, mouth_z=-0.13, mouth_w=0.09)
    sp = Part("specs", rigid("head"))
    fy = -0.15 - 0.23 * 0.88
    for sx in (1, -1):
        c0 = Vector((0.10 * sx, fy - 0.02, 1.55))
        tube(sp, [c0 + Vector((0.065 * math.cos(a), 0, 0.065 * math.sin(a))) for a in [2 * math.pi * k / 8 for k in range(9)]], .011, 4, "gold", False, False, up=Vector((0, -1, 0)))
    box(sp, (0, fy - 0.02, 1.565), (.07, .012, .012), "gold")
    hr = Part("hair", rigid("head"))
    for sx in (1, -1):
        ell(hr, (0.23 * sx, -0.10, 1.60), (.07, .09, .09), 6, 4, "white")
        ell(hr, (0.20 * sx, 0.02, 1.66), (.08, .09, .08), 6, 4, "white")
    ht = Part("hat", rigid("head"))
    hc = Vector((0, -0.12, 1.75))
    vs = cyl(ht, hc, hc + Vector((0, 0, 0.045)), .50, .46, 16, "c2")
    for v in vs:  # droopy brim edges
        d = math.hypot(v.co.x, v.co.y - hc.y)
        if d > 0.4:
            v.co.z -= 0.05 * abs(math.cos(math.atan2(v.co.y - hc.y, v.co.x)))
    cr = cyl(ht, hc + Vector((0, 0, 0.04)), hc + Vector((0.0, 0.03, 0.26)), .23, .17, 10, "c2_light")
    for v in cr:  # crooked crown
        if v.co.z > hc.z + 0.2:
            v.co.x += 0.06; v.co.y += 0.03
    cyl(ht, hc + Vector((0, 0, 0.04)), hc + Vector((0, 0, 0.09)), .235, .225, 10, "c3")
    shade_down(ht, "c2", "c2_dark", -0.5)

# ================================================================ MONK: the Hollow Monk
@reg("Monk", dict(bone="#EEE7D2", bone_shade="#C4BBA3", socket="#081010", eye="#5CF2D6", eye_hi="#E8FFFA",
                  glow="#5CF2D6", glow2="#9AD9C8", c1="#D08A2E", c1_dark="#9A5E1C", c1_light="#E8A84A", c1_inner="#2A1606",
                  c2="#8C8A86", c2_dark="#64625E", c3="#E6E0D0", c3_dark="#B8B1A0", wood="#B07A48", wood_dark="#7A5230",
                  sole="#5A4632", boot_dark="#4A3220", rope="#8C8A86"))
def build_monk():
    torso([(0.58, .25, .22), (0.74, .27, .235), (0.90, .285, .24), (1.04, .28, .23), (1.17, .22, .19), (1.27, .12, .11)], "c1", 12)
    # ash sash from the left shoulder to the right hip, and a waist wrap
    sh = Part("sash", TORSO_W)
    tube(sh, [(0.20, -0.10, 1.27), (0.12, -0.25, 1.08), (-0.06, -0.28, 0.86), (-0.22, -0.22, 0.68)], (.07, .02), 4, "c2")
    tube(sh, [(0.20, 0.10, 1.27), (0.10, 0.24, 1.05), (-0.08, 0.26, 0.84), (-0.24, 0.20, 0.68)], (.07, .02), 4, "c2_dark")
    belt(0.66, .275, .245, .03, "c2", 16, 4, wts=TORSO_W)
    kn = Part("knot", rigid("hips"))
    ell(kn, (-0.18, -0.21, 0.64), (.06, .04, .05), 6, 4, "c2")
    tube(kn, [(-0.18, -0.23, 0.62), (-0.20, -0.25, 0.48)], (.04, .012), 4, "c2_dark")
    # knee-length robe skirt
    hem = [0.0, 0.03, 0.0, 0.04] * 4
    skirt([(0.64, .265, .235, 0), (0.48, .30, .265, 0), (0.34, .33, .29, 0.01)], hem, "c1", "c1_inner", "c1_dark",
          hem_sw="c1_dark", wts=skirt_w(0.60, 0.36, 0.9, 0.15, 0.10))
    # left arm: robe sleeve; right arm: bare bone with a wrist wrap
    bony_arm("l", 1, 0.034, "fist", 1.1)
    sleeve("l", 1, .10, .105, .13, 0.62, "c1", "c1_inner", "c1_dark", jag=(0, .02), n=10, droop=0.01)
    bony_arm("r", -1, 0.036, "fist", 1.1)
    ww = Part("wristwrap_r", rigid("lowerarm.r"))
    for k, x in enumerate((0.62, 0.67)):
        cyl(ww, (-x, 0, ZC), (-(x + 0.045), 0, ZC), .05, .048, 6, "c3")
    sp = Part("shoulder_r", sleeve_up_w("r", -1))
    ell(sp, (-0.22, 0, 1.12), (.09, .09, .09), 6, 4, "bone")
    for side, s in LEGS:
        bony_leg(side, s, 0.042)
        wrap_rings(side, s, (0.18, 0.225, 0.27), 0.052, "c3")
        boot(side, s, "sandal", "bone", "boot_dark", "sole", sc=1.05)
    # bald skull with a glowing third eye, calm slit eyes
    skull((0, -0.08, 1.57), (.27, .25, .28), 0.88, eyes=dict(r=.06, socket=.07, sx=0.10, sh=0.45, hi=False), mouth="grin",
          eye_z=0.0, mouth_z=-0.14, mouth_w=0.08)
    te = Part("third_eye", rigid("head"))
    fy = -0.08 - 0.25 * 0.88
    disc(te, (0, fy - 0.006, 1.69), .05, .065, 4, "c1_dark", phase=0.0)
    disc(te, (0, fy - 0.011, 1.69), .032, .045, 4, "glow")
    te.recalc = False
    for f in te.bm.faces:
        f.normal_update()
        if f.normal.y > 0:
            f.normal_flip()
    # prayer-bead orbit around the waist (tilted ring of floating beads, big guru bead in front)
    bd = Part("bead_orbit", rigid("spine"))
    nb = 12
    tilt = ROT(8, 'X')
    for k in range(nb):
        a = -math.pi / 2 + 2 * math.pi * k / nb
        c0 = tilt @ Vector((0.60 * math.cos(a), 0.60 * math.sin(a), 0.0)) + Vector((0, 0, 0.86))
        r = 0.09 if k == 0 else 0.065
        ell(bd, c0, (r, r, r), 6, 4, "glow" if k % 3 == 0 else "wood")
        if k % 3 == 0 and k:
            pass
    for k in range(nb):  # faint glow link between beads
        a0 = -math.pi / 2 + 2 * math.pi * (k + 0.5) / nb
        c0 = tilt @ Vector((0.60 * math.cos(a0), 0.60 * math.sin(a0), 0.0)) + Vector((0, 0, 0.86))
        ell(bd, c0, (.018, .018, .018), 4, 3, "glow2")

# ================================================================ INTERN: tiny skeleton in an oversized hoodie
@reg("Intern", dict(bone="#F2EBDD", bone_shade="#C9C1AE", socket="#0C0B10", eye="#9AF4FF", eye_hi="#FFFFFF",
                    glow="#9AF4FF", c1="#D6C460", c1_dark="#A89838", c1_light="#F2E394", c1_inner="#3A3414",
                    red="#D8434B", red_dark="#9E2A30", white="#F7F4EE", paper="#F2EBDD", coffee="#7A5A3A", wood="#9A7A50",
                    boot="#D8434B", boot_dark="#9E2A30", sole="#F7F4EE", ink="#2A3A6A", metal="#9A9AA4", c2="#3A4A6A", c2_dark="#26324A"))
def build_intern():
    hood_w = skirt_w(0.58, 0.34, 0.55, 0.0, 0.14)
    def hw(co):
        return hood_w(co) if co.z < 0.62 else TORSO_W(co)
    p = Part("hoodie", hw)
    spec = [(0.34, .32, .28), (0.40, .345, .30), (0.56, .36, .31), (0.74, .37, .32), (0.92, .36, .31), (1.06, .33, .28), (1.18, .26, .23), (1.27, .15, .14)]
    rings = [hring(z, rx, ry, 14) for z, rx, ry in spec]
    g, faces, caps = loft(p, rings, "c1", cap0=False, cap1=True)
    p.tag(faces[:14], "c1_dark")
    shell(p, faces[:14], lambda co: Vector((co.x * .85, co.y * .85, co.z + 0.01)), "c1_inner", "c1_dark")
    # pocket with a clipboard poking out
    pk = Part("pocket", hw)
    box(pk, (0, -0.33, 0.60), (.34, .04, .16), "c1_dark", 0.015)
    cb = Part("clipboard", rigid("spine"))
    box(cb, (0.08, -0.345, 0.74), (.16, .015, .22), "wood", 0.005, ROT(-12, 'Y'))
    box(cb, (0.083, -0.355, 0.76), (.13, .01, .17), "paper", 0, ROT(-12, 'Y'))
    box(cb, (0.10, -0.358, 0.85), (.06, .02, .03), "metal", 0.005, ROT(-12, 'Y'))
    # bunched hood behind the neck, drawstrings
    hd = Part("hood_bunch", rigid("chest"))
    vs = ell(hd, (0, 0.13, 1.33), (.30, .19, .15), 10, 6, "c1")
    shade_down(hd, "c1", "c1_dark", -0.4)
    loft(hd, [hring(1.24, .19, .18, 12), hring(1.36, .21, .20, 12)], "c1_dark", cap0=False, cap1=False)
    for sx in (0.05, -0.05):
        tube(hd, [(sx, -0.18, 1.30), (sx * 1.2, -0.25, 1.16), (sx * 1.3, -0.28, 1.02)], .012, 4, "white")
    # red lanyard + ID badge
    ly = Part("lanyard", rigid("chest"))
    tube(ly, [(0.12, -0.15, 1.27), (0.07, -0.29, 1.12), (0.0, -0.33, 1.0)], (.02, .006), 4, "red")
    tube(ly, [(-0.12, -0.15, 1.27), (-0.07, -0.29, 1.12), (0.0, -0.33, 1.0)], (.02, .006), 4, "red")
    box(ly, (0, -0.335, 0.92), (.12, .015, .15), "white", 0.005)
    box(ly, (0, -0.344, 0.96), (.12, .01, .04), "c2", 0)
    box(ly, (0, -0.344, 0.89), (.05, .008, .05), "bone_shade", 0)
    # baggy sleeves that swallow the hands
    for side, s in LEGS:
        bony_arm(side, s, 0.026, "fist", 0.85)
        p = Part("sleeve_up_" + side, sleeve_up_w(side, s))
        loft(p, [ring((x * s, 0, ZC - 0.01), (0, -1, 0), (0, 0, 1), r, r * 1.05, 8, math.pi / 8) for x, r in ((0.15, .13), (0.32, .13), (0.465, .125))], "c1")
        q = Part("sleeve_lo_" + side, rigid("lowerarm." + side))
        r0 = ring((0.43 * s, 0, ZC - 0.01), (0, -1, 0), (0, 0, 1), .125, .13, 8, math.pi / 8)
        r1 = ring((0.66 * s, 0, ZC - 0.03), (0, -1, 0), (0, 0, 1), .12, .12, 8, math.pi / 8)
        g, faces, caps = loft(q, [r0, r1], "c1", cap0=True, cap1=False)
        c = Part("cuff_" + side, rigid("wrist." + side))
        r2 = ring((0.68 * s, 0, ZC - 0.03), (0, -1, 0), (0, 0, 1), .11, .11, 8, math.pi / 8)
        r3 = ring((0.80 * s, 0, ZC - 0.04), (0, -1, 0), (0, 0, 1), .085, .085, 8, math.pi / 8)
        g, faces, caps = loft(c, [r2, r3], "c1_dark", cap0=False, cap1=False)
        shell(c, faces, lambda co: Vector((co.x, co.y * .8, (ZC - 0.035) + (co.z - (ZC - 0.035)) * .8)), "c1_inner", "c1_dark")
    # coffee cup in the left hand
    cp = Part("coffee", rigid("hand.l"))
    cc = Vector((0.90, -0.10, 1.10))
    cc = Vector((0.90, -0.12, 1.12))
    cyl(cp, cc - Vector((0, 0, 0.11)), cc + Vector((0, 0, 0.10)), .06, .078, 8, "white")
    cyl(cp, cc - Vector((0, 0, 0.04)), cc + Vector((0, 0, 0.04)), .072, .078, 8, "coffee")
    cyl(cp, cc + Vector((0, 0, 0.10)), cc + Vector((0, 0, 0.13)), .084, .068, 8, "red_dark")
    # thin legs, big sneakers
    for side, s in LEGS:
        bony_leg(side, s, 0.034)
        boot(side, s, "sneaker", "boot", "boot_dark", "sole", sc=1.18)
    skirt([(0.42, .25, .22, 0), (0.30, .27, .24, 0)], [0, 0.01], "c2", "c1_inner", "c2_dark", n=12,
          wts=skirt_w(0.46, 0.34, 0.9, 0.0, 0.07), name="shorts")
    # skull sunk into the hoodie, huge nervous eyes, pen behind the ear
    skull((0, -0.08, 1.45), (.26, .24, .26), 0.88, eyes=dict(r=.075, socket=.092, sx=0.105, sh=1.1), mouth="wobble",
          eye_z=0.03, mouth_z=-0.13, mouth_w=0.08)
    pn = Part("pen", rigid("head"))
    cyl(pn, (0.25, -0.12, 1.58), (0.25, 0.10, 1.53), .014, .014, 5, "c2")
    cone(pn, (0.25, -0.12, 1.58), (0.25, -0.16, 1.59), .014, 5, "ink")

# ================================================================ main
def build(mid):
    rig, img, mats = start(mid, PAL[mid])
    BUILD[mid]()
    finish(mid, rig, img, mats)

if __name__ == "__main__":
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    for m in (args or list(BUILD)):
        build(m)
