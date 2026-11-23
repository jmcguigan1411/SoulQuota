# Read Reaper_Custom.fbx (read only) -> local working blend with rig + test actions, no mesh.
import bpy, sys
from mathutils import Matrix
SRC = "/home/claude/sq/ArtSource/Characters/Reaper_Custom.fbx"
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.context.scene.render.fps = 30
bpy.ops.import_scene.fbx(filepath=SRC)
rig = bpy.data.objects["Rig"]
print("RIG mw", [list(map(lambda x: round(x,6), r)) for r in rig.matrix_world])
print("NBONES", len(rig.data.bones))
for b in rig.data.bones:
    h = rig.matrix_world @ b.head_local; t = rig.matrix_world @ b.tail_local
    print("BONE", b.name, b.parent.name if b.parent else None, tuple(round(x,3) for x in h), tuple(round(x,3) for x in t))
for o in list(bpy.data.objects):
    if o.type == 'MESH':
        print("MESH", o.name, len(o.data.polygons), [s.material.name for s in o.material_slots if s.material])
        me = o.data; bpy.data.objects.remove(o); bpy.data.meshes.remove(me)
for m in list(bpy.data.materials): bpy.data.materials.remove(m)
for i in list(bpy.data.images): bpy.data.images.remove(i)
names = []
for a in bpy.data.actions:
    if a.name.startswith("Rig|"): a.name = a.name[4:]
    a.use_fake_user = True
    names.append((a.name, tuple(a.frame_range)))
print("ACTIONS", len(names))
print("ACTLIST", sorted(n for n, r in names))
rig.animation_data.action = None
for pb in rig.pose.bones: pb.matrix_basis = Matrix.Identity(4)
bpy.ops.wm.save_as_mainfile(filepath="/home/claude/blender_work/reapers2/reaper_rig.blend")
