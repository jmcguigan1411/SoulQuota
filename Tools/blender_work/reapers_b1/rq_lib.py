# Soul Quota reapers (batch 1) - shared parts on top of the monster helpers (original work).
# Parts are built in world space at the rig's rest pose (Z up, facing -Y, T-pose arms along +-X at z 1.107).
import bpy, bmesh, math, sys, os
sys.path.insert(0, "/home/claude/blender_work/monsters")
import mq_lib as M
from mq_lib import (Part, PARTS, BONES, ring, hring, loft, shell, box, ell, cyl, cone, tube, disc, slab, sbox,
                    frame, rigid, ablend, zblend, segw, mix, read_bones, part_to_object, tri_count, check_weights,
                    export_fbx, swatch_uv)
from mathutils import Vector, Matrix

WORK = "/home/claude/blender_work/reapers_b1/"
CH = "/home/claude/sq/ArtSource/Characters/"
PREV = "/home/claude/sq/ArtSource/Previews/Reapers/"
RIG_BASE = WORK + "rig_base.blend"

# ---------------------------------------------------------------- palette layout (8x8 swatches of 16 px)
LAYOUT = ["bone", "bone_shade", "bone_dark", "socket", "eye", "eye_hi", "glow", "glow2",
          "c1", "c1_dark", "c1_light", "c1_inner", "c2", "c2_dark", "c2_light", "c3",
          "c3_dark", "trim", "trim_dark", "leather", "leather_dark", "metal", "metal_dark", "gold",
          "rope", "wood", "wood_dark", "boot", "boot_dark", "sole", "paper", "red",
          "red_dark", "white", "black", "lace", "coffee", "glass", "skin", "skin_dark",
          "accent", "accent_dark", "glow3", "ink"]
M.LAYOUT[:] = LAYOUT
M.SW.clear(); M.SW.update({n: i for i, n in enumerate(LAYOUT)})
M.GLOW.clear(); M.GLOW.update({"eye", "eye_hi", "glow", "glow2", "glow3"})
SW = M.SW

def hexc(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4))

def make_palette(name, colours, path):
    W = 128
    img = bpy.data.images.new(name, W, W, alpha=False)
    px = [0.0] * (W * W * 4)
    for i in range(64):
        c = colours.get(LAYOUT[i], "#808080") if i < len(LAYOUT) else "#808080"
        c = hexc(c) if isinstance(c, str) else c
        cx, cy = i % 8, i // 8
        for y in range(cy * 16, cy * 16 + 16):
            for x in range(cx * 16, cx * 16 + 16):
                k = (y * W + x) * 4
                px[k:k + 4] = [c[0], c[1], c[2], 1.0]
    img.pixels = px
    img.filepath_raw = path
    img.file_format = 'PNG'
    img.save()
    return img

def make_materials(mid, img):
    mats = []
    for name, emis in (("M_R_" + mid, 0.0), ("M_R_" + mid + "_Glow", 4.0)):
        m = bpy.data.materials.new(name)
        m.use_nodes = True
        nt = m.node_tree
        bsdf = nt.nodes.get("Principled BSDF")
        tex = nt.nodes.new("ShaderNodeTexImage")
        tex.image = img
        tex.interpolation = 'Closest'
        nt.nodes.active = tex
        nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
        bsdf.inputs["Roughness"].default_value = 0.8
        if emis:
            nt.links.new(tex.outputs["Color"], bsdf.inputs["Emission Color"])
            bsdf.inputs["Emission Strength"].default_value = emis
        mats.append(m)
    return mats

ROT = lambda deg, ax: Matrix.Rotation(math.radians(deg), 3, ax)
ZC = 1.107  # arm height

def sstep(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)

def tag_all(p, sw):
    for f in p.bm.faces:
        f[p.sw] = SW[sw]

def shade_down(p, src, dst, nz=-0.55):
    p.recolor(lambda c, n, cur: cur == src and n.z < nz, dst)

def transform_part(p, fn):
    for v in p.bm.verts:
        v.co = fn(v.co.copy())

# ---------------------------------------------------------------- weights
TORSO_W = zblend([(0.56, {"hips": 1}), (0.68, {"hips": .5, "spine": .5}), (0.86, {"spine": 1}),
                  (1.04, {"chest": .7, "spine": .3}), (1.14, {"chest": 1})])

