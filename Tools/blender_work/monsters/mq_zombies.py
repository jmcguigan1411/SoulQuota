# Soul Quota - three original zombies on the KayKit "Rig" (Skeleton_Minion skeleton), mesh only.
# usage: python3 mq_zombies.py [Zombie|ZombieBloat|ZombieRunner|Wraith ...]
import bpy, math, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mathutils import Vector, Matrix
from mq_lib import *

ROT = lambda deg, ax: Matrix.Rotation(math.radians(deg), 3, ax)

PALETTES = {
    "Zombie": dict(skin=(0.56, 0.68, 0.47), skin_dark=(0.40, 0.52, 0.36), skin_light=(0.68, 0.78, 0.58),
                   shirt=(0.42, 0.56, 0.68), shirt_dark=(0.28, 0.38, 0.50), pants=(0.45, 0.34, 0.25), pants_dark=(0.30, 0.22, 0.17),
                   cavity=(0.16, 0.04, 0.06), bone=(0.95, 0.92, 0.80), bone_shade=(0.78, 0.74, 0.62), eye_white=(0.96, 0.95, 0.86),
                   pupil=(0.08, 0.07, 0.08), glow=(0.80, 1.00, 0.30), mouth=(0.14, 0.05, 0.07), tooth=(0.98, 0.94, 0.72),
                   hair=(0.20, 0.17, 0.14), stitch=(0.12, 0.10, 0.10), belt=(0.22, 0.16, 0.12), metal=(0.62, 0.62, 0.58),
                   flesh=(0.78, 0.33, 0.36), shoe=(0.30, 0.22, 0.18), tongue=(0.80, 0.40, 0.45), accent=(0.86, 0.30, 0.22),
                   accent_dark=(0.55, 0.18, 0.14), nail=(0.30, 0.26, 0.20)),
}
PALETTES["ZombieBloat"] = dict(PALETTES["Zombie"], skin=(0.68, 0.74, 0.44), skin_dark=(0.50, 0.58, 0.32), skin_light=(0.80, 0.84, 0.56),
                               shirt=(0.90, 0.86, 0.72), shirt_dark=(0.70, 0.64, 0.50), pants=(0.46, 0.36, 0.60), pants_dark=(0.32, 0.24, 0.44),
                               pustule=(0.95, 0.85, 0.30), glow=(0.55, 1.00, 0.35), accent=(0.95, 0.55, 0.20), accent_dark=(0.62, 0.32, 0.10))
PALETTES["ZombieRunner"] = dict(PALETTES["Zombie"], skin=(0.55, 0.68, 0.60), skin_dark=(0.36, 0.48, 0.42), skin_light=(0.68, 0.78, 0.70),
                                shirt=(0.62, 0.24, 0.22), shirt_dark=(0.42, 0.14, 0.14), pants=(0.36, 0.38, 0.42), pants_dark=(0.24, 0.25, 0.29),
                                glow=(1.00, 0.30, 0.18), hair=(0.12, 0.12, 0.14), nail=(0.18, 0.16, 0.16))

# ================================================================ shared pieces
def arm(side, s, R, sleeve="shirt", sleeve_end=0.36, jag=(0, .04, 0, .03, .01, .05, 0, .03), claw=0.0, nails=True):
    """R: dict of radii; arm lies along +-X at z=1.107 (T-pose)."""
    zc = 1.107
    up, lo, wr, ha = "upperarm." + side, "lowerarm." + side, "wrist." + side, "hand." + side
    # skin arm, continuous tube shoulder -> wrist, smooth elbow
    p = Part("arm_" + side, segw(["chest", up, lo, wr], k=5, bias={"chest": 1.6}))
    xs = [0.20, 0.33, 0.454, 0.58, 0.70, 0.80]
    rs = R["arm"]
    tube(p, [(x * s, 0, zc) for x in xs], rs, 6, "skin")
    p.recolor(lambda c, n, cur: n.z < -0.55, "skin_dark")
    if sleeve:
        q = Part("sleeve_" + side, ablend(0, [(0.20, {"chest": .5, up: .5}), (0.30, {up: 1})], s))
        n = 8
        rr = R["sleeve"]
        r0 = ring((0.15 * s, 0, zc), (0, -1, 0), (0, 0, 1), rr, rr * 1.05, n)
        r1 = ring((sleeve_end * s, 0, zc - 0.005), (0, -1, 0), (0, 0, 1), rr * 1.04, rr * 1.06, n)
        r1 = [co + Vector((jag[k % len(jag)] * s, 0, 0)) for k, co in enumerate(r1)]
        g, faces, caps = loft(q, [r0, r1], sleeve, cap0=False, cap1=False)
        shell(q, faces, lambda co: Vector((co.x, co.y * .8, zc + (co.z - zc) * .8)), "shirt_dark", "shirt_dark")
    # hand: chunky palm + 3 fingers + thumb, fingers curl down a little
    h = Part("hand_" + side, rigid(ha))
    hs = R.get("hand", 1.0)
    px = 0.855
    box(h, (px * s, 0, zc - 0.01), (.16 * hs, .19 * hs, .10 * hs), "skin", 0.03)
    fl = R.get("finger", 0.11) + claw
    for k, y in enumerate((-0.06, 0.0, 0.06)):
        y *= hs
        L = fl * (0.9 if k == 2 else 1.0)
        a = Vector(((px + 0.06) * s, y, zc - 0.01))
        b = a + Vector((L * s, 0, -0.035 - claw * 0.6))
        tube(h, [a, (a + b) / 2 + Vector((0, 0, 0.012)), b], [.034 * hs, .03 * hs, .024 * hs], 5, "skin")
        if nails:
            cone(h, b + Vector((-0.012 * s, 0, 0.0)), b + Vector((0.04 * s + claw * 0.4 * s, 0, -0.03 - claw * 0.3)), 0.018 * hs, 4, "nail")
    a = Vector(((px - 0.01) * s, -0.10 * hs, zc - 0.005))
    tube(h, [a, a + Vector((0.05 * s, -0.05, -0.01)), a + Vector((0.09 * s, -0.07, -0.03))], [.035 * hs, .03 * hs, .025 * hs], 5, "skin")
    h.recolor(lambda c, n, cur: cur == "skin" and n.z < -0.6, "skin_dark")

