# usage: python3 sq_preview.py <blend> <out_prefix> <engine:WB|EEVEE> <res> <shot>...
# shot = action:frame:view  (action "REST" = T-pose rest, view in front|q34|game|side|back)
import bpy, sys, math
from mathutils import Vector, Matrix

blend, outp, eng, res = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
shots = sys.argv[5:]
bpy.ops.wm.open_mainfile(filepath=blend)
sc = bpy.context.scene
rig = bpy.data.objects["Rig"]

# optional scythe
scy = bpy.data.objects.get("Reaper_Scythe")
if scy is None:
    try:
        with bpy.data.libraries.load("/home/claude/sq/ArtSource/Characters/Reaper_Scythe.blend") as (src, dst):
            dst.objects = ["Reaper_Scythe"]
        scy = dst.objects[0]
        sc.collection.objects.link(scy)
    except Exception as e:
        print("no scythe", e)
        scy = None

clip = None
try:
    with bpy.data.libraries.load("/home/claude/sq/ArtSource/Characters/Reaper_Clipboard.blend") as (src, dst):
        dst.objects = ["Reaper_Clipboard"]
    clip = dst.objects[0]
    sc.collection.objects.link(clip)
    clip.hide_render = True
except Exception as e:
    print("no clip", e)

import os
if os.environ.get("TINT"):
    tint = [float(x) for x in os.environ["TINT"].split(",")] + [1.0]
    for m in bpy.data.materials:
        if not m.use_nodes or not m.name.startswith("M_Reaper"):
            continue
        nt = m.node_tree
        tex = [n for n in nt.nodes if n.type == 'TEX_IMAGE'][0]
        mul = nt.nodes.new("ShaderNodeMix"); mul.data_type = 'RGBA'; mul.blend_type = 'MULTIPLY'
        mul.inputs[0].default_value = 1.0
        mul.inputs[7].default_value = tint
        nt.links.new(tex.outputs["Color"], mul.inputs[6])
        for l in list(tex.outputs["Color"].links):
            if l.to_node != mul:
                nt.links.new(mul.outputs[2], l.to_socket)

cam_d = bpy.data.cameras.new("cam")
cam = bpy.data.objects.new("cam", cam_d)
sc.collection.objects.link(cam)
sc.camera = cam
for l in list(bpy.data.lights):
    bpy.data.lights.remove(l)
def light(name, rot, energy, color=(1, 1, 1)):
    ld = bpy.data.lights.new(name, 'SUN'); ld.energy = energy; ld.color = color
    lo = bpy.data.objects.new(name, ld); sc.collection.objects.link(lo); lo.rotation_euler = rot
    ld.angle = math.radians(8)
light("key", (math.radians(50), 0, math.radians(-35)), 3.2, (1, 0.96, 0.9))
light("rim", (math.radians(-60), 0, math.radians(160)), 2.2, (0.6, 0.8, 1.0))
light("fill", (math.radians(70), 0, math.radians(100)), 0.6, (0.8, 0.8, 1.0))
w = bpy.data.worlds.new("w"); sc.world = w
w.use_nodes = True
w.node_tree.nodes["Background"].inputs[0].default_value = (0.045, 0.045, 0.065, 1)
w.node_tree.nodes["Background"].inputs[1].default_value = 0.6

# ground disc
bpy.ops.mesh.primitive_circle_add(vertices=48, radius=1.4, fill_type='NGON', location=(0, 0, 0))
g = bpy.context.active_object
gm = bpy.data.materials.new("ground"); gm.use_nodes = True
gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.09, 0.09, 0.12, 1)
g.data.materials.append(gm)

sc.render.resolution_x = sc.render.resolution_y = res
sc.render.film_transparent = False
if eng == "WB":
    sc.render.engine = 'BLENDER_WORKBENCH'
    sh = sc.display.shading
    sh.light = 'STUDIO'; sh.color_type = 'TEXTURE'; sh.show_shadows = True
    sh.show_cavity = True; sh.cavity_type = 'WORLD'
    sh.show_object_outline = True
    sh.background_type = 'WORLD'
