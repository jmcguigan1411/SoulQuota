# Soul Quota - author extra reaper actions on the KayKit Rig by procedural pose layering.
# All edits are made on armature-space pose matrices (evaluated from a base action),
# then converted back to bone-local loc/rot/scale keys on every frame.
import bpy, math, sys
from mathutils import Vector, Matrix, Quaternion

BLEND_IN = "/home/claude/sq/ArtSource/Characters/Reaper_Custom_base.blend"
BLEND_OUT = "/home/claude/sq/ArtSource/Characters/Reaper_Custom.blend"

bpy.ops.wm.open_mainfile(filepath=BLEND_IN)
sc = bpy.context.scene
rig = bpy.data.objects["Rig"]
A = rig.matrix_world.to_3x3().normalized()
Ainv = A.inverted()
BONES = [b.name for b in rig.data.bones]
CHILDREN = {b.name: [c.name for c in b.children_recursive] for b in rig.data.bones}
REST = {b.name: b.matrix_local.copy() for b in rig.data.bones}
PARENT = {b.name: (b.parent.name if b.parent else None) for b in rig.data.bones}

def set_action(act):
    rig.animation_data.action = act
    if len(act.slots):
        rig.animation_data.action_slot = act.slots[0]

def eval_pose(act, frame):
    set_action(act)
    f = math.floor(frame)
    sc.frame_set(f, subframe=frame - f)
    return {pb.name: pb.matrix.copy() for pb in rig.pose.bones}

# ------------------------------------------------------------ pose ops (world-space inputs)
def W(v):
    return Ainv @ Vector(v)

def head_w(P, b):
    return A @ P[b].translation

def rot_subtree(P, bone, q_world, pivot_world=None):
    if pivot_world is None:
        pivot_world = head_w(P, bone)
    Rw = q_world.to_matrix()
    Ra = (Ainv @ Rw @ A).to_4x4()
    piv = Ainv @ Vector(pivot_world)
    M = Matrix.Translation(piv) @ Ra @ Matrix.Translation(-piv)
    for b in [bone] + CHILDREN[bone]:
        P[b] = M @ P[b]

def move_subtree(P, bone, dw):
    d = Ainv @ Vector(dw)
    for b in [bone] + CHILDREN[bone]:
        P[b] = Matrix.Translation(d) @ P[b]

def frame_rot(P, bone):
    """world rotation of bone relative to its rest orientation"""
    return (A @ P[bone].to_3x3()) @ (A @ REST[bone].to_3x3()).inverted()

def bone_dir_w(P, bone):
    return (A @ P[bone].to_3x3() @ Vector((0, 1, 0))).normalized()

def aim(P, bone, d_world, w=1.0):
    cur = bone_dir_w(P, bone)
    q = cur.rotation_difference(Vector(d_world).normalized())
    q = Quaternion().slerp(q, w)
    rot_subtree(P, bone, q)

def align_slot(P, hand, slot, z_world, y_world, w=1.0):
    """rotate hand so that its weapon slot's +Z points along z_world and +Y towards y_world"""
    z = Vector(z_world).normalized()
    y = Vector(y_world)
    y = (y - z * y.dot(z)).normalized()
    x = y.cross(z)
    T = Matrix((x, y, z)).transposed()
    S = (A @ P[slot].to_3x3()).normalized()
    q = (T @ S.inverted()).to_quaternion()
    q = Quaternion().slerp(q, w)
    rot_subtree(P, hand, q, head_w(P, hand))

def ik2(P, upper, lower, hand, target, pole, w=1.0):
    S = head_w(P, upper)
    L1 = (head_w(P, lower) - S).length
    L2 = (head_w(P, hand) - head_w(P, lower)).length
    D = Vector(target) - S
    d = max(0.05, min(D.length, (L1 + L2) * 0.999))
    dn = D.normalized()
    a = (L1 * L1 + d * d - L2 * L2) / (2 * L1 * d)
    a = max(-1.0, min(1.0, a))
    pv = Vector(pole) - S
    perp = (pv - dn * pv.dot(dn))
    perp = perp.normalized() if perp.length > 1e-6 else Vector((0, 0, -1))
    E = S + dn * (L1 * a) + perp * (L1 * math.sqrt(1 - a * a))
    aim(P, upper, E - S, w)
    aim(P, lower, Vector(target) - head_w(P, lower), w)

