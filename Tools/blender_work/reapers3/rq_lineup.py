# Batch 3 lineup: the seven reapers side by side (hero 3/4 view on top, top-down game camera below).
# usage: python3 rq_lineup.py out.png   (env ENGINE=WB for workbench)
import bpy, sys, math, os
from mathutils import Vector
from PIL import Image, ImageDraw, ImageFont

IDS = ["Actuary", "Sweep", "Solace", "Pallbearers", "Ectoplasm", "Neckless", "Tickwell"]
NAMES = ["The Actuary", "The Chimney Sweep", "Sister Solace", "The Pallbearers", "Madame Ectoplasm", "Sir Neckless", "Old Tickwell"]
IDLE = {"Actuary": "SQ_Idle_Remaster", "Sweep": "SQ_Idle_Remaster", "Solace": "SQ_Idle_Proud", "Pallbearers": "SQ_Idle_Hunched",
        "Ectoplasm": "SQ_Idle_Float", "Neckless": "SQ_Idle_Proud", "Tickwell": "SQ_Idle_Hunched"}
out = sys.argv[-1]
CH = "/home/claude/sq/ArtSource/Characters/"
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.render.fps = 30
with bpy.data.libraries.load("/tmp/rq/anims.blend", link=False) as (src, dst):
    dst.actions = sorted(set(IDLE.values()))
SP = 1.55
x0 = -SP * (len(IDS) - 1) / 2
for i, rid in enumerate(IDS):
    with bpy.data.libraries.load(CH + "R_%s.blend" % rid, link=False) as (src, dst):
        dst.objects = [n for n in src.objects]
    rig = body = None
    for o in dst.objects:
        sc.collection.objects.link(o)
        if o.type == 'ARMATURE':
            rig = o
        else:
            body = o
    for m in body.modifiers:
        if m.type == 'ARMATURE':
            m.object = rig
    em = bpy.data.objects.new('slot_%d' % i, None); sc.collection.objects.link(em)
    em.location.x = x0 + i * SP
    rig.parent = em
    rig.animation_data_create()
    act = bpy.data.actions[IDLE[rid]]
    rig.animation_data.action = act
    if len(act.slots):
        rig.animation_data.action_slot = act.slots[0]
sc.frame_set(12)
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
bpy.ops.mesh.primitive_plane_add(size=1, location=(0, 0, 0))
g = bpy.context.active_object
g.scale = (14, 8, 1)
gm = bpy.data.materials.new("ground"); gm.use_nodes = True
gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.13, 0.12, 0.15, 1)
g.data.materials.append(gm)
cam_d = bpy.data.cameras.new("cam"); cam = bpy.data.objects.new("cam", cam_d)
sc.collection.objects.link(cam); sc.camera = cam
if os.environ.get("ENGINE") == "WB":
    sc.render.engine = 'BLENDER_WORKBENCH'
    sh = sc.display.shading; sh.light = 'STUDIO'; sh.color_type = 'TEXTURE'; sh.show_shadows = True
else:
    sc.render.engine = 'BLENDER_EEVEE'
    try:
        sc.eevee.taa_render_samples = 24
    except Exception:
        pass
    sc.view_settings.view_transform = 'AgX'
    sc.view_settings.look = 'AgX - Medium High Contrast'
shots = []
for nm, loc, tgt, lens, res in (("hero", (2.2, -13.0, 2.8), (0, 0, 1.0), 40, (2400, 760)),
                                ("game", (0.0, -5.0, 11.5), (0, 0, 0.6), 34, (2400, 700))):
    cam.location = Vector(loc)
    cam.rotation_euler = (Vector(tgt) - Vector(loc)).to_track_quat('-Z', 'Y').to_euler()
    cam_d.lens = lens
    sc.render.resolution_x, sc.render.resolution_y = res
    sc.render.filepath = "/tmp/rq/lineup_%s.png" % nm
    bpy.ops.render.render(write_still=True)
    shots.append(sc.render.filepath)
a, b = Image.open(shots[0]).convert("RGB"), Image.open(shots[1]).convert("RGB")
W = Image.new("RGB", (a.width, a.height + b.height + 80), (18, 18, 24))
W.paste(a, (0, 40)); W.paste(b, (0, a.height + 80))
dr = ImageDraw.Draw(W)
try:
    f = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 22)
except Exception:
    f = ImageFont.load_default()
dr.text((12, 8), "Soul Quota reapers, batch 3 (idle, frame 12) - hero view", fill=(235, 235, 240), font=f)
dr.text((12, a.height + 48), "top-down game camera", fill=(235, 235, 240), font=f)
# names under the hero row, spaced by projecting each reaper's x position
cam.location = Vector((2.2, -13.0, 2.8)); cam.rotation_euler = (Vector((0, 0, 1.0)) - cam.location).to_track_quat('-Z', 'Y').to_euler()
cam_d.lens = 40; sc.render.resolution_x, sc.render.resolution_y = (2400, 760)
from bpy_extras.object_utils import world_to_camera_view
bpy.context.view_layer.update()
for i, nm in enumerate(NAMES):
    p = world_to_camera_view(sc, cam, Vector((x0 + i * SP, -0.6, 0.0)))
    tx = int(p.x * 2400)
    tw = dr.textlength(nm, font=f)
    dr.text((tx - tw / 2, 40 + 760 - 36), nm, fill=(235, 235, 240), font=f)
W.save(out)
print("LINEUP", out, W.size)
