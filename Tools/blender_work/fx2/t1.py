import sys, time; sys.path.insert(0,'/home/claude/blender_work/fx2')
import render as R, prims as F, sim, bpy
R.setup(res=(448,252), fov=58, samples=2, look=(150,0,50))
R.SC.eevee.use_shadows=False
R.actor("reaper", R.ART+"Characters/R_Grim.fbx", (0,0,0), 0, 175)
col = sim.hx(0xE8E2D0)
s=sim.Sys(1); F.old_swing_arc(s,(0,0,55),180,360,255,col,58,0.3,0.0)
for i in range(6): s.step(1/60)
R.draw(s.snapshot())
for k in range(3):
    t=time.time(); a=R.shoot("/tmp/fx2/p.exr"); t1=time.time(); im=R.grade(a); print("shoot %.1f grade %.1f"%(t1-t, time.time()-t1))
im.save("/tmp/fx2/t1.jpg")