def slot_axes(P, slot):
    M = (A @ P[slot].to_3x3()).normalized()
    return M.col[0], M.col[1], M.col[2], A @ P[slot].translation

def left_hand_on_pole(P, w, along=0.30):
    x, y, z, g = slot_axes(P, "handslot.r")
    target = g + z * along
    # hand bone head sits ~6cm above its slot; offset along slot's up-ish axis
    hx, hy, hz, hg = slot_axes(P, "handslot.l")
    off = head_w(P, "hand.l") - hg
    ik2(P, "upperarm.l", "lowerarm.l", "hand.l", target + off, head_w(P, "chest") + Vector((0.6, 0.3, -0.6)), w)
    align_slot(P, "hand.l", "handslot.l", z, y, w)

def yaw(a):
    return Quaternion((0, 0, 1), math.radians(a))

def pitch(a):  # + = lean forward (top toward -Y)
    return Quaternion((1, 0, 0), math.radians(a))

def roll(a):   # + = lean to character's left (+X)
    return Quaternion((0, 1, 0), math.radians(a))

def body_turn(P, q, keep_feet=True):
    """rotate whole upper+lower body around hips but keep legs' world orientation"""
    rot_subtree(P, "hips", q)
    if keep_feet:
        for s in "lr":
            rot_subtree(P, "upperleg." + s, q.inverted())

def knee_bend(P, deg):
    """crouch both legs symmetrically and lower the hips so the feet stay on the ground"""
    if abs(deg) < 1e-4:
        return
    t = math.radians(deg)
    L1 = (REST["lowerleg.l"].translation - REST["upperleg.l"].translation).length
    L2 = (REST["foot.l"].translation - REST["lowerleg.l"].translation).length
    drop = (L1 + L2) * (1 - math.cos(t))
    for s in "lr":
        rot_subtree(P, "upperleg." + s, pitch(-deg))
        rot_subtree(P, "lowerleg." + s, pitch(2 * deg))
        rot_subtree(P, "foot." + s, pitch(-deg))
    move_subtree(P, "hips", (0, 0, -drop))
    # move the leg chains back up isn't needed: whole hips subtree moved, feet follow -> correct for foot height
    for s in "lr":
        pass
    return drop

def chest_dir(P, d):
    """direction given in the character's rest frame, carried by the chest's current rotation"""
    return frame_rot(P, "chest") @ Vector(d).normalized()

# ------------------------------------------------------------ interpolation
def smooth(t):
    return t * t * (3 - 2 * t)

def ease_in(t):
    return t * t * t

def ease_out(t):
    return 1 - (1 - t) ** 3

EASE = {"s": smooth, "i": ease_in, "o": ease_out, "l": lambda t: t}

def track(keys, f):
    """keys: [(frame, value, ease_into_this_key)] values float or tuple"""
    if f <= keys[0][0]:
        return keys[0][1]
    for (f0, v0, _), (f1, v1, e) in zip(keys, keys[1:]):
        if f0 <= f <= f1:
            t = EASE[e]((f - f0) / (f1 - f0))
            if isinstance(v0, (tuple, list, Vector)):
                return tuple(a + (b - a) * t for a, b in zip(v0, v1))
            return v0 + (v1 - v0) * t
    return keys[-1][1]

# ------------------------------------------------------------ writing actions
def to_basis(P):
    out = {}
    for b in BONES:
        p = PARENT[b]
        if p is None:
            out[b] = REST[b].inverted() @ P[b]
        else:
            out[b] = (REST[p].inverted() @ REST[b]).inverted() @ P[p].inverted() @ P[b]
    return out

