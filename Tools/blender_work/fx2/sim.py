# Soul Quota effects preview: a Python mirror of ASQSparks (SQFx.cpp) + the recipe interpreter (SQFxRecipe.cpp).
# Same particle model, same maths, same FxRecipes.csv, so what is rendered here is what the game code produces.
import math, random, csv
import bpy  # noqa: mathutils lives inside the bpy module
from mathutils import Vector, Quaternion, Matrix

TUNE = dict(StyleFxSize=1.3, StyleFxCount=1.4, StyleFxGlow=1.25, StyleFxVivid=0.35, StyleFxFlash=1.0, StyleFxAdditive=1.0)
KIT = ["Crescent", "Ring", "Flame", "Puff", "Spiral", "Shard", "Bolt", "Star", "Bat", "Crow", "Moth", "Leaf", "Feather", "Rune", "Seal",
       "Cleaver", "Claw", "Bandage", "Seed", "Drop", "Swoosh", "Arc", "Streak", "Wave", "Spikes", "Ember", "Petal", "Chevron", "Wisp", "Teeth", "Sigil", "Cog", "Web", "Casket"]
LIGHT = {"Sphere", "Flame", "Star", "Spiral", "Bolt", "Rune", "Seal", "Crescent", "Swoosh", "Arc", "Streak", "Wave", "Spikes", "Ember", "Petal", "Chevron", "Wisp", "Teeth", "Sigil", "Cog", "Web", "Casket"}
MATTE = {"Puff", "Bat", "Crow"}
UP = Vector((0, 0, 1))
LEGACY_SHAPE = "Sphere"    # tune.py sets "Ember" from iteration 5 (ASQSparks::Emit with the motion kit in)
STREAK_W = 1.0 / 0.09   # the Streak kit mesh is 9% as wide as it is long: side sizes are given in cm of width
def streak_size(length, width): return Vector((length, width * STREAK_W, width * STREAK_W))

def hx(h):
    def lin(c):
        c /= 255.0
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    return (lin((h >> 16) & 255), lin((h >> 8) & 255), lin(h & 255))

def lerp(a, b, t): return a + (b - a) * t
def clerp(a, b, t): return tuple(lerp(a[i], b[i], t) for i in range(3))
def yaw_of(v): return math.degrees(math.atan2(v.y, v.x))
def rot(pitch=0.0, yaw=0.0, roll=0.0):   # FRotator(Pitch, Yaw, Roll).Quaternion()
    return Quaternion((0, 0, 1), math.radians(yaw)) @ Quaternion((0, 1, 0), math.radians(-pitch)) @ Quaternion((1, 0, 0), math.radians(roll))
def from_x(v):                             # FRotationMatrix::MakeFromX
    x = Vector(v).normalized()
    up = UP if abs(x.z) < 0.999 else Vector((1, 0, 0))
    y = up.cross(x).normalized(); z = x.cross(y)
    return Matrix((x, y, z)).transposed().to_quaternion()
def on_circle(c, r, deg, z=0.0):
    a = math.radians(deg)
    return Vector(c) + Vector((math.cos(a) * r, math.sin(a) * r, z))

class P:   # FSQFxParticle (+ the Nov 19 additions: FadeIn, FadeOut, Bright, OrbitRate / OrbitPivot, bStretch)
    def __init__(s, **kw):
        s.Pos = Vector((0, 0, 0)); s.Vel = Vector((0, 0, 0)); s.Color = (1, 1, 1); s.Shape = "Sphere"; s.Life = 0.3; s.Delay = 0.0
        s.SizeStart = Vector((10, 10, 10)); s.SizeEnd = Vector((10, 10, 10)); s.Rot = Quaternion(); s.SpinAxis = Vector((0, 0, 1)); s.SpinRate = 0.0
        s.Gravity = 0.0; s.Drag = 0.0; s.FloorZ = -1e9; s.bAlignToVelocity = False; s.bShrinkOut = True; s.bEaseOut = False
        s.TrailEvery = 0.0; s.TrailSize = 6.0; s.TrailColor = (1, 1, 1); s.DeathFx = 0; s.DeathCount = 6; s.DeathSize = 7.0; s.DeathColor = (1, 1, 1)
        s.FadeIn = 0.0; s.FadeOut = 0.0; s.Bright = 1.0; s.OrbitRate = 0.0; s.OrbitPivot = Vector((0, 0, 0)); s.SpinDecay = 0.0
        for k, v in kw.items():
            if k in ("Pos", "Vel", "SizeStart", "SizeEnd", "SpinAxis", "OrbitPivot"): v = Vector(v) if not isinstance(v, (int, float)) else Vector((v, v, v))
            setattr(s, k, v)

