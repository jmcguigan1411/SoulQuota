# Soul Quota: "Midnight Fairground" signature set pieces (original work, Nov 16 2026 map quality pass).
# Same pipeline as sets/set_build.py (cm, Z up, +X = front, UCX_ hulls), own palette
# (ArtSource/Props/fair_palette.png -> T_Fair / MI_Fair). usage: python3 fair_build.py
import sys, os, math, json
sys.path.insert(0, "/home/claude/blender_work/sets")
sys.path.insert(0, "/home/claude/blender_work/park")
import bpy, bmesh
from mathutils import Vector
import set_build as sb
pb = sb.pb

OUT = "/home/claude/sq/ArtSource/Props/"
PAL = OUT + "fair_palette.png"
COLOURS = {
    "stone": (136, 130, 158), "stone_dark": (80, 74, 104), "stone_light": (186, 180, 204), "iron": (44, 40, 60),
    "wood": (112, 76, 70), "wood_dark": (68, 44, 50), "wood_light": (158, 116, 92), "plum": (112, 52, 128),
    "plum_dark": (70, 32, 92), "cream": (232, 220, 198), "cream_dark": (176, 160, 150), "glow": (255, 176, 84),
    "glow_hot": (255, 226, 160), "rope": (170, 146, 112), "bone": (226, 216, 196), "canvas": (196, 176, 168),
    "canvas_dark": (132, 112, 122), "crimson": (176, 44, 70), "night": (24, 18, 42), "brass": (188, 140, 66),
    "brass_dark": (124, 88, 52), "copper": (184, 104, 70), "copper_dark": (116, 62, 54), "hull": (96, 70, 78),
    "hull_dark": (58, 40, 54), "barnacle": (120, 132, 128), "net": (150, 134, 110), "slate": (74, 80, 112),
    "slate_dark": (46, 48, 80), "magenta": (204, 72, 150), "indigo": (72, 70, 160), "patina": (98, 136, 132),
    "face": (236, 228, 206), "glow_cool": (196, 176, 255), "bark": (70, 54, 70), "bark_dark": (42, 32, 48),
    "moss": (40, 78, 72), "red_stripe": (168, 54, 62),
}
LAYOUT = list(COLOURS.keys())
pb.LAYOUT[:] = LAYOUT
pb.COLOURS.clear(); pb.COLOURS.update(COLOURS)
pb.SW.clear(); pb.SW.update({n: i for i, n in enumerate(LAYOUT)})
pb.PAL_PATH = PAL
Piece, box, frustum, ico, tube, F, V = pb.Piece, pb.box, pb.frustum, pb.ico, pb.tube, pb.F, pb.V
cyl, disc_x, gable, pyramid, hbox, gear = sb.cyl, sb.disc_x, sb.gable, sb.pyramid, sb.hbox, sb.gear
TAU = 2 * math.pi


def lantern(P, x, y, ztop, drop=40, s=1.0, sw="glow"):
    """a hanging lantern: string from ztop down `drop`, iron cap, glowing body"""
    if drop > 0:
        box(P, (x, y, ztop - drop / 2), (3, 3, drop), "iron")
    z = ztop - drop
    box(P, (x, y, z - 5 * s), (30 * s, 30 * s, 8 * s), "iron")
    frustum(P, (x - 14 * s, x + 14 * s, y - 14 * s, y + 14 * s), (x - 10 * s, x + 10 * s, y - 10 * s, y + 10 * s), z - 46 * s, z - 9 * s, sw, sw, hull=False)
    box(P, (x, y, z - 49 * s), (22 * s, 22 * s, 6 * s), "iron")


def pennants(P, a, b, sag, n, cols, size=46, axis="y"):
    """string a->b with sag, n triangular pennants hanging; returns sample points"""
    pts = []
    for k in range(n * 2 + 1):
        t = k / (n * 2)
        p = Vector(a).lerp(Vector(b), t)
        p.z -= sag * 4 * t * (1 - t)
        pts.append(p)
    tube(P, [tuple(p) for p in pts], 2.2, 4, "rope", cap=False)
    for k in range(n):
        p0, p1 = pts[k * 2], pts[k * 2 + 2]
        mid = (p0 + p1) / 2
        bm = P.part(closed=False)
        tip = mid + Vector((0, 0, -size * 1.5))
        for flip in (False, True):
            vs = [V(bm, p0 + (p1 - p0) * 0.12), V(bm, p1 - (p1 - p0) * 0.12), V(bm, tip)]
            F(bm, vs[::-1] if flip else vs, cols[k % len(cols)])
    return pts


def F2(bm, cos, sw, sw_back=None):
    """two-sided face from coordinates (own verts each side)"""
    F(bm, [V(bm, c) for c in cos], sw)
    F(bm, [V(bm, c) for c in cos][::-1], sw_back or sw)


