# snapshot of Reaper_Custom.fbx rig + actions -> /tmp/rq/anims.blend (read-only copy; never written back)
import bpy
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.context.scene.render.fps = 30
bpy.ops.import_scene.fbx(filepath="/tmp/rq/Reaper_Custom.fbx")
rig = bpy.data.objects["Rig"]
me = [o for o in bpy.data.objects if o.type == 'MESH'][0]
import mathutils
bb = [me.matrix_world @ v.co for v in me.data.vertices]
print("REAPER z", min(v.z for v in bb), max(v.z for v in bb), "x", min(v.x for v in bb), max(v.x for v in bb), "y", min(v.y for v in bb), max(v.y for v in bb))
for a in bpy.data.actions:
    if a.name.startswith("Rig|"):
        a.name = a.name[4:]
    a.use_fake_user = True
print("scale", rig.scale, rig.rotation_euler)
bpy.ops.wm.save_as_mainfile(filepath="/tmp/rq/anims.blend")
