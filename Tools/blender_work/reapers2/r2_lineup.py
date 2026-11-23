# Batch-2 lineup: all 7 bodies side by side, front 3/4 row + top-down game-camera row.
import bpy, math, os
from mathutils import Vector
from PIL import Image, ImageDraw, ImageFont
CH = "/home/claude/sq/ArtSource/Characters/"
IDS = ["Femur", "Digby", "Collector", "Beak", "Tallow", "Lookout", "Vexa"]
NAMES = ["Femur the Hound", "Digby Sixfeet", "The Debt Collector", "Doctor Beak", "Tallow", "The Graveyard Lookout", "Vexa the Veiled"]
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sp = 1.5
for i, rid in enumerate(IDS):
    with bpy.data.libraries.load(CH + "R_%s.blend" % rid, link=False) as (src, dst):
        dst.objects = [n for n in src.objects]
    for o in dst.objects:
        sc.collection.objects.link(o)
        if o.type == 'ARMATURE':
            o.location.x = (i - 3) * sp
for l in list(bpy.data.lights): bpy.data.lights.remove(l)
def light(name, rot, energy, color=(1, 1, 1)):
    ld = bpy.data.lights.new(name, 'SUN'); ld.energy = energy; ld.color = color; ld.angle = math.radians(8)
    lo = bpy.data.objects.new(name, ld); sc.collection.objects.link(lo); lo.rotation_euler = rot
light("key", (math.radians(50), 0, math.radians(-35)), 3.2, (1, 0.96, 0.9))
light("rim", (math.radians(-60), 0, math.radians(160)), 2.2, (0.6, 0.8, 1.0))
light("fill", (math.radians(70), 0, math.radians(100)), 0.9, (0.8, 0.8, 1.0))
w = bpy.data.worlds.new("w"); sc.world = w; w.use_nodes = True
w.node_tree.nodes["Background"].inputs[0].default_value = (0.045, 0.045, 0.065, 1)
w.node_tree.nodes["Background"].inputs[1].default_value = 0.6
bpy.ops.mesh.primitive_plane_add(size=1, location=(0, 0, 0)); g = bpy.context.active_object; g.scale = (14, 6, 1)
gm = bpy.data.materials.new("ground"); gm.use_nodes = True
gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.12, 0.12, 0.15, 1); g.data.materials.append(gm)
sc.render.engine = 'BLENDER_EEVEE'; sc.eevee.taa_render_samples = 16
sc.view_settings.view_transform = 'AgX'; sc.view_settings.look = 'AgX - Medium High Contrast'
cam_d = bpy.data.cameras.new("cam"); cam = bpy.data.objects.new("cam", cam_d); sc.collection.objects.link(cam); sc.camera = cam
outs = []
for k, (loc, tgt, ortho, res) in enumerate((((0.8, -14, 2.4), (0, 0, 1.0), 11.2, (2400, 640)), ((0.6, -5.5, 14), (0, 0, 0.6), 11.2, (2400, 760)))):
    cam_d.type = 'ORTHO'; cam_d.ortho_scale = ortho
    cam.location = loc; cam.rotation_euler = (Vector(tgt) - Vector(loc)).to_track_quat('-Z', 'Y').to_euler()
    sc.render.resolution_x, sc.render.resolution_y = res
    sc.render.filepath = "/tmp/r2/lineup_%d.png" % k
    bpy.ops.render.render(write_still=True); outs.append(sc.render.filepath)
a, b = Image.open(outs[0]).convert("RGB"), Image.open(outs[1]).convert("RGB")
W = Image.new("RGB", (2400, 60 + a.height + 40 + b.height), (18, 18, 24)); dr = ImageDraw.Draw(W)
f = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 22)
fs = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 18)
dr.text((16, 16), "Soul Quota reapers, batch 2 (front, ortho)", fill=(235, 235, 240), font=f)
W.paste(a, (0, 60))
for i, n in enumerate(NAMES):
    x = 2400 / 2 + (i - 3) * sp * 2400 / 11.2
    tw = dr.textlength(n, font=fs); dr.text((x - tw / 2, 60 + a.height - 34), n, fill=(235, 235, 240), font=fs)
dr.text((16, 60 + a.height + 8), "top-down game camera", fill=(235, 235, 240), font=f)
W.paste(b, (0, 60 + a.height + 40))
W.save("/home/claude/sq/ArtSource/Previews/Reapers/Batch2_Lineup.png"); print("LINEUP done", W.size)