def legs_bare(side, s, R, shoe=True, pants=True, cuff_z=0.27, toe_out=False):
    x = 0.171 * s
    ul, ll, ft, ts = "upperleg." + side, "lowerleg." + side, "foot." + side, "toes." + side
    if pants:
        p = Part("thigh_" + side, ablend(2, [(0.30, {ul: .65, ll: .35}), (0.38, {ul: 1})]))
        n = 8
        rt = R["thigh"]
        jag = [0, .05, .01, .035, 0, .06, .015, .03]
        r0 = hring(0.54, rt * 1.05, rt, n, cx=x)
        r1 = hring(0.40, rt, rt * 0.97, n, cx=x)
        r2 = hring(cuff_z, rt * 1.08, rt * 1.05, n, cx=x, cy=0.005)
        r2 = [co - Vector((0, 0, jag[k])) for k, co in enumerate(r2)]
        g, faces, caps = loft(p, [r0, r1, r2], "pants", cap0=True, cap1=False)
        shell(p, faces[n:], lambda co: Vector((x + (co.x - x) * .8, co.y * .8, co.z)), "pants_dark", "pants_dark")
    # shin (skin), knee blend
    p = Part("shin_" + side, segw([ul, ll], k=6))
    tube(p, [(x, -0.004, 0.36), (x, -0.008, 0.29), (x, 0.012, 0.13)], [R["shin"] * 1.1, R["shin"] * 1.15, R["shin"] * 0.9], 6, "skin")
    # foot
    if shoe:
        p = Part("foot_" + side, rigid(ft))
        box(p, (x, -0.0, 0.075), (.20, .22, .14), "shoe", 0.045)
        box(p, (x, -0.005, 0.016), (.21, .23, .032), "pants_dark", 0.01)
        q = Part("toe_" + side, rigid(ts))
        box(q, (x, -0.17, 0.065), (.20, .17, .12), "shoe", 0.05)
        box(q, (x, -0.175, 0.016), (.21, .18, .032), "pants_dark", 0.01)
        if toe_out:  # a big toe poking out of a hole
            ell(q, (x - 0.05 * s, -0.255, 0.05), (.035, .04, .03), 6, 4, "skin")
            ell(q, (x - 0.05 * s, -0.24, 0.07), (.042, .02, .028), 6, 3, "cavity")
    else:  # big bare foot
        p = Part("foot_" + side, rigid(ft))
        box(p, (x, -0.01, 0.065), (.19, .21, .12), "skin", 0.05)
        q = Part("toe_" + side, rigid(ts))
        box(q, (x, -0.15, 0.05), (.20, .14, .09), "skin", 0.035)
        for k, dx in enumerate((-0.06, 0.0, 0.06)):
            ell(q, (x + dx * s, -0.225, 0.04), (.03, .035, .03), 6, 4, "skin")
            ell(q, (x + dx * s, -0.255, 0.05), (.015, .01, .012), 4, 3, "nail")
        q.recolor(lambda c, n, cur: cur == "skin" and n.z < -0.6, "skin_dark")

TORSO_W = zblend([(0.58, {"hips": 1}), (0.70, {"hips": .5, "spine": .5}), (0.88, {"spine": 1}),
                  (1.04, {"chest": .7, "spine": .3}), (1.14, {"chest": 1})])

def neck(r=0.09, top=1.38, fwd=0.0):
    p = Part("neck", zblend([(1.20, {"chest": 1}), (1.30, {"chest": .4, "head": .6}), (1.36, {"head": 1})]))
    tube(p, [(0, 0.01, 1.16), (0, -fwd * 0.5, (1.16 + top) / 2), (0, -fwd, top)], [r, r * .95, r * .9], 6, "skin_dark")

