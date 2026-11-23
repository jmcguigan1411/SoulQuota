# Re-import each batch-3 reaper FBX and check its armature against the Reaper rig (snapshot of Reaper_Custom.fbx).
import bpy, re
CH = "/home/claude/sq/ArtSource/Characters/"
def load(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=path)
    arm = [o for o in bpy.data.objects if o.type == 'ARMATURE'][0]
    bones = {b.name: (arm.matrix_world @ b.head_local, arm.matrix_world @ b.tail_local, b.parent.name if b.parent else None, b.matrix_local.copy()) for b in arm.data.bones}
    order = [b.name for b in arm.data.bones]
    meshes = [o for o in bpy.data.objects if o.type == 'MESH']
    tris = sum(sum(len(p.vertices) - 2 for p in m.data.polygons) for m in meshes)
    bb = [m.matrix_world @ v.co for m in meshes for v in m.data.vertices]
    mats = sorted({s.material.name for m in meshes for s in m.material_slots if s.material})
    imgs = sorted({n.image.name for m in bpy.data.materials if m.node_tree for n in m.node_tree.nodes if n.type == 'TEX_IMAGE' and n.image})
    maxinf = max(len([g for g in v.groups if g.weight > 1e-4]) for m in meshes for v in m.data.vertices) if meshes else 0
    return arm.matrix_world.copy(), arm.name, bones, order, tris, bb, mats, imgs, maxinf, len(bpy.data.actions), [m.name for m in meshes]
def takes(path):
    d = open(path, "rb").read()
    return len(re.findall(rb"AnimStack", d))
ra, ran, rb, rorder, *_ = load("/tmp/rq/Reaper_Custom.fbx")
for rid in ("Actuary", "Sweep", "Solace", "Pallbearers", "Ectoplasm", "Neckless", "Tickwell"):
    path = CH + "R_%s.fbx" % rid
    a, an, b, order, tris, bb, mats, imgs, maxinf, nact, mnames = load(path)
    names = set(b) == set(rb)
    par = all(b[n][2] == rb[n][2] for n in rb)
    maxd = max(max((b[n][0] - rb[n][0]).length, (b[n][1] - rb[n][1]).length) for n in rb)
    maxm = max(max(abs(x - y) for r1, r2 in zip(b[n][3], rb[n][3]) for x, y in zip(r1, r2)) for n in rb)
    mw = max(abs(x - y) for r1, r2 in zip(a, ra) for x, y in zip(r1, r2))
    zs = [v.z for v in bb]; xs = [v.x for v in bb]; ys = [v.y for v in bb]
    print(f"R_{rid}: armature '{an}' bones {len(b)}/{len(rb)} names {names} parents {par} order {order == rorder} rest_diff {maxd:.1e} "
          f"bone_matrix_diff {maxm:.1e} obj_matrix_diff {mw:.1e}")
    print(f"   meshes {mnames} tris {tris} mats {mats} images {imgs} max_influences {maxinf} actions {nact} animstack_tokens {takes(path)} "
          f"height {100*(max(zs)-min(zs)):.1f} cm  x {min(xs):.2f}..{max(xs):.2f} y {min(ys):.2f}..{max(ys):.2f}")
