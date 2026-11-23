import bpy
from mathutils import Vector
for body in ("Minion","Mage"):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=f"/mnt/user-data/outputs/ArtSource/Characters/Skeleton_{body}.fbx")
    rig=[o for o in bpy.data.objects if o.type=='ARMATURE'][0]
    print(body, "rig", rig.name, rig.matrix_world.to_scale(), rig.rotation_euler[:], "nbones", len(rig.data.bones))
    for o in bpy.data.objects:
        if o.type=='MESH':
            bb=[o.matrix_world@Vector(c) for c in o.bound_box]
            print(" MESH",o.name,o.parent.name if o.parent else None,o.parent_type, o.parent_bone, sum(len(p.vertices)-2 for p in o.data.polygons),[round(min(v[i] for v in bb),3) for i in range(3)],[round(max(v[i] for v in bb),3) for i in range(3)], o.matrix_world.to_scale(), [m.name for m in o.data.materials])