# ================================================================ ZOMBIE (shambler)
def build_zombie():
    R = dict(arm=[.085, .08, .082, .084, .078, .07], sleeve=.12, thigh=.105, shin=.06, finger=.11, hand=1.28)
    # pants / hips
    p = Part("pants", rigid("hips"))
    loft(p, [hring(0.40, .23, .19, 10), hring(0.50, .265, .215, 10), hring(0.635, .27, .22, 10)], "pants")
    q = Part("belt", rigid("hips"))
    loft(q, [hring(0.60, .282, .232, 10), hring(0.655, .282, .232, 10)], "belt", cap0=False, cap1=False)
    box(q, (0.0, -0.236, 0.627), (.08, .03, .06), "metal", 0.01)
    # shirt torso, torn hem
    p = Part("shirt", TORSO_W)
    n = 12
    hem = [0.0, 0.07, 0.01, 0.05, 0.0, 0.08, 0.02, 0.04, 0.0, 0.09, 0.01, 0.05]
    spec = [(0.60, .262, .222), (0.76, .285, .235), (0.93, .29, .235), (1.06, .275, .225), (1.17, .225, .185), (1.25, .12, .11)]
    rings = [hring(z, rx, ry, n) for z, rx, ry in spec]
    rings[0] = [co - Vector((0, 0, hem[k])) for k, co in enumerate(rings[0])]
    g, faces, caps = loft(p, rings, "shirt", cap0=False, cap1=True)
    p.tag(faces[:n], "shirt_dark")
    # torn shirt hole on the back showing skin
    q = Part("backpatch", TORSO_W)
    ell(q, (-0.10, 0.225, 0.82), (.10, .03, .09), 6, 4, "skin_dark")
    ell(q, (0.12, 0.23, 1.0), (.07, .03, .06), 6, 4, "accent")  # sewn-on patch
    # exposed ribs, front left of the chest
    q = Part("ribs", TORSO_W)
    jagr = [1.0, 0.86, 0.97, 0.80, 0.93, 1.02, 0.84, 0.95, 0.78, 0.92, 1.0, 0.83, 0.96, 0.88]
    for sw_, sc, yy in (("flesh", 1.0, -0.222), ("cavity", 0.80, -0.236)):
        pts = []
        for k in range(14):
            a = 2 * math.pi * k / 14
            r = jagr[(k * 3 + (0 if sw_ == "flesh" else 5)) % 14] * sc
            pts.append(Vector((0.10 + 0.135 * r * math.cos(a), yy, 0.94 + 0.16 * r * math.sin(a))))
        slab(q, pts, (0, 0.05, 0), sw_)
    for z, w, bend in ((1.03, .09, 0.0), (0.95, .12, -0.02), (0.87, .10, 0.01)):
        tube(q, [(0.0, -0.262, z + 0.012), (0.08, -0.266, z + bend), (0.08 + w, -0.247, z - 0.03)], [.021, .023, .017], 5, "bone")
    tube(q, [(-0.005, -0.262, 1.07), (-0.005, -0.266, 0.83)], .02, 5, "bone")
    # a torn shirt flap hanging off the left hip
    slab(q, ((0.16, -0.205, 0.66), (0.26, -0.15, 0.66), (0.27, -0.16, 0.50), (0.20, -0.20, 0.54)), (0, 0.015, 0), "shirt_dark")
    # collar
    q = Part("collar", rigid("chest"))
    g, faces, caps = loft(q, [hring(1.20, .19, .16, 10), hring(1.27, .15, .14, 10, cy=0.01)], "shirt_dark", cap0=False, cap1=False)
    shell(q, faces, lambda co: Vector((co.x * .85, co.y * .85, co.z)), "shirt_dark", "shirt_dark")
    neck(0.085)
    for side, s in (("l", 1), ("r", -1)):
        arm(side, s, R, jag=(0, .05, 0, .03, .01, .06, 0, .02) if s > 0 else (0.02, 0, .06, 0, .03, 0, .05, .01))
        legs_bare(side, s, R, shoe=(s < 0), toe_out=True)
    # pants patch
    q = Part("kneepatch", rigid("upperleg.r"))
    box(q, (-0.171 - 0.0, -0.11, 0.40), (.09, .02, .09), "accent", 0.0, ROT(8, 'Y'))
    # ---- head
    hc = Vector((0, -0.03, 1.62))
    h = Part("head", rigid("head"))
    vs = sbox(h, hc, (.68, .62, .60), 12, 8, "skin", power=0.5, rot=ROT(-4, 'Y'), taper=lambda zn: (1.0 - 0.05 * zn, 1.0))
    for v in vs:  # lump on the top left for a lopsided skull
        if v.co.z > hc.z + 0.2 and v.co.x > 0.05:
            v.co.z += 0.035
    h.recolor(lambda c, n, cur: n.z < -0.6, "skin_dark")
    box(h, (0.0, -0.30, 1.735), (.54, .09, .075), "skin_dark", 0.025, ROT(-6, 'Y'))  # brow
    # left: big bulging eye with a tiny pupil looking off
    ell(h, (0.15, -0.30, 1.625), (.115, .085, .12), 8, 6, "eye_white")
    ell(h, (0.18, -0.375, 1.60), (.04, .02, .04), 6, 4, "pupil")
    # right: sunken socket with a glowing pinprick
    ell(h, (-0.14, -0.29, 1.635), (.09, .05, .08), 8, 5, "cavity")
    ell(h, (-0.13, -0.322, 1.63), (.03, .02, .03), 6, 4, "glow")
    box(h, (0.0, -0.345, 1.555), (.07, .06, .075), "skin_dark", 0.02)  # nose
    box(h, (-0.02, -0.316, 1.475), (.36, .06, .06), "mouth", 0.015)
    # underbite jaw with crooked teeth
    j = box(h, (0.0, -0.245, 1.405), (.46, .22, .12), "skin", 0.035, ROT(3, 'Y'))
    for x, ht, tilt in ((-0.14, .07, 10), (-0.02, .05, -6), (0.11, .085, -12)):
        box(h, (x, -0.33, 1.47 + ht * 0.3), (.045, .03, ht), "tooth", 0.008, ROT(tilt, 'Y'))
    # stitches on the right temple, ear on the left, ear hole on the right
    stitch_line(h, (-0.327, -0.16, 1.79), (-0.327, 0.10, 1.55), 4, "stitch", (-1, 0, 0))
    ell(h, (0.335, 0.0, 1.59), (.04, .075, .095), 6, 4, "skin_dark")
    ell(h, (-0.31, 0.02, 1.58), (.02, .04, .045), 6, 4, "cavity")
    # hair tufts
    for a, b, r in (((0.05, -0.02, 1.85), (0.08, -0.08, 2.02), .06), ((-0.10, 0.06, 1.85), (-0.20, 0.10, 1.98), .05),
                    ((0.15, 0.12, 1.84), (0.24, 0.20, 1.95), .045)):
        cone(h, a, b, r, 5, "hair")
    return "Minion"

