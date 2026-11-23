# Soul Quota: landmark set pieces for the three maps (original work, Nov 12 2026 map remaster).
# Same pipeline as blender_work/park/park_build.py (cm, Z up, +X = front, UCX_ hulls) with its own palette
# (ArtSource/Props/set_palette.png -> T_Set / MI_Set) so the park palette asset is never re-imported.
# usage: python3 set_build.py
import sys, os, math, json
sys.path.insert(0, "/home/claude/blender_work/park")
import bpy, bmesh
from mathutils import Vector
import park_build as pb

OUT = "/home/claude/sq/ArtSource/Props/"
PAL = OUT + "set_palette.png"
LAYOUT = ["stone", "stone_dark", "stone_light", "moss", "wood", "wood_dark", "wood_light", "iron",
          "roof", "roof_dark", "glow", "plaster", "brass", "brass_dark", "teal", "red",
          "rope", "leaf_dark", "leaf", "bark", "white", "night", "plaster_dark", "glow_cool"]
COLOURS = {
    "stone": (128, 124, 150), "stone_dark": (78, 74, 100), "stone_light": (170, 166, 190), "moss": (70, 120, 84),
    "wood": (122, 82, 60), "wood_dark": (76, 50, 44), "wood_light": (166, 120, 80), "iron": (52, 50, 66),
    "roof": (84, 62, 128), "roof_dark": (56, 40, 92), "glow": (255, 196, 96), "plaster": (188, 180, 206),
    "brass": (214, 166, 72), "brass_dark": (150, 108, 52), "teal": (47, 183, 166), "red": (196, 62, 60),
    "rope": (190, 160, 110), "leaf_dark": (28, 74, 62), "leaf": (44, 110, 84), "bark": (72, 54, 66),
    "white": (238, 236, 244), "night": (26, 20, 44), "plaster_dark": (140, 132, 164), "glow_cool": (150, 240, 230),
}
pb.LAYOUT[:] = LAYOUT
pb.COLOURS.clear(); pb.COLOURS.update(COLOURS)
pb.SW.clear(); pb.SW.update({n: i for i, n in enumerate(LAYOUT)})
pb.PAL_PATH = PAL
Piece, box, frustum, ico, tube, F, V = pb.Piece, pb.box, pb.frustum, pb.ico, pb.tube, pb.F, pb.V


def cyl(P, c, r0, r1, h, n, side, top=None, hull=False):
    """tapered n-gon column standing on c (base centre)"""
    bm = P.part()
    a = [V(bm, (c[0] + r0 * math.cos(2 * math.pi * k / n), c[1] + r0 * math.sin(2 * math.pi * k / n), c[2])) for k in range(n)]
    b = [V(bm, (c[0] + r1 * math.cos(2 * math.pi * k / n), c[1] + r1 * math.sin(2 * math.pi * k / n), c[2] + h)) for k in range(n)]
    F(bm, a[::-1], side)
    F(bm, b, top or side)
    for k in range(n):
        F(bm, [a[k], a[(k + 1) % n], b[(k + 1) % n], b[k]], side)
    if hull:
        P.hull([v.co.copy() for v in a + b])
    return bm


def disc_x(P, c, r, t, n, sw, rim=None):
    """n-gon disc facing +X/-X (axis along X), centre c, thickness t"""
    bm = P.part()
    a = [V(bm, (c[0] - t / 2, c[1] + r * math.cos(2 * math.pi * k / n), c[2] + r * math.sin(2 * math.pi * k / n))) for k in range(n)]
    b = [V(bm, (c[0] + t / 2, c[1] + r * math.cos(2 * math.pi * k / n), c[2] + r * math.sin(2 * math.pi * k / n))) for k in range(n)]
    F(bm, a[::-1], sw)
    F(bm, b, sw)
    for k in range(n):
        F(bm, [a[k], a[(k + 1) % n], b[(k + 1) % n], b[k]], rim or sw)
    return bm


