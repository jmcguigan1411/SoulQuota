# Soul Quota - reaper batch 2: shared helpers on top of monsters/mq_lib.py (original work).
# Mesh-only reaper bodies bound to the exact "Rig" from ArtSource/Characters/Reaper_Custom.fbx (read only).
import bpy, math, sys, os
sys.path.insert(0, "/home/claude/blender_work/monsters")
from mathutils import Vector, Matrix
import mq_lib
from mq_lib import *

RIG_BLEND = "/home/claude/blender_work/reapers2/reaper_rig.blend"   # rig + actions read from Reaper_Custom.fbx
REAPER_FBX = CH + "Reaper_Custom.fbx"

# extra palette swatches (indices 32-63); same 8x8 grid of 16 px as reaper_palette.png
EXTRA = ["gold", "gold_dark", "leather", "leather_dark", "lens", "flame", "flame_core", "wax",
         "wax_dark", "wax_drip", "fabric", "fabric_dark", "fabric2", "fabric2_dark", "hat", "hat_dark",
         "light", "glass", "red", "red_dark", "white", "white_dark", "chain", "paper",
         "ink", "mud", "mud_dark", "stripe", "fur", "fur_dark", "sole", "skin_mid"]
if len(mq_lib.LAYOUT) == 32:
    mq_lib.LAYOUT.extend(EXTRA)
    mq_lib.SW.clear()
    mq_lib.SW.update({n: i for i, n in enumerate(mq_lib.LAYOUT)})
    mq_lib.GLOW.update({"lens", "flame", "flame_core", "light"})

ROT = lambda deg, ax: Matrix.Rotation(math.radians(deg), 3, ax)
ZC = 1.107  # arm height in the T pose

def hexc(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4))

def pal(**kw):
    return {k: (hexc(v) if isinstance(v, str) else v) for k, v in kw.items()}

def load_reaper_rig():
    bpy.ops.wm.open_mainfile(filepath=RIG_BLEND)
    for a in list(bpy.data.actions):
        bpy.data.actions.remove(a)
    rig = bpy.data.objects["Rig"]
    if rig.animation_data:
        rig.animation_data.action = None
    for pb in rig.pose.bones:
        pb.matrix_basis = Matrix.Identity(4)
    bpy.context.scene.render.fps = 30
    read_bones(rig)
    return rig

TORSO_W = zblend([(0.58, {"hips": 1}), (0.70, {"hips": .5, "spine": .5}), (0.88, {"spine": 1}),
                  (1.04, {"chest": .7, "spine": .3}), (1.14, {"chest": 1})])
NECK_W = zblend([(1.20, {"chest": 1}), (1.30, {"chest": .4, "head": .6}), (1.36, {"head": 1})])

def neck(sw, r=0.08, top=1.38, fwd=0.0, z0=1.16):
    p = Part("neck", NECK_W)
    tube(p, [(0, 0.01, z0), (0, -fwd * 0.5, (z0 + top) / 2), (0, -fwd, top)], [r, r * .95, r * .9], 6, sw)

# ---------------------------------------------------------------- limbs
def arm_tube(side, s, radii, sw, sw_under=None, x0=0.19, x1=0.80, n=6, name="arm"):
    """continuous tube shoulder->wrist with smooth distance weights (no tearing at the elbow)"""
    up, lo, wr = "upperarm." + side, "lowerarm." + side, "wrist." + side
    p = Part(name + "_" + side, segw(["chest", up, lo, wr], k=5, bias={"chest": 1.6}))
    xs = [x0 + (x1 - x0) * i / (len(radii) - 1) for i in range(len(radii))]
    tube(p, [(x * s, 0, ZC) for x in xs], radii, n, sw)
    if sw_under:
        p.recolor(lambda c, nn, cur: nn.z < -0.55, sw_under)
    return p

def cuff(side, s, x, r, sw, w=0.05, bone="lowerarm"):
    p = Part("cuff_%s_%d" % (side, int(x * 100)), rigid(bone + "." + side))
    tube(p, [(x * s - w / 2 * s, 0, ZC), (x * s + w / 2 * s, 0, ZC)], [r, r], 8, sw)

