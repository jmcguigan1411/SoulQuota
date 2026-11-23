# Impostor check sheet tiles for one model: full-res skeletal pose (reference) + the 4 exported impostor FBXs,
# re-imported from disk (checks the round trip: same space, slots, tri count).
# usage: python3 imp_render.py <Model> <tile_dir> <px>
import bpy, sys, os, math, json
from mathutils import Vector
sys.path.insert(0, os.path.dirname(__file__))
import imp_build as IB

args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
model, tdir, px = args[0], args[1], int(args[2])
os.makedirs(tdir, exist_ok=True)
rig, act, meshes = IB.load(model)
sc = bpy.context.scene
TEX = {"Minion": "skeleton_texture.png", "Warrior": "skeleton_texture.png", "Rogue": "skeleton_texture.png",
       "Mage": "skeleton_texture.png"}.get(model, model.lower() + "_palette.png")
img = bpy.data.images.load(IB.CH + TEX)


def tex_mat(name, glow):
    m = bpy.data.materials.new(name + "_chk")
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes.get("Principled BSDF")
    t = nt.nodes.new("ShaderNodeTexImage")
    t.image = img
    t.interpolation = 'Closest'
    nt.links.new(t.outputs["Color"], b.inputs["Base Color"])
    b.inputs["Roughness"].default_value = 0.85
    if glow:
        nt.links.new(t.outputs["Color"], b.inputs["Emission Color"])
        b.inputs["Emission Strength"].default_value = 3.0
    return m


def retex(ob):
    for i, slot in enumerate(ob.material_slots):
        nm = slot.material.name if slot.material else "x"
        slot.material = tex_mat(nm, "glow" in nm.lower())


for o in meshes:
    retex(o)

# scene
cam_d = bpy.data.cameras.new("cam")
cam_d.type = 'ORTHO'
cam = bpy.data.objects.new("cam", cam_d)
sc.collection.objects.link(cam)
sc.camera = cam
tgt = Vector((0, 0, 1.35 if model == "Gravebat" else 1.15))
loc = tgt + Vector((3.2, -5.0, 3.6))
cam.location = loc
cam.rotation_euler = (tgt - loc).to_track_quat('-Z', 'Y').to_euler()
cam_d.ortho_scale = 3.4 if model != "Mage" else 3.8


def light(name, rot, energy, color=(1, 1, 1)):
    ld = bpy.data.lights.new(name, 'SUN')
    ld.energy = energy
    ld.color = color
    lo = bpy.data.objects.new(name, ld)
    sc.collection.objects.link(lo)
    lo.rotation_euler = rot


light("key", (math.radians(50), 0, math.radians(-35)), 3.2, (1, 0.96, 0.9))
light("rim", (math.radians(-60), 0, math.radians(160)), 2.0, (0.6, 0.8, 1.0))
light("fill", (math.radians(70), 0, math.radians(100)), 0.8)
w = bpy.data.worlds.new("w")
sc.world = w
w.use_nodes = True
w.node_tree.nodes["Background"].inputs[0].default_value = (0.05, 0.05, 0.07, 1)
bpy.ops.mesh.primitive_circle_add(vertices=48, radius=1.4, fill_type='NGON', location=(0, 0, 0))
g = bpy.context.active_object
gm = bpy.data.materials.new("ground")
gm.use_nodes = True
gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.13, 0.13, 0.16, 1)
g.data.materials.append(gm)
sc.render.resolution_x = sc.render.resolution_y = px
sc.render.engine = 'BLENDER_EEVEE'
sc.view_settings.view_transform = 'AgX'
try:
    sc.eevee.taa_render_samples = 16
except Exception:
    pass


def shot(name):
    sc.render.filepath = os.path.join(tdir, "%s_%s.png" % (model, name))
    bpy.ops.render.render(write_still=True)


r0, r1 = act.frame_range
sc.frame_set(int(r0))
shot("full")
for o in meshes:
    o.hide_render = True
info = {}
for i in range(4):
    path = IB.OUT + "%s_Walk_%d.fbx" % (model, i)
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=path)
    new = [o for o in bpy.data.objects if o not in before]
    ob = [o for o in new if o.type == 'MESH'][0]
    tris = sum(len(p.vertices) - 2 for p in ob.data.polygons)
    cos = [ob.matrix_world @ v.co for v in ob.data.vertices]
    info[i] = {"tris": tris, "slots": [s.material.name for s in ob.material_slots],
               "min": [round(min(c[k] for c in cos) * 100, 1) for k in range(3)],
               "max": [round(max(c[k] for c in cos) * 100, 1) for k in range(3)]}
    retex(ob)
    shot("imp%d" % i)
    ob.hide_render = True
json.dump(info, open(os.path.join(tdir, model + ".json"), "w"))
print("RENDERED", model, json.dumps(info))
