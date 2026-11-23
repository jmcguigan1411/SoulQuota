# Labelled contact sheet for a monster model.
# usage: python3 mq_render.py <Model> <out.png> <cols> <tile_px> <action:frame:view[:label]>...
#   action REST = bind pose; views front|side|q34|back|game|low ; env ENGINE=WB for workbench
import bpy, sys, math, os
from mathutils import Vector, Matrix
from PIL import Image, ImageDraw, ImageFont

ANIM_SRC = "/tmp/rq/anims.blend"
args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
model, out, cols, res = args[0], args[1], int(args[2]), int(args[3])
shots = args[4:]
bpy.ops.wm.open_mainfile(filepath="/home/claude/sq/ArtSource/Characters/%s.blend" % model)
sc = bpy.context.scene
sc.render.fps = 30
rig = bpy.data.objects["Rig"]
if rig.animation_data is None:
    rig.animation_data_create()
need = sorted({s.split(":")[0] for s in shots} - {"REST"} - set(a.name for a in bpy.data.actions))
if need:
    with bpy.data.libraries.load(ANIM_SRC, link=False) as (src, dst):
        dst.actions = [n for n in need if n in src.actions]
cam_d = bpy.data.cameras.new("cam"); cam = bpy.data.objects.new("cam", cam_d)
sc.collection.objects.link(cam); sc.camera = cam
for l in list(bpy.data.lights):
    bpy.data.lights.remove(l)
def light(name, rot, energy, color=(1, 1, 1)):
    ld = bpy.data.lights.new(name, 'SUN'); ld.energy = energy; ld.color = color; ld.angle = math.radians(8)
    lo = bpy.data.objects.new(name, ld); sc.collection.objects.link(lo); lo.rotation_euler = rot
light("key", (math.radians(50), 0, math.radians(-35)), 3.2, (1, 0.96, 0.9))
light("rim", (math.radians(-60), 0, math.radians(160)), 2.2, (0.6, 0.8, 1.0))
light("fill", (math.radians(70), 0, math.radians(100)), 0.7, (0.8, 0.8, 1.0))
w = bpy.data.worlds.new("w"); sc.world = w; w.use_nodes = True
w.node_tree.nodes["Background"].inputs[0].default_value = (0.045, 0.045, 0.065, 1)
w.node_tree.nodes["Background"].inputs[1].default_value = 0.6
bpy.ops.mesh.primitive_circle_add(vertices=48, radius=1.8, fill_type='NGON', location=(0, 0, 0))
g = bpy.context.active_object
gm = bpy.data.materials.new("ground"); gm.use_nodes = True
gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.12, 0.12, 0.15, 1)
g.data.materials.append(gm)
sc.render.resolution_x = sc.render.resolution_y = res
if os.environ.get("ENGINE") == "WB":
    sc.render.engine = 'BLENDER_WORKBENCH'
    sh = sc.display.shading; sh.light = 'STUDIO'; sh.color_type = 'TEXTURE'; sh.show_shadows = True
    sh.show_object_outline = True; sh.background_type = 'WORLD'
else:
    sc.render.engine = 'BLENDER_EEVEE'
    try:
        sc.eevee.taa_render_samples = 16
    except Exception:
        pass
    sc.view_settings.view_transform = 'AgX'
    sc.view_settings.look = 'AgX - Medium High Contrast'
H = float(os.environ.get("TGT_Z", "1.0"))
D = float(os.environ.get("DIST", "1.0"))
VIEWS = {
    "front": (Vector((0, -5.6, 1.3)), Vector((0, 0, H)), 62),
    "side": (Vector((5.6, -0.3, 1.3)), Vector((0, 0, H)), 62),
    "q34": (Vector((3.4, -4.4, 2.2)), Vector((0, 0, H)), 62),
    "back": (Vector((-3.0, 4.6, 2.0)), Vector((0, 0, H)), 62),
    "game": (Vector((1.6, -3.8, 6.0)), Vector((0, 0, H * 0.7)), 75),
    "low": (Vector((2.6, -4.6, 0.5)), Vector((0, 0, H * 1.1)), 62),
    "top": (Vector((0.01, -0.6, 6.5)), Vector((0, 0, H)), 55),
}
tmp = "/tmp/rq_tiles_%s" % model; os.makedirs(tmp, exist_ok=True)
tiles = []
for i, shot in enumerate(shots):
    parts = shot.split(":")
    a, fr, view = parts[0], parts[1], parts[2]
    if fr.startswith("p") and a != "REST":
        r0, r1 = bpy.data.actions[a].frame_range
        fr = int(round(r0 + (r1 - r0) * float(fr[1:])))
    else:
        fr = int(fr.lstrip("p") or 1) if not fr.startswith("p") else 1
    label = parts[3] if len(parts) > 3 else (f"{a} f{fr} {view}" if a != "REST" else f"bind pose {view}")
    if a == "REST":
        rig.animation_data.action = None
        for pb in rig.pose.bones:
            pb.matrix_basis = Matrix.Identity(4)
    else:
        act = bpy.data.actions[a]
        rig.animation_data.action = act
        if len(act.slots):
            rig.animation_data.action_slot = act.slots[0]
    sc.frame_set(fr)
    loc, tgt, lens = VIEWS[view]
    loc = tgt + (loc - tgt) * D
    cam.location = loc; cam.rotation_euler = (tgt - loc).to_track_quat('-Z', 'Y').to_euler(); cam_d.lens = lens
    sc.render.filepath = f"{tmp}/t{i:03d}.png"
    bpy.ops.render.render(write_still=True)
    tiles.append((sc.render.filepath, label))
rows = (len(tiles) + cols - 1) // cols
lab = 30
W = Image.new("RGB", (cols * res, rows * (res + lab)), (18, 18, 24))
dr = ImageDraw.Draw(W)
try:
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 16)
except Exception:
    font = ImageFont.load_default()
for i, (p, label) in enumerate(tiles):
    x, y = (i % cols) * res, (i // cols) * (res + lab)
    W.paste(Image.open(p).convert("RGB"), (x, y + lab))
    dr.text((x + 8, y + 6), label, fill=(235, 235, 240), font=font)
W.save(out)
print("SHEET", out, W.size)
