import sys, math
sys.path.insert(0, "/home/claude/blender_work/sets")
import bpy
from mathutils import Vector
import set_build as sb
pb = sb.pb
bpy.ops.wm.read_factory_settings(use_empty=True)
img = bpy.data.images.load(sb.PAL)
mat = pb.make_material(img)
rows = [sb.PIECES[:7], sb.PIECES[7:11], sb.PIECES[11:]]
y = 0
for r, fns in enumerate(rows):
    x = 0
    for fn in fns:
        P = fn()
        ob, hulls, off = pb.build_objects(P, mat)
        for h in hulls:
            bpy.data.objects.remove(h)
        w = ob.dimensions.x
        ob.location = (x + w / 2, -y, 0)
        x += max(w, ob.dimensions.y) + 2.5
    y += 14
# reaper-height marker (1.8 m)
bpy.ops.mesh.primitive_cylinder_add(radius=0.3, depth=1.8, location=(-1.5, 0, 0.9))
bpy.ops.mesh.primitive_plane_add(size=200, location=(20, -14, 0))
g = bpy.data.materials.new("g"); g.use_nodes = True; g.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.03, 0.10, 0.07, 1)
bpy.context.object.data.materials.append(g)
sun = bpy.data.lights.new("s", 'SUN'); sun.energy = 3.5; so = bpy.data.objects.new("s", sun); so.rotation_euler = (math.radians(55), 0, math.radians(35)); bpy.context.scene.collection.objects.link(so)
w = bpy.data.worlds.new("w"); bpy.context.scene.world = w; w.use_nodes = True; w.node_tree.nodes["Background"].inputs[0].default_value = (0.12, 0.06, 0.22, 1); w.node_tree.nodes["Background"].inputs[1].default_value = 1.2
cam = bpy.data.cameras.new("c"); cam.lens = 40; co = bpy.data.objects.new("c", cam); bpy.context.scene.collection.objects.link(co)
co.location = (22, -62, 26); d = Vector((22, -13, 3)) - co.location; co.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
sc = bpy.context.scene; sc.camera = co; sc.render.resolution_x = 1600; sc.render.resolution_y = 900
try: sc.render.engine = 'BLENDER_EEVEE_NEXT'
except Exception: sc.render.engine = 'BLENDER_EEVEE'
sc.render.filepath = "/mnt/user-data/outputs/maps/SetPieces_Kit.png"
bpy.ops.render.render(write_still=True)
