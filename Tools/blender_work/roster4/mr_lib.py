# Soul Quota monster roster (Nov 2026): original monster bodies on the unchanged Reaper rig + one weapon mesh each.
# Body: ArtSource/Characters/R_<Id>.fbx (mesh only, Reaper skeleton). Weapon: ArtSource/Characters/W_<Id>.fbx
# (static, pivot at the grip, pole along +Z, head toward +Y), UV'd to the same r_<id>_palette.png.
# Swatch convention here: "bone" = the character's skin (hands use it), "bone_shade" = skin shadow.
import bpy, math, sys, os
sys.path.insert(0, "/home/claude/blender_work/reapers3")
sys.path.insert(0, "/home/claude/blender_work/monsters")
from mathutils import Vector, Matrix
import mq_lib
from rq_lib import *

def torso(spec, sw, shade=None, n=12, name="torso", wfn=None):
    t = Part(name, wfn or TORSO_W)
    g, faces, caps = loft(t, [hring(z, rx, ry, n, cy=cy) for z, rx, ry, *rest in spec for cy in [rest[0] if rest else 0.0]], sw)
    if shade:
        t.recolor(lambda c, nn, cur: cur == sw and nn.z < -0.5, shade)
    return t, faces

def legs(sw, r=(.10, .095, .085, .08), shoe="shoe", sole="sole", scale=1.0, cuff_z=0.18, boot=False, x=0.171, ankle=False):
    for side, s in (("l", 1), ("r", -1)):
        leg(side, s, sw, r=r, cuff_z=cuff_z, shoe_sw=shoe, sole_sw=sole, shoe_scale=scale, ankle=ankle, boot=boot, x=x)

def arms(sw, rad=(.09, .086, .082, .08, .076), hand_sw="bone", hs=1.3, cuff=None, x1=0.76):
    for side, s in (("l", 1), ("r", -1)):
        arm(side, s, rad=rad, sleeve_sw=sw, hand_sw=hand_sw, hand_scale=hs, cuff_sw=cuff, bony_wrist=False, x1=x1)

def claw_hand(side, s, sw, nail="extra5", hs=1.35):
    """furry/monster hand with long pointed nails"""
    h = Part("claw_" + side, rigid("hand." + side))
    px = 0.855
    box(h, (px * s, 0, ZC - 0.005), (.14 * hs, .16 * hs, .09 * hs), sw, 0.03)
    for y in (-0.055, 0.0, 0.055):
        a = Vector(((px + 0.06 * hs) * s, y * hs, ZC))
        cone(h, a, a + Vector((0.13 * s, 0, -0.04)), .03 * hs, 5, nail)
    a = Vector(((px - 0.01) * s, -0.09 * hs, ZC))
    cone(h, a, a + Vector((0.06 * s, -0.08, -0.03)), .03 * hs, 5, nail)

