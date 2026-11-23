# usage: python3 sheets.py <iter> <what> [ids]   what: prims | Attack | E | R | events | true
import sys, os, csv, math, time
sys.path.insert(0, os.path.dirname(__file__))
import bpy
import render as R, prims as F, recipe as RC, sim
from mathutils import Vector
IT = int(sys.argv[1]); WHAT = sys.argv[2]; ONLY = sys.argv[3].split(",") if len(sys.argv) > 3 else None
OUT = "/mnt/user-data/outputs/fx_v2/"
if IT > 1: exec(open(os.path.dirname(__file__) + "/tune.py").read())      # TUNE / GLOW overrides for this iteration (mirrors Tuning.csv + SQFx.cpp constants)
kits = {r["ReaperId"]: r for r in csv.DictReader(l for l in open("/home/claude/sq/Content/Data/Kits.csv") if not l.startswith("#"))}
ups = {r["Id"]: r for r in csv.DictReader(l for l in open("/home/claude/sq/Content/Data/Upgrades.csv") if not l.startswith("#"))}
ORDER = list(kits.keys())
def fnum(v, d):
    try: return float(v) if float(v) > 0 else d
    except Exception: return d
ZOMB = []
TRUE = len(sys.argv) > 4 and sys.argv[4] == "true"      # the game's own framing: FOV 90, 850 cm back, 18 degrees down, 640x360
def stage(rid=None, res=(400, 225), fov=60, look=(170, 0, 40)):
    if TRUE: res, fov, look = (640, 360), 90, (120, 0, 60)
    R.setup(res=res, fov=fov, samples=2, look=look); R.SC.eevee.use_shadows = False
    if rid: R.actor("reaper", R.ART + "Characters/R_%s.fbx" % rid, (0, 0, 0), 0, 175)
    for i, (x, y) in enumerate(((270, -70), (350, 80), (450, -10))): R.actor("z%d" % i, R.ART + "Characters/Zombie.fbx", (x, y, 0), 180, 150)

rows = []
if WHAT == "prims":
    stage("Grim"); col = sim.hx(0xE8E2D0) if IT < 3 else sim.hx(0x5CF2D6); red = sim.hx(0xD8434B)
    T = [0.03, 0.08, 0.14, 0.22, 0.32]
    def old_sweep(s):
        F.old_swing_arc(s, (0, 0, 55), 180, 360, 255, sim.clerp(col, (1, 1, 1), 0.2), 58, 0.3); F.old_swing_arc(s, (0, 0, 45), 200, 324, 150, sim.clerp(col, (1, 1, 1), 0.55), 20, 0.26)
        F.old_shockwave(s, (0, 0, 8), 300, col, 0.22, 16, 0.07)
        for i, (x, y) in enumerate(((270, -70), (350, 80))): F.old_impact(s, (x, y, 80), col, 1, i == 1, 0.07 + 0.02 * i)
    def new_sweep(s):
        F.swing_arc(s, (0, 0, 55), 180, 360, 255, col, 58, 0.3); F.shockwave(s, (0, 0, 8), 300, col, 0.22, 16, 0.07)
        for i, (x, y) in enumerate(((270, -70), (350, 80))): F.impact(s, (x, y, 80), col, 1, i == 1, 0.07 + 0.02 * i)
    def old_smack(s):
        F.old_swing_arc(s, (0, 0, 50), -80, 160, 210, red, 46, 0.22); F.old_shockwave(s, (0, 0, 8), 300, red, 0.3, 48, 0.07); F.old_flash(s, (60, 0, 8), sim.clerp(red, (1, 1, 1), 0.55), 90, 0.12, 0.07)
        F.ground_cracks(s, (60, 0, 8), 135, red, 6, 0.87, 6, 0.07)
    def new_smack(s):
        F.swing_arc(s, (0, 0, 50), -80, 160, 210, red, 46, 0.22); F.shockwave(s, (0, 0, 8), 300, red, 0.3, 48, 0.07); F.flash(s, (60, 0, 8), sim.clerp(red, (1, 1, 1), 0.55), 90, 0.12, 0.07)
        F.ground_cracks(s, (60, 0, 8), 135, red, 6, 0.87, 6, 0.07)
    for lab, fn in (("OLD sweep + shockwave + hits (Nov 12 build)", old_sweep), ("NEW sweep + shockwave + hits", new_sweep), ("OLD smack (red)", old_smack), ("NEW smack (red)", new_smack)):
        if (IT == 1 and lab.startswith("NEW")) or (IT > 2 and lab.startswith("OLD")): continue
        rows.append((lab, R.run(fn, T, "pr%d" % len(rows))))
