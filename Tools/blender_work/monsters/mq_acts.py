import bpy
with bpy.data.libraries.load("/home/claude/blender_work/sq_enemy_anims.blend") as (s, d):
    print(sorted(n for n in s.actions if n.startswith("SQ_")))
    print(len(s.actions))