def leg_side(co, soft=0.10):
    """share of the left leg (0..1) by x, smooth across the middle"""
    return sstep(0.5 + co.x / (2 * soft))

def skirt_w(z_waist=0.58, z_full=0.30, leg_max=0.85, low_k=0.35, soft=0.10, top=None):
    """continuous skirt: torso weights at the waist, then the legs take over going down (split smoothly by x);
    below the knee part of each leg share goes to the shin."""
    top = top or TORSO_W
    def fn(co):
        base = top(co) if co.z > 0.56 else {"hips": 1.0}
        t = sstep((z_waist - co.z) / (z_waist - z_full)) * leg_max
        if t <= 0:
            return base
        l = leg_side(co, soft)
        out = {k: v * (1 - t) for k, v in base.items()}
        kl = sstep((0.30 - co.z) / 0.22) * low_k
        for side, share in (("l", l), ("r", 1 - l)):
            if share < 1e-3:
                continue
            out["upperleg." + side] = out.get("upperleg." + side, 0) + t * share * (1 - kl)
            if kl > 0:
                out["lowerleg." + side] = out.get("lowerleg." + side, 0) + t * share * kl
        return out
    return fn

def sleeve_up_w(side, s):
    return ablend(0, [(0.19, {"chest": .55, "upperarm." + side: .45}), (0.30, {"upperarm." + side: 1})], s)

def head_neck_w(z0=1.20, z1=1.33):
    return zblend([(z0, {"chest": 1}), ((z0 + z1) / 2, {"chest": .5, "head": .5}), (z1, {"head": 1})])

# ---------------------------------------------------------------- skull head
def skull(c, rad=(.27, .25, .26), face_cut=0.86, seg=(12, 8), sw="bone", shade="bone_shade", eyes=None,
          mouth="grin", nose=True, name="skull", bone="head", brow=0.0, eye_z=0.03, mouth_z=-0.15, mouth_w=0.11):
    """chunky cartoon skull. returns the front plane y."""
    c = Vector(c)
    p = Part(name, rigid(bone))
    vs = ell(p, c, rad, seg[0], seg[1], sw)
    fy = c.y - rad[1] * face_cut
    for v in vs:
        if v.co.y < fy:
            v.co.y = fy
    shade_down(p, sw, shade)
    if brow:
        box(p, (c.x, fy - 0.01, c.z + eye_z + 0.085), (rad[0] * 1.5, .07, brow), sw, 0.02)
    d = Part(name + "_face", rigid(bone))
    E = dict(r=.075, socket=.088, glow="eye", hi=True, sx=0.105, sh=1.07)
    E.update(eyes or {})
    for s in (1, -1):
        ex = E["sx"] * s
        disc(d, (c.x + ex, fy - 0.004, c.z + eye_z), E["socket"], E["socket"] * E["sh"], 10, "socket")
        if E["r"] > 0:
            disc(d, (c.x + ex, fy - 0.008, c.z + eye_z - 0.004), E["r"], E["r"] * E["sh"], 10, E["glow"])
            if E["hi"]:
                disc(d, (c.x + ex - 0.022, fy - 0.012, c.z + eye_z + 0.026), .02, .02, 6, "eye_hi")
    if nose:
        z = c.z + eye_z - 0.075
        f = d.face([d.v((c.x - 0.026, fy - 0.004, z)), d.v((c.x, fy - 0.004, z - 0.04)), d.v((c.x + 0.026, fy - 0.004, z))], "socket")
    if mouth == "grin":
        segs = 8
        top, bot = [], []
        for k in range(segs + 1):
            t = -1 + 2 * k / segs
            x = mouth_w * t
            zc = c.z + mouth_z + 0.03 * t * t
            th = 0.013 + 0.008 * (1 - t * t)
            top.append(d.v((c.x + x, fy - 0.004, zc + th / 2)))
            bot.append(d.v((c.x + x, fy - 0.004, zc - th / 2)))
        for k in range(segs):
            d.face([bot[k], bot[k + 1], top[k + 1], top[k]], "socket")
        for t in (-0.6, -0.2, 0.2, 0.6):
            x = mouth_w * t
            zc = c.z + mouth_z + 0.03 * t * t
            d.face([d.v((c.x + x - 0.006, fy - 0.005, zc - 0.026)), d.v((c.x + x + 0.006, fy - 0.005, zc - 0.026)),
                    d.v((c.x + x + 0.006, fy - 0.005, zc + 0.024)), d.v((c.x + x - 0.006, fy - 0.005, zc + 0.024))], "socket")
    elif mouth == "teeth":  # open toothy jaw line
        box(d, (c.x, fy - 0.004, c.z + mouth_z), (mouth_w * 2, .006, .05), "socket")
        for k in range(5):
            x = mouth_w * (-0.8 + 0.4 * k)
            box(d, (c.x + x, fy - 0.009, c.z + mouth_z + 0.004), (.032, .006, .036), sw)
    elif mouth == "o":
        disc(d, (c.x, fy - 0.004, c.z + mouth_z), .035, .045, 8, "socket")
    elif mouth == "wobble":
        pts = []
        for k in range(7):
            t = -1 + 2 * k / 6
            pts.append((c.x + mouth_w * t, c.z + mouth_z + (0.012 if k % 2 else -0.012)))
        for (x0, z0), (x1, z1) in zip(pts, pts[1:]):
            d.face([d.v((x0, fy - 0.004, z0 - 0.009)), d.v((x1, fy - 0.004, z1 - 0.009)),
                    d.v((x1, fy - 0.004, z1 + 0.009)), d.v((x0, fy - 0.004, z0 + 0.009))], "socket")
    d.bm.normal_update()
    for f in d.bm.faces:
        if f.normal.y > 0:
            f.normal_flip()
    d.recalc = False
    return fy

