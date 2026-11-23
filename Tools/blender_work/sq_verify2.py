import bpy
for f in ["Reaper_Scythe", "Reaper_Clipboard"]:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=f"/home/claude/sq/ArtSource/Characters/{f}.fbx")
    o = [o for o in bpy.data.objects if o.type == 'MESH'][0]
    ws = [o.matrix_world @ v.co for v in o.data.vertices]
    print("CHK", f, "tris", sum(len(p.vertices) - 2 for p in o.data.polygons), "min", [round(min(w[i] for w in ws), 3) for i in range(3)], "max", [round(max(w[i] for w in ws), 3) for i in range(3)], "mats", [m.name for m in o.data.materials])
