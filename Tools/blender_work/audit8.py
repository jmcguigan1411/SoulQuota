# numeric audit of the hero takes: feet under the ground, loop seam velocity, impact frame hand speed peak
import bpy, math, sys
_src = open("/home/claude/blender_work/sq_anims2.py").read()
bpy_open = "/home/claude/sq/ArtSource/Characters/Reaper_Custom.blend"
exec(compile(_src[:_src.index("# ============================================================ REMASTER 2026-11-07: overlap")].replace('ONLY = sys.argv[sys.argv.index("--only") + 1].split(",") if "--only" in sys.argv else None', 'ONLY = None'), "h", "exec"))
ids = sys.argv[sys.argv.index("--ids") + 1].split(",")
nz = min(head_w(eval_pose(ACT["Idle"], 1), "foot.l").z, head_w(eval_pose(ACT["Idle"], 1), "foot.r").z)
bad = []
for rid in ids:
    for fam in ("Run", "Walk", "Idle", "Sig", "E", "R", "Hit", "Downed", "Revive"):
        a = ACT.get("SQ_%s_%s" % (fam, rid))
        if not a: bad.append("MISSING SQ_%s_%s" % (fam, rid)); continue
        N = int(a.frame_range[1])
        Ps = [eval_pose(a, f) for f in range(1, N + 1)]
        minz = min(min(head_w(P, "foot.l").z, head_w(P, "foot.r").z) for P in Ps)
        msg = ""
        if minz < nz - 0.03 and fam not in ("Downed", "Revive") and rid not in ("Grim", "Widow", "Ectoplasm"): msg += " feet %.2f under" % (nz - minz)
        if a.get("sq_loop"):
            d0 = sum((head_w(Ps[1], b) - head_w(Ps[0], b)).length for b in ("hand.l", "hand.r", "head", "hips"))
            d1 = sum((head_w(Ps[-1], b) - head_w(Ps[-2], b)).length for b in ("hand.l", "hand.r", "head", "hips"))
            sm = sum((head_w(Ps[-1], b) - head_w(Ps[0], b)).length for b in ("hand.l", "hand.r", "head", "hips"))
            if sm > 0.01: msg += " seam gap %.3f" % sm
            if abs(d0 - d1) > 0.6 * max(d0, d1, 0.02): msg += " seam speed %.3f vs %.3f" % (d0, d1)
        if fam in ("Sig", "E", "R"):
            I = a.get("sq_impact", 0)
            sp = [max((head_w(Ps[i + 1], h) - head_w(Ps[i], h)).length for h in ("hand.l", "hand.r")) for i in range(N - 1)]
            pk = 2 + max(range(N - 1), key=lambda i: sp[i])
            if abs(pk - I) > 2: msg += " hand speed peaks f%d, impact f%d" % (pk, I)
        if msg: bad.append("SQ_%s_%s:%s" % (fam, rid, msg))
print("AUDIT", len(bad)); [print("  ", b) for b in bad]