def gable(P, c, s, rise, sw, end=None, over=20):
    """gable roof: ridge along X, base rectangle centred on c (z = eaves), s = (len, width)"""
    bm = P.part()
    x0, x1 = c[0] - s[0] / 2 - over, c[0] + s[0] / 2 + over
    y0, y1 = c[1] - s[1] / 2 - over, c[1] + s[1] / 2 + over
    z = c[2]
    v = [V(bm, (x0, y0, z)), V(bm, (x1, y0, z)), V(bm, (x1, y1, z)), V(bm, (x0, y1, z)), V(bm, (x0, c[1], z + rise)), V(bm, (x1, c[1], z + rise))]
    F(bm, [v[0], v[3], v[2], v[1]], end or sw)
    F(bm, [v[0], v[1], v[5], v[4]], sw)
    F(bm, [v[2], v[3], v[4], v[5]], sw)
    F(bm, [v[0], v[4], v[3]], end or sw)
    F(bm, [v[1], v[2], v[5]], end or sw)
    return bm


def pyramid(P, c, s, rise, sw):
    return frustum(P, (c[0] - s / 2, c[0] + s / 2, c[1] - s / 2, c[1] + s / 2), (c[0] - 4, c[0] + 4, c[1] - 4, c[1] + 4), c[2], c[2] + rise, sw, sw, hull=False)


def hbox(P, c, s):
    x0, x1, y0, y1, z0, z1 = c[0] - s[0] / 2, c[0] + s[0] / 2, c[1] - s[1] / 2, c[1] + s[1] / 2, c[2] - s[2] / 2, c[2] + s[2] / 2
    P.hull([(x, y, z) for x in (x0, x1) for y in (y0, y1) for z in (z0, z1)])


def window(P, x, y, z, w, h, face="x", sw="glow"):
    if face == "x":
        box(P, (x, y, z), (6, w, h), sw)
        box(P, (x, y, z + h / 2 + 6), (10, w + 16, 12), "stone_dark")
    else:
        box(P, (x, y, z), (w, 6, h), sw)
        box(P, (x, y, z + h / 2 + 6), (w + 16, 10, 12), "stone_dark")


# ------------------------------------------------------------------ graveyard
def chapel():
    P = Piece("Set_Chapel")
    box(P, (0, 0, 20), (760, 470, 40), "stone_dark")
    box(P, (0, 0, 220), (700, 410, 360), "plaster")
    hbox(P, (0, 0, 200), (700, 410, 400))
    gable(P, (0, 0, 400), (700, 410), 230, "roof", "plaster_dark", 26)
    for k in range(4):   # buttresses + side windows
        x = -260 + k * 173
        for s in (-1, 1):
            frustum(P, (x - 22, x + 22, s * 205 - 34, s * 205 + 34), (x - 22, x + 22, s * 205 - 8, s * 205 + 8), 40, 330, "stone", "stone", hull=False)
            if k < 3:
                window(P, x + 86, s * 207, 240, 60, 150, "y")
    # tower at the front (+X)
    box(P, (440, 0, 400), (250, 250, 720), "plaster_dark")
    hbox(P, (440, 0, 380), (250, 250, 760))
    box(P, (440, 0, 770), (280, 280, 26), "stone")
    for s in (-1, 1):
        window(P, 440, s * 127, 640, 70, 120, "y", "night")
    window(P, 567, 0, 640, 70, 120, "x", "night")
    cyl(P, (440, 0, 580), 46, 30, 70, 8, "brass", "brass_dark")
    pyramid(P, (440, 0, 783), 290, 360, "roof_dark")
    box(P, (440, 0, 1170), (8, 8, 70), "brass")
    box(P, (440, 0, 1180), (8, 44, 8), "brass")
    box(P, (568, 0, 150), (8, 120, 220), "wood_dark")           # door
    box(P, (569, 0, 290), (10, 150, 22), "stone_dark")
    window(P, 567, 0, 430, 90, 90, "x")
    return P


