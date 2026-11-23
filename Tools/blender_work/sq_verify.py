import bpy
from mathutils import Vector
SRC = "/mnt/user-data/outputs/ArtSource/Characters/Skeleton_Mage.fbx"
NEW = "/home/claude/sq/ArtSource/Characters/Reaper_Custom.fbx"
def sample(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=path)
    arm = [o for o in bpy.data.objects if o.type == 'ARMATURE'][0]
    out = {}
    arm.data.pose_position = 'REST'; bpy.context.view_layer.update()
    out['rest_dims'] = tuple(round(x, 4) for x in arm.dimensions)
    arm.data.pose_position = 'POSE'
    for an, fr in (("Running_A", 7), ("Walking_A", 13), ("Death_A", 20), ("Cheer", 25)):
        act = [a for a in bpy.data.actions if a.name.endswith("|" + an) or a.name == an][0]
        arm.animation_data.action = act
        arm.animation_data.action_slot = act.slots[0]
        bpy.context.scene.frame_set(fr)
        out[an] = {pb.name: (arm.matrix_world @ pb.head).copy() for pb in arm.pose.bones}
    acts = {a.name.split("|")[-1]: tuple(a.frame_range) for a in bpy.data.actions}
    return out, acts, arm.name, arm.matrix_world.copy()
s, sa, sn, sm = sample(SRC)
n, na, nn, nm = sample(NEW)
print("armature object name", sn, nn, "| world matrix equal:", all(abs(a - b) < 1e-5 for r1, r2 in zip(sm, nm) for a, b in zip(r1, r2)))
print("rest dims src", s['rest_dims'], "new", n['rest_dims'])
for an in ("Running_A", "Walking_A", "Death_A", "Cheer"):
    d = max((s[an][b] - n[an][b]).length for b in s[an])
    print(f"{an}: max posed bone-head diff {d*100:.3f} cm")
diff = [k for k in sa if sa[k] != na.get(k)]
print("frame range mismatches:", diff[:10])
for k in sorted(na):
    if k.startswith("Reaper_"): print("NEW", k, na[k])
