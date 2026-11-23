# SQFx helper functions: OLD (what the Nov 12 build draws, kept for the "before" sheet) and NEW (ported to SQFx.cpp).
import math
from sim import *
WHITE = (1, 1, 1)

# ---------------------------------------------------------------- OLD (pre Nov 19)
def old_flash(s, loc, color, size, life=0.12, delay=0.0):
    s.emit(P(Pos=loc, Color=color, Life=life, Delay=delay, SizeStart=size * 0.6, SizeEnd=size * 1.4, bEaseOut=True))
def old_swing_arc(s, center, start_yaw, sweep, radius, color, width, duration, delay=0.0):
    seg = min(32, max(5, round(abs(sweep) / 12))); step = sweep / seg
    seglen = 2 * math.pi * radius * abs(step) / 360 * 1.35; hot = clerp(color, WHITE, 0.45)
    for k in range(seg + 1):
        yaw = start_yaw + step * k; rad = math.radians(yaw); radial = Vector((math.cos(rad), math.sin(rad), 0)); lead = k / seg
        w = width * (0.55 + 0.45 * lead)
        s.emit(P(Pos=Vector(center) + radial * radius, Vel=radial * 160, Drag=4.0, Color=hot if k == seg else color, Delay=delay + duration * 0.45 * lead,
                 Life=duration * 0.65, Rot=rot(0, yaw + (90 if sweep >= 0 else -90)), SizeStart=(seglen, w, w * 0.3), SizeEnd=(seglen * 0.8, w * 0.2, w * 0.1), bShrinkOut=False))
def old_shockwave(s, center, radius, color, duration, thick, delay=0.0):
    R = max(40, radius); n = min(44, max(12, round(R / 22))); r0 = R * 0.15; dur = max(0.08, duration); sp = (R - r0) / dur; a0 = s.fr(0, 360)
    for k in range(n):
        yaw = a0 + 360 * k / n; rad = math.radians(yaw); radial = Vector((math.cos(rad), math.sin(rad), 0))
        s.emit(P(Pos=Vector(center) + radial * r0, Vel=radial * sp, Color=color, Delay=delay, Life=dur, Rot=rot(0, yaw + 90),
                 SizeStart=(2 * math.pi * r0 / n * 1.25, thick, thick * 0.45), SizeEnd=(2 * math.pi * R / n * 1.25, thick * 0.35, thick * 0.15), bShrinkOut=False))
def old_impact(s, loc, color, scale, crit, delay=0.0):
    S = min(4, max(0.3, scale))
    burst(s, loc, color, round((10 if crit else 6) * S), 380, 0.3, (8 if crit else 6) * S, -800, delay)
    old_flash(s, loc, clerp(color, WHITE, 0.6), (55 if crit else 32) * S, 0.1, delay)
    if crit: ring(s, loc, (1, 0.82, 0.3), 10, 420, 0.3, 6 * S, delay)

# ---------------------------------------------------------------- NEW (Nov 19 2026)
SWOOSH_R = 0.5 / 0.8660254      # Swoosh kit mesh: size (longest side) -> arc radius

def flash(s, loc, color, size, life=0.12, delay=0.0):
    """soft orb that blooms and fades + a spinning starburst so a hit has a shape, not a ball"""
    s.emit(P(Pos=loc, Color=color, Life=life, Delay=delay, SizeStart=size * 0.3, SizeEnd=size * 0.8, bEaseOut=True, bShrinkOut=False, FadeOut=0.7, Bright=0.8))
    s.emit(P(Pos=loc, Color=clerp(color, WHITE, 0.5), Shape="Spikes", Life=life * 1.15, Delay=delay, SizeStart=size * 0.7, SizeEnd=size * 2.1, bEaseOut=True,
             bShrinkOut=False, FadeOut=0.6, Rot=rot(0, s.fr(0, 360)), SpinRate=s.fr(-3, 3), Bright=1.1))