# ---------------------------------------------------------------- hood (deep cowl with a tip)
def hood(c, rad, tip=((0, .07, .10, .125), (0, .20, .155, .08), (0, .33, .15, .045)), tip_end=(0, .44, .10),
         open_deg=58, open_rows=(1, 3), lats=(-64, -44, -20, 6, 34, 54, 70), n=16, sw="c1", inner="c1_inner",
         rim="trim", rim_dark="c1_dark", back_bulge=1.12, name="hood", bone="head", wts=None):
    c, rad = Vector(c), Vector(rad)
    p = Part(name, wts or rigid(bone))
    grid = []
    lats = list(lats)
    for la in lats:
        phi = math.radians(la)
        r = []
        for k in range(n):
            th = 2 * math.pi * k / n
            x = rad.x * math.cos(phi) * math.sin(th)
            y = -rad.y * math.cos(phi) * math.cos(th)
            if y > 0:
                y *= back_bulge
            r.append(p.v(c + Vector((x, y, rad.z * math.sin(phi)))))
        grid.append(r)
    top = c + Vector((0, 0, rad.z * math.sin(math.radians(lats[-1]))))
    prev = top
    ntip = 0
    for ox, oy, oz, rr in tip:
        cpos = top + Vector((ox, oy, oz))
        tang = (cpos - prev).normalized()
        u = Vector((1, 0, 0))
        v = tang.cross(u).normalized()
        grid.append([p.v(cpos + u * (rr * math.sin(2 * math.pi * k / n)) - v * (rr * math.cos(2 * math.pi * k / n))) for k in range(n)])
        prev = cpos
        ntip += 1
    tipv = p.v(top + Vector(tip_end))
    faces = []
    for i in range(len(grid) - 1):
        for k in range(n):
            thc = 2 * math.pi * (k + 0.5) / n
            ang = math.degrees(min(thc, 2 * math.pi - thc))
            if ang < open_deg and open_rows[0] <= i <= open_rows[1]:
                continue
            faces.append(p.face([grid[i][k], grid[i][(k + 1) % n], grid[i + 1][(k + 1) % n], grid[i + 1][k]], sw))
    tipset = set(v for r in grid[len(grid) - ntip - 1:] for v in r) if ntip else set()
    main = [f for f in faces if not all(v in tipset for v in f.verts)] if ntip else list(faces)
    for k in range(n):
        p.face([grid[-1][k], grid[-1][(k + 1) % n], tipv], sw)
    newf = shell(p, main, lambda co: c + Vector(((co - c).x * .87, (co - c).y * .87, (co - c).z * .87 - 0.01)), inner, rim_dark)
    for f in newf:
        if f[p.sw] == SW[rim_dark]:
            cz = sum((v.co.z for v in f.verts), 0) / len(f.verts)
            cy = sum((v.co.y for v in f.verts), 0) / len(f.verts)
            if cz > c.z - rad.z * 0.75 and cy < c.y:
                f[p.sw] = SW[rim]
    return p

