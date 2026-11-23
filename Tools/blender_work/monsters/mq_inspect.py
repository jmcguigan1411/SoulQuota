import bpy
from mathutils import Vector
bpy.ops.wm.open_mainfile(filepath="/home/claude/blender_work/sq_enemy_anims.blend")
print("OBJS", [(o.name,o.type,o.parent.name if o.parent else None) for o in bpy.data.objects])
print("ACTS", len(bpy.data.actions), sorted(a.name for a in bpy.data.actions if "Enemy" in a.name or a.name in("Walking_D_Skeletons","Death_A","1H_Melee_Attack_Chop")))
rig=bpy.data.objects["Rig"]
print("RIG mw", rig.matrix_world, rig.dimensions)
for b in rig.data.bones:
    h=rig.matrix_world@b.head_local; t=rig.matrix_world@b.tail_local
    print(f"{b.name:16s} parent={(b.parent.name if b.parent else "-"):12s} head=({h.x:.3f},{h.y:.3f},{h.z:.3f}) tail=({t.x:.3f},{t.y:.3f},{t.z:.3f})")
for o in bpy.data.objects:
    if o.type=='MESH':
        bb=[o.matrix_world@Vector(c) for c in o.bound_box]
        print("MESH",o.name,len(o.data.polygons),[round(min(v[i] for v in bb),3) for i in range(3)],[round(max(v[i] for v in bb),3) for i in range(3)], [g.name for g in o.vertex_groups][:40])
