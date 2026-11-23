import bpy
for f in ["Skeleton_Axe","Skeleton_Staff","Skeleton_Blade"]:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=f"/mnt/user-data/outputs/ArtSource/Characters/{f}.fbx")
    for o in bpy.data.objects:
        if o.type!='MESH': print(f,"OBJ",o.name,o.type); continue
        ws=[o.matrix_world@v.co for v in o.data.vertices]
        mn=[min(w[i] for w in ws) for i in range(3)]; mx=[max(w[i] for w in ws) for i in range(3)]
        print(f,o.name,"rot",tuple(round(x,3) for x in o.rotation_euler),"scale",tuple(round(x,3) for x in o.scale),"min",[round(x,3) for x in mn],"max",[round(x,3) for x in mx], "tris",sum(len(p.vertices)-2 for p in o.data.polygons))
