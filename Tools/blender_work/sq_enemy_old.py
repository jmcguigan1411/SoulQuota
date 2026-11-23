# pull the non-KayKit takes that are already inside Skeleton_Minion.fbx (so the re-export keeps every take the game references)
import bpy, re
from mathutils import Vector
CH = "/home/claude/sq/ArtSource/Characters/"
bpy.ops.wm.open_mainfile(filepath="/tmp/enemy8.blend")
rr = bpy.data.objects["Rig"]
REST = {b.name: b.matrix_local.copy() for b in rr.data.bones}
NEW = [a.name for a in bpy.data.actions if a.get("sq_remaster") == 8]
def pose_of(rig, act, f, bone):
    rig.animation_data.action = act
    if len(act.slots): rig.animation_data.action_slot = act.slots[0]
    bpy.context.scene.frame_set(f); bpy.context.view_layer.update()
    return (rig.matrix_world @ rig.pose.bones[bone].matrix).translation.copy()
with bpy.data.libraries.load("/home/claude/blender_work/sq_enemy_anims.blend") as (s, d):
    d.actions = ["SQ_Enemy_Dizzy"] if "SQ_Enemy_Dizzy" in s.actions else []
ref = pose_of(rr, bpy.data.actions["SQ_Enemy_Dizzy"], 10, "hand.l") if "SQ_Enemy_Dizzy" in bpy.data.actions else None
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.context.scene.render.fps = 30
bpy.ops.import_scene.fbx(filepath=CH + "Skeleton_Minion.fbx")
rig = [o for o in bpy.data.objects if o.type == 'ARMATURE'][0]
md = max(max(abs(x - y) for r1, r2 in zip(b.matrix_local, REST[b.name]) for x, y in zip(r1, r2)) for b in rig.data.bones)
print("REST DIFF", md, "scale", tuple(rig.scale))
for a in bpy.data.actions:
    if a.name.startswith("Rig|"): a.name = a.name[4:]
names = sorted(a.name for a in bpy.data.actions)
bpy.ops.import_scene.fbx(filepath="/mnt/user-data/outputs/ArtSource/Characters/Skeleton_Minion.fbx") if False else None
kk = set(m.decode() for m in re.findall(rb"([\x20-\x7e]{1,60})\x00\x01AnimStack", open("/mnt/user-data/outputs/ArtSource/Characters/Skeleton_Minion.fbx", "rb").read()))
kk = set(n[4:] if n.startswith("Rig|") else n for n in kk)
keep = [n for n in names if n not in kk and n not in NEW]
print("IN FBX", len(names), "kaykit", len(kk), "keep old", len(keep), keep)
if ref is not None and "SQ_Enemy_Dizzy" in bpy.data.actions:
    got = pose_of(rig, bpy.data.actions["SQ_Enemy_Dizzy"], 10, "hand.l")
    print("DIZZY hand.l", tuple(round(v, 4) for v in ref), tuple(round(v, 4) for v in got))
for a in list(bpy.data.actions):
    if a.name not in keep: bpy.data.actions.remove(a)
    else: a.use_fake_user = True
open("/tmp/enemy_keep.txt", "w").write("\n".join(keep))
bpy.ops.wm.save_as_mainfile(filepath="/tmp/enemy_old_actions.blend")
print("NEW8", NEW)
