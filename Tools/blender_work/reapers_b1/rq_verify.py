# Re-import each batch-1 reaper FBX and compare its armature with Reaper_Custom.fbx (rig only).
import bpy, re
CH = "/home/claude/sq/ArtSource/Characters/"
def load(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=path)
    arm = [o for o in bpy.data.objects if o.type == 'ARMATURE'][0]
    bones = {b.name: (arm.matrix_world @ b.head_local, arm.matrix_world @ b.tail_local, b.parent.name if b.parent else None, b.matrix_local.copy()) for b in arm.data.bones}
    meshes = [o for o in bpy.data.objects if o.type == 'MESH']
    return arm, bones, meshes
def takes(path):
    d = open(path, "rb").read()
    return len(re.findall(rb"AnimStack", d))
ra, rb, _ = load("/home/claude/blender_work/reapers_b1/rig_snapshot.fbx")
rmw = [list(r) for r in ra.matrix_world]; RNAME = ra.name
for mid in ("Grim", "Mort", "Widow", "Bonesmith", "Keeper", "Monk", "Intern"):
    path = CH + "R_%s.fbx" % mid
    a, b, ms = load(path)
    names = set(b) == set(rb)
    par = all(b[n][2] == rb[n][2] for n in rb if n in b)
    maxd = max(max((b[n][0] - rb[n][0]).length, (b[n][1] - rb[n][1]).length) for n in rb if n in b)
    roll = max(max(abs(x - y) for r1, r2 in zip(b[n][3], rb[n][3]) for x, y in zip(r1, r2)) for n in rb if n in b)
    mw = max(abs(x - y) for r1, r2 in zip(a.matrix_world, rmw) for x, y in zip(r1, r2))
    m = ms[0]
    tris = sum(len(p.vertices) - 2 for p in m.data.polygons)
    zs = [(m.matrix_world @ v.co).z for v in m.data.vertices]
    groups = {g.name for g in m.vertex_groups}
    maxinf = max(len([g for g in v.groups if g.weight > 0]) for v in m.data.vertices)
    print(f"R_{mid}: bones {len(b)}/{len(rb)} names_equal {names} parents_equal {par} max_rest_diff {maxd:.1e} bone_matrix_diff {roll:.1e} "
          f"armature '{a.name}' matrix_diff {mw:.1e} | meshes {len(ms)} '{m.name}' tris {tris} height {max(zs):.3f} minz {min(zs):.3f} "
          f"mats {[s.material.name for s in m.material_slots]} groups_not_bones {sorted(groups - set(rb))} max_influences {maxinf} animstacks {takes(path)} actions {len(bpy.data.actions)}")
