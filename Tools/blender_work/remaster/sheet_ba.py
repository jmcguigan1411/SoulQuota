# Before/after contact sheets: one row per take version, 8 evenly spaced frames (+ marked key frames).
# usage: python3 sheet_ba.py <after.blend> <before.blend|-> <out.png> <view> <spec>...
#   spec = AFTER_TAKE[=BEFORE_TAKE][@k1,k2]  (BEFORE_TAKE defaults to AFTER_TAKE; '-' before blend = after file)
#   frames: 8 evenly spaced over the take; @k frames replace the nearest evenly spaced ones (impacts).
# env MESH=<fbx> renders that body instead of the reaper (e.g. Skeleton_Minion.fbx for enemies).
import bpy, sys, math, os
from mathutils import Vector, Matrix
from PIL import Image, ImageDraw, ImageFont

after, before, out, view = sys.argv[1:5]
specs = sys.argv[5:]
TILE = int(os.environ.get("TILE", "200"))
NF = int(os.environ.get("NF", "8"))
bpy.ops.wm.open_mainfile(filepath=after)
sc = bpy.context.scene
rig = bpy.data.objects["Rig"]
OLD = {}
if before != "-":
    names = set()
    for sp in specs:
        a = sp.split("@")[0]
        names.add(a.split("=")[1] if "=" in a else a)
    with bpy.data.libraries.load(before, link=False) as (src, dst):
        avail = set(src.actions)
        dst.actions = [n for n in names if n in avail]
    for a in dst.actions:
        if a is None:
            continue
        nm = a.name
        base = nm.rsplit(".", 1)[0] if nm[-4:-3] == "." and nm[-3:].isdigit() else nm
        OLD[base] = a
mesh_env = os.environ.get("MESH")
if mesh_env:
    for o in list(bpy.data.objects):
        if o.type == 'MESH':
            o.hide_render = True
    keep = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=mesh_env)
    newo = [o for o in bpy.data.objects if o not in keep]
    nrig = [o for o in newo if o.type == 'ARMATURE'][0]
    for o in newo:
        if o.type == 'MESH':
            for m in o.modifiers:
                if m.type == 'ARMATURE':
                    m.object = rig
            mw = o.matrix_world.copy()
            o.parent = rig if o.parent == nrig else o.parent
            if o.parent_type == 'BONE':
                pass
            o.matrix_world = mw
    # bone-parented props (hats etc.)
    for o in newo:
        if o.type == 'MESH' and o.parent is None:
            pass
    for o in newo:
        if o.type == 'MESH' and o.parent == rig and o.parent_type == 'BONE':
            pb = o.parent_bone
    nrig.hide_render = True
    nrig.hide_viewport = True
    # simplest robust approach: drive the imported rig with the same action instead of re-parenting
    rig_r = nrig
    for o in newo:
        if o.type == 'MESH':
            o.parent = nrig if o.parent == rig else o.parent
            for m in o.modifiers:
                if m.type == 'ARMATURE':
                    m.object = nrig
    RIGS = [nrig]
else:
    RIGS = [rig]
# env SCYTHE=<fbx|blend>: attach the reaper scythe to handslot.r (Copy Transforms = slot frame, pole +Z, blade +Y)
if os.environ.get("SCYTHE"):
    keep = set(bpy.data.objects)
    with bpy.data.libraries.load(os.environ["SCYTHE"], link=False) as (src_, dst_):
        dst_.objects = [n for n in src_.objects]
    for o in dst_.objects:
        if o is not None and o.type == 'MESH':
            sc.collection.objects.link(o)
            o.parent = None
            c = o.constraints.new('COPY_TRANSFORMS'); c.target = RIGS[0]; c.subtarget = "handslot.r"