def swoosh(s, center, start_yaw, sweep, radius, color, thick, duration, delay=0.0, bright=1.0, lean=0.0, shape="Swoosh"):
    """one tapered arc mesh swept through `sweep` degrees (fast, easing out): the trail is a single clean shape that fades"""
    cw = sweep < 0
    q = rot(0, start_yaw + (60 if not cw else -60)) @ (Quaternion((1, 0, 0), math.pi) if cw else Quaternion()) @ Quaternion((1, 0, 0), math.radians(lean))
    size = radius / SWOOSH_R
    travel = math.radians(max(0.0, abs(sweep) - 120.0))          # the mesh already spans 120 degrees
    decay = 3.2
    rate = travel * decay / max(0.05, 1.0 - math.exp(-decay * duration * 0.8))
    s.emit(P(Pos=center, Color=color, Shape=shape, Life=duration, Delay=delay, SizeStart=(size * 0.94, size * 0.94, size * 0.5 * thick), SizeEnd=(size * 1.05, size * 1.05, size * 0.2 * thick),
             Rot=q, SpinAxis=(0, 0, 1), SpinRate=rate, SpinDecay=decay, bShrinkOut=False, bEaseOut=True, FadeIn=0.015, FadeOut=0.5, Bright=bright))

def swing_arc(s, center, start_yaw, sweep, radius, color, width, duration, delay=0.0):
    """layered swing: coloured tapered body, thin white-hot edge leading it, speed streaks thrown off the tip"""
    k = max(0.5, width / 58.0)
    hot = clerp(color, WHITE, 0.75)
    swoosh(s, center, start_yaw, sweep, radius, color, k, duration, delay, 1.0)
    swoosh(s, Vector(center) + Vector((0, 0, 4)), start_yaw + (14 if sweep >= 0 else -14), sweep, radius * 1.015, hot, k, duration * 0.75, delay + 0.01, 1.3, 0.0, "Arc")
    n = 5
    for i in range(n):
        yaw = start_yaw + sweep * (0.35 + 0.6 * i / (n - 1)); rad = math.radians(yaw)
        radial = Vector((math.cos(rad), math.sin(rad), 0)); tang = Vector((-math.sin(rad), math.cos(rad), 0)) * (1 if sweep >= 0 else -1)
        s.emit(P(Pos=Vector(center) + radial * radius, Vel=(tang * 0.8 + radial * 0.6) * 520 + Vector((0, 0, s.fr(20, 120))), Drag=3.0, Color=hot, Shape="Streak",
                 Life=0.22, Delay=delay + duration * 0.3 * i / n, SizeStart=streak_size(s.fr(40, 70), 7), SizeEnd=streak_size(14, 2.5), bAlignToVelocity=True, bShrinkOut=False, FadeOut=0.6))

def shockwave(s, center, radius, color, duration, thick, delay=0.0):
    """a crisp ring racing out and fading, with a fainter coloured echo behind it"""
    R = max(40, radius); dur = max(0.08, duration); z = min(2.5, max(0.5, thick / 26.0))
    c = Vector(center)
    s.emit(P(Pos=c, Color=clerp(color, WHITE, 0.45), Shape="Wave", Life=dur, Delay=delay, SizeStart=(R * 0.3, R * 0.3, R * 0.6 * z), SizeEnd=(R * 2, R * 2, R * z),
             bEaseOut=True, bShrinkOut=False, FadeOut=0.6, Bright=1.1))
    s.emit(P(Pos=c + Vector((0, 0, 1)), Color=color, Shape="Wave", Life=dur * 1.3, Delay=delay + dur * 0.2, SizeStart=(R * 0.2, R * 0.2, R * 0.4 * z), SizeEnd=(R * 1.6, R * 1.6, R * 0.6 * z),
             bEaseOut=True, bShrinkOut=False, FadeOut=0.8, Bright=0.5))

