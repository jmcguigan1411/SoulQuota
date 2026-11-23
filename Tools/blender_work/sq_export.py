# Export Reaper_Custom.fbx (rig + body + all actions) and verify against the KayKit source.
import bpy, re, sys
from mathutils import Vector

CH = "/home/claude/sq/ArtSource/Characters/"
SRC = "/mnt/user-data/outputs/ArtSource/Characters/Skeleton_Mage.fbx"
OUT = CH + "Reaper_Custom.fbx"

bpy.ops.wm.open_mainfile(filepath=CH + "Reaper_Custom.blend")
rig = bpy.data.objects["Rig"]
body = bpy.data.objects["Reaper_Body"]
# strip the "Rig|" prefix the FBX importer added, so take names match the source file (plain "Idle", ...)
for a in bpy.data.actions:
    if a.name.startswith("Rig|"):
        a.name = a.name[4:]
    a.use_fake_user = True
rig.animation_data.action = bpy.data.actions["Idle"]
rig.animation_data.action_slot = bpy.data.actions["Idle"].slots[0]
bpy.context.scene.frame_set(1)
bpy.ops.object.select_all(action='DESELECT')
rig.select_set(True); body.select_set(True)
bpy.context.view_layer.objects.active = rig
# Blender names takes "<Object>|<Action>"; the KayKit source uses plain action names, so
# patch the exporter's take-name helper to emit just the action name (same as the source file).
from io_scene_fbx import export_fbx_bin as _efb
_orig_name = _efb.get_blenderID_name
def _take_name(bid):
    if isinstance(bid, (tuple, list)) and len(bid) == 2 and isinstance(bid[1], bpy.types.Action):
        return bid[1].name
    return _orig_name(bid)
_efb.get_blenderID_name = _take_name
bpy.ops.export_scene.fbx(
    filepath=OUT, use_selection=True, object_types={'ARMATURE', 'MESH'},
    apply_unit_scale=True, global_scale=1.0, apply_scale_options='FBX_SCALE_UNITS',
    axis_forward='-Z', axis_up='Y', use_space_transform=True, bake_space_transform=False,
    mesh_smooth_type='FACE', use_mesh_modifiers=False, add_leaf_bones=False,
    primary_bone_axis='Y', secondary_bone_axis='X', armature_nodetype='NULL',
    bake_anim=True, bake_anim_use_all_bones=True, bake_anim_use_nla_strips=False,
    bake_anim_use_all_actions=True, bake_anim_force_startend_keying=True,
    bake_anim_step=1.0, bake_anim_simplify_factor=1.0, path_mode='STRIP')
bpy.ops.wm.save_as_mainfile(filepath=CH + "Reaper_Custom.blend")
print("EXPORTED", OUT)

def fbx_info(path):
    d = open(path, "rb").read()
    i = d.find(b"UnitScaleFactor")
    import struct
    j = d.find(b"D", i + 40)
    stacks = sorted(set(m.decode() for m in re.findall(rb"([\x20-\x7e]{1,60})\x00\x01AnimStack", d)))
    return stacks

def load(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=path)
    arm = [o for o in bpy.data.objects if o.type == 'ARMATURE'][0]
    bones = {b.name: (arm.matrix_world @ b.head_local, arm.matrix_world @ b.tail_local) for b in arm.data.bones}
    meshes = [o for o in bpy.data.objects if o.type == 'MESH']
    acts = sorted(a.name for a in bpy.data.actions)
    tris = sum(sum(len(p.vertices) - 2 for p in m.data.polygons) for m in meshes)
    bb = []
    for m in meshes:
        bb += [m.matrix_world @ Vector(c) for c in m.bound_box]
    return arm, bones, acts, tris, bb, tuple(arm.dimensions), arm.matrix_world.copy()

sa, sb, sacts, stris, sbb, sdim, smw = load(SRC)
ra, rb, racts, rtris, rbb, rdim, rmw = load(OUT)
print("SRC armature dims", tuple(round(x, 4) for x in sdim), "matrix_world rot", smw.to_euler(), "scale", smw.to_scale())
print("NEW armature dims", tuple(round(x, 4) for x in rdim), "matrix_world rot", rmw.to_euler(), "scale", rmw.to_scale())
missing = set(sb) - set(rb); extra = set(rb) - set(sb)
print("bone name diff: missing", missing, "extra", extra)
maxd = max(max((sb[n][0] - rb[n][0]).length, (sb[n][1] - rb[n][1]).length) for n in sb if n in rb)
print("max bone head/tail position difference (m):", round(maxd, 6))
print("NEW mesh tris", rtris, "bbox z", round(min(v.z for v in rbb), 3), round(max(v.z for v in rbb), 3))
print("SRC mesh bbox z", round(min(v.z for v in sbb), 3), round(max(v.z for v in sbb), 3))
print("SRC actions", len(sacts), "NEW actions", len(racts))
print("NEW-only actions", sorted(set(racts) - set(sacts)))
print("NEW actions list", racts)
print("SRC-only actions", sorted(set(sacts) - set(racts)))
src_st, new_st = fbx_info(SRC), fbx_info(OUT)
print("FBX takes src", len(src_st), "new", len(new_st), "new sample", new_st[:5])
print("takes only in src", sorted(set(src_st) - set(new_st))[:10])
print("ALL_TAKES", new_st)
