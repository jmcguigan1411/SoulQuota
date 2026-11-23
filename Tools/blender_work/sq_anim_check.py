# Numeric QA for the SQ_ actions on the custom reaper.
import bpy, math
from mathutils import Vector
bpy.ops.wm.open_mainfile(filepath="/home/claude/sq/ArtSource/Characters/Reaper_Custom.blend")
rig = bpy.data.objects["Rig"]; sc = bpy.context.scene; A = rig.matrix_world
DEF = ["hips", "spine", "chest", "head", "upperarm.l", "lowerarm.l", "hand.l", "upperarm.r", "lowerarm.r", "hand.r",
       "upperleg.l", "lowerleg.l", "foot.l", "toes.l", "upperleg.r", "lowerleg.r", "foot.r", "toes.r"]
for a in sorted([a for a in bpy.data.actions if a.name.startswith("SQ_")], key=lambda a: a.name):
    rig.animation_data.action = a; rig.animation_data.action_slot = a.slots[0]
    N = int(a.frame_range[1]); loop = bool(a.get("sq_loop"))
    pos = []
    for f in range(1, N + 1):
        sc.frame_set(f)
        pb = rig.pose.bones
        d = {b: A @ pb[b].head for b in DEF}
        d.update({b + "_t": A @ pb[b].tail for b in ("toes.l", "toes.r", "hand.l", "hand.r", "head")})
        d["root"] = A @ pb["root"].head
        pos.append(d)
    seam = max((pos[0][k] - pos[-1][k]).length for k in pos[0]) if loop else 0.0
    rootdev = max(Vector((p["root"].x, p["root"].y)).length for p in pos)
    minz = min(min(p["toes.l"].z, p["toes.r"].z, p["toes.l_t"].z, p["toes.r_t"].z) for p in pos)
    # biggest jump between consecutive frames (pops), for any tracked point
    jump = 0.0; jf = 0
    for i in range(1, N):
        j = max((pos[i][k] - pos[i - 1][k]).length for k in pos[i])
        if j > jump:
            jump, jf = j, i + 1
    # stance sliding: frames where a ball-of-foot is on the ground (z<0.035): its y speed should be steady
    slide = []
    for s in "lr":
        for i in range(1, N):
            p0, p1 = pos[i - 1]["toes." + s], pos[i]["toes." + s]
            if p0.z < 0.03 and p1.z < 0.03:
                slide.append(abs(p1.x - p0.x))   # sideways skating per frame
    sk = max(slide) if slide else 0.0
    hip_hi = max(p["hips"].z for p in pos)
    print(f"{a.name:26s} N={N:3d} loop={loop!s:5s} seam={seam*100:5.2f}cm root_xy={rootdev*100:4.2f}cm "
          f"min_toe_z={minz*100:5.1f}cm max_jump={jump*100:5.1f}cm@f{jf} side_skate={sk*100:4.2f}cm/f hips_max_z={hip_hi:.3f}")