def stripes_drum(P, c, r, h, n, cols, r_top=None):
    r_top = r if r_top is None else r_top
    bm = P.part(closed=False)
    for k in range(n):
        a0, a1 = TAU * k / n, TAU * (k + 1) / n
        v = [V(bm, (c[0] + r * math.cos(a0), c[1] + r * math.sin(a0), c[2])), V(bm, (c[0] + r * math.cos(a1), c[1] + r * math.sin(a1), c[2])),
             V(bm, (c[0] + r_top * math.cos(a1), c[1] + r_top * math.sin(a1), c[2] + h)), V(bm, (c[0] + r_top * math.cos(a0), c[1] + r_top * math.sin(a0), c[2] + h))]
        F(bm, v, cols[k % len(cols)])


def stripes_cone(P, c, r, h, n, cols, r_top=6):
    stripes_drum(P, c, r, h, n, cols, r_top)


def ring(P, c, r, t, n, sw, axis="x"):
    pts = []
    for k in range(n + 1):
        a = TAU * k / n
        if axis == "x":
            pts.append((c[0], c[1] + r * math.cos(a), c[2] + r * math.sin(a)))
        else:
            pts.append((c[0] + r * math.cos(a), c[1], c[2] + r * math.sin(a)))
    tube(P, pts, t, 6, sw, cap=False)


def coffin(P, c, s=1.0, sw="wood", axis="x"):
    """a coffin gondola hanging below c (top centre), long axis vertical, lid facing +X"""
    w = [(-26, 0), (-40, -60), (-24, -190), (24, -190), (40, -60), (26, 0)]
    bm = P.part()
    fr = [V(bm, (c[0] + 22 * s, c[1] + y * s, c[2] + z * s)) for y, z in w]
    bk = [V(bm, (c[0] - 22 * s, c[1] + y * s, c[2] + z * s)) for y, z in w]
    F(bm, fr, "wood_light"); F(bm, bk[::-1], sw)
    for k in range(6):
        F(bm, [fr[k], bk[k], bk[(k + 1) % 6], fr[(k + 1) % 6]], "wood_dark")
    box(P, (c[0] + 24 * s, c[1], c[2] - 80 * s), (4, 8 * s, 90 * s), "bone")
    box(P, (c[0] + 24 * s, c[1], c[2] - 60 * s), (4, 40 * s, 8 * s), "bone")


# ------------------------------------------------------------------ shared
def podium():
    P = Piece("Fair_Podium")
    box(P, (0, 0, 60), (1240, 940, 120), "stone", "stone_light")
    box(P, (0, 0, 130), (1180, 880, 20), "stone_light")
    hbox(P, (0, 0, 70), (1240, 940, 140))
    for k in range(5):   # front steps (+X)
        box(P, (630 + k * 36, 0, 126 - k * 28 - 14), (36, 520, 28), "stone_light" if k % 2 == 0 else "stone")
    P.hull([(612, -260, 0), (612, 260, 0), (612, -260, 140), (612, 260, 140), (800, -260, 0), (800, 260, 0)])
    for sx, sy in ((1, -1), (1, 1)):
        x, y = sx * 590, sy * 330
        box(P, (x, y, 190), (70, 70, 100), "stone_dark")
        box(P, (x, y, 246), (86, 86, 12), "stone_light")
        lantern(P, x, y, 318, 0, 1.5)
    for sy in (-1, 1):   # side buttress blocks so the plinth reads as built
        for x in (-400, 0, 400):
            frustum(P, (x - 40, x + 40, sy * 470 - 44, sy * 470 + 44), (x - 40, x + 40, sy * 470 - 12, sy * 470 + 12), 0, 124, "stone_dark", "stone_dark", hull=False)
    return P


def bunting():
    P = Piece("Fair_Bunting")
    for s in (-1, 1):
        y = s * 470
        cyl(P, (0, y, 0), 30, 22, 40, 8, "stone_dark")
        cyl(P, (0, y, 40), 11, 8, 520, 6, "plum_dark", hull=False)
        for z in (150, 300, 450):
            cyl(P, (0, y, z), 13, 13, 22, 6, "cream", hull=False)
        ico(P, (0, y, 575), 20, "brass")
        P.hull([(x, y + dy, z) for x in (-16, 16) for dy in (-16, 16) for z in (0, 560)])
    pts = pennants(P, (0, -470, 548), (0, 470, 548), 110, 11, ["magenta", "cream", "indigo", "crimson"], 44)
    pennants(P, (0, -470, 470), (0, 470, 470), 60, 7, ["cream", "plum"], 30)
    for k in (5, 11, 17):
        lantern(P, 0, pts[k].y, pts[k].z, 46, 1.25)
    return P