# ---------------------------------------------------------------- bony limbs
def bony_arm(side, s, r=0.034, hand="fist", hs=1.0, sw="bone", shade="bone_shade", x_start=0.22, hand_sw=None):
    up, lo, wr, ha = "upperarm." + side, "lowerarm." + side, "wrist." + side, "hand." + side
    p = Part("armbone_up_" + side, rigid(up))
    cyl(p, (x_start * s, 0, ZC), (0.44 * s, 0.01, ZC), r, r * 0.9, 6, sw)
    q = Part("armbone_lo_" + side, rigid(lo))
    ell(q, (0.454 * s, 0.012, ZC), (r * 1.45, r * 1.45, r * 1.45), 6, 4, sw)
    cyl(q, (0.46 * s, 0.01, ZC), (0.72 * s, 0, ZC), r * 0.9, r * 0.85, 6, sw)
    w = Part("wrist_" + side, rigid(wr))
    cyl(w, (0.70 * s, 0, ZC), (0.80 * s, 0, ZC), r * 0.85, r * 0.85, 6, sw)
    bony_hand(side, s, hand, hs, hand_sw or sw, shade)

def bony_hand(side, s, style="fist", hs=1.0, sw="bone", shade="bone_shade", px=0.865):
    h = Part("hand_" + side, rigid("hand." + side))
    def S(co):
        o = Vector((px * s, 0, 1.092))
        return o + (Vector(co) - o) * hs
    if style == "fist":
        box(h, S((px * s, 0, 1.092)), (.14 * hs, .14 * hs, .12 * hs), sw, 0.03 * hs)
        for k, y in enumerate((-0.045, -0.015, 0.015, 0.045)):
            box(h, S(((px + 0.06) * s, y * 1.1, 1.045 - abs(y) * 0.2)), (.05 * hs, .031 * hs, .08 * hs), sw, 0.009 * hs)
        box(h, S(((px - 0.025) * s, -0.075, 1.065)), (.06 * hs, .04 * hs, .05 * hs), sw, 0.01 * hs, ROT(-25 * s, 'Z'))
    elif style == "mitt":  # big glove
        box(h, S((px * s, 0, 1.092)), (.16 * hs, .17 * hs, .13 * hs), sw, 0.035 * hs)
        box(h, S(((px + 0.075) * s, 0.005, 1.06)), (.07 * hs, .16 * hs, .10 * hs), sw, 0.03 * hs)
        box(h, S(((px - 0.01) * s, -0.10, 1.07)), (.08 * hs, .05 * hs, .06 * hs), sw, 0.015 * hs, ROT(-25 * s, 'Z'))
        cyl(h, S(((px - 0.10) * s, 0, 1.095)), S(((px - 0.05) * s, 0, 1.095)), .085 * hs, .08 * hs, 8, sw)
    elif style == "claw":  # long thin skeletal fingers
        box(h, S((px * s, 0, 1.095)), (.11 * hs, .12 * hs, .07 * hs), sw, 0.02 * hs)
        for k, y in enumerate((-0.04, 0.0, 0.04)):
            a0 = Vector(((px + 0.05) * s, y, 1.095))
            tube(h, [S(a0), S(a0 + Vector((0.08 * s, y * 0.3, -0.015))), S(a0 + Vector((0.15 * s, y * 0.5, -0.07)))],
                 [.019 * hs, .016 * hs, .011 * hs], 4, sw)
        tube(h, [S(((px - 0.01) * s, -0.06, 1.095)), S(((px + 0.04) * s, -0.11, 1.07)), S(((px + 0.08) * s, -0.13, 1.03))],
             [.019 * hs, .015 * hs, .011 * hs], 4, sw)
    shade_down(h, sw, shade, -0.6)
    return h

def bony_leg(side, s, r=0.045, sw="bone", shade="bone_shade", knee=True, top=0.52):
    x = 0.171 * s
    p = Part("thigh_" + side, rigid("upperleg." + side))
    cyl(p, (x, 0, top), (x, -0.008, 0.30), r, r * 0.9, 6, sw)
    q = Part("shin_" + side, rigid("lowerleg." + side))
    if knee:
        ell(q, (x, -0.012, 0.29), (r * 1.38, r * 1.38, r * 1.3), 6, 4, sw)
    cyl(q, (x, -0.008, 0.29), (x, 0.012, 0.15), r, r * 0.93, 6, sw)
    shade_down(q, sw, shade, -0.5)

