import bpy
bpy.ops.wm.open_mainfile(filepath="/home/claude/blender_work/reapers2/reaper_rig.blend")
acts = bpy.data.actions
print(len(acts))
for a in acts:
    if a.name.split(".")[0] in ("Running_A","SQ_Walk_Sneak","Death_A","SQ_Attack_Slash_R2"):
        n = sum(len(cb.fcurves) for l in a.layers for s in l.strips for cb in s.channelbags) if hasattr(a,"layers") else 0
        print(repr(a.name), a.frame_range[:], n, [s.identifier for s in a.slots])
