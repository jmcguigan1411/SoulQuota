import bpy
bpy.ops.wm.open_mainfile(filepath="/home/claude/sq/ArtSource/Characters/Reaper_Custom_base.blend")
b=bpy.data.objects['Reaper_Body']; rig=bpy.data.objects['Rig']
print(rig.animation_data.action, rig.matrix_world.to_euler())
me=b.data
uv=me.uv_layers[0].data
dg=bpy.context.evaluated_depsgraph_get()
ev=b.evaluated_get(dg).to_mesh()
import collections
for name_idx in [4,10,0,21]:
    vs=set()
    for p in me.polygons:
        u=uv[p.loop_start].uv
        idx=int(u.x*8)+8*int(u.y*8)
        if idx==name_idx: vs.update(p.vertices)
    vs=list(vs)
    if not vs: print(name_idx,"none"); continue
    co=[b.matrix_world@me.vertices[i].co for i in vs]; ce=[b.matrix_world@ev.vertices[i].co for i in vs]
    print(name_idx,len(vs),"rest z",round(min(c.z for c in co),3),round(max(c.z for c in co),3),"eval z",round(min(c.z for c in ce),3),round(max(c.z for c in ce),3))
    g=collections.Counter(b.vertex_groups[gg.group].name for i in vs for gg in me.vertices[i].groups)
    print(g.most_common(6))