def lantern_arch():
    P = Piece("Fair_LanternArch")
    for s in (-1, 1):
        y = s * 430
        box(P, (0, y, 22), (150, 150, 44), "stone_dark")
        box(P, (0, y, 200), (110, 110, 312), "stone")
        box(P, (0, y, 366), (136, 136, 22), "stone_light")
        pyramid(P, (0, y, 377), 120, 70, "slate")
        lantern(P, 62, y, 300, 0, 1.4)
        box(P, (40, y, 306), (50, 6, 6), "iron")
        P.hull([(x, y + dy, z) for x in (-75, 75) for dy in (-75, 75) for z in (0, 440)])
    n = 18
    arc = [(0, -430 * math.cos(math.pi * k / n), 400 + 300 * math.sin(math.pi * k / n)) for k in range(n + 1)]
    tube(P, arc, 9, 6, "iron")
    arc2 = [(0, -330 * math.cos(math.pi * k / n), 400 + 210 * math.sin(math.pi * k / n)) for k in range(n + 1)]
    tube(P, arc2, 6, 6, "iron")
    for k in range(1, n, 1):   # scroll bars between the two arcs
        tube(P, [arc[k], arc2[k]], 4, 4, "iron")
    # crest: a crescent moon and star finial (generic)
    ring(P, (0, 0, 760), 48, 7, 10, "brass")
    ico(P, (0, 22, 772), 40, "night")
    box(P, (0, 0, 708), (10, 10, 30), "iron")
    for k in (3, 6, 9, 12, 15):
        lantern(P, 0, arc2[k][1], arc2[k][2], 50 + (20 if k == 9 else 0), 1.3)
    pennants(P, (6, -400, 392), (6, 400, 392), 70, 9, ["magenta", "cream", "indigo"], 40)
    return P


def coffin_wheel():
    P = Piece("Fair_CoffinWheel")
    R, H = 640, 820
    box(P, (0, 0, 30), (420, 1100, 60), "stone_dark", "stone")
    hbox(P, (0, 0, 110), (420, 1100, 220))
    for sx in (-1, 1):   # A-frames
        for sy in (-1, 1):
            tube(P, [(sx * 120, sy * 420, 60), (sx * 60, 0, H)], [22, 14], 6, "plum_dark")
        tube(P, [(sx * 110, -300, 300), (sx * 110, 300, 300)], 10, 5, "iron")
    tube(P, [(-90, 0, H), (90, 0, H)], 30, 8, "brass")
    P.hull([(x, y, z) for x in (-70, 70) for y in (-70, 70) for z in (200, H + 40)])
    for sx in (-34, 34):
        ring(P, (sx, 0, H), R, 11, 24, "cream" if sx > 0 else "plum")
        ring(P, (sx, 0, H), R * 0.52, 7, 16, "iron")
        for k in range(8):
            a = TAU * k / 8 + 0.2
            tube(P, [(sx, 0, H), (sx, R * math.cos(a), H + R * math.sin(a))], 6, 4, "iron")
    for k in range(8):
        a = TAU * k / 8 + 0.2
        y, z = R * math.cos(a), H + R * math.sin(a)
        tube(P, [(-40, y, z), (40, y, z)], 7, 5, "iron")
        coffin(P, (0, y, z - 6), 0.9, "wood" if k % 2 else "plum_dark")
        if k % 2 == 0:
            lantern(P, 30, y, z - 180, 0, 0.9)
    disc_x(P, (60, 0, H), 90, 16, 10, "glow", "brass")
    for k in range(12):   # bulbs round the rim
        a = TAU * k / 12
        ico(P, (48, (R + 4) * math.cos(a), H + (R + 4) * math.sin(a)), 13, "glow_hot")
    return P


def tent_crypt():
    P = Piece("Fair_TentCrypt")
    r, h = 430, 300
    cyl(P, (0, 0, 0), r + 30, r + 30, 30, 12, "stone_dark", "stone")
    stripes_drum(P, (0, 0, 30), r, h, 12, ["plum", "cream"])
    stripes_drum(P, (0, 0, 30 + h), r + 34, 34, 12, ["cream", "plum"], r)   # scalloped eave band
    stripes_cone(P, (0, 0, 30 + h + 30), r + 30, 330, 12, ["plum", "cream"], 30)
    cyl(P, (0, 0, 30 + h + 360), 30, 16, 60, 8, "brass")
    box(P, (0, 0, 800), (6, 6, 120), "iron")
    pennants(P, (0, 0, 850), (0, 70, 830), 4, 1, ["magenta"], 34)
    P.hull([((r + 20) * math.cos(TAU * k / 12), (r + 20) * math.sin(TAU * k / 12), z) for k in range(12) for z in (0, 420)])
    # crypt portal at +X: stone surround, dark doorway, cool glow slit
    box(P, (r - 20, 0, 170), (110, 300, 300), "stone")
    box(P, (r + 40, 0, 150), (20, 170, 240), "night")
    box(P, (r + 52, 0, 150), (6, 26, 190), "glow_cool")
    gable(P, (r - 10, 0, 320), (130, 320), 90, "slate", "stone_light", 12)
    for s in (-1, 1):
        cyl(P, (r + 46, s * 130, 30), 24, 20, 270, 8, "stone_light")
        lantern(P, r + 90, s * 190, 300, 30, 1.3)
        box(P, (r + 66, s * 190, 300), (56, 6, 6), "iron")
    hbox(P, (r + 10, 0, 180), (130, 320, 360))
    for k in range(12):   # guy ropes with small pennants make it read as a tent from afar
        a = TAU * (k + 0.5) / 12
        if abs(math.sin(a)) < 0.3 and math.cos(a) > 0:
            continue
        tube(P, [((r + 30) * math.cos(a), (r + 30) * math.sin(a), 330), ((r + 150) * math.cos(a), (r + 150) * math.sin(a), 6)], 2.5, 4, "rope", cap=False)
    return P


