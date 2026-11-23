# Re-import each batch-2 FBX and compare its armature with Reaper_Custom.fbx (read only).
import bpy, re
CH = "/home/claude/sq/ArtSource/Characters/"
IDS = ["Femur", "Digby", "Collector", "Beak", "Tallow", "Lookout", "Vexa"]
def load(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=path)
    arm = [o for o in bpy.data.objects if o.type == 'ARMATURE'][0]
    bones = {b.name: (arm.matrix_world @ b.head_local, arm.matrix_world @ b.tail_local, b.parent.name if b.parent else None, b.matrix_local.copy()) for b in arm.data.bones}
    meshes = [o for o in bpy.data.objects if o.type == 'MESH']
    return arm, bones, meshes
def takes(path):
    d = open(path, "rb").read()
    return sorted(set(m.decode() for m in re.findall(rb"([\x20-\x7e]{1,60})\x00\x01AnimStack", d)))
ra, rb, _ = load(CH + "Reaper_Custom.fbx")
rmw = ra.matrix_world.copy(); rname = ra.name
for rid in IDS:
    path = CH + "R_%s.fbx" % rid
    a, b, ms = load(path)
    m = ms[0]
    maxd = max(max((b[n][0] - rb[n][0]).length, (b[n][1] - rb[n][1]).length) for n in rb if n in b)
    maxm = max(max(abs(x - y) for r1, r2 in zip(b[n][3], rb[n][3]) for x, y in zip(r1, r2)) for n in rb if n in b)
    mw = max(abs(x - y) for r1, r2 in zip(a.matrix_world, rmw) for x, y in zip(r1, r2))
    tris = sum(len(p.vertices) - 2 for p in m.data.polygons)
    zs = [(m.matrix_world @ v.co).z for v in m.data.vertices]
    infl = max(len([g for g in v.groups if g.weight > 1e-4]) for v in m.data.vertices)
    wsum = min(sum(g.weight for g in v.groups) for v in m.data.vertices)
    bad_groups = [g.name for g in m.vertex_groups if g.name not in b]
    print(f"VERIFY R_{rid}: arm '{a.name}'=='{rname}' {a.name == rname} bones {len(b)}/{len(rb)} names_eq {set(b) == set(rb)} "
          f"parents_eq {all(b[n][2] == rb[n][2] for n in rb)} max_headtail_diff {maxd:.1e} max_restmatrix_diff {maxm:.1e} objmatrix_diff {mw:.1e}")
    print(f"   tris {tris} mats {[s.material.name for s in m.material_slots]} takes {takes(path)} actions {len(bpy.data.actions)} "
          f"height_cm {(max(zs)-min(zs))*100:.1f} max_infl {infl} min_wsum {wsum:.3f} unknown_groups {bad_groups}")
