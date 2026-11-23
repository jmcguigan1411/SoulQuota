# Verify the re-exported FBXs by re-importing them and comparing with their sources.
import bpy, re, struct, math
from mathutils import Vector

SRC = "/mnt/user-data/outputs/ArtSource/Characters/"
OUT = "/home/claude/sq/ArtSource/Characters/"
DEF = ["hips", "spine", "chest", "head", "hand.l", "hand.r", "foot.l", "foot.r", "toes.l", "toes.r", "lowerarm.l", "lowerleg.r"]

def header(p):
    d = open(p, "rb").read(); out = {}
    for key in (b"UnitScaleFactor", b"UpAxis", b"UpAxisSign", b"FrontAxis", b"FrontAxisSign", b"CoordAxis", b"CoordAxisSign"):
        i = d.find(b"S" + struct.pack("<I", len(key)) + key); j = i + 5 + len(key); v = None
        for _ in range(4):
            t = d[j:j + 1]
            if t == b"S":
                n = struct.unpack("<I", d[j + 1:j + 5])[0]; j += 5 + n
            elif t == b"D":
                v = struct.unpack("<d", d[j + 1:j + 9])[0]; break
            elif t == b"I":
                v = struct.unpack("<i", d[j + 1:j + 5])[0]; break
            else:
                break
        out[key.decode()] = v
    takes = set(m.decode() for m in re.findall(rb"([\x20-\x7e]{1,60})\x00\x01AnimStack", d))
    return out, takes