def lantern_tree():
    P = Piece("Fair_LanternTree")
    trunk = [(0, 0, 0), (10, 6, 220), (-18, 14, 430), (12, -8, 640), (-6, 4, 820)]
    tube(P, trunk, [120, 96, 78, 60, 40], 8, "bark")
    for k in range(7):   # roots
        a = TAU * k / 7 + 0.3
        tube(P, [(60 * math.cos(a), 60 * math.sin(a), 90), (190 * math.cos(a), 190 * math.sin(a), 18), (270 * math.cos(a), 270 * math.sin(a), -6)], [46, 30, 12], 6, "bark_dark")
    P.hull([(105 * math.cos(TAU * k / 8), 105 * math.sin(TAU * k / 8), z) for k in range(8) for z in (0, 600)])
    box(P, (96, 0, 250), (30, 70, 110), "night")           # hollow
    box(P, (104, 0, 250), (10, 30, 70), "glow")
    limbs = [((-18, 14, 430), (-260, 180, 560), (-430, 240, 720), (-520, 200, 900)),
             ((12, -8, 640), (220, -180, 760), (400, -260, 860), (470, -380, 1020)),
             ((-6, 4, 820), (-120, -220, 960), (-170, -400, 1040), (-120, -520, 1180)),
             ((-6, 4, 820), (160, 140, 980), (300, 300, 1080), (340, 460, 1220)),
             ((10, 6, 300), (240, 120, 420), (420, 150, 520), (560, 90, 640)),
             ((-6, 4, 820), (-40, 30, 1010), (-150, 70, 1160), (-190, 20, 1320))]
    hang = []
    for L in limbs:
        tube(P, list(L), [44, 30, 18, 6], 6, "bark")
        hang += [L[2], L[3]]
        tw = Vector(L[2])
        tube(P, [tuple(tw), tuple(tw + Vector((60, -70, 90))), tuple(tw + Vector((70, -150, 200)))], [14, 9, 3], 5, "bark_dark")
    for k, p in enumerate(hang):
        lantern(P, p[0], p[1], p[2] - 10, 90 + 50 * (k % 3), 1.5, "glow" if k % 4 else "glow_cool")
    return P


def stall():
    P = Piece("Fair_Stall")
    box(P, (0, 0, 55), (230, 420, 110), "plum_dark", "wood_light")
    for k in range(6):
        box(P, (116, -175 + k * 70, 55), (4, 34, 110), "cream" if k % 2 == 0 else "crimson")
    for sx in (-100, 100):
        for sy in (-195, 195):
            box(P, (sx, sy, 180), (14, 14, 360), "wood_dark")
    for k in range(8):                                        # striped back cloth (reads from the lane side too)
        box(P, (-108, -175 + k * 50, 200), (8, 50, 290), "plum" if k % 2 == 0 else "cream_dark")
    box(P, (-114, 0, 352), (14, 430, 16), "wood_dark")
    pennants(P, (-118, -205, 340), (-118, 205, 340), 36, 7, ["magenta", "cream", "indigo"], 34)
    lantern(P, -128, 0, 250, 0, 1.2)
    box(P, (-120, 0, 256), (20, 6, 6), "iron")
    hbox(P, (0, 0, 180), (240, 430, 360))
    bm = P.part(closed=False)                                 # striped awning, sloping to the front
    for k in range(8):
        y0, y1 = -230 + k * 57.5, -230 + (k + 1) * 57.5
        sw = "cream" if k % 2 == 0 else "red_stripe"
        F2(bm, [(-130, y0, 400), (-130, y1, 400), (170, y1, 320), (170, y0, 320)], sw)
        F2(bm, [(170, y0, 320), (170, y1, 320), (172, (y0 + y1) / 2, 280)], sw)
    for y in (-150, 0, 150):
        lantern(P, 120, y, 322, 26, 1.1)
    return P


