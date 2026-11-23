import bpy
bpy.ops.wm.open_mainfile(filepath="/home/claude/sq/ArtSource/Characters/Reaper_Custom.blend")
rig=bpy.data.objects['Rig']; act=bpy.data.actions['Reaper_Taunt_Clipboard']
rig.animation_data.action=act; rig.animation_data.action_slot=act.slots[0]
bpy.context.scene.frame_set(18)
for b in ['handslot.l','hand.l','lowerarm.l']:
    M=rig.matrix_world@rig.pose.bones[b].matrix
    print(b,[tuple(round(x,2) for x in M.col[i][:3]) for i in range(4)])
print(act.slots[0].identifier, [l for l in act.layers], len(act.layers[0].strips[0].channelbags))
