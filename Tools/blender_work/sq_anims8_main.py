exec(open("/home/claude/blender_work/sq_anims8.py").read())
for _p in sorted(__import__("glob").glob("/home/claude/blender_work/sq_anims8_*.py")):
    if not _p.endswith("_main.py"):
        exec(open(_p).read())
for rid in IDS:
    for fam, fn in FAMS.items():
        if want(rid, fam):
            fn(rid)
bpy.ops.wm.save_as_mainfile(filepath=BLEND)
print("DONE8", len(DONE8))
print("STRIDE2", " | ".join(STRIDE_REPORT))
# ---- finalize: marker, drop superseded takes nothing references, stride report
for n in ["SQ_Idle_Remaster", "SQ_Run_Remaster", "SQ_Walk_Remaster"] + ["SQ_Marker_Remaster%d" % i for i in range(2, 9)]:
    a = bpy.data.actions.get(n)
    if a and not ONLY_ID and not ONLY_FAM:
        bpy.data.actions.remove(a)
if not ONLY_ID and not ONLY_FAM:
    idle = ACT["Idle"]
    m = write_action("SQ_Marker_Remaster9", idle, [eval_pose(idle, 1), eval_pose(idle, 2)])
    m["sq_loop"] = False
    rep = []
    for rid in IDS:
        if rid in FLOAT:
            rep.append("%s float" % rid); continue
        act = bpy.data.actions["SQ_Run_" + rid]; N = int(act.frame_range[1])
        ys = [head_w(eval_pose(act, f), "foot.l").y for f in range(1, N + 1)]
        v = max((ys[i + 1] - ys[i]) * 30 for i in range(N - 1))      # fastest backward (+Y) foot speed = stance speed
        rep.append("%s N=%d stance %.2f u/s skate x%.1f" % (rid, N, v, 5.91 / max(0.01, v)))
    print("STRIDE", " | ".join(rep))
set_action(ACT["Idle"]); sc.frame_set(1)
bpy.ops.wm.save_as_mainfile(filepath=BLEND)
print("SAVED", len(bpy.data.actions))