class Sys:
    """ASQSparks: Emit* allocate sparks, step(dt) advances them, snapshot() lists what is drawn"""
    def __init__(s, seed=1):
        s.sparks = []; s.rng = random.Random(seed); s.t = 0.0; s.shake = []
    def fr(s, a, b): return s.rng.uniform(a, b)
    def vrand(s):
        while True:
            v = Vector((s.fr(-1, 1), s.fr(-1, 1), s.fr(-1, 1)))
            if 0.001 < v.length <= 1: return v.normalized()
    def vivid(s, c):
        v = TUNE["StyleFxVivid"]
        lum = 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]; ch = 1 + v
        return tuple(min(1, max(0, lum + (x - lum) * ch)) for x in c)
    def emit_legacy(s, loc, color, count, speed, life, size, gravity, ring, delay=0.0):
        n = max(1, round(count * TUNE["StyleFxCount"])); size *= TUNE["StyleFxSize"] * (1.5 if LEGACY_SHAPE == "Ember" else 1.0)
        a0 = s.fr(0, 2 * math.pi)
        col = s.vivid(color)
        if not ring and count >= 5 and TUNE["StyleFxFlash"] > 0:
            ps = min(130, max(14, size * 3.2 * TUNE["StyleFxFlash"])) / 50
            s.sparks.append(dict(Pos=Vector(loc), Vel=Vector((0, 0, 0)), Age=-max(0, delay), Life=0.11, A=Vector((ps,) * 3), B=Vector((ps,) * 3), Accel=0, Drag=0,
                                 Shape=LEGACY_SHAPE, Color=col, legacy=True))
        for i in range(n):
            if ring:
                a = a0 + 2 * math.pi * i / n + s.fr(-0.15, 0.15); d = Vector((math.cos(a), math.sin(a), 0.12))
            else:
                d = s.vrand(); d.z = abs(d.z) * 0.8 + 0.25; d.normalize()
            sc = max(0.5, size * s.fr(0.7, 1.2)) / 50
            s.sparks.append(dict(Pos=Vector(loc), Vel=d * speed * s.fr(0.65, 1.0), Age=-max(0, delay), Life=max(0.05, life * s.fr(0.75, 1.15)), A=Vector((sc,) * 3),
                                 B=Vector((sc,) * 3), Accel=0 if ring else gravity, Drag=3.0 if ring else 1.5, Shape=LEGACY_SHAPE, Color=col, legacy=True))
    def emit(s, p):
        k = TUNE["StyleFxSize"] / 100.0
        s.sparks.append(dict(Pos=p.Pos.copy(), Vel=p.Vel.copy(), Age=-max(0, p.Delay), Life=max(0.02, p.Life), Accel=p.Gravity, Drag=max(0, p.Drag), Shape=p.Shape,
                             Color=s.vivid(p.Color), legacy=False, align=p.bAlignToVelocity, shrink=p.bShrinkOut, ease=p.bEaseOut, A=p.SizeStart * k, B=p.SizeEnd * k,
                             Rot=p.Rot.copy(), SpinAxis=(p.SpinAxis.normalized() if p.SpinAxis.length > 1e-6 else UP.copy()), SpinRate=p.SpinRate, SpinAngle=0.0, FloorZ=p.FloorZ,
                             TrailEvery=max(0, p.TrailEvery), TrailClock=0.0, TrailSize=p.TrailSize, TrailColor=s.vivid(p.TrailColor), DeathFx=p.DeathFx,
                             DeathCount=p.DeathCount, DeathSize=p.DeathSize, DeathColor=p.DeathColor, FadeIn=p.FadeIn, FadeOut=p.FadeOut, Bright=p.Bright,
                             OrbitRate=p.OrbitRate, OrbitPivot=p.OrbitPivot.copy(), SpinDecay=p.SpinDecay))
    def step(s, dt):
        s.t += dt; later = []
        for S in s.sparks:
            if S.get("dead"): continue
            S["Age"] += dt
            if S["Age"] < 0: continue
            if S["Age"] >= S["Life"]:
                if S.get("DeathFx", 0) > 0: later.append((S["Pos"].copy(), S["DeathColor"], S["DeathCount"], S["DeathSize"], S["DeathFx"]))
                S["dead"] = True; continue
            S["Vel"].z += S["Accel"] * dt
            S["Vel"] *= max(0.0, 1 - S["Drag"] * dt)
            S["Pos"] += S["Vel"] * dt
            if S.get("OrbitRate", 0.0) != 0.0:
                q = Quaternion((0, 0, 1), S["OrbitRate"] * dt)
                S["Pos"] = S["OrbitPivot"] + q @ (S["Pos"] - S["OrbitPivot"]); S["Vel"] = q @ S["Vel"]
                if not S["align"]: S["Rot"] = q @ S["Rot"]
            if S["Pos"].z < S.get("FloorZ", -1e9):
                S["Pos"].z = S["FloorZ"]; S["Vel"] = Vector((0, 0, 0)); S["Accel"] = 0
            if not S["legacy"]:
                if S["SpinDecay"] > 0: S["SpinRate"] *= max(0.0, 1 - S["SpinDecay"] * dt)
                S["SpinAngle"] += S["SpinRate"] * dt
                if S["align"] and S["Vel"].length > 1e-4:
                    S["Rot"] = from_x(S["Vel"]) @ Quaternion((1, 0, 0), S["SpinAngle"])
                elif S["SpinRate"] != 0:
                    S["Rot"] = (S["Rot"] @ Quaternion(S["SpinAxis"], S["SpinRate"] * dt)).normalized()
                if S["TrailEvery"] > 0:
                    S["TrailClock"] += dt
                    if S["TrailClock"] >= S["TrailEvery"]:
                        S["TrailClock"] = 0; later.append((S["Pos"].copy(), S["TrailColor"], 1, S["TrailSize"], 0))
        for pos, col, cnt, size, kind in later:
            if kind == 0:
                s.sparks.append(dict(Pos=pos, Vel=Vector((0, 0, 20)), Age=0.0, Life=0.28, A=Vector((size / 50,) * 3), B=Vector((size / 50,) * 3), Accel=0, Drag=2.0,
                                     Shape=LEGACY_SHAPE, Color=col, legacy=True))
            else:
                s.emit_legacy(pos, col, cnt, 340, 0.32, size, -800, False)
                if kind == 2: s.emit_legacy(pos, col, max(8, cnt), 420, 0.35, size * 0.9, 0, True)
        s.sparks = [S for S in s.sparks if not S.get("dead")]
    def snapshot(s):
        out = []
        for S in s.sparks:
            if S["Age"] < 0: continue
            al = S["Age"] / S["Life"]
            if S["legacy"]:
                sc = S["A"] * (min(1, al * 8) * math.sqrt(max(0, 1 - al))); q = Quaternion(); alpha = 1.0
            else:
                sa = 1 - (1 - al) ** 2 if S["ease"] else al
                sc = S["A"].lerp(S["B"], sa)
                sc = sc * (min(1, S["Age"] / 0.03 + 0.25) * (min(1, max(0, (1 - al) / 0.35)) if S["shrink"] else 1))
                q = S["Rot"]
                alpha = S["Bright"]
                if S["FadeIn"] > 0: alpha *= min(1.0, S["Age"] / S["FadeIn"])
                if S["FadeOut"] > 0: alpha *= min(1.0, max(0.0, (1 - al) / S["FadeOut"]))
            out.append((S["Shape"], S["Pos"].copy(), q.copy(), sc.copy(), S["Color"], alpha))
        return out

# ================================================================ SQFx helpers (SQFx.cpp namespace SQFx)
def burst(s, loc, color, count, speed, life, size, gravity=-900.0, delay=0.0): s.emit_legacy(loc, color, count, speed, life, size, gravity, False, delay)
def ring(s, c, color, count, speed, life, size, delay=0.0): s.emit_legacy(c, color, count, speed, life, size, 0.0, True, delay)
