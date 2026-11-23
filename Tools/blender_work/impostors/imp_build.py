# Soul Quota: far-distance flipbook impostors (original work).
# For one enemy model: pose its skeletal FBX with its main move take at 4 evenly spaced loop phases, bake the armature
# into a static mesh (world space = the skeletal mesh's bind/component space: same axes, same cm scale, rig root/feet
# at the origin), decimate to ~600-900 tris, re-project palette UVs + material slots from the full-res posed mesh,
# and export ArtSource/Impostors/<Model>_Walk_<i>.fbx with the same FBX axis/unit settings as the character FBXs.
# usage: python3 imp_build.py <Model> [<Model>...]
import bpy, bmesh, sys, os, json, math
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from mathutils.geometry import closest_point_on_tri, barycentric_transform

CH = "/home/claude/sq/ArtSource/Characters/"
OUT = "/home/claude/sq/ArtSource/Impostors/"
POSES = 4
TARGET = 820          # total tris per pose (allowed 600-900)
GLOW_MAX = 70         # glow-slot tris kept at most (eyes stay readable)

# model -> (mesh FBX, FBX that holds the take (None = same file), take)
MODELS = {
    "Minion": ("Skeleton_Minion.fbx", None, "Walking_D_Skeletons"),
    "Warrior": ("Skeleton_Warrior.fbx", None, "SQ_Run_Heavy"),
    "Rogue": ("Skeleton_Rogue.fbx", None, "SQ_Run_Sprint"),
    "Mage": ("Skeleton_Mage.fbx", None, "SQ_Walk_Waddle"),
    "Zombie": ("Zombie.fbx", "Skeleton_Minion.fbx", "SQ_Enemy_Shamble"),
    "ZombieBloat": ("ZombieBloat.fbx", "Skeleton_Minion.fbx", "SQ_Walk_Waddle"),
    "ZombieRunner": ("ZombieRunner.fbx", "Skeleton_Minion.fbx", "SQ_Run_Sprint"),
    "Wraith": ("Wraith.fbx", "Skeleton_Mage.fbx", "SQ_Run_Float"),
    "Gravebat": ("Gravebat.fbx", None, "Fly"),
}


def find_action(take):
    for a in bpy.data.actions:
        if a.name == take or a.name.endswith("|" + take):
            return a
    raise RuntimeError("take not found: " + take)


def load(model):
    mesh_fbx, anim_fbx, take = MODELS[model]
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.fps = 30
    if anim_fbx:
        # take source first: keep its actions, drop its meshes and rig
        bpy.ops.import_scene.fbx(filepath=CH + anim_fbx)
        act = find_action(take)
        act.use_fake_user = True
        for o in list(bpy.data.objects):
            bpy.data.objects.remove(o)
        for a in list(bpy.data.actions):
            if a != act:
                bpy.data.actions.remove(a)
        act.name = "TAKE_" + take
    bpy.ops.import_scene.fbx(filepath=CH + mesh_fbx)
    rig = [o for o in bpy.data.objects if o.type == 'ARMATURE'][0]
    act = find_action(take) if not anim_fbx else bpy.data.actions["TAKE_" + take]
    if rig.animation_data is None:
        rig.animation_data_create()
    rig.animation_data.action = act
    if len(act.slots) and rig.animation_data.action_slot is None:
        rig.animation_data.action_slot = act.slots[0]
    meshes = [o for o in bpy.data.objects if o.type == 'MESH']
    return rig, act, meshes


def ordered_materials(meshes):
    mats = []
    for o in meshes:
        for m in o.data.materials:
            if m and m not in mats:
                mats.append(m)
    return [m for m in mats if "glow" not in m.name.lower()] + [m for m in mats if "glow" in m.name.lower()]