def impact(s, loc, color, scale, crit, delay=0.0):
    """hit: starburst flash, fast streak sparks thrown out, a few slower embers that linger"""
    S = min(4, max(0.3, scale)); hot = clerp(color, WHITE, 0.65); L = Vector(loc)
    flash(s, L, hot, (58 if crit else 36) * S, 0.11, delay)
    n = round((9 if crit else 5) * S)
    for i in range(n):
        d = s.vrand(); d.z = abs(d.z) * 0.7 + 0.2; d.normalize()
        sp = s.fr(380, 640) * (1.2 if crit else 1.0)
        s.emit(P(Pos=L, Vel=d * sp, Drag=4.5, Gravity=-500, Color=color if i % 2 else hot, Shape="Streak", Life=s.fr(0.16, 0.26), Delay=delay,
                 SizeStart=streak_size(s.fr(34, 58) * S, 6 * S), SizeEnd=streak_size(10 * S, 2 * S), bAlignToVelocity=True, bShrinkOut=False, FadeOut=0.5, Bright=1.1))
    for i in range(3 if crit else 2):
        d = s.vrand(); d.z = abs(d.z) + 0.4
        s.emit(P(Pos=L, Vel=d * s.fr(60, 150), Drag=1.5, Gravity=120, Color=color, Shape="Ember", Life=s.fr(0.45, 0.7), Delay=delay + 0.04, SizeStart=9 * S, SizeEnd=4 * S,
                 SpinRate=6, SpinAxis=(0, 0, 1), FadeOut=0.6, Bright=0.9))
    if crit:
        s.emit(P(Pos=L, Color=(1, 0.82, 0.3), Shape="Wave", Life=0.26, Delay=delay, SizeStart=20 * S, SizeEnd=(150 * S, 150 * S, 110 * S), bEaseOut=True, bShrinkOut=False,
                 FadeOut=0.6, Rot=rot(s.fr(-25, 25), s.fr(0, 360), s.fr(-25, 25))))

def streak(s, a, b, color, width, life, delay=0.0):
    a, b = Vector(a), Vector(b); span = b - a; ln = span.length
    if ln < 1: return
    s.emit(P(Pos=(a + b) * 0.5, Color=color, Shape="Streak", Life=life, Delay=delay, Rot=from_x(span), SizeStart=streak_size(ln, width * 1.6), SizeEnd=streak_size(ln, width * 0.4),
             bShrinkOut=False, bEaseOut=True, FadeOut=0.6))

def glint(s, loc, color, size, delay=0.0):
    hot = clerp(color, WHITE, 0.7)
    s.emit(P(Pos=loc, Color=hot, Shape="Star", Life=0.16, Delay=delay, SizeStart=size * 0.3, SizeEnd=size * 1.1, bEaseOut=True, Rot=rot(0, s.fr(0, 90)), SpinRate=5, FadeOut=0.5))
    s.emit(P(Pos=loc, Color=hot, Life=0.1, Delay=delay, SizeStart=size * 0.2, SizeEnd=size * 0.5, bEaseOut=True, FadeOut=0.6))

def ground_cracks(s, center, radius, color, count, heal_at, width=7.0, delay=0.0):
    lines = min(16, max(3, count)); R = max(40, radius); race = 0.16; hot = clerp(color, WHITE, 0.35); a0 = s.fr(0, 360); C = Vector(center)
    for line in range(lines):
        yaw = a0 + 360 * line / lines + s.fr(-10, 10); prev = C + Vector((0, 0, 2))
        for kink in range(1, 5):
            frac = kink / 4; yaw += s.fr(-22, 22); rad = math.radians(yaw)
            nxt = C + Vector((math.cos(rad), math.sin(rad), 0)) * (R * frac * s.fr(0.85, 1.05)) + Vector((0, 0, 2))
            span = nxt - prev; ln = Vector((span.x, span.y)).length
            if ln > 2:
                d = delay + race * (frac - 0.25); w = width * (1.15 - 0.5 * frac)
                s.emit(P(Pos=(prev + nxt) * 0.5, Color=hot if kink == 1 else color, Shape="Cube", Delay=d, Life=max(0.2, heal_at - d), Rot=from_x((span.x, span.y, 0)),
                         SizeStart=(ln * 0.3, w * 0.4, 2), SizeEnd=(ln * 1.1, w, 2), bEaseOut=True))
            prev = nxt
