import bpy
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath="/mnt/user-data/outputs/ArtSource/Characters/Skeleton_Mage.fbx")
for o in bpy.data.objects:
    print("OBJ",o.name,o.type,o.parent.name if o.parent else None, o.parent_bone, tuple(round(x,3) for x in o.location), tuple(round(x,3) for x in o.rotation_euler), tuple(round(x,3) for x in o.scale), tuple(round(x,3) for x in o.dimensions))
arm=[o for o in bpy.data.objects if o.type=='ARMATURE'][0]
for b in arm.data.bones:
    hl=arm.matrix_world@b.head_local; tl=arm.matrix_world@b.tail_local
    print("BONE",b.name,b.parent.name if b.parent else None,tuple(round(x,3) for x in hl),tuple(round(x,3) for x in tl), round(b.length,3))
print("NACT",len(bpy.data.actions))
for a in bpy.data.actions: print("ACT",a.name,tuple(a.frame_range))
print(bpy.context.scene.render.fps)
for m in bpy.data.materials: print("MAT",m.name)
for o in bpy.data.objects:
    if o.type=='MESH': print("MESHTRI",o.name,sum(len(p.vertices)-2 for p in o.data.polygons), [v.name for v in o.vertex_groups][:40])