def shoulder_pad(side, s, r, sw, z=ZC + 0.01, x=0.25):
    q = Part("shoulder_" + side, ablend(0, [(0.20, {"chest": .6, "upperarm." + side: .4}), (0.32, {"upperarm." + side: 1})], s))
    ell(q, (x * s, 0.0, z), (r * 1.05, r, r * 0.95), 8, 5, sw)

def mitt(side, s, sw, scale=1.0, sw_dark=None, claw_sw=None, claw=0.0, fingers=3, px=0.845):
    """chunky hand: palm + curled fingers + thumb. Weapon grip (handslot) sits under the palm."""
    ha = "hand." + side
    h = Part("hand_" + side, rigid(ha))
    hs = scale
    box(h, (px * s, 0, ZC - 0.01), (.15 * hs, .17 * hs, .10 * hs), sw, 0.03)
    ys = (-0.055, 0.0, 0.055) if fingers == 3 else (-0.06, -0.02, 0.02, 0.06)
    for k, y in enumerate(ys):
        y *= hs
        a = Vector(((px + 0.06 * hs) * s, y, ZC - 0.005))
        b = a + Vector((0.07 * hs * s, 0, -0.025))
        c = b + Vector((0.01 * s, 0, -0.06 * hs))
        tube(h, [a, b, c], [.031 * hs, .029 * hs, .025 * hs], 5, sw)
        if claw_sw:
            cone(h, c + Vector((0, 0, 0.01)), c + Vector((-0.02 * s, 0, -0.04 - claw)), 0.02 * hs, 4, claw_sw)
    a = Vector(((px - 0.02) * s, -0.085 * hs, ZC - 0.005))
    tube(h, [a, a + Vector((0.04 * s, -0.04 * hs, -0.01)), a + Vector((0.08 * s, -0.05 * hs, -0.04))], [.032 * hs, .028 * hs, .024 * hs], 5, sw)
    if sw_dark:
        h.recolor(lambda c, n, cur: cur == sw and n.z < -0.6, sw_dark)
    return h

def leg_tube(side, s, radii, zs, sw, sw_under=None, name="leg", n=8, x=0.171):
    """leg tube hip->ankle, smooth knee: weights blend upperleg -> lowerleg by distance"""
    ul, ll = "upperleg." + side, "lowerleg." + side
    def w(co):
        if co.z > 0.36:
            t = min(1.0, (co.z - 0.36) / 0.06)
            return {ul: 1.0} if t >= 1 else {ul: 0.5 + 0.5 * t, ll: 0.5 - 0.5 * t}
        if co.z < 0.24:
            return {ll: 1.0}
        t = (co.z - 0.24) / 0.12
        t = t * t * (3 - 2 * t)
        return {ul: 0.5 * t, ll: 1 - 0.5 * t}
    p = Part(name + "_" + side, w)
    pts = [(x * s, 0.0 if z > 0.3 else 0.006, z) for z in zs]
    tube(p, pts, radii, n, sw, up=Vector((0, 1, 0)))
    if sw_under:
        p.recolor(lambda c, nn, cur: nn.z < -0.6, sw_under)
    return p

def boot(side, s, sw, sole_sw, size=1.0, toe_sw=None, cuff_sw=None, h=0.15, x=0.171, round_toe=True):
    ft, ts = "foot." + side, "toes." + side
    k = size
    p = Part("boot_" + side, rigid(ft))
    box(p, (x * s, -0.01 * k, h / 2 + 0.02), (.19 * k, .22 * k, h), sw, 0.045)
    box(p, (x * s, -0.01 * k, 0.016), (.20 * k, .235 * k, .032), sole_sw, 0.01)
    if cuff_sw:
        box(p, (x * s, -0.005, h + 0.02), (.21 * k, .22 * k, .05), cuff_sw, 0.02)
    q = Part("toe_" + side, rigid(ts))
    box(q, (x * s, -0.165 * k, 0.065), (.19 * k, .17 * k, .11), toe_sw or sw, 0.05 if round_toe else 0.02)
    box(q, (x * s, -0.17 * k, 0.016), (.20 * k, .18 * k, .032), sole_sw, 0.01)

