import bpy, sys
bpy.ops.wm.open_mainfile(filepath="/home/claude/sq/ArtSource/Characters/R_%s.blend" % sys.argv[-1])
ob = [o for o in bpy.data.objects if o.type == 'MESH'][0]
rig = bpy.data.objects["Rig"]
me = ob.data
uv = me.uv_layers[0].data
from collections import Counter
cnt = Counter()
for p in me.polygons:
    u = uv[p.loop_start].uv
    idx = int(u.x * 8) + 8 * int(u.y * 8)
    cnt[(idx, p.material_index)] += 1
    if p.material_index == 1:
        c = rig.matrix_world @ ob.matrix_world @ p.center if False else ob.matrix_world @ p.center
print(sorted(cnt.items()))
import mathutils
gl = [ob.matrix_world @ p.center for p in me.polygons if p.material_index == 1]
print("glow faces", len(gl), "miny", min(v.y for v in gl) if gl else None, [tuple(round(x,3) for x in v) for v in gl[:6]])
