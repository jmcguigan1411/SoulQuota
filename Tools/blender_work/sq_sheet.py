# Render labelled contact sheets of actions on the custom reaper.
# usage: python3 sq_sheet.py <out.png> <cols> <tile_px> <view> <action:frame[:label]>...
#   view: q34 | side | front | game ; engine EEVEE (env ENGINE=WB for workbench)
import bpy, sys, math, os
from mathutils import Vector, Matrix
from PIL import Image, ImageDraw, ImageFont

out, cols, res, view = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
shots = sys.argv[5:]
bpy.ops.wm.open_mainfile(filepath="/home/claude/sq/ArtSource/Characters/Reaper_Custom.blend")
sc = bpy.context.scene
rig = bpy.data.objects["Rig"]
cam_d = bpy.data.cameras.new("cam"); cam = bpy.data.objects.new("cam", cam_d)
sc.collection.objects.link(cam); sc.camera = cam
for l in list(bpy.data.lights):
    bpy.data.lights.remove(l)
def light(name, rot, energy, color=(1, 1, 1)):
    ld = bpy.data.lights.new(name, 'SUN'); ld.energy = energy; ld.color = color; ld.angle = math.radians(8)
    lo = bpy.data.objects.new(name, ld); sc.collection.objects.link(lo); lo.rotation_euler = rot
light("key", (math.radians(50), 0, math.radians(-35)), 3.2, (1, 0.96, 0.9))
light("rim", (math.radians(-60), 0, math.radians(160)), 2.2, (0.6, 0.8, 1.0))
light("fill", (math.radians(70), 0, math.radians(100)), 0.6, (0.8, 0.8, 1.0))
w = bpy.data.worlds.new("w"); sc.world = w; w.use_nodes = True
w.node_tree.nodes["Background"].inputs[0].default_value = (0.045, 0.045, 0.065, 1)
w.node_tree.nodes["Background"].inputs[1].default_value = 0.6
# ground grid disc with stripes to read foot contact
bpy.ops.mesh.primitive_circle_add(vertices=48, radius=1.6, fill_type='NGON', location=(0, 0, 0))
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
VIEWS = {
    "q34": (Vector((3.0, -4.2, 2.3)), Vector((0, 0, 0.72)), 80),
    "side": (Vector((5.4, -0.6, 1.0)), Vector((0, 0, 0.72)), 80),
    "front": (Vector((0, -5.4, 1.2)), Vector((0, 0, 0.72)), 80),
    "game": (Vector((1.4, -3.4, 5.6)), Vector((0, 0, 0.6)), 85),
    "sheet": (Vector((3.0, -4.2, 2.1)), Vector((0, 0, 0.82)), 62),
}
loc, tgt, lens = VIEWS[view]
cam.location = loc; cam.rotation_euler = (tgt - loc).to_track_quat('-Z', 'Y').to_euler(); cam_d.lens = lens
tmp = "/tmp/sq_sheet_tiles"; os.makedirs(tmp, exist_ok=True)
tiles = []
for i, shot in enumerate(shots):
    parts = shot.split(":")
    a, fr = parts[0], int(parts[1])
    label = parts[2] if len(parts) > 2 else f"{a} f{fr}"
    act = bpy.data.actions[a]
    rig.animation_data.action = act
    rig.animation_data.action_slot = act.slots[0]
    sc.frame_set(fr)
    sc.render.filepath = f"{tmp}/t{i:03d}.png"
    bpy.ops.render.render(write_still=True)
    tiles.append((sc.render.filepath, label))
rows = (len(tiles) + cols - 1) // cols
lab = 34
W = Image.new("RGB", (cols * res, rows * (res + lab)), (18, 18, 24))
dr = ImageDraw.Draw(W)
try:
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 18)
except Exception:
    font = ImageFont.load_default()
for i, (p, label) in enumerate(tiles):
    x, y = (i % cols) * res, (i // cols) * (res + lab)
    W.paste(Image.open(p).convert("RGB"), (x, y + lab))
    dr.text((x + 8, y + 7), label, fill=(235, 235, 240), font=font)
W.save(out)
print("SHEET", out, W.size)
