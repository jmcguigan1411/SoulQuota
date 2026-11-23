import bpy
from mathutils import Matrix
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.context.scene.render.fps = 30
bpy.ops.import_scene.fbx(filepath="/home/claude/blender_work/reapers_b1/rig_snapshot.fbx")
rig = bpy.data.objects["Rig"]
for o in list(bpy.data.objects):
    if o.type == 'MESH':
        me=o.data; bpy.data.objects.remove(o); bpy.data.meshes.remove(me)
for m in list(bpy.data.materials): bpy.data.materials.remove(m)
for i in list(bpy.data.images): bpy.data.images.remove(i)
rig.animation_data.action = None
for a in bpy.data.actions:
    if a.name.startswith("Rig|"): a.name = a.name[4:]
    a.use_fake_user = True
for pb in rig.pose.bones: pb.matrix_basis = Matrix.Identity(4)
print([o.name for o in bpy.data.objects])
bpy.ops.wm.save_as_mainfile(filepath="/home/claude/blender_work/reapers_b1/rig_anims.blend")
for a in list(bpy.data.actions): bpy.data.actions.remove(a)
bpy.ops.wm.save_as_mainfile(filepath="/home/claude/blender_work/reapers_b1/rig_base.blend")
