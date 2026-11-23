import bpy
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath="/mnt/user-data/outputs/ArtSource/Characters/Skeleton_Mage.fbx")
rig=bpy.data.objects['Rig']
for bn in ['handslot.r','hand.r','hips','chest','head','upperarm.l','upperleg.l','foot.l']:
    m=rig.matrix_world@rig.data.bones[bn].matrix_local
    print(bn,"X",tuple(round(x,2) for x in m.col[0][:3]),"Y",tuple(round(x,2) for x in m.col[1][:3]),"Z",tuple(round(x,2) for x in m.col[2][:3]))
a=bpy.data.actions['Rig|Running_A']
print(type(a), [s.identifier for s in a.slots])
paths=set()
try:
    for fc in a.fcurves: paths.add(fc.data_path.split('"]')[-1])
except Exception as e:
    print("no a.fcurves",e)
    for l in a.layers:
        for st in l.strips:
            for cb in st.channelbags:
                for fc in cb.fcurves: paths.add(fc.data_path.split('"]')[-1])
print(paths)
print([pb.rotation_mode for pb in rig.pose.bones][:3])
print(rig.animation_data.action.name if rig.animation_data and rig.animation_data.action else None)