def mausoleum():
    P = Piece("Set_Mausoleum")
    box(P, (0, 0, 15), (560, 460, 30), "stone_dark")
    box(P, (0, 0, 45), (520, 420, 30), "stone")
    box(P, (-30, 0, 220), (400, 340, 320), "stone_light")
    hbox(P, (0, 0, 200), (500, 400, 400))
    for y in (-150, -50, 50, 150):
        cyl(P, (210, y, 60), 26, 22, 300, 8, "stone_light")
        box(P, (210, y, 368), (64, 64, 16), "stone")
    box(P, (0, 0, 392), (540, 440, 32), "stone")
    gable(P, (0, 0, 408), (520, 420), 120, "roof", "stone_light", 10)
    box(P, (172, 0, 180), (6, 110, 240), "night")
    box(P, (175, 0, 180), (6, 30, 200), "glow_cool")
    for s in (-1, 1):
        cyl(P, (250, s * 215, 60), 30, 20, 60, 6, "stone_dark")
        ico(P, (250, s * 215, 140), 20, "glow_cool")
    return P


def gate():
    P = Piece("Set_Gate")
    W = 330   # half opening (clear 660 cm)
    for s in (-1, 1):
        box(P, (0, s * (W + 60), 15), (170, 170, 30), "stone_dark")
        box(P, (0, s * (W + 60), 240), (120, 120, 420), "stone")
        hbox(P, (0, s * (W + 60), 225), (120, 120, 450))
        box(P, (0, s * (W + 60), 462), (150, 150, 24), "stone_light")
        pyramid(P, (0, s * (W + 60), 474), 150, 70, "roof_dark")
        ico(P, (0, s * (W + 60), 566), 22, "glow")
    # iron arch between the piers (no collision: you run under it)
    n = 12
    pts = [(0, -W * math.cos(math.pi * k / n), 440 + 130 * math.sin(math.pi * k / n)) for k in range(n + 1)]
    tube(P, pts, 9, 5, "iron")
    pts2 = [(0, -W * math.cos(math.pi * k / n), 400 + 110 * math.sin(math.pi * k / n)) for k in range(n + 1)]
    tube(P, pts2, 6, 5, "iron")
    for k in range(1, n):
        a, b = pts[k], pts2[k]
        tube(P, [b, a], 4, 4, "iron")
    box(P, (0, 0, 590), (10, 150, 44), "iron")
    box(P, (6, 0, 590), (4, 120, 22), "brass")
    return P


def wall():
    P = Piece("Set_Wall")
    box(P, (0, 0, 100), (400, 56, 200), "stone")
    hbox(P, (0, 0, 100), (400, 56, 200))
    box(P, (0, 0, 208), (410, 76, 18), "stone_light")
    for x in (-150, -50, 50, 150):
        box(P, (x, 0, 232), (36, 60, 30), "stone_dark")
    box(P, (-120, 29, 60), (90, 4, 40), "stone_dark")
    box(P, (70, -29, 120), (110, 4, 36), "stone_dark")
    box(P, (120, 29, 150), (70, 4, 50), "moss")
    return P


def yew():
    P = Piece("Set_Yew")
    cyl(P, (0, 0, 0), 46, 28, 300, 7, "bark", hull=True)
    tube(P, [(0, 0, 220), (60, 40, 330), (110, 70, 400)], 14, 5, "bark")
    tube(P, [(0, 0, 240), (-70, -30, 340), (-120, -40, 420)], 13, 5, "bark")
    for c, r, sq, sw in (((0, 0, 470), 190, (1, 1, 0.75), "leaf_dark"), ((130, 80, 430), 120, (1, 1, 0.8), "leaf"),
                         ((-140, -50, 450), 130, (1, 1, 0.8), "leaf"), ((20, -90, 560), 120, (1, 1, 0.85), "leaf"), ((-30, 60, 620), 90, (1, 1, 0.9), "leaf_dark")):
        ico(P, c, r, sw, sq, subd=1)
    return P


def statue():
    P = Piece("Set_Statue")
    box(P, (0, 0, 20), (190, 190, 40), "stone_dark")
    box(P, (0, 0, 115), (140, 140, 150), "stone")
    hbox(P, (0, 0, 100), (190, 190, 200))
    box(P, (0, 0, 198), (160, 160, 16), "stone_light")
    cyl(P, (0, 0, 206), 58, 30, 210, 8, "stone_light")          # robe
    ico(P, (4, 0, 440), 36, "stone_light", (1, 1, 1.15))          # hood
    box(P, (30, 0, 436), (10, 30, 34), "night")
    tube(P, [(-24, 44, 380), (10, 60, 330), (46, 40, 310)], 11, 5, "stone_light")   # arms holding a lantern
    tube(P, [(-24, -44, 380), (10, -60, 330), (46, -40, 310)], 11, 5, "stone_light")
    box(P, (54, 0, 300), (34, 34, 44), "iron")
    box(P, (54, 0, 300), (24, 40, 30), "glow")
    box(P, (72, 0, 115), (4, 90, 70), "brass_dark")
    return P