def split_skirt(name, spec, n, sw, inner_sw, rim_sw, hem=None, cy=0.0, shell_k=0.9, leg_k=1.0, gap_deg=4.0,
                top_w=None, front_gap_deg=None):
    """coat / skirt split into left and right panels (front and back seams). spec = [(z, rx, ry), ...] top->bottom.
    Each panel blends hips (or top_w) at the top -> its own upper leg toward the hem. n = verts per half ring."""
    out = []
    top_z, bot_z = spec[0][0], spec[-1][0]
    fg = gap_deg if front_gap_deg is None else front_gap_deg
    for side, s in (("l", 1), ("r", -1)):
        ul = "upperleg." + side
        def w(co, ul=ul):
            t = max(0.0, min(1.0, (top_z - co.z) / max(0.05, (top_z - bot_z) * 0.8)))
            t = t * t * (3 - 2 * t) * leg_k
            base = top_w(co) if top_w else {"hips": 1.0}
            o = {k: v * (1 - t) for k, v in base.items()}
            o[ul] = o.get(ul, 0) + t
            return o
        p = Part(name + "_" + side, w)
        rings = []
        for i, (z, rx, ry) in enumerate(spec):
            r = []
            for k in range(n):
                a = math.radians(-90 + fg + (180 - fg - gap_deg) * k / (n - 1))
                co = Vector((s * rx * math.cos(a), cy + ry * math.sin(a), z))
                if i == len(spec) - 1 and hem:
                    co.z -= hem[k % len(hem)]
                r.append(co)
            rings.append(r)
        g, faces, caps = loft(p, rings, sw, closed=False, cap0=False, cap1=False)
        shell(p, faces, lambda co: Vector((co.x * shell_k, cy + (co.y - cy) * shell_k, co.z)), inner_sw, rim_sw)
        out.append(p)
    return out

# ---------------------------------------------------------------- heads
def skull(hc, size=(.56, .52, .52), sw="bone", sw_shade="bone_shade", sock="cavity", eye="glow", eye_r=0.075,
          eye_dx=0.12, jaw=True, teeth=4, power=0.6, eye_shape="round", grin_sw="cavity", tooth_sw="tooth", useg=12, vseg=8,
          fy=None):
    """cute chunky skull: soft-cube cranium, big sockets with glowing eyes, nose hole, stitched grin."""
    h = Part("skull", rigid("head"))
    hc = Vector(hc)
    sbox(h, hc, size, useg, vseg, sw, power=power, taper=lambda zn: (1.0 - 0.10 * max(0, -zn), 1.0 - 0.06 * max(0, -zn)))
    h.recolor(lambda c, n, cur: cur == sw and n.z < -0.5, sw_shade)
    fy = hc.y - size[1] / 2 if fy is None else fy
    ez = hc.z + 0.01
    for sx in (1, -1):
        ell(h, (eye_dx * sx, fy + 0.035, ez), (eye_r * 1.05, 0.05, eye_r * 1.05), 8, 5, sock)
        if eye_shape == "round":
            ell(h, (eye_dx * sx, fy - 0.005, ez), (eye_r * 0.55, 0.025, eye_r * 0.55), 6, 4, eye)
        else:
            ell(h, (eye_dx * sx, fy - 0.005, ez), (eye_r * 0.7, 0.025, eye_r * 0.32), 6, 4, eye, ROT(-12 * sx, 'Y'))
    # nose: small upside-down triangle
    tri = [Vector((-0.025, fy - 0.002, ez - 0.085)), Vector((0.025, fy - 0.002, ez - 0.085)), Vector((0.0, fy - 0.002, ez - 0.13))]
    vs = [h.v(c) for c in tri]
    f = h.face(vs, sock)
    f.normal_update()
    if f.normal.y > 0:
        f.normal_flip()
    # grin: dark bar with tooth ticks
    gz = hc.z - size[2] * 0.30
    box(h, (0, fy + 0.012, gz), (0.24, 0.03, 0.022), grin_sw, 0.0)
    for k in range(teeth + 1):
        x = -0.12 + 0.24 * k / teeth
        box(h, (x, fy + 0.008, gz), (0.012, 0.03, 0.06), grin_sw, 0.0)
    if jaw:
        box(h, (0, hc.y - 0.02, hc.z - size[2] * 0.47), (size[0] * 0.62, size[1] * 0.68, 0.08), sw_shade, 0.03)
    return h