# ------------------------------------------------------------------ harbour
def wreck():
    P = Piece("Fair_Wreck")
    L, n = 1500, 9
    secs = []
    bm = P.part(closed=False)
    for i in range(n + 1):
        t = i / n
        x = -L / 2 + L * t
        w = 250 * math.sin(math.pi * min(1.0, t * 1.25 + 0.08)) ** 0.7 * (0.55 + 0.45 * (1 - t * 0.5))
        top = 330 + 170 * (1 - t) ** 2 + 120 * t ** 3
        prof = [(-w, top), (-w * 0.92, top * 0.45), (-w * 0.5, 40), (0, 0), (w * 0.5, 40), (w * 0.92, top * 0.45), (w, top)]
        secs.append([Vector((x, y, z)) for y, z in prof])
    for i in range(n):
        if i == 4:
            continue   # the break amidships
        for k in range(6):
            sw = ["hull", "hull_dark", "barnacle", "barnacle", "hull_dark", "hull"][k]
            F2(bm, [secs[i][k], secs[i][k + 1], secs[i + 1][k + 1], secs[i + 1][k]], sw, "hull_dark")
    for i in range(n + 1):   # ribs
        pts = [tuple(v) for v in secs[i]]
        tube(P, pts, 9, 4, "wood_dark", cap=False)
    for i in (1, 2, 3, 6, 7):   # deck
        x0, x1 = secs[i][0].x, secs[i + 1][0].x
        w0 = secs[i][6].y * 0.9
        z = min(secs[i][0].z, secs[i + 1][0].z) - 60
        box(P, ((x0 + x1) / 2, 0, z), (x1 - x0, w0 * 2, 12), "wood_light")
    box(P, (-L / 2 + 150, 0, 520), (300, 360, 16), "wood")     # stern castle roof
    box(P, (-L / 2 + 20, 0, 420), (20, 300, 180), "hull")
    for y in (-90, 0, 90):
        box(P, (-L / 2 + 8, y, 430), (8, 50, 60), "glow")
    mast = [(-80, 0, 250), (-150, 60, 700), (-260, 150, 1150)]
    tube(P, mast, [34, 26, 16], 8, "wood")
    tube(P, [(-180, -200, 830), (-230, 330, 930)], 12, 6, "wood_dark")
    bm2 = P.part(closed=False)                                 # torn sail
    sv = [(-176, -190, 820), (-226, 320, 920), (-250, 250, 640), (-215, 60, 560), (-200, -120, 620)]
    F2(bm2, sv, "canvas", "canvas_dark")
    cyl(P, (-222, 118, 1010), 60, 70, 60, 8, "wood_dark", hull=False)   # crow's nest
    lantern(P, -L / 2 - 30, 0, 600, 60, 2.0)
    tube(P, [(-L / 2 + 10, 0, 500), (-L / 2 - 30, 0, 600)], 8, 5, "iron")
    tube(P, [(L / 2 - 40, 0, 420), (L / 2 + 230, 0, 560)], [20, 8], 6, "wood")   # bowsprit
    hbox(P, (-L / 4 - 60, 0, 260), (L / 2 - 80, 440, 520))
    hbox(P, (L / 4 + 60, 0, 220), (L / 2 - 120, 380, 440))
    return P


def stilt_house():
    P = Piece("Fair_StiltHouse")
    for x in (-320, 0, 320):
        for y in (-220, 220):
            cyl(P, (x, y, 0), 26, 22, 280, 6, "hull_dark", hull=False)
            cyl(P, (x, y, 60), 30, 30, 26, 6, "barnacle", hull=False)
    for x in (-320, 320):
        tube(P, [(x, -220, 40), (x, 220, 250)], 8, 4, "wood_dark")
    box(P, (0, 0, 290), (820, 600, 24), "wood_light", "wood")
    box(P, (-40, 0, 470), (640, 430, 340), "hull")
    for k in range(9):
        box(P, (-40 + (k - 4) * 70, 216, 470), (8, 6, 340), "hull_dark")
        box(P, (-40 + (k - 4) * 70, -216, 470), (8, 6, 340), "hull_dark")
    gable(P, (-40, 0, 640), (640, 430), 190, "slate", "hull", 30)
    box(P, (282, 0, 420), (6, 110, 230), "wood_dark")
    for y in (-140, 140):
        box(P, (282, y, 500), (6, 70, 80), "glow")
    box(P, (-40, 218, 500), (90, 6, 80), "glow")
    tube(P, [(280, 0, 800), (470, 0, 800)], 12, 5, "wood_dark")       # loading beam
    box(P, (450, 0, 720), (4, 4, 150), "rope")
    ico(P, (450, 0, 600), 60, "net", (1, 1, 1.2))                       # hanging net sack
    lantern(P, 380, 150, 620, 50, 1.6)
    box(P, (330, 150, 624), (110, 6, 6), "iron")
    for k in range(8):                                                   # stair to the deck
        box(P, (420 + k * 30, -200, 280 - k * 34 - 6), (34, 150, 12), "wood")
    for x in (-380, 380):
        for k in range(3):
            box(P, (x, -200 + k * 200, 360), (10, 10, 120), "wood_dark")
        box(P, (x, 0, 420), (10, 590, 10), "wood")
    hbox(P, (0, 0, 420), (820, 600, 840))
    P.hull([(410, -280, 0), (410, -120, 0), (680, -280, 0), (680, -120, 0), (410, -280, 300), (410, -120, 300)])
    return P