else:
    sc.render.engine = 'BLENDER_EEVEE'
    try:
        sc.eevee.taa_render_samples = 32
    except Exception:
        pass
    sc.view_settings.view_transform = 'AgX'
    sc.view_settings.look = 'AgX - Medium High Contrast'

VIEWS = {
    "front": (Vector((0, -5.2, 1.15)), Vector((0, 0, 1.0)), 50),
    "q34": (Vector((3.3, -4.1, 2.2)), Vector((0, 0, 0.98)), 50),
    "side": (Vector((5.2, 0, 1.15)), Vector((0, 0, 1.0)), 50),
    "back": (Vector((-2.8, 4.4, 1.8)), Vector((0, 0, 1.0)), 50),
    "game": (Vector((2.4, -5.2, 7.6)), Vector((0, 0, 0.8)), 85),
    "wide": (Vector((3.8, -5.0, 2.6)), Vector((0, 0, 1.1)), 50),
    "close": (Vector((1.0, -2.4, 2.3)), Vector((0, 0, 1.0)), 50),
}

def set_action(name):
    if name == "REST":
        rig.animation_data.action = None
        for pb in rig.pose.bones:
            pb.matrix_basis = Matrix.Identity(4)
        return
    act = bpy.data.actions.get(name) or bpy.data.actions.get(name.replace("Rig|", ""))
    rig.animation_data.action = act
    if len(act.slots):
        rig.animation_data.action_slot = act.slots[0]

def attach_scythe(on):
    if scy is None:
        return
    scy.hide_render = not on
    if on:
        scy.parent = rig
        scy.parent_type = 'BONE'
        scy.parent_bone = "handslot.r"
        scy.matrix_parent_inverse = Matrix.Identity(4)
        scy.location = (0, -rig.data.bones["handslot.r"].length, 0)
        scy.rotation_euler = (0, 0, 0)

for shot in shots:
    a, fr, view = shot.split(":")
    weapon = view.endswith("+s")
    withclip = view.endswith("+c")
    view = view.replace("+s", "").replace("+c", "")
    if clip is not None:
        clip.hide_render = not withclip
        if withclip:
            clip.parent = rig; clip.parent_type = 'BONE'; clip.parent_bone = "handslot.l"
            clip.matrix_parent_inverse = Matrix.Identity(4)
            clip.location = (0, -rig.data.bones["handslot.l"].length, 0)
    if a == "SCYTHE":
        rig.hide_render = True
        for o in bpy.data.objects:
            if o.parent == rig and o.type == 'MESH':
                o.hide_render = True
        scy.parent = None
        scy.matrix_world = Matrix.Translation((0, 0, 0.5))
        scy.hide_render = False
        cam.location = Vector((3.6, -3.6, 1.6)) if view != "front" else Vector((0, -4.5, 1.0))
        tgt = Vector((0, 0, 0.95))
        cam.rotation_euler = (tgt - cam.location).to_track_quat('-Z', 'Y').to_euler()
        cam_d.lens = 50
    else:
        rig.hide_render = False
        for o in bpy.data.objects:
            if o.parent == rig and o.type == 'MESH' and o not in (scy, clip):
                o.hide_render = False
        set_action(a)
        attach_scythe(weapon)
        sc.frame_set(int(fr))
        loc, tgt, lens = VIEWS[view]
        cam.location = loc
        cam.rotation_euler = (tgt - loc).to_track_quat('-Z', 'Y').to_euler()
        cam_d.lens = lens
    sc.render.filepath = f"{outp}_{a.replace('Rig|','')}_{fr}_{view}{'_s' if weapon else ''}.png"
    bpy.ops.render.render(write_still=True)
    print("WROTE", sc.render.filepath)
