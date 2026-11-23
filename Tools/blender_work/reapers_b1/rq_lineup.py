# All batch-1 reapers side by side at the same scale (bind pose, Idle frame 1), Eevee.
import bpy, math
from mathutils import Vector
from PIL import Image, ImageDraw, ImageFont
CH = "/home/claude/sq/ArtSource/Characters/"
IDS = ["Grim", "Mort", "Widow", "Bonesmith", "Keeper", "Monk", "Intern"]
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
with bpy.data.libraries.load("/home/claude/blender_work/reapers_b1/rig_anims.blend", link=False) as (src, dst):
    dst.actions = ["Idle"]
idle = bpy.data.actions["Idle"]
X = {}
for i, mid in enumerate(IDS):
    with bpy.data.libraries.load(CH + "R_%s.blend" % mid, link=False) as (src, dst):
        dst.objects = list(src.objects)
    rig = body = None
    for o in dst.objects:
        sc.collection.objects.link(o)
        if o.type == 'ARMATURE': rig = o
        else: body = o
    x = (i - 3) * 1.25
    emp = bpy.data.objects.new("off_" + mid, None); sc.collection.objects.link(emp)
    emp.location.x = x
    rig.parent = emp
    X[mid] = x
    rig.animation_data_create(); rig.animation_data.action = idle
    if len(idle.slots): rig.animation_data.action_slot = idle.slots[0]
sc.frame_set(1)
def light(name, rot, energy, color):
    ld = bpy.data.lights.new(name, 'SUN'); ld.energy = energy; ld.color = color; ld.angle = math.radians(8)
    lo = bpy.data.objects.new(name, ld); sc.collection.objects.link(lo); lo.rotation_euler = rot
light("key", (math.radians(50), 0, math.radians(-25)), 3.2, (1, 0.96, 0.9))
light("rim", (math.radians(-60), 0, math.radians(160)), 2.2, (0.6, 0.8, 1.0))
light("fill", (math.radians(70), 0, math.radians(100)), 0.7, (0.8, 0.8, 1.0))
w = bpy.data.worlds.new("w"); sc.world = w; w.use_nodes = True
w.node_tree.nodes["Background"].inputs[0].default_value = (0.045, 0.045, 0.065, 1)
w.node_tree.nodes["Background"].inputs[1].default_value = 0.6
bpy.ops.mesh.primitive_plane_add(size=80)
gm = bpy.data.materials.new("g"); gm.use_nodes = True
gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.12, 0.12, 0.15, 1)
bpy.context.active_object.data.materials.append(gm)
# 1 m scale bar ticks
cam_d = bpy.data.cameras.new("cam"); cam_d.type = 'ORTHO'; cam_d.ortho_scale = 9.4
cam = bpy.data.objects.new("cam", cam_d); sc.collection.objects.link(cam); sc.camera = cam
cam.location = (0, -12, 2.6); cam.rotation_euler = (math.radians(84), 0, 0)
sc.render.resolution_x, sc.render.resolution_y = 2400, 900
sc.render.engine = 'BLENDER_EEVEE'
sc.eevee.taa_render_samples = 16
sc.view_settings.view_transform = 'AgX'; sc.view_settings.look = 'AgX - Medium High Contrast'
out = "/home/claude/sq/ArtSource/Previews/Reapers/Batch1_Lineup.png"
sc.render.filepath = "/tmp/lineup_raw.png"
bpy.ops.render.render(write_still=True)
im = Image.open("/tmp/lineup_raw.png").convert("RGB")
dr = ImageDraw.Draw(im)
font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 26)
for mid in IDS:
    px = 1200 + X[mid] / 9.4 * 2400
    dr.text((px - 13 * len(mid) / 1.6, 840), mid, fill=(235, 235, 240), font=font)
dr.text((20, 20), "Soul Quota reapers, batch 1 (same scale, Idle f1, orthographic)", fill=(235, 235, 240), font=font)
im.save(out)
print("LINEUP", out)