def posed_bmesh(meshes, mats):
    """world-space evaluated mesh of every mesh object, triangulated, material index remapped to `mats` order"""
    dg = bpy.context.evaluated_depsgraph_get()
    bm = bmesh.new()
    uv_l = bm.loops.layers.uv.new("UVMap")
    for o in meshes:
        ev = o.evaluated_get(dg)
        me = ev.to_mesh()
        src = bmesh.new()
        src.from_mesh(me)
        src.transform(o.matrix_world)
        bmesh.ops.triangulate(src, faces=src.faces)
        suv = src.loops.layers.uv.active
        remap = [mats.index(m) if m in mats else 0 for m in o.data.materials] or [0]
        vmap = {}
        for v in src.verts:
            vmap[v] = bm.verts.new(v.co)
        for f in src.faces:
            try:
                nf = bm.faces.new([vmap[v] for v in f.verts])
            except ValueError:
                continue
            nf.material_index = remap[min(f.material_index, len(remap) - 1)]
            for nl, l in zip(nf.loops, f.loops):
                nl[uv_l].uv = l[suv].uv if suv else (0.5, 0.5)
        src.free()
        ev.to_mesh_clear()
    return bm


def bm_to_obj(bm, name, mats):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    for m in mats:
        me.materials.append(m)
    return ob


def decimate_obj(ob, target):
    n0 = len(ob.data.polygons)
    if n0 <= target:
        return
    mod = ob.modifiers.new("dec", 'DECIMATE')
    mod.decimate_type = 'COLLAPSE'
    mod.use_collapse_triangulate = True
    ratio = target / n0
    dg = bpy.context.evaluated_depsgraph_get()
    for _ in range(8):  # aim for the target count
        mod.ratio = max(0.01, min(1.0, ratio))
        dg.update()
        n = len(ob.evaluated_get(dg).data.polygons)
        if abs(n - target) <= target * 0.03:
            break
        ratio *= target / max(n, 1)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.modifier_apply(modifier=mod.name)


def split_by_material(bm, idx):
    out = bm.copy()
    bmesh.ops.delete(out, geom=[f for f in out.faces if f.material_index != idx], context='FACES')
    return out


def reproject(ob, src_bm, idx):
    """per-loop UVs from the closest point on the nearest full-res triangle of the same slot (flat palette colours,
    gradients kept, no bleeding across swatches)"""
    sub = split_by_material(src_bm, idx)
    sub.faces.ensure_lookup_table()
    tree = BVHTree.FromBMesh(sub)
    uv_s = sub.loops.layers.uv.active
    me = ob.data
    if not me.uv_layers:
        me.uv_layers.new(name="UVMap")
    uvd = me.uv_layers[0].data
    for p in me.polygons:
        p.material_index = idx
        c = p.center
        hit = tree.find_nearest(c)
        if hit[2] is None:
            continue
        f = sub.faces[hit[2]]
        a, b, cc = (l.vert.co for l in f.loops)
        ua, ub, uc = (l[uv_s].uv.to_3d() for l in f.loops)
        for li in p.loop_indices:
            co = me.vertices[me.loops[li].vertex_index].co
            # pull the vertex halfway to the face centre so a corner on a colour border keeps the face's colour
            q = closest_point_on_tri(co.lerp(c, 0.5), a, b, cc)
            uv = barycentric_transform(q, a, b, cc, ua, ub, uc)
            uvd[li].uv = (uv.x, uv.y)
    sub.free()


def export_static(ob, path):
    bpy.ops.object.select_all(action='DESELECT')
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.fbx(
        filepath=path, use_selection=True, object_types={'MESH'},
        apply_unit_scale=True, global_scale=1.0, apply_scale_options='FBX_SCALE_UNITS',
        axis_forward='-Z', axis_up='Y', use_space_transform=True, bake_space_transform=False,
        mesh_smooth_type='FACE', use_mesh_modifiers=False, add_leaf_bones=False,
        bake_anim=False, path_mode='STRIP')


def bounds(obs_or_bm):
    if isinstance(obs_or_bm, bmesh.types.BMesh):
        cos = [v.co for v in obs_or_bm.verts]
    else:
        cos = [o.matrix_world @ v.co for o in obs_or_bm for v in o.data.vertices]
    return [round(min(c[i] for c in cos) * 100, 1) for i in range(3)], [round(max(c[i] for c in cos) * 100, 1) for i in range(3)]