def boot(side, s, style="boot", main="boot", dark="boot_dark", sole="sole", sc=1.0, cuff=True, cuff_sw=None):
    x = 0.171 * s
    def S(co):
        o = Vector((x, -0.02, 0.0))
        d = Vector(co) - o
        return o + Vector((d.x * sc, d.y * sc, d.z * min(sc, 1.15)))
    p = Part("foot_" + side, rigid("foot." + side))
    q = Part("toe_" + side, rigid("toes." + side))
    if style == "boot":
        cyl(p, S((x, 0.015, 0.07)), S((x, 0.015, 0.225)), .094 * sc, .1 * sc, 8, main)
        if cuff:
            cyl(p, S((x, 0.015, 0.20)), S((x, 0.015, 0.262)), .118 * sc, .124 * sc, 8, cuff_sw or dark)
        box(p, S((x, -0.02, 0.072)), (.205 * sc, .235 * sc, .13 * sc), main, 0.04 * sc)
        box(p, S((x, -0.01, 0.017)), (.215 * sc, .25 * sc, .034 * sc), sole, 0.01)
        box(q, S((x, -0.165, 0.068)), (.215 * sc, .16 * sc, .125 * sc), main, 0.05 * sc)
        box(q, S((x, -0.17, 0.017)), (.225 * sc, .17 * sc, .034 * sc), sole, 0.012)
    elif style == "sneaker":
        box(p, S((x, -0.0, 0.08)), (.20 * sc, .24 * sc, .15 * sc), main, 0.05 * sc)
        box(p, S((x, 0.0, 0.022)), (.215 * sc, .26 * sc, .044 * sc), sole, 0.012)
        cyl(p, S((x, 0.03, 0.12)), S((x, 0.03, 0.19)), .085 * sc, .09 * sc, 8, dark)
        box(q, S((x, -0.17, 0.07)), (.21 * sc, .19 * sc, .12 * sc), main, 0.055 * sc)
        box(q, S((x, -0.175, 0.022)), (.225 * sc, .2 * sc, .044 * sc), sole, 0.015)
        box(q, S((x, -0.135, 0.125)), (.12 * sc, .07 * sc, .02 * sc), dark, 0.005)  # laces
    elif style == "bare":  # bony foot
        box(p, S((x, -0.02, 0.05)), (.13 * sc, .17 * sc, .08 * sc), main, 0.03 * sc)
        cyl(p, S((x, 0.01, 0.06)), S((x, 0.01, 0.16)), .04 * sc, .045 * sc, 6, main)
        for k, dx in enumerate((-0.045, 0.0, 0.045)):
            a = Vector((x + dx * s * sc, -0.11, 0.035))
            tube(q, [S(a), S(a + Vector((dx * 0.2 * s, -0.11, -0.012)))], [.022 * sc, .02 * sc], 5, main)
        shade_down(p, main, dark, -0.6)
        shade_down(q, main, dark, -0.6)
    elif style == "sandal":
        box(p, S((x, -0.02, 0.05)), (.13 * sc, .17 * sc, .08 * sc), main, 0.03 * sc)
        cyl(p, S((x, 0.01, 0.06)), S((x, 0.01, 0.16)), .04 * sc, .045 * sc, 6, main)
        box(p, S((x, -0.02, 0.012)), (.17 * sc, .24 * sc, .024 * sc), sole, 0.008)
        box(p, S((x, -0.04, 0.07)), (.15 * sc, .04 * sc, .05 * sc), dark, 0.01)
        for k, dx in enumerate((-0.045, 0.0, 0.045)):
            a = Vector((x + dx * s * sc, -0.11, 0.04))
            tube(q, [S(a), S(a + Vector((dx * 0.2 * s, -0.10, -0.014)))], [.022 * sc, .02 * sc], 5, main)
        box(q, S((x, -0.17, 0.012)), (.18 * sc, .14 * sc, .024 * sc), sole, 0.008)
    elif style == "clog":
        box(p, S((x, -0.01, 0.08)), (.19 * sc, .24 * sc, .14 * sc), main, 0.05 * sc)
        cyl(p, S((x, 0.02, 0.12)), S((x, 0.02, 0.20)), .07 * sc, .075 * sc, 6, dark)
        box(q, S((x, -0.17, 0.065)), (.20 * sc, .19 * sc, .12 * sc), main, 0.06 * sc)
        cone(q, S((x, -0.22, 0.09)), S((x, -0.31, 0.16)), .05 * sc, 5, main)  # curled toe

