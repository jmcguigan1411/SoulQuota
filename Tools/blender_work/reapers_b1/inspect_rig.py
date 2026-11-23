import bpy, time
t=time.time()
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath="/home/claude/sq/ArtSource/Characters/Reaper_Custom.fbx")
print("import", time.time()-t)
rig=[o for o in bpy.data.objects if o.type=='ARMATURE'][0]
print(rig.name, rig.matrix_world, rig.dimensions)
for b in rig.data.bones:
    h=rig.matrix_world@b.head_local; tl=rig.matrix_world@b.tail_local
    print(f"{b.name:22s} {b.parent.name if b.parent else '-':16s} h({h.x:.3f},{h.y:.3f},{h.z:.3f}) t({tl.x:.3f},{tl.y:.3f},{tl.z:.3f})")
for o in bpy.data.objects:
    if o.type=='MESH':
        import mathutils
        bb=[o.matrix_world@v.co for v in o.data.vertices]
        print(o.name, len(o.data.polygons), [round(min(v[i] for v in bb),3) for i in range(3)], [round(max(v[i] for v in bb),3) for i in range(3)], [s.material.name for s in o.material_slots])
print(len(bpy.data.actions), sorted(a.name for a in bpy.data.actions if not a.name[4:] in ()) [:200])
