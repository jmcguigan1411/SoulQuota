import bpy, time
t=time.time()
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath="/tmp/rq/Reaper_Custom.fbx")
print("import s", time.time()-t)
rig=[o for o in bpy.data.objects if o.type=='ARMATURE'][0]
print(rig.name, tuple(rig.matrix_world), rig.dimensions)
for b in rig.data.bones:
    print(b.name, b.parent.name if b.parent else None, tuple(round(x,3) for x in rig.matrix_world@b.head_local), tuple(round(x,3) for x in rig.matrix_world@b.tail_local))
for o in bpy.data.objects:
    if o.type=='MESH':
        print("MESH",o.name,len(o.data.polygons),[m.name for m in o.data.materials], o.dimensions)
print(len(bpy.data.actions), sorted(a.name for a in bpy.data.actions))