# ---------------------------------------------------------------- cloth pieces
def sleeve(side, s, r_up=.10, r_lo=.11, bell=.16, end_x=0.69, sw="c1", inner="c1_inner", rim="c1_dark",
           jag=(0.0, 0.05, 0.0, 0.035, 0.0, 0.055, 0.0, 0.03, 0.0, 0.045), n=10, droop=0.02, cuff=None, start=0.16):
    up, lo = "upperarm." + side, "lowerarm." + side
    p = Part("sleeve_up_" + side, sleeve_up_w(side, s))
    loft(p, [ring((x * s, 0, ZC), (0, -1, 0), (0, 0, 1), r, r * 1.02, 8, math.pi / 8)
             for x, r in [(start, r_up), (0.32, r_up * 1.04), (0.465, r_up * 0.98)]], sw)
    q = Part("sleeve_lo_" + side, rigid(lo))
    r0 = ring((0.43 * s, 0, ZC), (0, -1, 0), (0, 0, 1), r_lo, r_lo, n)
    r1 = ring((((0.43 + end_x) / 2) * s, 0, ZC - droop * 0.4), (0, -1, 0), (0, 0, 1), (r_lo + bell) / 2, (r_lo + bell) / 2, n)
    r2 = ring((end_x * s, 0, ZC - droop), (0, -1, 0), (0, 0, 1), bell, bell * 0.98, n)
    r2 = [co + Vector((jag[k % len(jag)] * s, 0, 0)) for k, co in enumerate(r2)]
    g, faces, caps = loft(q, [r0, r1, r2], sw, cap0=True, cap1=False)
    shell(q, faces[:-1], lambda co: Vector((co.x, co.y * .82, ZC + (co.z - ZC) * .82)), inner, rim)
    if cuff:
        loft(q, [ring(((end_x - 0.05) * s, 0, ZC - droop), (0, -1, 0), (0, 0, 1), bell * 1.04, bell * 1.04, n),
                 ring((end_x * s, 0, ZC - droop), (0, -1, 0), (0, 0, 1), bell * 1.06, bell * 1.06, n)], cuff, cap0=False, cap1=False)
    return p, q

def torso(spec, sw="c1", n=12, wts=None, name="torso", cap0=True, cap1=True):
    p = Part(name, wts or TORSO_W)
    rings = [hring(z, rx, ry, n, cy=cy) for z, rx, ry, cy in [(s_[0], s_[1], s_[2], s_[3] if len(s_) > 3 else 0.0) for s_ in spec]]
    g, faces, caps = loft(p, rings, sw, cap0=cap0, cap1=cap1)
    return p, g, faces

def skirt(spec, hem, sw="c1", inner="c1_inner", rim="c1_dark", hem_sw=None, n=16, wts=None, name="skirt", shell_k=0.9):
    """spec: [(z, rx, ry, cy)], hem: per-column extra drop on the last ring"""
    p = Part(name, wts or skirt_w())
    rings = [hring(z, rx, ry, n, cy=cy) for z, rx, ry, cy in spec]
    rings[-1] = [co - Vector((0, 0, hem[k % len(hem)])) for k, co in enumerate(rings[-1])]
    g, faces, caps = loft(p, rings, sw, cap0=False, cap1=False)
    if hem_sw:
        p.tag(faces[-n:], hem_sw)
    shell(p, faces, lambda co: Vector((co.x * shell_k, co.y * shell_k, co.z + 0.005)), inner, rim)
    return p, g, faces

def belt(zc, rx, ry, tr, sw, n=16, m=4, wts=None, cy=0.0, name="belt"):
    p = Part(name, wts or rigid("hips"))
    grid = []
    for i in range(n):
        a = 2 * math.pi * i / n
        cc = Vector((rx * math.cos(a), cy + ry * math.sin(a), zc))
        out = Vector((math.cos(a) / rx, math.sin(a) / ry, 0)).normalized()
        grid.append([p.v(cc + out * (tr * math.cos(2 * math.pi * j / m + math.pi / m)) + Vector((0, 0, tr * 1.3 * math.sin(2 * math.pi * j / m + math.pi / m)))) for j in range(m)])
    for i in range(n):
        for j in range(m):
            p.face([grid[i][j], grid[(i + 1) % n][j], grid[(i + 1) % n][(j + 1) % m], grid[i][(j + 1) % m]], sw)
    return p