# ================================================================ ZOMBIE BLOAT
def build_bloat():
    R = dict(arm=[.10, .095, .095, .095, .09, .08], sleeve=.13, thigh=.13, shin=.075, finger=.10, hand=1.12)
    p = Part("pants", rigid("hips"))
    loft(p, [hring(0.38, .25, .21, 10), hring(0.48, .29, .24, 10), hring(0.58, .30, .25, 10)], "pants")
    # suspenders over the belly
    belly_c = Vector((0, -0.10, 0.80))
    belly_r = Vector((.47, .44, .40))
    BELLY_W = zblend([(0.55, {"hips": 1}), (0.68, {"hips": .55, "spine": .45}), (0.85, {"spine": .85, "chest": .15}),
                      (1.02, {"chest": .6, "spine": .4}), (1.12, {"chest": 1})])
    b = Part("belly", BELLY_W)
    ell(b, belly_c, belly_r, 12, 8, "skin_light")
    b.recolor(lambda c, n, cur: n.z < -0.55, "skin")
    # chest / back mass over the belly (vest)
    p = Part("chest", TORSO_W)
    spec = [(0.62, .26, .20), (0.90, .32, .25), (1.06, .30, .24), (1.17, .25, .20), (1.26, .13, .12)]
    loft(p, [hring(z, rx, ry, 12, cy=0.04) for z, rx, ry in spec], "skin", cap0=True, cap1=True)
    # tiny tank top that can't cover the belly
    q = Part("tank", TORSO_W)
    g, faces, caps = loft(q, [hring(1.00, .43, .40, 14, cy=-0.04), hring(1.12, .30, .26, 14, cy=0.02), hring(1.22, .22, .19, 14, cy=0.02)],
                          "shirt", cap0=False, cap1=False)
    shell(q, faces, lambda co: Vector((co.x * .93, 0.02 + (co.y - 0.02) * .93, co.z)), "shirt_dark", "shirt_dark")
    ell(q, (0.12, -0.33, 1.09), (.07, .02, .05), 6, 4, "accent")  # stain
    # stitches: big zig zag scar across the belly, a patch, navel, pustules
    s = Part("belly_detail", BELLY_W)
    def on_belly(x, z, out=0.006):
        d = Vector(((x - belly_c.x) / belly_r.x, 0, (z - belly_c.z) / belly_r.z))
        k = 1 - d.length_squared
        y = belly_c.y - belly_r.y * math.sqrt(max(0.0, k))
        nrm = Vector(((x - belly_c.x) / belly_r.x ** 2, (y - belly_c.y) / belly_r.y ** 2, (z - belly_c.z) / belly_r.z ** 2)).normalized()
        return Vector((x, y, z)) + nrm * out, nrm
    pts = [(-0.30, 0.95), (-0.12, 0.82), (0.05, 0.86), (0.25, 0.70)]
    for (x0, z0), (x1, z1) in zip(pts, pts[1:]):
        a, na = on_belly(x0, z0)
        bb, nb = on_belly(x1, z1)
        stitch_line(s, a, bb, 3, "stitch", (na + nb) / 2, w=0.016, cross=0.07)
    pc, pn = on_belly(0.20, 0.92, 0.004)
    box(s, pc, (.14, .13, .02), "accent", 0.0, frame(Vector((0, 0, 1)) - pn * pn.z, pn).normalized() @ ROT(10, 'Y'))
    nc, nn = on_belly(0.0, 0.66, -0.01)
    ell(s, nc, (.035, .03, .04), 6, 4, "cavity")
    for x, z, r in ((-0.22, 0.66, .045), (-0.28, 0.76, .03), (0.30, 0.86, .035), (0.12, 0.55, .03)):
        c, nrm = on_belly(x, z, 0.0)
        ell(s, c, (r, r, r), 6, 4, "pustule")
    # suspenders
    for sx in (1, -1):
        a, na = on_belly(0.16 * sx, 0.50, 0.01)
        tube(s, [(0.16 * sx, -0.20, 1.18), (0.20 * sx, -0.36, 1.00), on_belly(0.21 * sx, 0.82, 0.015)[0], a], .022, 4, "accent_dark")
    neck(0.11)
    for side, sd in (("l", 1), ("r", -1)):
        arm(side, sd, R, sleeve=None)
        legs_bare(side, sd, R, shoe=False, cuff_z=0.30)
        # shoulder fat
        q = Part("shoulder_" + side, ablend(0, [(0.22, {"chest": .6, "upperarm." + side: .4}), (0.34, {"upperarm." + side: 1})], sd))
        ell(q, (0.27 * sd, 0.0, 1.11), (.13, .14, .14), 8, 5, "skin")
    # ---- head: round, bald, double chin, tiny eyes, tongue out
    hc = Vector((0, -0.05, 1.58))
    h = Part("head", rigid("head"))
    ell(h, hc, (.30, .28, .27), 12, 8, "skin")
    ell(h, (0, -0.16, 1.40), (.25, .18, .11), 10, 5, "skin_light")  # double chin
    h.recolor(lambda c, n, cur: cur == "skin" and n.z < -0.6, "skin_dark")
    for sx in (1, -1):
        ell(h, (0.11 * sx, -0.30, 1.63), (.055, .03, .05), 6, 4, "eye_white")
        ell(h, (0.11 * sx + 0.01, -0.327, 1.625), (.022, .01, .022), 5, 3, "glow")
        box(h, (0.11 * sx, -0.30, 1.70), (.12, .04, .03), "skin_dark", 0.01, ROT(-14 * sx, 'Y'))  # brows
        ell(h, (0.30 * sx, -0.02, 1.58), (.04, .06, .07), 6, 4, "skin_dark")  # ears
    ell(h, (0, -0.33, 1.56), (.05, .04, .04), 6, 4, "skin_dark")  # nose
    ell(h, (0.0, -0.305, 1.485), (.15, .04, .045), 8, 4, "mouth")
    box(h, (0.05, -0.345, 1.45), (.08, .06, .09), "tongue", 0.025, ROT(-10, 'Y'))
    box(h, (-0.07, -0.33, 1.51), (.04, .03, .04), "tooth", 0.006)
    for a, b in (((0, 0.05, 1.84), (0.04, 0.0, 1.93)), ((0.03, 0.05, 1.84), (0.10, 0.06, 1.92))):
        tube(h, [a, b], .008, 3, "hair")
    stitch_line(h, (-0.15, -0.18, 1.80), (0.12, -0.22, 1.79), 4, "stitch", (0, -0.3, 1))
    return "Minion"