def floating_gown(spec, sw, dark=None, inner="cloth_inner", rim=None, n=16, wave=0.035, name="gown"):
    def gown_w(co):
        base = TORSO_W(co) if co.z > 0.60 else {"hips": 1.0}
        t = max(0.0, min(1.0, (0.42 - co.z) / 0.34))
        if t <= 0:
            return base
        side = "upperleg.l" if co.x > 0 else "upperleg.r"
        k = min(1.0, abs(co.x) / 0.12)
        w = {b: v * (1 - 0.35 * t * k) for b, v in base.items()}
        w[side] = w.get(side, 0) + 0.35 * t * k
        return w
    g_ = Part(name, gown_w)
    rings = [hring(z, rx, ry, n) for z, rx, ry in spec]
    wv = [0.0, wave, 0.0, -wave] * (n // 4)
    rings[-1] = [co + Vector((0, 0, wv[k])) for k, co in enumerate(rings[-1])]
    g, faces, caps = loft(g_, rings, sw, cap0=True, cap1=False)
    if dark:
        g_.tag(faces[-n:], dark)
    shell(g_, faces[-3 * n:], lambda co: Vector((co.x * .9, co.y * .9, co.z + 0.02)), inner, rim or (dark or sw))
    return g_, rings, gown_w

def skirt(spec, sw, dark=None, inner="cloth_inner", n=14, leg_max=0.55, wave=0.03, name="skirt"):
    z_top, z_bot = spec[0][0], spec[-1][0]
    p = Part(name, skirt_w(z_top, z_bot, leg_max=leg_max))
    rings = [hring(z, rx, ry, n) for z, rx, ry in spec]
    wv = [0.0, wave, 0.0, -wave] * (n // 4 + 1)
    rings[-1] = [co + Vector((0, 0, wv[k])) for k, co in enumerate(rings[-1])]
    g, faces, caps = loft(p, rings, sw, cap0=False, cap1=False)
    if dark:
        p.tag(faces[-n:], dark)
    shell(p, faces, lambda co: Vector((co.x * .94, co.y * .94, co.z)), inner, dark or sw)
    return p

def cape(z_top=1.24, z_bot=0.55, w_top=.24, w_bot=.40, sw="shirt", dark="shirt_dark", back=0.12, n=9, jag=0.06, name="cape"):
    cp = Part(name, TORSO_W)
    rows = []
    for z, rx, ry, cy in ((z_top, w_top, .18, 0.08), ((z_top + z_bot) / 2, (w_top + w_bot) / 2 + 0.04, .28, 0.10), (z_bot, w_bot, .30, 0.12)):
        rows.append([Vector((rx * math.cos(math.pi * k / (n - 1)), cy + ry * math.sin(math.pi * k / (n - 1)) + back, z)) for k in range(n)])
    rows[-1] = [co - Vector((0, 0, (jag if k % 2 else 0.0))) for k, co in enumerate(rows[-1])]
    g, faces, caps = loft(cp, rows, sw, closed=False)
    shell(cp, faces, lambda co: Vector((co.x * .97, co.y - 0.02, co.z)), dark, dark)
    return cp

def eye(p, c, r, white="paper", pupil="ink", pr=0.45, look=(0, 0)):
    c = Vector(c)
    ell(p, c, (r, r * 0.55, r * 1.1), 8, 5, white)
    ell(p, c + Vector((look[0] * r * 0.3, -r * 0.45, look[1] * r * 0.3)), (r * pr, r * 0.25, r * pr * 1.1), 6, 4, pupil)

def neck_stub(r=0.07, top=1.42, sw="bone"):
    neck(r, top, sw)

def mouth_slab(p, c, w, h, sw="socket", teeth=None, n_teeth=4, fangs=False, depth=0.02):
    c = Vector(c)
    pts = [c + Vector((-w / 2, 0, 0)), c + Vector((-w / 4, 0, -h * 0.8)), c + Vector((w / 4, 0, -h * 0.8)), c + Vector((w / 2, 0, 0)), c + Vector((0, 0, -h * 0.15))]
    slab(p, pts, (0, depth, 0), sw)
    if teeth:
        for k in range(n_teeth):
            x = (k - (n_teeth - 1) / 2) * w / (n_teeth + 0.5)
            if fangs and 0 < k < n_teeth - 1:
                continue
            cone(p, c + Vector((x, -0.004, -0.005)), c + Vector((x, -0.004, -h * (0.7 if fangs else 0.4))), w * 0.07, 4, teeth)

# ---------------------------------------------------------------- build + export
WPARTS = []

def build_monster(rid, palette, builder, weapon=None):
    set_layout()
    rig = load_reaper_rig()
    model = "R_" + rid
    img = make_palette(model, palette, CH + model.lower() + "_palette.png")
    mats = make_materials(model, img)
    builder()
    body = assemble(model, rig, mats)
    zs = [(body.matrix_world @ v.co).z for v in body.data.vertices]
    h = 100 * (max(zs) - min(zs))
    print("MODEL", model, "TRIS", tri_count(body), "UNWEIGHTED", check_weights(body), "HEIGHT_CM %.1f" % h, "MINZ %.3f" % min(zs),
          "ARTSCALE %.2f" % (h / 205.0))
    img.filepath = "//" + model.lower() + "_palette.png"
    bpy.ops.wm.save_as_mainfile(filepath=CH + model + ".blend", relative_remap=True)
    export_fbx(CH + model + ".fbx", [rig, body], anims=False)
    if weapon:
        mq_lib.PARTS.clear()
        weapon()
        objs = [part_to_object(p, mats) for p in mq_lib.PARTS]
        mq_lib.PARTS.clear()
        for o in objs:
            o.vertex_groups.clear()
        bpy.ops.object.select_all(action='DESELECT')
        for o in objs:
            o.select_set(True)
        bpy.context.view_layer.objects.active = objs[0]
        bpy.ops.object.join()
        w = bpy.context.view_layer.objects.active
        w.name = "W_" + rid
        bpy.ops.object.shade_flat()
        ws = [v.co for v in w.data.vertices]
        L = max(max(c[i] for c in ws) - min(c[i] for c in ws) for i in range(3))
        print("WEAPON W_%s TRIS %d LONGEST_M %.2f MINZ %.2f" % (rid, tri_count(w), L, min(c.z for c in ws)))
        export_fbx(CH + "W_" + rid + ".fbx", [w], anims=False)
        bpy.ops.wm.save_as_mainfile(filepath=CH + "W_" + rid + ".blend", relative_remap=True)
    return body

def wpart(name):
    return Part(name, lambda co: {})
