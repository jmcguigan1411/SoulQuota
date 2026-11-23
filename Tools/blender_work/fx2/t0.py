import sys, time; sys.path.insert(0,'/home/claude/blender_work/fx2')
import render as R, prims as F, sim
from mathutils import Vector
t=time.time()
R.setup(res=(640,360))
R.actor("reaper", R.ART+"Characters/R_Grim.fbx", (0,0,0), 0, 175)
for i,(x,y) in enumerate(((260,-60),(330,90),(420,-140))): R.actor("z%d"%i, R.ART+"Characters/Zombie.fbx", (x,y,0), 180, 150)
print("setup", time.time()-t)
col = sim.hx(0xE8E2D0)
def old(s):
    F.old_swing_arc(s,(0,0,55),180,360,300*0.85,sim.clerp(col,(1,1,1),0.2),58,0.3,0.0)
    F.old_swing_arc(s,(0,0,45),200,324,300*0.5,sim.clerp(col,(1,1,1),0.55),20,0.26,0.0)
    F.old_shockwave(s,(0,0,8),300,col,0.22,16,0.07)
    for i,(x,y) in enumerate(((260,-60),(330,90))): F.old_impact(s,(x,y,80),col,1,False,0.07+0.02*i)
t=time.time()
tiles=R.run(old,[0.03,0.08,0.14,0.22,0.32],"old")
print("render", time.time()-t)
R.sheet([("OLD sweep (Shade Scythe)", tiles)], "/tmp/fx2/t0.jpg")
