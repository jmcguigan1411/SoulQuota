# Per-frame heights for SQ_Rise_FromGrave: top of each hand, top of the rest of the body, lowest foot point (world z, m)
import bpy, sys
from mathutils import Vector
bpy.ops.wm.open_mainfile(filepath=sys.argv[-1] if sys.argv[-1].endswith(".blend") else "/home/claude/sq/ArtSource/Characters/Reaper_Custom.blend")
rig = bpy.data.objects["Rig"]; body = bpy.data.objects["Reaper_Body"]
a = bpy.data.actions["SQ_Rise_FromGrave"]; rig.animation_data.action = a; rig.animation_data.action_slot = a.slots[0]
gi = {g.name: g.index for g in body.vertex_groups}
def grp(v):
    best = max(v.groups, key=lambda g: g.weight) if v.groups else None
    return body.vertex_groups[best.group].name if best else ""
names = [grp(v) for v in body.data.vertices]
HAND = {"r": {"hand.r", "handslot.r", "wrist.r"}, "l": {"hand.l", "handslot.l", "wrist.l"}}
FEET = {"foot.l", "foot.r", "toes.l", "toes.r"}
for f in range(1, int(a.frame_range[1]) + 1):
    bpy.context.scene.frame_set(f)
    dg = bpy.context.evaluated_depsgraph_get()
    ev = body.evaluated_get(dg); m = ev.to_mesh(); M = body.matrix_world
    zs = [(M @ v.co).z for v in m.vertices]
    hr = max(z for z, n in zip(zs, names) if n in HAND["r"])
    hl = max(z for z, n in zip(zs, names) if n in HAND["l"])
    rest = max(z for z, n in zip(zs, names) if n not in HAND["r"] | HAND["l"])
    feet = min(z for z, n in zip(zs, names) if n in FEET)
    low = min(zs)
    ev.to_mesh_clear()
    print(f"f{f:2d} handR {hr:6.3f} handL {hl:6.3f} body_top {rest:6.3f} feet_min {feet:6.3f} lowest {low:6.3f}")