def belltower():
    P = Piece("Set_BellTower")
    box(P, (0, 0, 20), (320, 320, 40), "stone_dark")
    hbox(P, (0, 0, 200), (300, 300, 400))
    for sx in (-1, 1):
        for sy in (-1, 1):
            box(P, (sx * 115, sy * 115, 440), (46, 46, 800), "wood_dark")
    for z in (260, 520, 760):
        for s in (-1, 1):
            box(P, (0, s * 115, z), (276, 22, 30), "wood")
            box(P, (s * 115, 0, z), (22, 276, 30), "wood")
    for s in (-1, 1):
        tube(P, [(-115, s * 115, 60), (115, s * 115, 250)], 9, 4, "wood_light")
        tube(P, [(s * 115, 115, 280), (s * 115, -115, 510)], 9, 4, "wood_light")
    box(P, (0, 0, 846), (330, 330, 20), "wood")
    pyramid(P, (0, 0, 856), 380, 260, "roof")
    cyl(P, (0, 0, 640), 78, 44, 110, 10, "brass", "brass_dark")
    cyl(P, (0, 0, 750), 10, 10, 96, 5, "iron")
    ico(P, (0, 0, 630), 20, "brass_dark")
    return P


# ------------------------------------------------------------------ harbour
def boathouse():
    P = Piece("Set_Boathouse")
    for sx in (-1, 1):
        for sy in (-1, 1):
            cyl(P, (sx * 270, sy * 180, 0), 24, 22, 150, 6, "wood_dark")
    box(P, (0, 0, 150), (680, 470, 24), "wood_light")
    box(P, (-20, 0, 300), (560, 380, 280), "wood")
    hbox(P, (0, 0, 220), (640, 440, 440))
    for x in range(-270, 271, 90):
        box(P, (x, 191, 300), (10, 4, 280), "wood_dark")
        box(P, (x, -191, 300), (10, 4, 280), "wood_dark")
    gable(P, (-20, 0, 440), (560, 380), 190, "teal", "wood", 30)
    box(P, (262, 0, 270), (6, 150, 200), "night")
    window(P, 262, 130, 330, 60, 70, "x")
    window(P, 262, -130, 330, 60, 70, "x")
    tube(P, [(300, -200, 162), (300, -200, 260), (300, 200, 260), (300, 200, 162)], 7, 4, "rope")
    cyl(P, (310, 150, 162), 34, 34, 70, 8, "wood_dark", "wood")
    ico(P, (310, -120, 200), 34, "red", (1, 1, 1.1))
    return P


def lighthouse():
    P = Piece("Set_Lighthouse")
    cyl(P, (0, 0, 0), 250, 230, 50, 10, "stone_dark", hull=True)
    bands = [("white", 190, 170, 240), ("red", 170, 150, 240), ("white", 150, 132, 240), ("red", 132, 116, 220)]
    z = 50
    for sw, r0, r1, h in bands:
        cyl(P, (0, 0, z), r0, r1, h, 10, sw)
        z += h
    P.hull([(190 * math.cos(2 * math.pi * k / 10), 190 * math.sin(2 * math.pi * k / 10), 50) for k in range(10)]
           + [(116 * math.cos(2 * math.pi * k / 10), 116 * math.sin(2 * math.pi * k / 10), z) for k in range(10)])
    cyl(P, (0, 0, z), 160, 160, 18, 10, "iron")
    cyl(P, (0, 0, z + 18), 90, 90, 130, 8, "glow")
    for k in range(8):
        a = 2 * math.pi * k / 8
        box(P, (96 * math.cos(a), 96 * math.sin(a), z + 83), (10, 10, 130), "iron")
    cyl(P, (0, 0, z + 148), 130, 20, 110, 10, "red")
    ico(P, (0, 0, z + 276), 18, "brass")
    box(P, (188, 0, 150), (8, 90, 180), "wood_dark")
    for zz in (420, 660, 880):
        window(P, 150 - (zz - 420) * 0.07, 0, zz, 40, 70, "x")
    return P