def load(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.render.fps = 30
    bpy.ops.import_scene.fbx(filepath=path)
    rig = [o for o in bpy.data.objects if o.type == 'ARMATURE'][0]
    acts = {a.name.replace("Rig|", ""): a for a in bpy.data.actions}
    rig.data.pose_position = 'REST'; bpy.context.view_layer.update()
    dims = tuple(rig.dimensions)
    rig.data.pose_position = 'POSE'; bpy.context.view_layer.update()
    bones = {b.name: (rig.matrix_world @ b.head_local, rig.matrix_world @ b.tail_local) for b in rig.data.bones}
    meshes = {}
    for o in bpy.data.objects:
        if o.type == 'MESH':
            uvs = [l.name for l in o.data.uv_layers]
            uvsum = sum(sum(abs(x) for x in d.uv) for d in o.data.uv_layers[0].data) if uvs else 0
            mats = [m.name.split(".")[0] if m else None for m in o.data.materials]
            imgs = sorted(set(n.image.name.split(".")[0] for m in o.data.materials if m and m.use_nodes
                              for n in m.node_tree.nodes if n.type == 'TEX_IMAGE' and n.image))
            meshes[o.name] = dict(v=len(o.data.vertices), p=len(o.data.polygons), mats=mats, uvs=len(uvs), uvsum=round(uvsum, 2),
                                  par=(o.parent_type, o.parent_bone), vg=len(o.vertex_groups), imgs=imgs)
    return rig, acts, dims, rig.matrix_world.copy(), bones, meshes

def sample(rig, act, f):
    rig.animation_data.action = act
    rig.animation_data.action_slot = act.slots[0]
    bpy.context.scene.frame_set(f)
    return {b: rig.matrix_world @ rig.pose.bones[b].head for b in DEF}

def capture(path, names):
    """posed bone samples for named actions at their middle frame + frame 1"""
    rig, acts, dims, mw, bones, meshes = load(path)
    res = {}
    for n in names:
        a = acts[n]
        mid = int((a.frame_range[0] + a.frame_range[1]) // 2)
        res[n] = (sample(rig, a, 1), sample(rig, a, mid), tuple(a.frame_range))
    return rig, acts, dims, mw, bones, meshes, res

def maxdiff(A, B):
    return max((A[k] - B[k]).length for k in A)

# reference samples of the SQ actions straight from the reaper .blend
bpy.ops.wm.open_mainfile(filepath=OUT + "Reaper_Custom.blend")
rr = bpy.data.objects["Rig"]
SQ = sorted(a.name for a in bpy.data.actions if a.name.startswith("SQ_"))
REF = {}
for n in SQ:
    a = bpy.data.actions[n]
    mid = int((a.frame_range[0] + a.frame_range[1]) // 2)
    REF[n] = (sample(rr, a, 1), sample(rr, a, mid), tuple(a.frame_range))
OLD = ["Running_A", "Idle", "Cheer", "Walking_A"]

ok_all = True
for name, src, ref_label in [("Reaper_Custom", "Skeleton_Mage", "rig from Skeleton_Mage"),
                             ("Skeleton_Mage", "Skeleton_Mage", None), ("Skeleton_Rogue", "Skeleton_Rogue", None),
                             ("Skeleton_Warrior", "Skeleton_Warrior", None), ("Skeleton_Minion", "Skeleton_Minion", None)]:
    print("=" * 30, name)
    hs, ts = header(SRC + src + ".fbx")
    ho, to = header(OUT + name + ".fbx")
    print("header src", hs); print("header out", ho, "equal:", hs == ho)
    s_rig, s_acts, s_dims, s_mw, s_bones, s_meshes, s_res = capture(SRC + src + ".fbx", OLD)
    s_names = set(s_acts); s_ranges = {n: tuple(a.frame_range) for n, a in s_acts.items()}
    o_rig, o_acts, o_dims, o_mw, o_bones, o_meshes, o_res = capture(OUT + name + ".fbx", OLD + SQ)
    o_names = set(o_acts)
    extra_expected = set(SQ) | ({"Reaper_Scythe_Swing", "Reaper_Scythe_Slam", "Reaper_Carry_Run", "Reaper_Bank_Channel",
                                 "Reaper_Victory_Dance", "Reaper_Taunt_Clipboard"} if name == "Reaper_Custom" else set())
    print("actions src", len(s_names), "out", len(o_names), "| missing originals:", sorted(s_names - o_names),
          "| unexpected:", sorted(o_names - s_names - extra_expected), "| missing new:", sorted(extra_expected - o_names))
    print("FBX takes in file:", len(to), "plain names (no 'Rig|'):", all("|" not in t for t in to))
    rng_bad = [n for n in s_names if n in o_acts and tuple(o_acts[n].frame_range) != s_ranges[n]]
    print("original frame ranges changed:", rng_bad)
    sq_rng_bad = [n for n in SQ if tuple(o_acts[n].frame_range) != REF[n][2]]
    print("SQ frame ranges differ from reaper blend:", sq_rng_bad)
    print("armature rest dims src", tuple(round(x, 4) for x in s_dims), "out", tuple(round(x, 4) for x in o_dims),
          "| matrix_world equal:", all(abs(a - b) < 1e-6 for r1, r2 in zip(s_mw, o_mw) for a, b in zip(r1, r2)))
    bd = max(max((s_bones[n][0] - o_bones[n][0]).length, (s_bones[n][1] - o_bones[n][1]).length) for n in s_bones)
    print("bone names equal:", set(s_bones) == set(o_bones), "max rest head/tail diff (m):", round(bd, 7))
    if name != "Reaper_Custom":
        mesh_bad = []
        for mn, sm in s_meshes.items():
            om = o_meshes.get(mn)
            if om is None or any(sm[k] != om[k] for k in ("v", "p", "mats", "uvs", "par", "vg", "imgs")) or abs(sm["uvsum"] - om["uvsum"]) > 0.05:
                mesh_bad.append((mn, sm, om))
        print("meshes:", len(s_meshes), "->", len(o_meshes), "| vertex counts", {k: v["v"] for k, v in o_meshes.items()})
        print("mesh mismatches (verts/faces/materials/UVs/parent bone/vertex groups/images):", mesh_bad if mesh_bad else "none")
    else:
        print("reaper meshes:", {k: (v["v"], v["p"], v["mats"]) for k, v in o_meshes.items()})
    od = max(max(maxdiff(s_res[n][0], o_res[n][0]), maxdiff(s_res[n][1], o_res[n][1])) for n in OLD)
    sd = max(max(maxdiff(REF[n][0], o_res[n][0]), maxdiff(REF[n][1], o_res[n][1])) for n in SQ)
    print(f"posed bones, original actions vs source: max {od*100:.4f} cm | SQ actions vs reaper.blend: max {sd*100:.4f} cm")
    good = (hs == ho and not (s_names - o_names) and not (extra_expected - o_names) and not rng_bad and not sq_rng_bad
            and bd < 1e-5 and od < 0.001 and sd < 0.01)   # SQ tolerance 1 cm: FBX key simplification (simplify_factor=1.0)
    ok_all &= good
    print("RESULT", name, "PASS" if good else "CHECK")
print("ALL_PASS", ok_all)
