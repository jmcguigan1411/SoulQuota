import bpy, sys
from mathutils import Vector
f = sys.argv[1]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=f)
for o in bpy.data.objects:
    print(o.name, o.type, o.parent.name if o.parent else None, o.parent_type, o.parent_bone, tuple(round(x,4) for x in o.location), tuple(round(x,4) for x in o.rotation_euler), tuple(round(x,4) for x in o.scale))
    if o.type=='MESH':
        dg=bpy.context.evaluated_depsgraph_get()
        me=o.evaluated_get(dg).to_mesh()
        ws=[o.matrix_world@v.co for v in me.vertices]
        mn=Vector([min(w[i] for w in ws) for i in range(3)]); mx=Vector([max(w[i] for w in ws) for i in range(3)])
        print("   tris", sum(len(p.vertices)-2 for p in me.polygons), "mats", [m.name for m in o.data.materials], "bb", tuple(round(x,3) for x in mn), tuple(round(x,3) for x in mx), "mods", [m.type for m in o.modifiers], "vg", len(o.vertex_groups))
acts=[a.name for a in bpy.data.actions]
print(len(acts), [a for a in acts if any(k in a for k in ("Walking_D","SQ_Run_Heavy","SQ_Run_Sprint","SQ_Walk_Waddle","SQ_Enemy_Shamble","SQ_Run_Float","Fly"))])
for a in bpy.data.actions:
    if a.name.endswith(("Walking_D_Skeletons","Fly")): print(a.name, a.frame_range)