elif WHAT in ("Attack", "E", "R"):
    for rid in ORDER:
        if ONLY and rid not in ONLY: continue
        stage(rid)
        k = kits[rid]; pal = RC.PALS.get(rid, RC.DEFPAL)
        if WHAT == "Attack":
            up = ups.get(k["SignatureWeaponId"], {}); rad = fnum(up.get("Radius"), 300.0); rad = min(900, max(120, rad)); beat = 0.08
            wcol = sim.hx(int(up.get("Color", "#FFFFFF").lstrip("#"), 16)); wcol = tuple(min(1, c * 1.25 + 0.05) for c in wcol)
            if IT >= 3: wcol = pal["Mid"]      # signature weapon: the swing wears the reaper's colour
            c = RC.Ctx(R=rad, pal=pal, beat=beat)
            def fn(s, c=c, rid=rid, up=up, wcol=wcol, rad=rad):
                if up.get("Behaviour") == "Arc":
                    (F.old_swing_arc if IT == 1 else F.swing_arc)(s, (0, 0, 55), 180, 360, rad * 0.85, sim.clerp(wcol, (1, 1, 1), 0.2), 58, 0.3, 0.013)
                    (F.old_shockwave if IT == 1 else F.shockwave)(s, (0, 0, 8), rad, wcol, 0.22, 16, 0.08)
                (F.old_impact if IT == 1 else F.impact)(s, (270, -70, 80), wcol, 1, False, 0.1)
                RC.play(s, rid, "Attack", c)
            T = [0.05, 0.11, 0.2, 0.42]; lab = "%s  %s (%s)" % (rid, up.get("Name"), up.get("Behaviour"))
        else:
            rad = fnum(k.get(WHAT + "_Radius"), 260.0); rad = min(600, max(140, rad))
            c = RC.Ctx(R=rad, T=(430, 0, 0), pal=pal)
            def fn(s, c=c, rid=rid): RC.play(s, rid, WHAT, c)
            T = [0.03, 0.12, 0.28, 0.6]; lab = "%s  %s: %s (%s)" % (rid, WHAT, k.get(WHAT + "_Name"), k.get(WHAT + "_Shape"))
        rows.append((lab, R.run(fn, T, "%s_%s" % (WHAT, rid))))
elif WHAT == "events":
    stage("Keeper")
    EV = [("Impact (red area hit)", "Impact", dict(R=260, O=(300, 0, 0), color=sim.hx(0xE0483A)), [0.03, 0.1, 0.25, 0.6]),
          ("Aura tick (ImpactLight)", "ImpactLight", dict(R=220, color=sim.hx(0xB48CFF)), [0.03, 0.1, 0.2, 0.35]),
          ("Boss slam (ImpactHeavy)", "ImpactHeavy", dict(R=420, T=(380, 0, 0), O=(380, 0, 0), color=sim.hx(0xE0483A)), [0.03, 0.12, 0.3, 0.7]),
          ("Shrine pulse", "ShrinePulse", dict(O=(300, 60, 0), R=240, color=sim.hx(0x7FE0FF)), [0.04, 0.15, 0.4, 0.8]),
          ("Hazard pulse", "HazardPulse", dict(O=(300, -60, 0), R=200, color=sim.hx(0xFF7A2A)), [0.03, 0.1, 0.22, 0.4]),
          ("Soul collected", "SoulCollect", dict(color=sim.hx(0x7FE8FF)), [0.02, 0.08, 0.16, 0.3]),
          ("Gold collected", "GoldCollect", dict(color=sim.hx(0xFFC83A)), [0.02, 0.08, 0.16, 0.3]),
          ("Bank at lantern", "Bank", dict(T=(300, 0, 0), color=sim.hx(0x7FE8FF)), [0.03, 0.1, 0.25, 0.45]),
          ("Enemy death puff", "Death", dict(O=(270, -70, 0), R=45, color=sim.hx(0x9AC48A)), [0.03, 0.1, 0.22, 0.4]),
          ("Level up", "LevelUp", dict(color=sim.hx(0x66E05A)), [0.04, 0.15, 0.4, 0.8]),
          ("Collect (XP)", "Collect", dict(color=sim.hx(0x66E05A)), [0.02, 0.06, 0.12, 0.25]),
          ("Chest opens", "ChestOpen", dict(T=(260, 60, 0), color=sim.hx(0xFFC83A)), [0.04, 0.15, 0.4, 0.8]),
          ("Lantern lights", "LanternLit", dict(T=(420, 0, 0), color=sim.hx(0xFFB04A)), [0.05, 0.2, 0.6, 1.2]),
          ("Lantern spent", "LanternSpent", dict(T=(420, 0, 0), color=sim.hx(0xFF7A3A)), [0.05, 0.2, 0.5, 0.9]),
          ("Dash", "Dash", dict(D=(-1, 0, 0), color=sim.hx(0x5CF2D6)), [0.02, 0.07, 0.14, 0.3]),
          ("Land", "Land", dict(color=sim.hx(0xB8B0A0)), [0.02, 0.07, 0.14, 0.3])]
    for lab, ev, kw, T in EV:
        c = RC.Ctx(**kw)
        if IT == 1 and ev != "Impact": continue
        if IT == 1:
            def fn(s, c=c):
                col = c.pal["Mid"]; F.old_shockwave(s, (300, 0, 8), 260, col, 0.3, 30); F.old_flash(s, (300, 0, 40), col, 120); sim.burst(s, (300, 0, 30), col, 12, 420, 0.4, 8)
        else:
            def fn(s, c=c, ev=ev): RC.play(s, "*", ev, c)
        rows.append((lab, R.run(fn, T, "ev_" + ev)))
R.sheet(rows, OUT + "FX_%s%s_iter%d.jpg" % (WHAT, "_true" if TRUE else "", IT))