def lantern(p, base, h=0.20, r=0.06, metal="metal", glow="glow", seg=6, hook=True):
    """small hanging lantern with its base centre at `base` (z up)"""
    b = Vector(base)
    cyl(p, b, b + Vector((0, 0, 0.03)), r * 1.1, r * 1.1, seg, metal)
    cyl(p, b + Vector((0, 0, 0.03)), b + Vector((0, 0, h * 0.72)), r * 0.78, r * 0.78, seg, glow)
    for k in range(3):
        a = 2 * math.pi * k / 3 + 0.5
        box(p, b + Vector((r * 0.95 * math.cos(a), r * 0.95 * math.sin(a), h * 0.4)), (.016, .016, h * 0.62), metal)
    cyl(p, b + Vector((0, 0, h * 0.70)), b + Vector((0, 0, h * 0.92)), r * 1.25, r * 0.35, seg, metal)
    if hook:
        cyl(p, b + Vector((0, 0, h * 0.92)), b + Vector((0, 0, h * 1.02)), .014, .014, 4, metal)

# ---------------------------------------------------------------- build / export
def start(mid, colours):
    bpy.ops.wm.open_mainfile(filepath=RIG_BASE)
    rig = bpy.data.objects["Rig"]
    for pb in rig.pose.bones:
        pb.matrix_basis = Matrix.Identity(4)
    read_bones(rig)
    img = make_palette("r_%s_palette" % mid.lower(), colours, CH + "r_%s_palette.png" % mid.lower())
    mats = make_materials(mid, img)
    return rig, img, mats

def assemble(mid, rig, mats):
    objs = [part_to_object(p, mats) for p in PARTS]
    PARTS.clear()
    bpy.ops.object.select_all(action='DESELECT')
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.join()
    body = bpy.context.view_layer.objects.active
    body.name = "R_%s_Body" % mid
    body.data.name = "R_%s_Body" % mid
    body.data.transform(rig.matrix_world.inverted())
    body.parent = rig
    body.matrix_parent_inverse = Matrix.Identity(4)
    body.matrix_basis = Matrix.Identity(4)
    mod = body.modifiers.new("Armature", 'ARMATURE')
    mod.object = rig
    bpy.ops.object.select_all(action='DESELECT')
    body.select_set(True)
    bpy.context.view_layer.objects.active = body
    bpy.ops.object.shade_flat()
    bpy.ops.object.mode_set(mode='WEIGHT_PAINT')
    bpy.ops.object.vertex_group_limit_total(group_select_mode='ALL', limit=4)
    bpy.ops.object.vertex_group_normalize_all(lock_active=False)
    bpy.ops.object.mode_set(mode='OBJECT')
    return body

def finish(mid, rig, img, mats):
    body = assemble(mid, rig, mats)
    zs = [(rig.matrix_world @ body.matrix_local @ v.co).z for v in body.data.vertices]
    print("MODEL R_%s TRIS %d VERTS %d UNWEIGHTED %d HEIGHT %.3f MINZ %.3f GROUPS %s" % (
        mid, tri_count(body), len(body.data.vertices), check_weights(body), max(zs), min(zs), sorted(g.name for g in body.vertex_groups)))
    bpy.ops.wm.save_as_mainfile(filepath=CH + "R_%s.blend" % mid, relative_remap=True)
    img.filepath = "//r_%s_palette.png" % mid.lower()
    img.reload()
    bpy.ops.wm.save_mainfile()
    export_fbx(CH + "R_%s.fbx" % mid, [rig, body], anims=False)
    print("EXPORTED", CH + "R_%s.fbx" % mid)
    return body

_tube = tube
def tube(p, pts, radii, seg, sw, cap0=True, cap1=True, up=Vector((0, 0, 1))):
    """as mq_lib.tube, but a (rx, rz) tuple means the same flat radius at every point"""
    if isinstance(radii, tuple) and len(radii) == 2 and all(isinstance(x, (int, float)) for x in radii):
        radii = [radii] * len(pts)
    return _tube(p, pts, radii, seg, sw, cap0, cap1, up)