def write_action(name, base_act, poses):
    """poses: list of P dicts for frames 1..N"""
    act = bpy.data.actions.get(name)
    if act:
        bpy.data.actions.remove(act)
    act = base_act.copy()
    act.name = name
    act.use_fake_user = True
    # remove bone channels, keep object channels
    cb = act.layers[0].strips[0].channelbags[0]
    for fc in list(cb.fcurves):
        if fc.data_path.startswith("pose.bones"):
            cb.fcurves.remove(fc)
    # trim object channels to new range: rewrite them flat from frame 1
    objvals = {}
    for fc in list(cb.fcurves):
        objvals[(fc.data_path, fc.array_index)] = fc.evaluate(1)
        cb.fcurves.remove(fc)
    N = len(poses)
    for (dp, idx), val in objvals.items():
        fc = cb.fcurves.new(dp, index=idx, group_name="Object Transforms")
        fc.keyframe_points.add(2)
        fc.keyframe_points[0].co = (1, val)
        fc.keyframe_points[1].co = (N, val)
        for k in fc.keyframe_points:
            k.interpolation = 'LINEAR'
    bases = [to_basis(P) for P in poses]
    for b in BONES:
        locs, quats, scales = [], [], []
        prev = None
        for B in bases:
            l, q, s = B[b].decompose()
            if prev is not None and prev.dot(q) < 0:
                q = -q
            prev = q
            locs.append(l); quats.append(q); scales.append(s)
        for dp, vals, n in (("location", locs, 3), ("rotation_quaternion", quats, 4), ("scale", scales, 3)):
            for i in range(n):
                fc = cb.fcurves.new(f'pose.bones["{b}"].{dp}', index=i, group_name=b)
                fc.keyframe_points.add(N)
                for k in range(N):
                    fc.keyframe_points[k].co = (k + 1, vals[k][i])
                    fc.keyframe_points[k].interpolation = 'LINEAR'
    act.frame_range = (1, N)
    act.use_frame_range = True
    print("ACTION", name, N, "frames")
    return act

# ------------------------------------------------------------ sanity: basis round trip
ACT = {a.name.replace("Rig|", ""): a for a in bpy.data.actions}
P = eval_pose(ACT["Running_A"], 7)
Bs = to_basis(P)
err = max((Bs[pb.name] - pb.matrix_basis).to_3x3().to_quaternion().angle if False else
          max(abs(x - y) for r1, r2 in zip(Bs[pb.name], pb.matrix_basis) for x, y in zip(r1, r2)) for pb in rig.pose.bones)
print("ROUNDTRIP_ERR", err)

def loop_frame(act, i, N):
    """sample a looping base action cyclically, i in [0,N)"""
    a0, a1 = act.frame_range
    return a0 + (a1 - a0) * (i / N)

# ------------------------------------------------------------ 1. Scythe swing (0.7s)
def scythe_swing():
    base = ACT["2H_Melee_Idle"]
    N = 22
    twist = [(1, 0, "s"), (7, -55, "o"), (9, -45, "s"), (12, 70, "i"), (15, 82, "o"), (22, 0, "s")]
    lean = [(1, 0, "s"), (7, -6, "s"), (12, 14, "i"), (16, 12, "s"), (22, 0, "s")]
    armyaw = [(1, -60, "s"), (7, -75, "s"), (9, -60, "s"), (12, 30, "i"), (15, 55, "o"), (22, 40, "s")]
    wt = [(1, 0, "s"), (5, 1, "s"), (17, 1, "s"), (22, 0, "s")]
    crouch = [(1, 0, "s"), (7, 10, "s"), (12, 16, "i"), (16, 14, "s"), (22, 0, "s")]
    poses = []
    for f in range(1, N + 1):
        P = eval_pose(base, loop_frame(base, f - 1, N))
        tw, w = track(twist, f), track(wt, f)
        body_turn(P, yaw(tw * 0.45))
        rot_subtree(P, "spine", yaw(tw * 0.30) @ pitch(track(lean, f) * 0.6))
        rot_subtree(P, "chest", yaw(tw * 0.25) @ pitch(track(lean, f) * 0.4))
        rot_subtree(P, "head", yaw(-tw * 0.35))
        ay = math.radians(track(armyaw, f))
        # both arms reach out along a horizontal fan relative to the chest
        d = Vector((math.sin(ay), -math.cos(ay), -0.15))
        dr = chest_dir(P, d)
        aim(P, "upperarm.r", chest_dir(P, d + Vector((-0.25, 0.1, 0.0))), w)
        aim(P, "lowerarm.r", dr, w)
        # scythe: pole outward along the reach, blade leading the swing
        zt = dr + Vector((0, 0, 0.25))
        sweep = Vector((0, 0, 1)).cross(dr)  # CCW sweep direction
        align_slot(P, "hand.r", "handslot.r", zt, sweep + Vector((0, 0, -0.3)), w)
        left_hand_on_pole(P, w, 0.28)
        knee_bend(P, track(crouch, f))
        poses.append(P)
    write_action("Reaper_Scythe_Swing", base, poses)