# ================================================================ ZOMBIE RUNNER
def build_runner():
    R = dict(arm=[.05, .046, .05, .05, .045, .04], sleeve=.075, thigh=.07, shin=.042, finger=.15, hand=0.95)
    p = Part("shorts", rigid("hips"))
    loft(p, [hring(0.42, .19, .16, 10), hring(0.52, .22, .18, 10), hring(0.62, .215, .175, 10)], "pants")
    # thin torso with visible ribs, ragged hoodie on top
    p = Part("torso", TORSO_W)
    spec = [(0.58, .17, .14), (0.72, .18, .14), (0.90, .22, .16), (1.06, .25, .19), (1.17, .21, .17), (1.25, .11, .10)]
    loft(p, [hring(z, rx, ry, 10) for z, rx, ry in spec], "skin", cap0=True, cap1=True)
    for z in (0.80, 0.86, 0.92):
        tube(p, [(-0.15, -0.115, z), (0, -0.16, z + 0.03), (0.15, -0.115, z)], .014, 4, "skin_dark")
    # hunch: a hump on the upper back + a forward-thrust neck
    q = Part("hump", zblend([(0.95, {"spine": .5, "chest": .5}), (1.1, {"chest": 1})]))
    ell(q, (0, 0.10, 1.08), (.22, .16, .17), 10, 6, "shirt")
    for z in (0.98, 1.08, 1.18):
        ell(q, (0, 0.245 - abs(z - 1.08) * 0.4, z), (.035, .03, .035), 5, 3, "bone")  # spine knobs
    # ragged hoodie vest
    q = Part("hoodie", TORSO_W)
    n = 12
    hem = [0.0, 0.08, 0.02, 0.10, 0.0, 0.06, 0.03, 0.11, 0.0, 0.07, 0.01, 0.09]
    rings = [hring(0.78, .215, .175, n), hring(0.95, .245, .19, n), hring(1.10, .265, .22, n), hring(1.21, .21, .18, n)]
    rings[0] = [co - Vector((0, 0, hem[k])) for k, co in enumerate(rings[0])]
    g, faces, caps = loft(q, rings, "shirt", cap0=False, cap1=False)
    # open the front: delete front column faces (centred on vertex 0 = front)
    for f in list(faces):
        cx = f.calc_center_median()
        if cx.y < 0 and abs(cx.x) < 0.07:
            q.bm.faces.remove(f)
            faces.remove(f)
    shell(q, faces, lambda co: Vector((co.x * .9, co.y * .9, co.z)), "shirt_dark", "shirt_dark")
    # hood bunched behind the neck
    ell(q, (0, 0.13, 1.26), (.18, .12, .09), 8, 5, "shirt_dark")
    neck(0.06, top=1.40, fwd=0.10)
    for side, s in (("l", 1), ("r", -1)):
        arm(side, s, R, sleeve="shirt", sleeve_end=0.40, claw=0.06)
        legs_bare(side, s, R, shoe=False, pants=False)
        # bony elbow spike and knee
        q = Part("elbow_" + side, rigid("lowerarm." + side))
        cone(q, (0.46 * s, 0.03, 1.107), (0.48 * s, 0.11, 1.10), .03, 4, "bone")
        q = Part("thighs_" + side, ablend(2, [(0.32, {"upperleg." + side: .6, "lowerleg." + side: .4}), (0.40, {"upperleg." + side: 1})]))
        tube(q, [(0.171 * s, 0, 0.50), (0.171 * s, -0.005, 0.30)], [.075, .06], 6, "skin")
        ell(q, (0.171 * s, -0.04, 0.28), (.06, .05, .055), 6, 4, "skin_dark")
        # torn shorts cuffs
        c = Part("cuff_" + side, rigid("upperleg." + side))
        jag = [0, .05, .01, .04, 0, .06, .02, .03]
        r0 = hring(0.47, .10, .095, 8, cx=0.171 * s)
        r1 = [co - Vector((0, 0, jag[k])) for k, co in enumerate(hring(0.40, .105, .10, 8, cx=0.171 * s))]
        g, faces, caps = loft(c, [r0, r1], "pants", cap0=False, cap1=False)
        shell(c, faces, lambda co, s=s: Vector((0.171 * s + (co.x - 0.171 * s) * .8, co.y * .8, co.z)), "pants_dark", "pants_dark")
    # ---- head: pushed forward and low (reads hunched even in upright anims), long jaw, wide open mouth
    hc = Vector((0, -0.14, 1.56))
    h = Part("head", rigid("head"))
    vs = sbox(h, hc, (.54, .52, .50), 12, 9, "skin", power=0.68, rot=ROT(8, 'X'), taper=lambda zn: (1.0 + 0.08 * zn, 1.0))
    h.recolor(lambda c, n, cur: n.z < -0.6, "skin_dark")
    box(h, (0, -0.37, 1.66), (.44, .08, .07), "skin_dark", 0.02, ROT(8, 'X'))  # heavy brow
    for sx in (1, -1):
        ell(h, (0.11 * sx, -0.36, 1.60), (.095, .04, .08), 8, 5, "skin_dark")
        ell(h, (0.11 * sx, -0.378, 1.60), (.07, .04, .055), 8, 5, "cavity")
        ell(h, (0.11 * sx, -0.405, 1.60), (.035, .02, .03), 6, 4, "glow")
        ell(h, (0.255 * sx, -0.10, 1.58), (.03, .06, .09), 5, 4, "skin_dark")  # pointy ears
        cone(h, (0.26 * sx, -0.08, 1.63), (0.36 * sx, -0.02, 1.75), .04, 4, "skin_dark")
    # gaping mouth: dark mouth block, top teeth, separate lower jaw hanging open
    box(h, (0, -0.37, 1.45), (.34, .10, .14), "mouth", 0.03)
    box(h, (0, -0.30, 1.32), (.36, .20, .08), "skin", 0.03, ROT(16, 'X'))
    for x in (-0.12, -0.06, 0.0, 0.06, 0.12):
        cone(h, (x, -0.41, 1.515), (x, -0.425, 1.45 - abs(x) * 0.2), .022, 4, "tooth")
    for x in (-0.10, 0.04, 0.12):
        cone(h, (x, -0.385, 1.37), (x, -0.40, 1.43), .02, 4, "tooth")
    box(h, (0.03, -0.36, 1.37), (.12, .08, .04), "tongue", 0.015)
    for k, (a, b) in enumerate((((0.18, -0.30, 1.75), (0.24, -0.20, 1.62)), ((0.20, -0.33, 1.72), (0.26, -0.22, 1.58)))):
        tube(h, [a, b], .012, 3, "flesh")
    for a, b in (((0.0, -0.05, 1.78), (0.03, 0.08, 1.88)), ((0.08, -0.08, 1.77), (0.14, 0.02, 1.86)), ((-0.08, -0.06, 1.77), (-0.16, 0.04, 1.85))):
        tube(h, [a, b], .01, 3, "hair")
    return "Minion"

