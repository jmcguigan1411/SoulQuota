# Compare every common take of two FBX files (posed bone heads, 5 frames per take). usage: fbx_diff.py OLD NEW
import bpy, sys
CHANGED = {"SQ_Attack_Slash_R2", "SQ_Attack_Chop_R2", "SQ_Attack_Punch_R2", "SQ_Cast_Shoot_R2", "SQ_Cast_Raise_R2", "SQ_Throw_R2"}
BON = ["hips", "chest", "head", "hand.l", "hand.r", "handslot.r", "foot.l", "foot.r", "toes.l"]
def grab(p):
    bpy.ops.wm.read_factory_settings(use_empty=True); bpy.context.scene.render.fps = 30
    bpy.ops.import_scene.fbx(filepath=p)
    rig = [o for o in bpy.data.objects if o.type == 'ARMATURE'][0]; out = {}
    for a in bpy.data.actions:
        n = a.name.replace("Rig|", ""); r0, r1 = a.frame_range
        rig.animation_data.action = a; rig.animation_data.action_slot = a.slots[0]; S = []
        for k in range(5):
            bpy.context.scene.frame_set(int(round(r0 + (r1 - r0) * k / 4)))
            S.append([rig.matrix_world @ rig.pose.bones[b].head for b in BON])
        out[n] = (tuple(a.frame_range), S)
    return out
old, new = sys.argv[-2], sys.argv[-1]
A_ = grab(old); B_ = grab(new)
common = sorted(set(A_) & set(B_))
worst = (0.0, ""); rng = []; chg = {}
for n in common:
    d = max((x - y).length for s1, s2 in zip(A_[n][1], B_[n][1]) for x, y in zip(s1, s2))
    if A_[n][0] != B_[n][0]: rng.append(n)
    if n in CHANGED: chg[n] = round(d * 100, 1); continue
    if d > worst[0]: worst = (d, n)
print("DIFF", new.split("/")[-1], "common", len(common), "| untouched takes max diff %.4f cm (%s)" % (worst[0] * 100, worst[1]),
      "| frame ranges changed:", rng, "| remastered takes diff (cm):", chg)