# ---------------------------------------------------------------- more helpers
def striped_hring(z, rx, ry, nst, frac=0.22, cx=0.0, cy=0.0):
    """horizontal ring of 2*nst verts: wide gaps alternate with narrow stripes (face j odd = stripe)"""
    out = []
    step = 2 * math.pi / nst
    for k in range(nst):
        for a in (k * step, k * step + step * (1 - frac)):
            a -= math.pi / 2
            out.append(Vector((cx + rx * math.cos(a), cy + ry * math.sin(a), z)))
    return out

def stripe_faces(p, faces, ncols, sw, rows=None):
    """tag every odd column of a closed loft (faces in row-major order) with the stripe swatch"""
    for i, f in enumerate(faces):
        if (i % ncols) % 2 == 1 and (rows is None or (i // ncols) in rows):
            f[p.sw] = SW[sw]

def leg_weights(side):
    ul, ll = "upperleg." + side, "lowerleg." + side
    def w(co):
        if co.z > 0.36:
            t = min(1.0, (co.z - 0.36) / 0.06)
            return {ul: 1.0} if t >= 1 else {ul: 0.5 + 0.5 * t, ll: 0.5 - 0.5 * t}
        if co.z < 0.24:
            return {ll: 1.0}
        t = (co.z - 0.24) / 0.12
        t = t * t * (3 - 2 * t)
        return {ul: 0.5 * t, ll: 1 - 0.5 * t}
    return w

def leg_loft(side, s, spec, sw, stripe_sw=None, nst=6, n=8, name="trouser", cap1=True, x=0.171):
    """trouser leg from horizontal rings spec = [(z, r, cy), ...] top->bottom, optional pinstripes"""
    p = Part(name + "_" + side, leg_weights(side))
    if stripe_sw:
        rings = [striped_hring(z, r, r * 0.95, nst, cx=x * s, cy=cy) for z, r, cy in spec]
    else:
        rings = [hring(z, r, r * 0.95, n, cx=x * s, cy=cy) for z, r, cy in spec]
    g, faces, caps = loft(p, rings, sw, cap0=True, cap1=cap1)
    if stripe_sw:
        stripe_faces(p, faces, 2 * nst, stripe_sw)
    return p, faces

def hat_brim(p, c, rx, ry, th, n, sw, sw_under=None, tilt=None):
    c = Vector(c)
    r0 = [Vector((c.x + rx * math.cos(2 * math.pi * k / n), c.y + ry * math.sin(2 * math.pi * k / n), c.z)) for k in range(n)]
    r1 = [v + Vector((0, 0, th)) for v in r0]
    g, faces, caps = loft(p, [r0, r1], sw)
    if sw_under:
        caps[0][p.sw] = SW[sw_under]
    return g

def dome(p, c, rx, ry, h, n, rows, sw, top_flat=0.0):
    """half ellipsoid dome on z=c.z, capped"""
    c = Vector(c)
    rings = []
    for i in range(rows + 1):
        a = (math.pi / 2) * i / (rows + 0.6)
        k = math.cos(a)
        rings.append([Vector((c.x + rx * k * math.cos(2 * math.pi * j / n), c.y + ry * k * math.sin(2 * math.pi * j / n), c.z + h * math.sin(a))) for j in range(n)])
    return loft(p, rings, sw, cap0=True, cap1=True)