def net_rack():
    P = Piece("Fair_NetRack")
    for s in (-1, 1):
        y = s * 470
        for dx in (-70, 70):
            tube(P, [(dx, y, 0), (0, y, 500)], [16, 10], 6, "hull_dark")
        cyl(P, (0, y, 0), 10, 8, 560, 6, "wood_dark", hull=False)
        P.hull([(x, y + dy, z) for x in (-40, 40) for dy in (-16, 16) for z in (0, 540)])
    tube(P, [(0, -470, 520), (0, 470, 520)], 9, 6, "wood")
    for k in range(13):   # diagonal net lattice
        y0 = -430 + k * 66
        for d in (-1, 1):
            tube(P, [(0, y0, 512), (0, max(-440, min(440, y0 + d * 190)), 190)], 2.2, 3, "net", cap=False)
    for z in (300, 410):
        pts = [(0, -430 + k * 86, z - 26 * math.sin(math.pi * k / 10)) for k in range(11)]
        tube(P, pts, 2.2, 3, "net", cap=False)
    for k, y in enumerate((-330, -110, 110, 330)):
        ico(P, (0, y, 168), 34, "glow" if k % 2 else "glow_cool")
        ring(P, (0, y, 168), 35, 3, 8, "rope", "y")
    pennants(P, (4, -470, 560), (4, 470, 560), 40, 8, ["crimson", "cream", "slate"], 34)
    return P


def cargo_pile():
    P = Piece("Fair_CargoPile")
    def crate(c, s, sw="wood"):
        box(P, c, s, sw, "wood_light")
        for dz in (-s[2] / 2 + 8, s[2] / 2 - 8):
            box(P, (c[0], c[1], c[2] + dz), (s[0] + 6, s[1] + 6, 12), "wood_dark")
    crate((0, -90, 75), (170, 170, 150)); crate((0, 95, 65), (160, 170, 130), "hull")
    crate((-150, 0, 80), (120, 300, 160), "hull"); crate((10, -60, 215), (150, 150, 130), "hull")
    crate((-120, 60, 215), (130, 140, 110)); crate((-20, 0, 335), (120, 120, 100))
    for y in (-140, 140):
        cyl(P, (130, y, 0), 46, 46, 120, 10, "wood", "wood_dark", hull=False)
        cyl(P, (130, y, 40), 49, 49, 10, 10, "iron", hull=False)
    bm = P.part(closed=False)                                 # tarp thrown over the top
    tv = [(-100, -80, 392), (70, -80, 392), (90, 80, 392), (-90, 80, 392), (-110, -100, 300), (95, -110, 280), (110, 100, 310), (-105, 105, 290)]
    v = [V(bm, p) for p in tv]
    F(bm, v[:4], "canvas")
    for a, b in ((0, 1), (1, 2), (2, 3), (3, 0)):
        F2(bm, [tv[a], tv[a + 4], tv[b + 4], tv[b]], "canvas_dark")
    for x in (-60, 40):
        tube(P, [(x, -180, 0), (x, -100, 300), (x, 0, 396), (x, 100, 300), (x, 180, 0)], 3, 4, "rope", cap=False)
    hbox(P, (-20, 0, 200), (400, 380, 400))
    return P


def boat():
    """a moored fishing boat (stands in the tide): hull, mast with a furled sail, stern lantern, mooring post"""
    P = Piece("Fair_Boat")
    L, n = 760, 8
    bm = P.part(closed=False)
    secs = []
    for i in range(n + 1):
        t = i / n
        x = -L / 2 + L * t
        w = 150 * math.sin(math.pi * min(1.0, t * 0.9 + 0.12)) ** 0.6
        top = 150 + 70 * (2 * t - 1) ** 2 + 40 * t ** 3
        secs.append([Vector((x, y, z)) for y, z in [(-w, top), (-w * 0.9, top * 0.4), (-w * 0.45, 20), (0, 0), (w * 0.45, 20), (w * 0.9, top * 0.4), (w, top)]])
    for i in range(n):
        for k in range(6):
            F2(bm, [secs[i][k], secs[i][k + 1], secs[i + 1][k + 1], secs[i + 1][k]], ["hull", "hull_dark", "barnacle", "barnacle", "hull_dark", "hull"][k], "hull_dark")
    for i in range(n):
        tube(P, [tuple(secs[i][0]), tuple(secs[i + 1][0])], 8, 4, "wood_light")
        tube(P, [tuple(secs[i][6]), tuple(secs[i + 1][6])], 8, 4, "wood_light")
    for i in (1, 2, 3, 4, 5, 6):
        x0, x1 = secs[i][0].x, secs[i + 1][0].x
        box(P, ((x0 + x1) / 2, 0, 110), (x1 - x0, min(secs[i][6].y, secs[i + 1][6].y) * 1.8, 10), "wood")
    box(P, (-L / 2 + 120, 0, 190), (170, 190, 150), "plum_dark")          # wheelhouse
    box(P, (-L / 2 + 120, 0, 272), (200, 220, 14), "slate")
    for y in (-96, 96):
        box(P, (-L / 2 + 130, y, 205), (70, 4, 50), "glow")
    tube(P, [(40, 0, 100), (40, 0, 760)], [16, 9], 6, "wood")
    tube(P, [(40, -190, 560), (40, 190, 600)], 9, 5, "wood_dark")
    tube(P, [(40, -180, 540), (40, -60, 520), (40, 60, 535), (40, 180, 575)], 26, 6, "canvas")   # furled sail
    pennants(P, (40, 0, 750), (L / 2 - 20, 0, 230), 30, 6, ["crimson", "cream", "slate"], 30)
    pennants(P, (40, 0, 750), (-L / 2 + 40, 0, 290), 30, 5, ["cream", "magenta"], 30)
    lantern(P, -L / 2 - 10, 0, 300, 30, 1.6)
    tube(P, [(-L / 2 + 20, 0, 260), (-L / 2 - 10, 0, 300)], 5, 4, "iron")
    lantern(P, 40, 60, 480, 40, 1.2, "glow_cool")
    cyl(P, (L / 2 + 120, 120, 0), 22, 18, 260, 6, "hull_dark")           # mooring post + rope
    tube(P, [(L / 2 - 30, 40, 200), (L / 2 + 60, 90, 120), (L / 2 + 120, 120, 200)], 3, 4, "rope", cap=False)
    hbox(P, (0, 0, 130), (L - 60, 260, 260))
    return P