def build(model):
    rig, act, meshes = load(model)
    sc = bpy.context.scene
    mats = ordered_materials(meshes)
    r0, r1 = act.frame_range
    L = r1 - r0  # loops: last frame == first frame
    info = {"model": model, "take": MODELS[model][2], "frames": [r0, r1], "cycle_s": round(L / 30.0, 4),
            "materials": [m.name for m in mats], "poses": []}
    # bind-pose bounds (what UE's skeletal GetImportedBounds sees)
    act_keep = rig.animation_data.action
    rig.animation_data.action = None
    for pb in rig.pose.bones:
        pb.matrix_basis.identity()
    sc.frame_set(int(r0))
    bm_rest = posed_bmesh(meshes, mats)
    info["bind_bounds_cm"] = bounds(bm_rest)
    bm_rest.free()
    rig.animation_data.action = act_keep
    if len(act_keep.slots) and rig.animation_data.action_slot is None:
        rig.animation_data.action_slot = act_keep.slots[0]
    os.makedirs(OUT, exist_ok=True)
    for i in range(POSES):
        fr = r0 + L * i / POSES
        sc.frame_set(int(math.floor(fr)), subframe=fr - math.floor(fr))
        src = posed_bmesh(meshes, mats)
        full_tris = len(src.faces)
        parts = []
        glow_idx = [k for k, m in enumerate(mats) if "glow" in m.name.lower()]
        glow_tris = sum(1 for f in src.faces if f.material_index in glow_idx)
        glow_keep = min(glow_tris, GLOW_MAX)
        for k, m in enumerate(mats):
            sub = split_by_material(src, k)
            if not sub.faces:
                sub.free()
                continue
            ob = bm_to_obj(sub, "%s_part%d" % (model, k), mats)
            sub.free()
            body_target = TARGET - glow_keep
            body_total = full_tris - glow_tris
            if k in glow_idx:
                tgt = max(8, int(round(glow_keep * len(ob.data.polygons) / max(glow_tris, 1))))
            else:
                tgt = max(8, int(round(body_target * len(ob.data.polygons) / max(body_total, 1))))
            decimate_obj(ob, tgt)
            reproject(ob, src, k)
            parts.append(ob)
        bpy.ops.object.select_all(action='DESELECT')
        for o in parts:
            o.select_set(True)
        bpy.context.view_layer.objects.active = parts[0]
        bpy.ops.object.join()
        ob = bpy.context.view_layer.objects.active
        name = "%s_Walk_%d" % (model, i)
        ob.name = name
        ob.data.name = name
        # drop zero-area leftovers, keep slot order (body, glow)
        bm = bmesh.new()
        bm.from_mesh(ob.data)
        bmesh.ops.dissolve_degenerate(bm, dist=1e-5, edges=bm.edges)
        bmesh.ops.triangulate(bm, faces=bm.faces)
        bm.to_mesh(ob.data)
        bm.free()
        assert [m.name for m in ob.data.materials] == [m.name for m in mats], [m.name for m in ob.data.materials]
        for p in ob.data.polygons:
            p.use_smooth = False
        tris = len(ob.data.polygons)
        path = OUT + name + ".fbx"
        export_static(ob, path)
        info["poses"].append({"name": name, "frame": round(fr, 3), "tris": tris, "full_tris": full_tris,
                              "bounds_cm": bounds([ob]), "slots": [m.name for m in ob.data.materials],
                              "glow_tris": sum(1 for p in ob.data.polygons if p.material_index in glow_idx)})
        print("POSE", json.dumps(info["poses"][-1]))
        bpy.data.objects.remove(ob)
        src.free()
    return info


if __name__ == "__main__":
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    report = {}
    rp = "/home/claude/blender_work/impostors/report.json"
    if os.path.isfile(rp):
        report = json.load(open(rp))
    for m in args:
        report[m] = build(m)
        json.dump(report, open(rp, "w"), indent=1)
        print("DONE", m)
