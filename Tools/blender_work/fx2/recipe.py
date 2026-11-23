# FxRecipes.csv interpreter (Python mirror of SQFxRecipe.cpp). One row = one layer of an effect.
import csv, math, re
from sim import *
import prims as F
CSV = "/home/claude/sq/Content/Data/FxRecipes.csv"
COLS = ["Id", "Event", "Verb", "Shape", "Col", "At", "Fwd", "Up", "Delay", "Life", "Count", "Size", "Size2", "Speed", "Radius", "Angle", "Sweep", "Lift", "Grav", "Spin", "Bright"]
_rows = None

def palettes():
    src = open("/home/claude/sq/Source/SoulQuota/SQFxTheme.cpp").read(); out = {}
    for m in re.finditer(r'M\.Add\(TEXT\("(\w+)"\), FPal\{ Hx\(0x(\w+)\), Hx\(0x(\w+)\), Hx\(0x(\w+)\), Hx\(0x(\w+)\) \}\)', src):
        out[m.group(1)] = dict(Core=hx(int(m.group(2), 16)), Mid=hx(int(m.group(3), 16)), Dark=hx(int(m.group(4), 16)), Alt=hx(int(m.group(5), 16)))
    return out
PALS = palettes()
DEFPAL = dict(Core=hx(0xFFFFFF), Mid=hx(0x5CF2D6), Dark=hx(0x16121C), Alt=hx(0xFFD26A))

def load(path=CSV):
    global _rows
    _rows = {}
    for r in csv.DictReader(l for l in open(path) if l.strip() and not l.startswith("#")):
        _rows.setdefault((r["Id"], r["Event"]), []).append(r)
    return _rows

class Ctx:
    def __init__(s, O=(0, 0, 0), D=(1, 0, 0), T=None, R=300.0, K=1.0, pal=None, beat=0.0, color=None):
        s.O = Vector(O); s.D = Vector(D).normalized(); s.R = R; s.K = K; s.pal = dict(pal or DEFPAL); s.beat = beat
        s.T = Vector(T) if T is not None else s.O + s.D * R
        s.L = max(1.0, (s.T - s.O).length)
        if color is not None: s.pal["Mid"] = color; s.pal["Core"] = clerp(color, (1, 1, 1), 0.7)

def num(r, key, c, default=0.0):
    v = (r.get(key) or "").strip()
    if not v: return default
    if v.endswith("R"): return float(v[:-1] or 1) * c.R
    if v.endswith("L"): return float(v[:-1] or 1) * c.L
    return float(v)
def colour(r, c):
    v = (r.get("Col") or "Mid").strip()
    if v.startswith("#"): return hx(int(v[1:], 16))
    if v == "White": return (1, 1, 1)
    return c.pal.get(v, c.pal["Mid"])

def has(rid, ev):
    if _rows is None: load()
    return (rid, ev) in _rows