# ------------------------------------------------------------ 2. Scythe slam (0.9s)
def scythe_slam():
    base = ACT["2H_Melee_Idle"]
    N = 28
    # arm elevation angle in the sagittal plane: 90 = straight up, 0 = forward, -40 = down at the ground
    elev = [(1, -10, "s"), (10, 115, "o"), (12, 118, "s"), (15, -12, "i"), (21, -10, "s"), (28, -10, "s")]
    lean = [(1, 0, "s"), (10, -14, "o"), (12, -15, "s"), (15, 28, "i"), (21, 24, "s"), (28, 0, "s")]
    crouch = [(1, 0, "s"), (8, 8, "s"), (10, 2, "s"), (15, 24, "i"), (21, 20, "s"), (28, 0, "s")]
    wt = [(1, 0, "s"), (6, 1, "s"), (22, 1, "s"), (28, 0, "s")]
    poses = []
    for f in range(1, N + 1):
        P = eval_pose(base, loop_frame(base, f - 1, N))
        w = track(wt, f)
        ln = track(lean, f)
        rot_subtree(P, "spine", pitch(ln * 0.55))
        rot_subtree(P, "chest", pitch(ln * 0.45))
        rot_subtree(P, "head", pitch(-ln * 0.4))
        e = math.radians(track(elev, f))
        d = Vector((0, -math.cos(e), math.sin(e)))
        dr = chest_dir(P, d)
        aim(P, "upperarm.r", chest_dir(P, d + Vector((-0.35, 0.15, -0.1))), w)
        aim(P, "lowerarm.r", chest_dir(P, d + Vector((0.12, 0, 0))), w)
        # pole continues the arms, blade faces the direction of the chop (forward/down)
        vel = chest_dir(P, Vector((1, 0, 0))).cross(dr)
        align_slot(P, "hand.r", "handslot.r", dr, vel, w)
        left_hand_on_pole(P, w, -0.22)
        knee_bend(P, track(crouch, f))
        poses.append(P)
    write_action("Reaper_Scythe_Slam", base, poses)

# ------------------------------------------------------------ 3. Carry run (loop)
def carry_run():
    base = ACT["Running_A"]
    N = int(base.frame_range[1] - base.frame_range[0]) + 1  # 25, last == first
    poses = []
    for f in range(1, N + 1):
        ph = 2 * math.pi * (f - 1) / (N - 1)
        P = eval_pose(base, base.frame_range[0] + (f - 1))
        rot_subtree(P, "spine", pitch(12))
        rot_subtree(P, "chest", pitch(4) @ yaw(0))
        rot_subtree(P, "head", pitch(-14))
        bob = 0.08 * math.sin(2 * ph)
        for s, sx in (("l", 1), ("r", -1)):
            aim(P, "upperarm." + s, chest_dir(P, (0.32 * sx, -0.55, -0.75 + bob)))
            aim(P, "lowerarm." + s, chest_dir(P, (-0.75 * sx, -0.55, 0.30 + bob)))
            rot_subtree(P, "hand." + s, Quaternion(bone_dir_w(P, "lowerarm." + s), math.radians(-35 * sx)))
        poses.append(P)
    write_action("Reaper_Carry_Run", base, poses)

# ------------------------------------------------------------ 4. Bank channel (loop 1.2s)
def bank_channel():
    base = ACT["Idle"]
    N = 37  # 36 intervals = 1.2 s, frame 37 == frame 1
    poses = []
    for f in range(1, N + 1):
        t = (f - 1) / (N - 1)
        ph = 2 * math.pi * t
        P = eval_pose(base, loop_frame(base, (f - 1) % (N - 1), N - 1))
        knee_bend(P, 9 + 4 * math.sin(2 * ph))
        rot_subtree(P, "spine", pitch(10 + 3 * math.sin(2 * ph)))
        rot_subtree(P, "chest", pitch(5) @ roll(4 * math.sin(ph)))
        rot_subtree(P, "head", pitch(18 + 4 * math.sin(2 * ph + 0.6)))
        for s, sx in (("l", 1), ("r", -1)):
            k = math.sin(ph + (0 if s == "l" else math.pi))
            aim(P, "upperarm." + s, chest_dir(P, (0.35 * sx, -0.7, -0.55 + 0.12 * k)))
            aim(P, "lowerarm." + s, chest_dir(P, (-0.45 * sx, -0.8, -0.05 + 0.15 * k)))
            rot_subtree(P, "hand." + s, Quaternion(bone_dir_w(P, "lowerarm." + s), math.radians((-60 - 30 * k) * sx)))
        poses.append(P)
    write_action("Reaper_Bank_Channel", base, poses)