cam_d = bpy.data.cameras.new("cam"); cam = bpy.data.objects.new("cam", cam_d)
sc.collection.objects.link(cam); sc.camera = cam
sc.render.engine = 'BLENDER_WORKBENCH'
sh = sc.display.shading; sh.light = 'STUDIO'; sh.color_type = 'TEXTURE'; sh.show_shadows = True
sh.show_object_outline = True; sh.background_type = 'VIEWPORT'; sh.background_color = (0.07, 0.07, 0.09)
sh.show_cavity = True
sc.render.resolution_x = sc.render.resolution_y = TILE
sc.render.film_transparent = False
# ground: checker grid (10 cm squares) so foot sliding shows
bpy.ops.mesh.primitive_grid_add(x_subdivisions=40, y_subdivisions=40, size=4.0, location=(0, 0, 0.0))
g = bpy.context.active_object
gm = bpy.data.materials.new("g"); g.data.materials.append(gm); gm.diffuse_color = (0.28, 0.28, 0.32, 1)
bpy.ops.object.modifier_add(type='WIREFRAME'); g.modifiers[-1].thickness = 0.006
bpy.ops.mesh.primitive_plane_add(size=4.0, location=(0, 0, -0.002))
if os.environ.get("NOGROUND"):
    bpy.context.active_object.hide_render = True
p2 = bpy.context.active_object; pm = bpy.data.materials.new("p"); p2.data.materials.append(pm); pm.diffuse_color = (0.13, 0.13, 0.16, 1)
VIEWS = {
    "q34": (Vector((2.6, -3.8, 1.9)), Vector((0, 0, 0.7)), 62),
    "side": (Vector((5.0, -0.25, 0.9)), Vector((0, 0, 0.7)), 66),
    "front": (Vector((0, -5.0, 1.0)), Vector((0, 0, 0.7)), 66),
    "game": (Vector((1.6, -3.6, 5.6)), Vector((0, 0, 0.6)), 55),
}
loc, tgt, lens = VIEWS[view]
cam.location = loc; cam.rotation_euler = (tgt - loc).to_track_quat('-Z', 'Y').to_euler(); cam_d.lens = lens
tmp = "/tmp/sq_ba_tiles_%d" % os.getpid(); os.makedirs(tmp, exist_ok=True)

def frames_for(act, keys):
    n = int(act.frame_range[1])
    fr = [1 + round(i * (n - 1) / (NF - 1)) for i in range(NF)]
    if act.get("sq_loop"):
        fr = [1 + round(i * (n - 1) / NF) for i in range(NF)]
    for k in keys:
        j = min(range(NF), key=lambda i: abs(fr[i] - k))
        fr[j] = k
    return sorted(set(fr)), n

rows = []
for sp in specs:
    keys = []
    if "@" in sp:
        sp, kk = sp.split("@")
        keys = [int(x) for x in kk.split(",") if x]
    an, bn = (sp.split("=") + [None])[:2]
    bn = bn or an
    versions = []
    if before != "-" and bn in OLD:
        versions.append(("BEFORE " + bn, OLD[bn]))
    elif before == "-" and bn != an and bn in bpy.data.actions:
        versions.append(("BEFORE " + bn, bpy.data.actions[bn]))
    versions.append(("AFTER " + an, bpy.data.actions[an]))
    for label, act in versions:
        frs, n = frames_for(act, keys)
        imgs = []
        for fr in frs:
            for r in RIGS:
                r.animation_data_create()
                r.animation_data.action = act
                if len(act.slots):
                    r.animation_data.action_slot = act.slots[0]
            sc.frame_set(fr)
            pth = f"{tmp}/r{len(rows):03d}_{fr:03d}.png"
            sc.render.filepath = pth
            bpy.ops.render.render(write_still=True)
            imgs.append((pth, fr))
        rows.append((label, n, imgs, keys))
cols = max(len(r[2]) for r in rows)
lab = 22
W = Image.new("RGB", (cols * TILE, len(rows) * (TILE + lab)), (16, 16, 20))
dr = ImageDraw.Draw(W)
try:
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 13)
except Exception:
    font = ImageFont.load_default()
for ri, (label, n, imgs, keys) in enumerate(rows):
    y = ri * (TILE + lab)
    col = (255, 190, 120) if label.startswith("BEFORE") else (140, 230, 160)
    dr.rectangle([0, y, cols * TILE, y + lab], fill=(30, 30, 38) if label.startswith("BEFORE") else (24, 40, 30))
    dr.text((6, y + 4), f"{label}  ({n} f)", fill=col, font=font)
    for ci, (p, fr) in enumerate(imgs):
        W.paste(Image.open(p).convert("RGB"), (ci * TILE, y + lab))
        dr.text((ci * TILE + 4, y + lab + 3), f"f{fr}" + (" *" if fr in keys else ""), fill=(255, 255, 120) if fr in keys else (220, 220, 220), font=font)
W.save(out)
print("SHEET", out, W.size)