def play(s, rid, ev, c):
    if _rows is None: load()
    rows = _rows.get((rid, ev))
    if not rows: return False
    yawD = yaw_of(c.D)
    for r in rows:
        verb = r["Verb"]; shape = r.get("Shape") or "Sphere"; col = colour(r, c); N = lambda k, d=0.0: num(r, k, c, d)
        at = (r.get("At") or "O").strip()
        base = {"O": c.O, "H": c.O + c.D * (c.R * 0.55), "T": c.T, "M": c.O + c.D * 45 + Vector((0, 0, 105))}[at]
        pos = base + c.D * N("Fwd") + Vector((0, 0, N("Up")))
        d = max(0.0, c.beat + N("Delay")); life = N("Life", 0.3); cnt = max(1, round(N("Count", 1))); size = N("Size", 30) * c.K; size2 = N("Size2", -1)
        size2 = size2 * c.K if size2 >= 0 else -1; speed = N("Speed"); rad = N("Radius", c.R); ang = N("Angle"); sweep = N("Sweep"); lift = N("Lift"); grav = N("Grav"); spin = N("Spin")
        bright = N("Bright", 1.0)
        if verb == "Flash": F.flash(s, pos, col, size, life, d)
        elif verb == "Glint": F.glint(s, pos, col, size, d)
        elif verb == "Cracks": F.ground_cracks(s, pos, rad, col, cnt, d + life, 7.0 * c.K, d)
        elif verb == "Shake": s.shake.append((d, N("Size", 0.3)))
        elif verb == "Wave":
            z = (size2 if size2 >= 0 else 1.0)
            s.emit(P(Pos=pos, Color=col, Shape="Teeth" if shape == "Teeth" else "Wave", Life=life, Delay=d, SizeStart=(rad * 0.3, rad * 0.3, rad * 0.6 * z), SizeEnd=(rad * 2, rad * 2, rad * z),
                     bEaseOut=True, bShrinkOut=False, FadeOut=0.55, Bright=bright, Rot=rot(ang, yawD, 0), SpinRate=math.radians(spin)))
        elif verb == "Swoosh": F.swoosh(s, pos, yawD + ang, sweep or 200.0, rad, col, max(0.2, N("Size", 1.0)), life, d, bright, lift, shape if shape in ("Arc", "Swoosh") else "Swoosh")
        elif verb == "Mark":
            s.emit(P(Pos=pos + Vector((0, 0, 3)), Color=col, Shape=shape, Life=life, Delay=d, SizeStart=size * 0.4, SizeEnd=size, bEaseOut=True, bShrinkOut=False, FadeIn=0.04,
                     FadeOut=0.4, Rot=rot(0, yawD + ang), SpinAxis=(0, 0, 1), SpinRate=math.radians(spin), Bright=bright))
        elif verb == "Pop":
            s.emit(P(Pos=pos, Vel=c.D * speed + Vector((0, 0, lift)), Color=col, Shape=shape, Life=life, Delay=d, SizeStart=size * 0.35, SizeEnd=size if size2 < 0 else size2, bEaseOut=True,
                     FadeOut=0.45, Rot=rot(sweep, yawD + ang), SpinAxis=(0, 0, 1), SpinRate=math.radians(spin), Drag=2.2 if speed else 0.0, Gravity=grav, Bright=bright))
        elif verb == "Pillar":
            s.emit(P(Pos=pos + Vector((0, 0, lift * 0.5)), Color=col, Shape="Streak", Life=life, Delay=d, SizeStart=streak_size(lift, size), SizeEnd=streak_size(lift * 1.15, size * 0.25),
                     bEaseOut=True, bShrinkOut=False, FadeIn=0.03, FadeOut=0.6, Rot=rot(90, 0), Bright=bright))
        elif verb in ("Burst", "Dust"):
            for i in range(cnt):
                yaw = yawD + s.fr(-ang, ang) if ang > 0 else yawD
                dirv = on_circle((0, 0, 0), 1.0, yaw) + Vector((0, 0, lift + s.fr(-0.15, 0.25)))
                st = shape == "Streak"
                if verb == "Dust":
                    s.emit(P(Pos=pos + on_circle((0, 0, 0), s.fr(0, rad if r.get("Radius") else 0), s.fr(0, 360)), Vel=dirv * speed * s.fr(0.5, 1.0), Color=col, Shape="Puff", Life=life * s.fr(0.8, 1.2),
                             Delay=d + s.fr(0, 0.05), SizeStart=size * 0.6, SizeEnd=size * 1.7, Drag=3.0, bEaseOut=True, Rot=rot(0, s.fr(0, 360)), SpinRate=s.fr(-1.5, 1.5)))
                    continue
                e2 = size2 if size2 >= 0 else size * 0.4
                s.emit(P(Pos=pos, Vel=dirv * speed * s.fr(0.6, 1.1), Color=col, Shape=shape, Life=life * s.fr(0.75, 1.15), Delay=d + s.fr(0, 0.06),
                         SizeStart=streak_size(size, size * 0.11) if st else size, SizeEnd=streak_size(e2, e2 * 0.11) if st else e2, Gravity=grav, Drag=4.0 if st else 1.6,
                         FloorZ=c.O.z if grav < 0 else -1e9, bAlignToVelocity=True, SpinRate=s.fr(-spin, spin), SpinAxis=(1, 0, 0), bShrinkOut=not st, FadeOut=0.5, Bright=bright))
        elif verb == "Rise":
            for i in range(cnt):
                p0 = pos + on_circle((0, 0, 0), rad * math.sqrt(s.fr(0, 1)), s.fr(0, 360))
                e2 = size2 if size2 >= 0 else size * 0.5
                s.emit(P(Pos=p0, Vel=Vector((s.fr(-12, 12), s.fr(-12, 12), speed * s.fr(0.7, 1.2))), Color=col, Shape=shape, Life=life * s.fr(0.75, 1.15), Delay=d + s.fr(0, life * 0.4),
                         SizeStart=size * s.fr(0.75, 1.15), SizeEnd=e2, Gravity=grav, Rot=rot(0, s.fr(0, 360)), SpinAxis=(0, 0, 1), SpinRate=math.radians(spin) * s.fr(0.6, 1.2),
                         bShrinkOut=False, FadeIn=0.05, FadeOut=0.5, Bright=bright))
        elif verb == "Ring":
            a0 = s.fr(0, 360) if ang == 0 else yawD + ang
            for i in range(cnt):
                a = a0 + 360.0 * i / cnt; radial = on_circle((0, 0, 0), 1.0, a)
                s.emit(P(Pos=pos + radial * rad, Vel=radial * speed + Vector((0, 0, lift)), Color=col, Shape=shape, Life=life, Delay=d + i * sweep, SizeStart=size * 0.35,
                         SizeEnd=size if size2 < 0 else size2, bEaseOut=True, Rot=rot(0, a), Drag=1.2 if speed else 0.0, OrbitRate=math.radians(spin), OrbitPivot=pos,
                         FadeIn=0.03, FadeOut=0.4, Bright=bright, Gravity=grav))
        elif verb == "Fan":
            for i in range(cnt):
                a = yawD + (-ang + 2 * ang * (i / (cnt - 1) if cnt > 1 else 0.5)); radial = on_circle((0, 0, 0), 1.0, a)
                s.emit(P(Pos=pos + radial * rad, Vel=radial * speed, Color=col, Shape=shape, Life=life, Delay=d + i * sweep, SizeStart=size * 0.35, SizeEnd=size if size2 < 0 else size2,
                         bEaseOut=True, Rot=rot(lift, a), Drag=2.5 if speed else 0.0, FadeIn=0.02, FadeOut=0.45, Bright=bright))
        elif verb == "Line":
            end = pos + c.D * rad
            for i in range(cnt):
                t = (i + 0.5) / cnt; p0 = pos.lerp(end, t) + Vector((0, 0, lift * math.sin(math.pi * t)))
                s.emit(P(Pos=p0, Color=col, Shape=shape, Life=life, Delay=d + i * sweep, SizeStart=size * 0.4, SizeEnd=size if size2 < 0 else size2, bEaseOut=True, Rot=rot(0, yawD + ang),
                         SpinAxis=(0, 0, 1), SpinRate=math.radians(spin), FadeIn=0.02, FadeOut=0.5, Bright=bright))
        elif verb == "Pillars":      # a row of light columns marching from pos along the aim: reads from behind the reaper, where a flat ground line is foreshortened away
            end = pos + c.D * rad
            for i in range(cnt):
                t = (i + 0.5) / cnt; p0 = pos.lerp(end, t); h = lift * (0.7 + 0.5 * math.sin(math.pi * t))
                s.emit(P(Pos=p0 + Vector((0, 0, h * 0.5)), Color=col, Shape="Streak", Life=life, Delay=d + i * sweep, SizeStart=streak_size(h, size), SizeEnd=streak_size(h * 1.2, size * 0.2),
                         bEaseOut=True, bShrinkOut=False, FadeIn=0.03, FadeOut=0.6, Rot=rot(90, 0), Bright=bright))
        elif verb == "Lance":        # one held beam from pos along the aim, colour body with a thin hot core
            span = c.D * rad; mid = pos + span * 0.5
            for (cc, w, lf, br) in ((col, size, life, bright), (c.pal["Core"], size * 0.4, life * 0.7, bright * 1.2)):
                s.emit(P(Pos=mid, Color=cc, Shape="Streak", Life=lf, Delay=d, Rot=from_x(span), SizeStart=streak_size(rad, w), SizeEnd=streak_size(rad, w * 0.15), bShrinkOut=False, bEaseOut=True,
                         FadeIn=0.02, FadeOut=0.6, Bright=br))
        elif verb == "Shot":
            end = pos + c.D * rad; span = end - pos
            s.emit(P(Pos=pos, Vel=span / max(0.03, life), Color=col, Shape=shape, Life=life, Delay=d, SizeStart=size, SizeEnd=size, bAlignToVelocity=True, bShrinkOut=False,
                     SpinRate=math.radians(spin), TrailEvery=0.018, TrailSize=(size2 if size2 >= 0 else size * 0.25), TrailColor=c.pal["Mid"], DeathFx=2 if cnt > 8 else 1, DeathCount=cnt,
                     DeathSize=size * 0.14, DeathColor=c.pal["Core"], Bright=bright))
        elif verb == "Bolt":
            top = pos + Vector((0, 0, lift)); prev = top; seg = max(3, cnt)
            for i in range(1, seg + 1):
                t = i / seg; nxt = top.lerp(pos, t) + (Vector((s.fr(-1, 1), s.fr(-1, 1), 0)) * (size * 2.2) if i < seg else Vector((0, 0, 0)))
                span = nxt - prev
                for (cc, w, lf, br) in ((c.pal["Core"], size * 0.5, life * 0.7, bright * 1.2), (col, size, life, bright)):
                    s.emit(P(Pos=(prev + nxt) * 0.5, Color=cc, Shape="Streak", Life=lf, Delay=d + 0.01 * i, Rot=from_x(span), SizeStart=streak_size(span.length * 1.15, w * 1.6),
                             SizeEnd=streak_size(span.length * 1.15, w * 0.3), bShrinkOut=False, bEaseOut=True, FadeOut=0.6, Bright=br))
                prev = nxt
        elif verb == "Rain":
            for i in range(cnt):
                p0 = pos + on_circle((0, 0, 0), rad * math.sqrt(s.fr(0, 1)), s.fr(0, 360)); dl = d + s.fr(0, sweep)
                fall = max(0.08, lift / max(1.0, speed))
                s.emit(P(Pos=p0 + Vector((0, 0, lift)), Vel=(0, 0, -speed), Color=col, Shape=shape, Life=fall, Delay=dl, SizeStart=size, SizeEnd=size, bAlignToVelocity=True, bShrinkOut=False,
                         FadeIn=0.04, DeathFx=1, DeathCount=4, DeathSize=size * 0.12, DeathColor=c.pal["Core"], Bright=bright))
                s.emit(P(Pos=p0 + Vector((0, 0, 3)), Color=col, Shape="Wave", Life=0.25, Delay=dl + fall, SizeStart=size * 0.5, SizeEnd=(size * 2.4, size * 2.4, size * 1.2), bEaseOut=True,
                         bShrinkOut=False, FadeOut=0.6, Bright=bright * 0.8))
        else:
            print("UNKNOWN VERB", verb)
    return True