# ================================================================ WRAITH (Mage skeleton)
PALETTES["Wraith"] = dict(robe=(0.36, 0.33, 0.48), robe_dark=(0.22, 0.20, 0.32), robe_inner=(0.05, 0.04, 0.08),
                          glow=(0.45, 1.00, 0.95), glow2=(0.62, 0.95, 0.90), bone=(0.90, 0.92, 0.88), bone_shade=(0.70, 0.74, 0.72),
                          metal=(0.55, 0.56, 0.62), accent=(0.62, 0.86, 0.80), accent_dark=(0.28, 0.42, 0.44),
                          cavity=(0.03, 0.02, 0.05), stitch=(0.12, 0.10, 0.16), shirt=(0.44, 0.40, 0.56), shirt_dark=(0.30, 0.27, 0.40),
                          skin=(0.74, 0.80, 0.82), skin_dark=(0.55, 0.60, 0.64), nail=(0.20, 0.22, 0.26))

def build_wraith():
    zc = 1.107
    # robe body: chest -> hips, flaring into a tattered tail. Main robe is rigid to the spine chain;
    # only the hanging tatters (below z 0.38) take a little of each upper leg, so walk cycles flutter them.
    def tail_w(co):
        base = TORSO_W(co) if co.z > 0.60 else {"hips": 1.0}
        t = max(0.0, min(1.0, (0.40 - co.z) / 0.30))
        if t <= 0:
            return base
        side = "upperleg.l" if co.x > 0 else "upperleg.r"
        side_k = min(1.0, abs(co.x) / 0.10)
        w = {k: v * (1 - 0.45 * t * side_k) for k, v in base.items()}
        w[side] = w.get(side, 0) + 0.45 * t * side_k
        return w
    p = Part("robe", tail_w)
    n = 16
    spec = [(1.27, .14, .13), (1.20, .25, .21), (1.05, .30, .25), (0.80, .33, .28), (0.58, .36, .31), (0.42, .40, .35), (0.30, .42, .37)]
    rings = [hring(z, rx, ry, n) for z, rx, ry in spec]
    # tattered hem: long and short strips
    drop = [0.18, 0.06, 0.22, 0.04, 0.16, 0.08, 0.24, 0.03, 0.20, 0.05, 0.17, 0.07, 0.23, 0.04, 0.15, 0.09]
    tip = [Vector((co.x * 0.92, co.y * 0.92, co.z - drop[k])) for k, co in enumerate(rings[-1])]
    rings.append(tip)
    g, faces, caps = loft(p, rings, "robe", cap0=True, cap1=False)
    # cut V notches between strips: pull alternate bottom verts up to make points
    for k, v in enumerate(g[-1]):
        if drop[k] < 0.1:
            v.co.z += 0.05
    hem_faces = faces[-n:]
    p.tag(faces[-2 * n:-n], "robe_dark")
    shell(p, faces[n * 3:], lambda co: Vector((co.x * .9, co.y * .9, co.z + 0.01)), "robe_inner", "robe_dark")
    p.recolor(lambda c, nn, cur: cur == "robe_dark" and c.z < 0.2, "glow2")  # ghost-light tips
    # ectoplasm wisps hanging below the hem (the "legs" of the ghost)
    q = Part("wisps", tail_w)
    for k in range(0, n, 2):
        co = tip[k]
        a = Vector((co.x * .9, co.y * .9, co.z + 0.09))
        cone(q, a, a + Vector((co.x * 0.15, co.y * 0.15, -0.09 - drop[k] * 0.15)), 0.045, 4, "glow2")
    # mantle / shawl with ragged edge over the shoulders
    q = Part("mantle", zblend([(1.0, {"chest": 1}), (1.3, {"chest": 1})]))
    hem = [0.0, 0.07, 0.02, 0.09, 0.0, 0.06, 0.02, 0.08, 0.0, 0.07, 0.01, 0.09, 0.0, 0.05, 0.02, 0.08]
    r0 = hring(1.33, .18, .17, 16)
    r1 = hring(1.21, .36, .30, 16)
    r2 = [co - Vector((0, 0, hem[k])) for k, co in enumerate(hring(1.04, .42, .35, 16))]
    g, faces, caps = loft(q, [r0, r1, r2], "robe_dark", cap0=False, cap1=False)
    shell(q, faces, lambda co: Vector((co.x * .9, co.y * .9, co.z - 0.015)), "robe_inner", "robe_dark")
    # rope belt with a hanging chain link tag
    q = Part("sash", rigid("spine"))
    loft(q, [hring(0.76, .345, .295, 16), hring(0.82, .34, .29, 16)], "accent_dark", cap0=False, cap1=False)
    for k in range(3):
        box(q, (0.16, -0.30 - 0.0, 0.72 - k * 0.07), (.03, .02, .06), "metal", 0.008, ROT(30 * (k % 2), 'Z'))
    # arms: big bell sleeves + ghostly bony hands
    for side, s in (("l", 1), ("r", -1)):
        up, lo, ha = "upperarm." + side, "lowerarm." + side, "hand." + side
        a = Part("sleeve_up_" + side, ablend(0, [(0.20, {"chest": .5, up: .5}), (0.30, {up: 1})], s))
        loft(a, [ring((x * s, 0, zc), (0, -1, 0), (0, 0, 1), r, r, 8) for x, r in ((0.16, .11), (0.30, .115), (0.46, .11))], "robe")
        b = Part("sleeve_lo_" + side, rigid(lo))
        jag = [0.0, 0.07, 0.0, 0.05, 0.0, 0.08, 0.0, 0.04, 0.0, 0.06]
        r0 = ring((0.43 * s, 0, zc), (0, -1, 0), (0, 0, 1), .11, .11, 10)
        r1 = ring((0.58 * s, 0, zc - 0.02), (0, -1, 0), (0, 0, 1), .15, .15, 10)
        r2 = ring((0.70 * s, 0, zc - 0.05), (0, -1, 0), (0, 0, 1), .19, .19, 10)
        r2 = [co + Vector((jag[k] * s, 0, -jag[k] * 0.5)) for k, co in enumerate(r2)]
        g, faces, caps = loft(b, [r0, r1, r2], "robe", cap0=True, cap1=False)
        shell(b, faces[:-1] if False else faces, lambda co, s=s: Vector((co.x, co.y * .8, zc + (co.z - zc) * .8)), "robe_inner", "robe_dark")
        w = Part("wrist_" + side, segw([lo, "wrist." + side, ha], k=6))
        tube(w, [(0.62 * s, 0, zc), (0.80 * s, 0, zc)], .035, 5, "glow2")
        h = Part("hand_" + side, rigid(ha))
        box(h, (0.84 * s, 0, zc), (.10, .12, .06), "glow2", 0.02)
        for k, y in enumerate((-0.045, 0.0, 0.045)):
            a0 = Vector((0.88 * s, y, zc))
            tube(h, [a0, a0 + Vector((0.09 * s, y * 0.3, -0.02)), a0 + Vector((0.17 * s, y * 0.5, -0.08))], [.02, .017, .012], 4, "glow2")
        tube(h, [(0.84 * s, -0.06, zc), (0.89 * s, -0.12, zc - 0.03), (0.93 * s, -0.15, zc - 0.07)], [.02, .016, .012], 4, "glow2")
    # ---- head: tall hood with a floppy point, void face, two glowing slit eyes
    hc = Vector((0, 0.0, 1.62))
    h = Part("hood", rigid("head"))
    n = 14
    lats = [(-60, 1.0), (-35, 1.0), (-8, 1.0), (20, 0.98), (45, 0.86), (65, 0.62)]
    grid = []
    for la, k in lats:
        phi = math.radians(la)
        r = []
        for i in range(n):
            th = 2 * math.pi * i / n
            x = 0.34 * k * math.cos(phi) * math.sin(th)
            y = -0.33 * k * math.cos(phi) * math.cos(th)
            if y > 0:
                y *= 1.15
            r.append(Vector((x, y, 0.36 * math.sin(phi))) + hc)
        grid.append(r)
    # pointed tip curving back
    top = grid[-1]
    for off, sc in ((Vector((0, 0.08, 0.10)), 0.55), (Vector((0, 0.22, 0.17)), 0.32), (Vector((0, 0.36, 0.15)), 0.2)):
        c0 = sum(top, Vector()) / n
        grid.append([c0 + (v - c0) * sc + off for v in top])
    vgrid = [[h.v(co) for co in r] for r in grid]
    faces = []
    for i in range(len(vgrid) - 1):
        for k in range(n):
            thc = 2 * math.pi * (k + 0.5) / n
            ang = math.degrees(min(thc, 2 * math.pi - thc))
            if ang < 52 and 1 <= i <= 2:
                continue  # face opening
            faces.append(h.face([vgrid[i][k], vgrid[i][(k + 1) % n], vgrid[i + 1][(k + 1) % n], vgrid[i + 1][k]], "robe"))
    tipv = h.v(sum(grid[-1], Vector()) / n + Vector((0, 0.09, -0.03)))
    for k in range(n):
        faces.append(h.face([vgrid[-1][k], vgrid[-1][(k + 1) % n], tipv], "robe"))
    main = [f for f in faces if all(v.co.z < hc.z + 0.36 for v in f.verts)]
    newf = shell(h, main, lambda co: hc + (co - hc) * 0.86, "robe_inner", "robe_dark")
    # void face + eyes
    ell(h, hc + Vector((0, -0.04, -0.02)), (.27, .25, .27), 10, 7, "robe_inner")
    for sx in (1, -1):
        e = ell(h, hc + Vector((0.10 * sx, -0.285, 0.02)), (.065, .02, .032), 6, 4, "glow", ROT(-14 * sx, 'Y'))
    ell(h, hc + Vector((0, -0.275, -0.12)), (.07, .015, .018), 6, 3, "glow2")  # faint wail mouth
    return "Mage"

BUILDERS = {"Zombie": build_zombie, "ZombieBloat": build_bloat, "ZombieRunner": build_runner, "Wraith": build_wraith}
OWNER = {"Zombie": "Minion", "ZombieBloat": "Minion", "ZombieRunner": "Minion", "Wraith": "Mage"}

def build(model):
    rig = load_kaykit_rig(OWNER[model])
    img = make_palette(model, PALETTES[model], CH + model.lower() + "_palette.png")
    mats = make_materials(model, img)
    BUILDERS[model]()
    body = assemble(model, rig, mats)
    print("MODEL", model, "TRIS", tri_count(body), "VERTS", len(body.data.vertices), "UNWEIGHTED", check_weights(body),
          "GROUPS", sorted(g.name for g in body.vertex_groups))
    img.filepath = "//" + model.lower() + "_palette.png"
    bpy.ops.wm.save_as_mainfile(filepath=CH + model + ".blend", relative_remap=True)
    export_fbx(CH + model + ".fbx", [rig, body], anims=False)
    print("EXPORTED", CH + model + ".fbx")

if __name__ == "__main__":
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    for m in (args or list(BUILDERS)):
        build(m)
