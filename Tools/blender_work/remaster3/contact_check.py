# Remaster 3 foot-contact QA: planted-foot drift (ball within 3.5 cm of the floor on consecutive frames),
# floor penetration per frame, end stance vs Idle f1 (feet), first frame vs Idle f1.
import bpy
from mathutils import Vector
bpy.ops.wm.open_mainfile(filepath="/home/claude/sq/ArtSource/Characters/Reaper_Custom.blend")
rig = bpy.data.objects["Rig"]; A = rig.matrix_world; sc = bpy.context.scene
def pts(act, f):
    rig.animation_data.action = act; rig.animation_data.action_slot = act.slots[0]; sc.frame_set(f)
    pb = rig.pose.bones
    return {s: (A @ pb["toes." + s].head, A @ pb["toes." + s].tail, A @ pb["foot." + s].head) for s in "lr"}
idle = pts(bpy.data.actions["Idle"], 1)
NAMES = ["SQ_Rise_FromGrave", "SQ_Reap_Swing", "SQ_Attack_Slash_R2", "SQ_Attack_Chop_R2", "SQ_Attack_Punch_R2", "SQ_Cast_Shoot_R2",
         "SQ_Cast_Raise_R2", "SQ_Throw_R2", "SQ_Attack_Slam_R3", "SQ_Attack_Thrust_R3"]
for n in NAMES:
    a = bpy.data.actions[n]; N = int(a.frame_range[1])
    P = [pts(a, f) for f in range(1, N + 1)]
    drift = 0.0; pen = []
    for s in "lr":
        for i in range(1, N):
            b0, b1 = P[i - 1][s][0], P[i][s][0]
            if b0.z < 0.035 and b1.z < 0.035 and b0.z > -0.01:
                drift = max(drift, (Vector((b1.x - b0.x, b1.y - b0.y))).length)
        for i in range(N):
            z = min(P[i][s][0].z, P[i][s][1].z)
            if z < 0.024:      # rest ball height is 0.026
                pen.append((i + 1, s, round(z * 100, 1)))
    end = max((P[-1][s][k] - idle[s][k]).length for s in "lr" for k in range(3))
    start = max((P[0][s][k] - idle[s][k]).length for s in "lr" for k in range(3))
    penf = sorted(set(p[0] for p in pen))
    print(f"{n:22s} planted-drift max {drift*100:.2f} cm/f | start vs idle {start*100:.2f} cm | end vs idle {end*100:.2f} cm | "
          f"penetration frames: {(str(penf[0])+'-'+str(penf[-1])) if penf else 'none'} ({len(pen)} foot-frames)")