# ------------------------------------------------------------------ clocktower
def clock_wall():
    P = Piece("Fair_ClockWall")
    W, H, T = 1200, 880, 130
    box(P, (0, 0, 30), (T + 60, W + 60, 60), "stone_dark")
    box(P, (0, 0, H / 2 + 30), (T, W, H), "stone")
    hbox(P, (0, 0, H / 2), (T + 40, W + 40, H + 60))
    for y in (-W / 2 + 60, W / 2 - 60):
        box(P, (0, y, H / 2 + 60), (T + 50, 130, H + 120), "stone_dark")
        pyramid(P, (0, y, H + 120), 190, 150, "slate")
    box(P, (0, 0, H + 50), (T + 30, W - 200, 40), "stone_light")
    gable(P, (0, 0, H + 70), (T + 20, W - 300), 190, "slate", "stone_light", 0)
    R = 340
    for sx in (1, -1):
        x = sx * (T / 2 + 4)
        disc_x(P, (x, 0, 500), R + 34, 14, 24, "brass", "brass_dark")
        disc_x(P, (x + sx * 6, 0, 500), R, 10, 24, "face", "brass_dark")
        for k in range(12):
            a = TAU * k / 12
            bm = box(P, (0, 0, 0), (8, 22 if k % 3 else 34, 70 if k % 3 else 100), "night")
            for v in bm.verts:
                y, z = v.co.y, v.co.z + R - 64
                v.co.x, v.co.y, v.co.z = v.co.x + x + sx * 14, y * math.cos(a) - z * math.sin(a), 500 + y * math.sin(a) + z * math.cos(a)
        for ang, ln, wd in ((0.9, 250, 30), (2.6, 170, 40)):
            bm = box(P, (0, 0, 0), (8, wd, ln), "iron")
            for v in bm.verts:
                y, z = v.co.y, v.co.z + ln / 2 - 20
                v.co.x, v.co.y, v.co.z = v.co.x + x + sx * 22, y * math.cos(ang) - z * math.sin(ang), 500 + y * math.sin(ang) + z * math.cos(ang)
        disc_x(P, (x + sx * 26, 0, 500), 30, 10, 8, "brass")
        for y in (-420, 420):
            gear(P, (x + sx * 6, y, 150), 110, 26, 9, "brass_dark", axis="x")
            box(P, (x + sx * 2, y, 760), (6, 70, 100), "glow")
    return P


def gear_bridge():
    P = Piece("Fair_GearBridge")
    for s in (-1, 1):
        y = s * 450
        box(P, (0, y, 22), (170, 170, 44), "stone_dark")
        box(P, (0, y, 210), (120, 120, 332), "stone")
        box(P, (0, y, 386), (150, 150, 22), "brass_dark")
        gear(P, (0, y, 430), 100, 60, 8, "brass", axis="x")
        P.hull([(x, y + dy, z) for x in (-85, 85) for dy in (-85, 85) for z in (0, 520)])
    n, R = 20, 450
    for k in range(n + 1):
        a = math.pi * k / n
        y, z = -R * math.cos(a), 430 + R * 0.8 * math.sin(a)
        ny, nz = -math.cos(a), 0.8 * math.sin(a)
        bm = box(P, (0, 0, 0), (70, 62, 62), "brass" if k % 2 == 0 else "brass_dark")
        for v in bm.verts:
            v.co = Vector((v.co.x, y + v.co.y, z + v.co.z))
        if k % 2 == 0:
            box(P, (0, y + ny * 52, z + nz * 52), (52, 40, 40), "brass_dark")
    arc = [(0, -R * 0.86 * math.cos(math.pi * k / n), 430 + R * 0.66 * math.sin(math.pi * k / n)) for k in range(n + 1)]
    tube(P, arc, 9, 6, "iron")
    gear(P, (10, 0, 430 + R * 0.8 + 70), 80, 40, 8, "copper", axis="x")
    lantern(P, 0, 0, 430 + R * 0.66 - 6, 70, 1.8)
    for k in (5, 15):
        lantern(P, 0, arc[k][1], arc[k][2] - 6, 44, 1.2)
    return P