def pilings():
    P = Piece("Set_Pilings")
    spots = [(-70, -50, 330), (60, -30, 420), (-10, 70, 380), (120, 80, 260), (-130, 60, 240)]
    for x, y, h in spots:
        cyl(P, (x, y, 0), 30, 26, h, 7, "wood_dark", "wood_light")
        cyl(P, (x, y, h - 70), 33, 33, 22, 7, "iron")
    hbox(P, (0, 10, 150), (300, 200, 300))
    tube(P, [(-70, -50, 250), (-5, -45, 205), (60, -30, 260)], 7, 4, "rope")
    tube(P, [(60, -30, 300), (30, 20, 250), (-10, 70, 300)], 7, 4, "rope")
    tube(P, [(-10, 70, 200), (-70, 70, 160), (-130, 60, 190)], 7, 4, "rope")
    ico(P, (62, -30, 452), 26, "glow", (1, 1, 1.2))
    box(P, (62, -30, 430), (40, 40, 8), "iron")
    return P


def crane():
    P = Piece("Set_Crane")
    box(P, (0, 0, 30), (300, 300, 60), "stone_dark")
    hbox(P, (0, 0, 150), (300, 300, 300))
    cyl(P, (0, 0, 60), 50, 40, 520, 8, "wood_dark")
    tube(P, [(0, 0, 520), (330, 0, 760)], 22, 5, "wood")
    tube(P, [(0, 0, 580), (-150, 0, 420)], 18, 5, "wood")
    tube(P, [(0, 0, 300), (200, 0, 680)], 12, 4, "wood_light")
    tube(P, [(330, 0, 760), (330, 0, 470)], 5, 4, "rope")
    box(P, (330, 0, 430), (110, 110, 90), "wood_light")
    box(P, (330, 0, 430), (114, 20, 94), "iron")
    cyl(P, (-150, 0, 330), 60, 60, 90, 8, "stone")
    ico(P, (0, 0, 600), 26, "brass")
    return P


# ------------------------------------------------------------------ clocktower
def clocktower():
    P = Piece("Set_ClockTower")
    box(P, (0, 0, 30), (420, 420, 60), "stone_dark")
    box(P, (0, 0, 420), (330, 330, 720), "stone")
    hbox(P, (0, 0, 420), (360, 360, 840))
    box(P, (0, 0, 790), (370, 370, 30), "brass_dark")
    box(P, (0, 0, 990), (300, 300, 370), "plaster")
    for s in (-1, 1):
        disc_x(P, (s * 154, 0, 990), 120, 8, 16, "white", "brass")
        box(P, (s * 160, 0, 1030), (4, 10, 90), "night")
        box(P, (s * 160, 30, 990), (4, 70, 8), "night")
    for s in (-1, 1):   # faces on +Y / -Y: thin white plates
        box(P, (0, s * 154, 990), (200, 8, 200), "white")
        box(P, (0, s * 160, 1030), (10, 4, 90), "night")
        box(P, (30, s * 160, 990), (70, 4, 8), "night")
    box(P, (0, 0, 1188), (340, 340, 26), "brass_dark")
    pyramid(P, (0, 0, 1200), 350, 330, "roof")
    box(P, (0, 0, 1560), (8, 8, 70), "brass")
    box(P, (168, 0, 180), (8, 130, 240), "wood_dark")
    for z in (420, 620):
        window(P, 166, 0, z, 60, 100, "x")
        window(P, 0, 166, z, 60, 100, "y")
        window(P, 0, -166, z, 60, 100, "y")
    for sx in (-1, 1):
        for sy in (-1, 1):
            box(P, (sx * 165, sy * 165, 420), (44, 44, 720), "stone_dark")
    return P


