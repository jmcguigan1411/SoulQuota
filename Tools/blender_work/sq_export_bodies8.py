# Export Skeleton_<Body>.fbx (KayKit mesh + materials + original 95 actions + the 39 SQ_ actions)
# into the Soul Quota ArtSource folder. The SQ_ actions are copied from Reaper_Custom.blend after checking
# that the rest skeleton is identical (no retarget needed if it is).
import bpy, sys, json, os
from mathutils import Vector

SRC_DIR = "/mnt/user-data/outputs/ArtSource/Characters/"
OUT_DIR = "/home/claude/sq/ArtSource/Characters/"
REAPER = OUT_DIR + "Reaper_Custom.blend"
ENEMY = "/home/claude/blender_work/sq_enemy_anims.blend"   # SQ_Enemy_* actions (sq_enemy_anims.py)
BODIES = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else ["Mage", "Rogue", "Warrior", "Minion"]

# reaper rest skeleton + SQ action names
bpy.ops.wm.open_mainfile(filepath=REAPER)
rr = bpy.data.objects["Rig"]
REAPER_REST = {b.name: (rr.matrix_world @ b.head_local, rr.matrix_world @ b.tail_local, b.matrix_local.copy()) for b in rr.data.bones}
SQ = []
NEW8 = sorted(open("/tmp/enemy_new.txt").read().split())
print("SQ actions in reaper:", len(SQ))

from io_scene_fbx import export_fbx_bin as _efb
_orig_name = _efb.get_blenderID_name
def _take_name(bid):
    if isinstance(bid, (tuple, list)) and len(bid) == 2 and isinstance(bid[1], bpy.types.Action):
        return bid[1].name
    return _orig_name(bid)
_efb.get_blenderID_name = _take_name

report = {}
for body in BODIES:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.render.fps = 30
    bpy.ops.import_scene.fbx(filepath=f"{SRC_DIR}Skeleton_{body}.fbx")
    rig = [o for o in bpy.data.objects if o.type == 'ARMATURE'][0]
    meshes = [o for o in bpy.data.objects if o.type == 'MESH']
    for a in bpy.data.actions:
        if a.name.startswith("Rig|"):
            a.name = a.name[4:]
        a.use_fake_user = True
    n_orig = len(bpy.data.actions)
    # rest-pose comparison against the reaper rig
    diff = 0.0; mdiff = 0.0
    for b in rig.data.bones:
        h, t, m = REAPER_REST[b.name]
        diff = max(diff, (rig.matrix_world @ b.head_local - h).length, (rig.matrix_world @ b.tail_local - t).length)
        mdiff = max(mdiff, max(abs(x - y) for r1, r2 in zip(b.matrix_local, m) for x, y in zip(r1, r2)))
    same = set(b.name for b in rig.data.bones) == set(REAPER_REST) and diff < 1e-5 and mdiff < 1e-5
    print(body, "rest vs reaper: names equal", set(b.name for b in rig.data.bones) == set(REAPER_REST),
          "max head/tail diff", diff, "max matrix_local diff", mdiff)
    if not same:
        raise SystemExit(f"{body}: rest skeleton differs from the reaper rig - would need per-rig baking")
    # Nov 19 2026: every non-KayKit take the file already carried (/tmp/enemy_old_actions.blend, read back from the previous
    # export and checked identical) plus the takes rebuilt on the hand-animated bases (/tmp/enemy8.blend), and the import marker
    with bpy.data.libraries.load("/tmp/enemy_old_actions.blend", link=False) as (osrc, odst):
        odst.actions = [n for n in osrc.actions if not n.startswith("SQ_Marker_")]
    with bpy.data.libraries.load("/tmp/enemy8.blend", link=False) as (esrc, edst):
        edst.actions = list(NEW8)
    mk = bpy.data.actions["Idle"].copy(); mk.name = "SQ_Marker_Remaster9"
    for a in bpy.data.actions:
        a.use_fake_user = True
    print(body, "actions", n_orig, "->", len(bpy.data.actions))
    idle = bpy.data.actions["Idle"]
    rig.animation_data.action = idle
    rig.animation_data.action_slot = idle.slots[0]
    bpy.context.scene.frame_set(1)
    bpy.ops.object.select_all(action='DESELECT')
    for o in [rig] + meshes:
        o.select_set(True)
    bpy.context.view_layer.objects.active = rig
    out = f"{OUT_DIR}Skeleton_{body}.fbx"
    bpy.ops.export_scene.fbx(
        filepath=out, use_selection=True, object_types={'ARMATURE', 'MESH'},
        apply_unit_scale=True, global_scale=1.0, apply_scale_options='FBX_SCALE_UNITS',
        axis_forward='-Z', axis_up='Y', use_space_transform=True, bake_space_transform=False,
        mesh_smooth_type='FACE', use_mesh_modifiers=False, add_leaf_bones=False,
        primary_bone_axis='Y', secondary_bone_axis='X', armature_nodetype='NULL',
        bake_anim=True, bake_anim_use_all_bones=True, bake_anim_use_nla_strips=False,
        bake_anim_use_all_actions=True, bake_anim_force_startend_keying=True,
        bake_anim_step=1.0, bake_anim_simplify_factor=1.0, path_mode='STRIP')
    print("EXPORTED", out)