def pendulum():
    P = Piece("Fair_Pendulum")
    box(P, (0, 0, 40), (360, 620, 80), "stone_dark", "stone")
    hbox(P, (0, 0, 60), (360, 620, 120))
    for s in (-1, 1):
        tube(P, [(0, s * 260, 80), (0, s * 90, 1050)], [30, 20], 6, "iron")
        tube(P, [(0, s * 200, 420), (0, -s * 130, 820)], 9, 4, "brass_dark")
    P.hull([(x, y, z) for x in (-30, 30) for y in (-110, 110) for z in (80, 1000)])
    box(P, (0, 0, 1060), (150, 260, 60), "stone_light")
    gear(P, (40, 0, 1060), 120, 34, 10, "brass", axis="x")
    pyramid(P, (0, 0, 1090), 200, 120, "slate")
    ang = 0.42
    top = Vector((30, 0, 1000))
    bot = top + Vector((0, math.sin(ang) * 760, -math.cos(ang) * 760))
    tube(P, [tuple(top), tuple(bot)], 9, 6, "brass_dark")
    disc_x(P, tuple(bot), 120, 40, 16, "brass", "brass_dark")
    disc_x(P, (bot.x + 22, bot.y, bot.z), 60, 8, 12, "glow")
    return P


def pipes():
    P = Piece("Fair_Pipes")
    box(P, (0, 0, 20), (300, 1000, 40), "stone_dark")
    for y in (-380, -130, 120):
        for s in (-1, 1):
            box(P, (s * 90, y, 110), (14, 14, 180), "iron")
        box(P, (0, y, 200), (220, 20, 14), "iron")
    for k, (x, sw) in enumerate(((-70, "copper"), (0, "copper_dark"), (70, "copper"))):
        tube(P, [(x, -480, 240), (x, 180, 240), (x, 240, 300), (x, 250, 420 + k * 60)], 26, 8, sw)
        for y in (-300, -60):
            tube(P, [(x, y - 12, 240), (x, y + 12, 240)], 32, 8, "brass_dark")
    disc_x(P, (110, -200, 300), 44, 8, 10, "crimson", "iron")       # valve wheel
    tube(P, [(70, -200, 260), (106, -200, 300)], 5, 4, "iron")
    cyl(P, (0, 330, 40), 150, 150, 330, 12, "copper_dark", "copper")
    cyl(P, (0, 330, 370), 150, 60, 80, 12, "copper")
    for z in (90, 200, 310):
        cyl(P, (0, 330, z), 154, 154, 14, 12, "brass_dark", hull=False)
    box(P, (146, 330, 200), (10, 70, 70), "glow")
    cyl(P, (0, 330, 450), 44, 36, 420, 8, "iron", hull=False)
    cyl(P, (0, 330, 870), 54, 54, 26, 8, "brass_dark", hull=False)
    hbox(P, (0, -150, 150), (240, 700, 300))
    P.hull([(150 * math.cos(TAU * k / 8), 330 + 150 * math.sin(TAU * k / 8), z) for k in range(8) for z in (0, 450)])
    return P


def glow_slot(ob, layout):
    """faces on a glow swatch go to a second material slot whose name contains "Glow": SQArt::ApplyGlowSlots lights it"""
    gm = bpy.data.materials.get("M_SetGlow") or bpy.data.materials.new("M_SetGlow")
    if len(ob.data.materials) < 2:
        ob.data.materials.append(gm)
    uv = ob.data.uv_layers[0].data
    n = 0
    for poly in ob.data.polygons:
        u, v = uv[poly.loop_indices[0]].uv
        idx = int(u * 8) + int(v * 8) * 8
        if idx < len(layout) and layout[idx].startswith("glow"):
            poly.material_index = 1; n += 1
    return n


PIECES = [podium, bunting, lantern_arch, coffin_wheel, tent_crypt, lantern_tree, stall, wreck, stilt_house, net_rack, cargo_pile, boat,
          clock_wall, gear_bridge, pendulum, pipes]

if __name__ == "__main__":
    pb.write_palette()
    report = {}
    for fn in PIECES:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        img = bpy.data.images.load(PAL)
        mat = pb.make_material(img)
        P = fn()
        ob, hulls, off = pb.build_objects(P, mat)
        glow_slot(ob, LAYOUT)
        pb.export(ob, hulls, OUT + P.name + ".fbx")
        pb.P_NAME[0] = P.name
        st = pb.stats(ob, hulls)
        report[P.name] = st
        print("PIECE", P.name, st["size_cm"], st["tris"], st["hulls"], [round(off.x, 1), round(off.y, 1)])
    json.dump(report, open("/home/claude/blender_work/fair/report.json", "w"), indent=1)