def gear(P, c, r, t, teeth, sw, axis="z"):
    n = teeth * 2
    if axis == "z":
        cyl(P, (c[0], c[1], c[2]), r, r, t, n, sw, "brass_dark" if sw == "brass" else "brass")
        for k in range(teeth):
            a = 2 * math.pi * k / teeth
            bm = box(P, (0, 0, 0), (r * 0.26, r * 0.2, t), sw)
            for v in bm.verts:
                x, y = v.co.x + r * 1.06, v.co.y
                v.co.x, v.co.y, v.co.z = c[0] + x * math.cos(a) - y * math.sin(a), c[1] + x * math.sin(a) + y * math.cos(a), v.co.z + c[2] + t / 2
        cyl(P, (c[0], c[1], c[2] + t), r * 0.25, r * 0.25, t * 0.35, 8, "iron")
    else:
        disc_x(P, c, r, t, n, sw, "brass_dark")
        for k in range(teeth):
            a = 2 * math.pi * k / teeth
            bm = box(P, (0, 0, 0), (t, r * 0.2, r * 0.26), sw)
            for v in bm.verts:
                y, z = v.co.y, v.co.z + r * 1.06
                v.co.x, v.co.y, v.co.z = v.co.x + c[0], c[1] + y * math.cos(a) - z * math.sin(a), c[2] + y * math.sin(a) + z * math.cos(a)
        disc_x(P, c, r * 0.22, t * 1.5, 8, "iron")


def gearstack():
    P = Piece("Set_GearStack")
    gear(P, (0, 0, 0), 260, 70, 12, "brass_dark")
    gear(P, (40, 20, 70), 190, 70, 10, "brass")
    gear(P, (-20, -30, 140), 120, 70, 8, "brass_dark")
    cyl(P, (-20, -30, 210), 30, 30, 240, 8, "iron")
    ico(P, (-20, -30, 480), 44, "glow", (1, 1, 1))
    P.hull([(290 * math.cos(2 * math.pi * k / 10), 290 * math.sin(2 * math.pi * k / 10), z) for k in range(10) for z in (0, 210)])
    return P


def bigwheel():
    P = Piece("Set_GearArch")
    # a giant half-buried gear standing on edge, with a doorway-sized bite: scenery you run round
    gear(P, (0, 0, 180), 380, 80, 14, "brass", axis="x")
    gear(P, (60, 300, 120), 200, 60, 10, "brass_dark", axis="x")
    box(P, (0, 0, 20), (220, 900, 40), "stone_dark")
    hbox(P, (0, 0, 250), (110, 780, 500))
    return P


def lamppost():
    P = Piece("Set_LampPost")
    cyl(P, (0, 0, 0), 26, 20, 40, 8, "iron", hull=False)
    cyl(P, (0, 0, 40), 10, 8, 330, 6, "iron")
    P.hull([(x, y, z) for x in (-14, 14) for y in (-14, 14) for z in (0, 380)])
    tube(P, [(0, 0, 370), (0, 30, 400), (0, 60, 390)], 5, 4, "iron")
    box(P, (0, 60, 350), (34, 34, 50), "glow")
    pyramid(P, (0, 60, 375), 44, 26, "iron")
    box(P, (0, 60, 322), (38, 38, 6), "iron")
    return P


PIECES = [chapel, mausoleum, gate, wall, yew, statue, belltower, boathouse, lighthouse, pilings, crane, clocktower, gearstack, bigwheel, lamppost]

if __name__ == "__main__":
    pb.write_palette()
    report = {}
    for fn in PIECES:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        img = bpy.data.images.load(PAL)
        mat = pb.make_material(img)
        P = fn()
        ob, hulls, off = pb.build_objects(P, mat)
        pb.export(ob, hulls, OUT + P.name + ".fbx")
        pb.P_NAME[0] = P.name
        st = pb.stats(ob, hulls)
        report[P.name] = st
        print("PIECE", P.name, st["size_cm"], st["tris"], st["hulls"], [round(off.x, 1), round(off.y, 1)])
        bpy.ops.wm.save_as_mainfile(filepath="/home/claude/blender_work/sets/%s.blend" % P.name)
    json.dump(report, open("/home/claude/blender_work/sets/report.json", "w"), indent=1)
