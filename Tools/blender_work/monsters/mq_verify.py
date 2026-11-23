# Re-import every exported monster FBX and check it against the KayKit owner skeleton.
import bpy, re, sys, os
from mathutils import Vector
CH = "/home/claude/sq/ArtSource/Characters/"
KAY = "/mnt/user-data/outputs/ArtSource/Characters/"

def load(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=path)
    arm = [o for o in bpy.data.objects if o.type == 'ARMATURE'][0]
    bones = {b.name: (arm.matrix_world @ b.head_local, arm.matrix_world @ b.tail_local, b.parent.name if b.parent else None) for b in arm.data.bones}
    meshes = [o for o in bpy.data.objects if o.type == 'MESH']
    tris = sum(sum(len(p.vertices) - 2 for p in m.data.polygons) for m in meshes)
    bb = []
    for m in meshes:
        bb += [m.matrix_world @ v.co for v in m.data.vertices]
    mats = sorted({s.material.name for m in meshes for s in m.material_slots if s.material})
    groups = sorted({g.name for m in meshes for g in m.vertex_groups})
    return arm, bones, tris, bb, mats, groups, sorted(a.name for a in bpy.data.actions)

def takes(path):
    d = open(path, "rb").read()
    return sorted(set(m.decode() for m in re.findall(rb"([\x20-\x7e]{1,60})\x00\x01AnimStack", d)))

ref = {}
for owner in ("Minion", "Mage"):
    a, b, t, bb, mats, g, acts = load(KAY + "Skeleton_%s.fbx" % owner)
    ref[owner] = (b, a.name, tuple(tuple(round(x, 6) for x in r) for r in a.matrix_world))
for model, owner in (("Zombie", "Minion"), ("ZombieBloat", "Minion"), ("ZombieRunner", "Minion"), ("Wraith", "Mage")):
    path = CH + model + ".fbx"
    a, b, t, bb, mats, g, acts = load(path)
    rb, rname, rmw = ref[owner]
    same_names = set(b) == set(rb)
    same_par = all(b[n][2] == rb[n][2] for n in rb if n in b)
    maxd = max(max((b[n][0] - rb[n][0]).length, (b[n][1] - rb[n][1]).length) for n in rb if n in b)
    zs = [v.z for v in bb]; xs = [v.x for v in bb]; ys = [v.y for v in bb]
    print(f"{model}: owner {owner} bones {len(b)} names_equal {same_names} parents_equal {same_par} max_rest_diff {maxd:.2e} "
          f"armature '{a.name}' (owner '{rname}') matrix_equal {tuple(tuple(round(x, 6) for x in r) for r in a.matrix_world) == rmw}")
    print(f"   tris {t} mats {mats} weighted bones {len(g)} actions {len(acts)} takes {takes(path)} "
          f"size x {max(xs)-min(xs):.3f} y {max(ys)-min(ys):.3f} z {min(zs):.3f}..{max(zs):.3f}")
path = CH + "Gravebat.fbx"
a, b, t, bb, mats, g, acts = load(path)
zs = [v.z for v in bb]; xs = [v.x for v in bb]; ys = [v.y for v in bb]
skull = [v for v in bb if abs(v.x) < 0.1 and v.z > 1.45]
print(f"Gravebat: bones {sorted(b)} tris {t} mats {mats} takes {takes(path)} actions {acts}")
print(f"   size x {max(xs)-min(xs):.3f} y {min(ys):.3f}..{max(ys):.3f} z {min(zs):.3f}..{max(zs):.3f} (front = -Y: skull min y {min(v.y for v in skull):.3f})")
# frame ranges after reimport
for act in bpy.data.actions:
    print("   take", act.name, tuple(act.frame_range))