# ------------------------------------------------------------ 5. Victory dance (loop)
def victory_dance():
    base = ACT["Idle"]
    N = 49  # 1.6 s, 4 beats
    poses = []
    for f in range(1, N + 1):
        t = (f - 1) / (N - 1)
        ph = 2 * math.pi * t
        P = eval_pose(base, loop_frame(base, (f - 1) % (N - 1), N - 1))
        sway = math.sin(2 * ph)      # 2 sways per loop
        beat = 0.5 - 0.5 * math.cos(8 * ph)  # 4 bounces... (8*ph -> 4 per loop)
        body_turn(P, yaw(18 * sway) @ roll(-6 * sway))
        knee_bend(P, 4 + 14 * beat)
        rot_subtree(P, "spine", roll(8 * sway))
        rot_subtree(P, "chest", yaw(-14 * sway) @ roll(6 * sway))
        rot_subtree(P, "head", roll(-12 * sway) @ pitch(10 * math.sin(8 * ph)))
        pump = math.sin(8 * ph)
        for s, sx in (("l", 1), ("r", -1)):
            alt = math.sin(2 * ph + (0 if s == "l" else math.pi))
            aim(P, "upperarm." + s, chest_dir(P, (0.75 * sx, -0.15, 0.55 + 0.35 * alt)))
            aim(P, "lowerarm." + s, chest_dir(P, (0.15 * sx, -0.25 * pump, 1.0)))
            rot_subtree(P, "hand." + s, Quaternion(bone_dir_w(P, "lowerarm." + s), math.radians(40 * pump * sx)))
        poses.append(P)
    write_action("Reaper_Victory_Dance", base, poses)

# ------------------------------------------------------------ 6. Clipboard taunt (2.0s)
def taunt_clipboard():
    base = ACT["Idle"]
    N = 61
    wt = [(1, 0, "s"), (9, 1, "s"), (52, 1, "s"), (61, 0, "s")]
    look = [(1, 0, "s"), (9, 26, "s"), (26, 24, "s"), (30, 8, "s"), (48, 10, "s"), (61, 0, "s")]
    shake = [(1, 0, "s"), (29, 0, "s"), (32, 24, "o"), (36, -24, "s"), (40, 22, "s"), (44, -20, "s"), (48, 0, "s"), (61, 0, "s")]
    slump = [(1, 0, "s"), (28, 2, "s"), (34, 12, "s"), (48, 10, "s"), (61, 0, "s")]
    tapw = [(1, 0, "s"), (11, 0, "s"), (15, 1, "s"), (26, 1, "s"), (31, 0, "s"), (61, 0, "s")]
    poses = []
    for f in range(1, N + 1):
        P = eval_pose(base, loop_frame(base, (f - 1) % 32, 32))
        w = track(wt, f)
        rot_subtree(P, "spine", pitch(track(slump, f) * 0.5))
        rot_subtree(P, "chest", pitch(track(slump, f) * 0.5))
        rot_subtree(P, "head", yaw(track(shake, f)) @ pitch(track(look, f)))
        # left arm holds the clipboard in front of the belly, board tilted up to the face
        aim(P, "upperarm.l", chest_dir(P, (0.40, -0.30, -0.86)), w)
        aim(P, "lowerarm.l", chest_dir(P, (-0.55, -0.80, 0.25)), w)
        align_slot(P, "hand.l", "handslot.l", chest_dir(P, (-0.3, -0.95, 0.12)), chest_dir(P, (0.0, -0.15, 1.0)), w)
        # right hand taps the board with a finger
        tw = track(tapw, f) * w
        tap = 0.12 * max(0.0, math.sin(2 * math.pi * (f - 13) / 5.0))
        aim(P, "upperarm.r", chest_dir(P, (-0.35, -0.35, -0.85)), tw)
        aim(P, "lowerarm.r", chest_dir(P, (0.55, -0.75, 0.30 + tap)), tw)
        poses.append(P)
    write_action("Reaper_Taunt_Clipboard", base, poses)

scythe_swing()
scythe_slam()
carry_run()
bank_channel()
victory_dance()
taunt_clipboard()

# leave rig on Idle
set_action(ACT["Idle"])
sc.frame_set(1)
bpy.ops.wm.save_as_mainfile(filepath=BLEND_OUT)
print("NACTIONS", len(bpy.data.actions))
