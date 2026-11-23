# Builds Content/Data/FxRecipes.csv: every reaper's signature-attack accent, E and R cast, and the shared events, from a motif
# (three shapes + the reaper's palette) laid over a template for what the ability does. Bespoke rows per reaper on top.
import csv, sys
sys.path.insert(0, "/home/claude/blender_work/fx2")
COLS = ["Id", "Event", "Verb", "Shape", "Col", "At", "Fwd", "Up", "Delay", "Life", "Count", "Size", "Size2", "Speed", "Radius", "Angle", "Sweep", "Lift", "Grav", "Spin", "Bright"]
ROWS = []
def r(verb, shape="", col="Mid", at="O", **kw):
    d = dict(Verb=verb, Shape=shape, Col=col, At=at)
    for k, v in kw.items(): d[k] = v
    return d
def put(rid, ev, rows):
    for x in rows:
        y = dict(x); y["Id"] = rid; y["Event"] = ev; ROWS.append(y)

# motif: s1 = the travelling shape, s2 = the rising / lingering shape, s3 = the ground mark
MOTIF = dict(Grim=("Crescent", "Wisp", "Spikes"), Mort=("Shard", "Ember", "Teeth"), Widow=("Feather", "Wisp", "Web"), Bonesmith=("Chevron", "Shard", "Teeth"),
             Keeper=("Flame", "Wisp", "Sigil"), Monk=("Bandage", "Ember", "Sigil"), Intern=("Chevron", "Flame", "Spikes"), Femur=("Claw", "Shard", "Spikes"),
             Digby=("Shard", "Ember", "Spikes"), Collector=("Drop", "Ember", "Sigil"), Beak=("Feather", "Wisp", "Sigil"), Tallow=("Seed", "Flame", "Spikes"),
             Lookout=("Feather", "Shard", "Sigil"), Vexa=("Cleaver", "Drop", "Spikes"), Actuary=("Leaf", "Moth", "Sigil"), Sweep=("Crow", "Feather", "Teeth"),
             Solace=("Bolt", "Star", "Sigil"), Pallbearers=("Shard", "Wisp", "Casket"), Ectoplasm=("Petal", "Wisp", "Sigil"), Neckless=("Flame", "Ember", "Spikes"),
             Tickwell=("Star", "Ember", "Cog"))
KIT = dict(Grim=("Arc", "Dash", "Nova"), Mort=("Projectile", "Volley", "Self"), Widow=("Aura", "Cone", "Zone"), Bonesmith=("Turret", "Circle", "Totem"),
           Keeper=("Nova", "Circle", "Nova"), Monk=("Orbit", "Dash", "Barrage"), Intern=("Chain", "Volley", "Zone"), Femur=("Drop", "Dash", "Nova"),
           Digby=("Arc", "Cone", "Zone"), Collector=("Projectile", "Line", "Circle"), Beak=("Aura", "Circle", "Global"), Tallow=("Projectile", "Blink", "Line"),
           Lookout=("Beam", "Line", "Totem"), Vexa=("Orbit", "Dash", "Self"), Actuary=("Drop", "Line", "Barrage"), Sweep=("Chain", "Blink", "Volley"),
           Solace=("Arc", "Circle", "Global"), Pallbearers=("Turret", "Circle", "Self"), Ectoplasm=("Chain", "Chain", "Totem"), Neckless=("Beam", "Dash", "Self"),
           Tickwell=("Nova", "Blink", "Nova"))

# ---------------------------------------------------------------- signature-attack accents (land on the blow: Delay 0 = impact)
def a_arc(m):
    return [r("Swoosh", col="Mid", Up=60, Radius="0.9R", Angle=-100, Sweep=200, Size=1.0, Life=0.3, Delay=-0.06, Bright=0.9),
            r("Swoosh", "Arc", "Core", Up=64, Radius="0.93R", Angle=-90, Sweep=200, Size=1.0, Life=0.2, Delay=-0.05, Bright=1.3),
            r("Burst", "Streak", "Core", Fwd="0.5R", Up=60, Angle=60, Count=6, Speed=700, Size=60, Life=0.22),
            r("Rise", m[1], "Mid", Fwd="0.45R", Radius="0.45R", Count=4, Speed=90, Size=34, Life=0.6, Delay=0.05, Bright=0.7)]
def sig(m, at="M", k=1.0):
    """the signature flick every non-swing weapon gets at the hand: starburst, a short flick, the reaper's own shapes thrown forward"""
    return [r("Glint", col="Core", at=at, Size=70 * k, Delay=-0.03), r("Pop", "Spikes", "Core", at, Size=130 * k, Life=0.12),
            r("Swoosh", col="Mid", Up=90, Fwd=20, Radius=95 * k, Angle=-75, Sweep=150, Size=0.9, Life=0.2, Bright=0.9),
            r("Fan", m[0], "Mid", at, Radius=60, Angle=22, Count=3, Speed=640, Size=120 * k, Life=0.34, Sweep=0.025),
            r("Burst", "Streak", "Alt", at, Angle=18, Count=5, Speed=1000, Size=80, Life=0.18)]
def a_projectile(m):
    return sig(m) + [r("Burst", m[1], "Alt", "M", Angle=40, Count=2, Speed=160, Size=24, Life=0.45, Lift=0.5)]
def a_aura(m):
    return [r("Wave", "Teeth", "Mid", Up=8, Radius="1R", Life=0.32, Bright=0.8, Spin=60), r("Wave", col="Alt", Up=60, Radius="0.7R", Life=0.5, Delay=0.05, Bright=0.5),
            r("Wave", col="Core", Up=120, Radius="0.45R", Life=0.5, Delay=0.1, Bright=0.5), r("Ring", m[0], "Alt", Up=50, Radius="0.4R", Count=6, Speed=300, Size=80, Life=0.5),
            r("Rise", m[1], "Core", Radius="0.9R", Count=5, Speed=110, Size=34, Life=0.7, Bright=0.8)]
def a_turret(m):
    return sig(m, "M", 0.8) + [r("Mark", m[2], "Alt", "H", Size=170, Life=0.4, Spin=90, Bright=0.45), r("Pillar", col="Mid", at="H", Size=30, Lift=220, Life=0.25),
            r("Burst", m[1], "Mid", "H", Up=40, Angle=180, Count=5, Speed=300, Size=34, Life=0.45, Lift=0.7, Grav=-700)]
def a_nova(m):
    return [r("Wave", "Teeth", "Mid", Up=8, Radius="1R", Life=0.32, Bright=1.1, Spin=90), r("Wave", col="Mid", Up=10, Radius="0.85R", Life=0.45, Delay=0.06, Bright=0.55),
            r("Ring", m[0], "Mid", Up=40, Radius="0.2R", Count=8, Speed=760, Size=74, Life=0.4), r("Mark", m[2], "Alt", Size="1.3R", Life=0.45, Spin=60, Bright=0.45),
            r("Pop", "Spikes", "Core", Up=70, Size=130, Life=0.12)]
def a_orbit(m):
    return [r("Ring", m[0], "Mid", Up=70, Radius="0.8R", Count=4, Size=110, Life=0.45, Spin=420), r("Swoosh", "Arc", "Core", Up=72, Radius="0.8R", Sweep=320, Life=0.4, Bright=1.1),
            r("Swoosh", col="Mid", Up=66, Radius="0.78R", Sweep=300, Size=0.7, Life=0.42, Delay=0.02, Bright=0.7), r("Rise", "Ember", "Alt", Radius="0.8R", Count=5, Speed=120, Size=16, Life=0.5)]
def a_chain(m):
    return sig(m, "M", 0.8) + [r("Line", "Bolt", "Mid", "M", Fwd=30, Radius="0.5R", Count=4, Size=120, Life=0.2, Sweep=0.012), r("Line", "Bolt", "Core", "M", Fwd=40, Up=6, Radius="0.5R", Count=3, Size=70, Life=0.14, Sweep=0.014),
            r("Wave", "Teeth", "Mid", "H", Up=8, Radius=130, Life=0.22, Spin=120), r("Pop", "Spikes", "Core", "H", Up=70, Size=120, Life=0.12, Delay=0.03),
            r("Burst", "Streak", "Core", "H", Up=60, Angle=180, Count=7, Speed=620, Size=60, Life=0.2, Lift=0.2, Delay=0.03)]
def a_drop(m):
    return [r("Mark", m[2], "Alt", "H", Size="1.5R", Life=0.5, Bright=0.45), r("Wave", "Teeth", "Mid", "H", Up=8, Radius="1R", Life=0.3, Spin=60), r("Wave", col="Mid", at="H", Up=10, Radius="0.8R", Life=0.42, Delay=0.06, Bright=0.5),
            r("Burst", m[0], "Mid", "H", Up=20, Angle=180, Count=6, Speed=420, Size=50, Life=0.55, Lift=0.9, Grav=-900), r("Pillar", col="Core", at="H", Size=26, Lift=260, Life=0.22, Delay=-0.03),
            r("Cracks", col="Mid", at="H", Radius="0.7R", Count=6, Life=0.8), r("Dust", col="Dark", at="H", Count=4, Size=40, Speed=200, Angle=180, Life=0.5),
            r("Shake", Size=0.25)]
def a_beam(m):
    return sig(m) + [r("Lance", col="Mid", at="M", Radius="0.95R", Size=22, Life=0.26), r("Pillar", col="Core", Fwd="1R", Size=18, Lift=240, Life=0.26, Delay=0.04), r("Wave", "Teeth", "Mid", Fwd="1R", Up=8, Radius=110, Life=0.26, Delay=0.04, Spin=120),
            r("Line", "Chevron", "Mid", "M", Fwd=40, Radius="0.9R", Count=5, Size=70, Life=0.26, Sweep=0.02, Bright=0.8),
            r("Line", m[1], "Alt", "M", Radius="1R", Count=5, Size=40, Life=0.32, Sweep=0.015), r("Flash", col="Core", Fwd="1R", Up=80, Size=110, Life=0.12, Delay=0.05)]
ATTACK = dict(Arc=a_arc, Projectile=a_projectile, Aura=a_aura, Turret=a_turret, Nova=a_nova, Orbit=a_orbit, Chain=a_chain, Drop=a_drop, Beam=a_beam)

# ---------------------------------------------------------------- casts (E; R = bigger, with a shake)
def c_dash(m, u):
    k = 1.3 if u else 1.0
    return [r("Swoosh", col="Mid", Up=50, Radius=110 * k, Angle=100, Sweep=160, Size=1.1, Life=0.28), r("Dust", col="Dark", Count=4, Size=44, Speed=180, Angle=180, Life=0.5),
            r("Line", m[0], "Mid", Up=60, Radius="1L", Count=6, Size=80 * k, Life=0.34, Sweep=0.02), r("Burst", "Streak", "Core", Up=70, Angle=10, Count=8, Speed=1300, Size=110, Life=0.22),
            r("Flash", col="Core", at="T", Up=60, Size=110 * k, Life=0.12, Delay=0.12), r("Wave", col="Mid", at="T", Up=8, Radius="1R", Life=0.35, Delay=0.12),
            r("Burst", m[1], "Alt", "T", Up=40, Angle=180, Count=6, Speed=300, Size=32, Life=0.5, Lift=0.7, Delay=0.12),
            r("Mark", m[2], "Alt", "T", Size="1.3R", Life=0.5, Delay=0.12, Bright=0.45)]
def c_volley(m, u):
    k = 1.3 if u else 1.0
    return [r("Glint", col="Core", at="M", Size=80 * k), r("Pop", "Spikes", "Core", "M", Size=120 * k, Life=0.12),
            r("Fan", m[0], "Mid", "M", Radius=70, Angle=32, Count=5 if not u else 9, Speed=1000, Size=120 * k, Life=0.36, Sweep=0.012), r("Pillars", col="Alt", Fwd=120, Radius=300, Count=3, Size=12, Lift=120, Life=0.24, Sweep=0.03),
            r("Burst", "Streak", "Alt", "M", Angle=28, Count=10, Speed=1100, Size=80, Life=0.2), r("Dust", col="Dark", Fwd=-30, Count=3, Size=36, Speed=140, Angle=180, Life=0.4),
            r("Swoosh", col="Mid", Up=90, Radius=120 * k, Angle=-60, Sweep=120, Size=0.8, Life=0.2)]
def c_cone(m, u):
    k = 1.3 if u else 1.0
    return [r("Swoosh", col="Mid", Up=60, Radius="0.9R", Angle=-75, Sweep=150, Size=1.2 * k, Life=0.32), r("Swoosh", "Arc", "Core", Up=66, Radius="0.93R", Angle=-65, Sweep=150, Size=1.0, Life=0.24, Bright=1.3),
            r("Fan", m[0], "Alt", Up=50, Radius="0.45R", Angle=50, Count=5, Speed=520, Size=80 * k, Life=0.4, Sweep=0.015),
            r("Burst", "Streak", "Core", Up=60, Angle=45, Count=10, Speed=900, Size=70, Life=0.22), r("Dust", col="Dark", Fwd="0.4R", Count=4, Radius="0.3R", Size=50, Speed=220, Angle=40, Life=0.5),
            r("Rise", m[1], "Mid", Fwd="0.5R", Radius="0.4R", Count=5, Speed=120, Size=30, Life=0.6, Delay=0.08, Bright=0.8)]
def c_zone(m, u):
    out = [r("Mark", m[2], "Alt", "T", Size="2R", Life=1.3, Spin=40, Bright=0.45), r("Wave", col="Core", at="T", Up=8, Radius="1R", Life=0.4),
           r("Ring", m[0], "Alt", "T", Up=40, Radius="0.8R", Count=6, Size=64, Life=1.2, Spin=140, Lift=30),
           r("Rise", m[1], "Mid", "T", Radius="0.9R", Count=10, Speed=140, Size=34, Life=0.9, Bright=0.9), r("Flash", col="Core", at="T", Up=50, Size=120, Life=0.12)]
    if u: out += [r("Pillar", col="Core", at="T", Size=70, Lift=560, Life=0.5), r("Wave", col="Mid", at="T", Up=10, Radius="1.15R", Life=0.6, Delay=0.1, Bright=0.6), r("Shake", Size=0.3)]
    return out
def c_nova(m, u):
    out = [r("Flash", col="Core", Up=60, Size=150, Life=0.13), r("Wave", col="Core", Up=8, Radius="1R", Life=0.32, Bright=1.15), r("Wave", col="Mid", Up=10, Radius="0.9R", Life=0.5, Delay=0.06, Bright=0.6),
           r("Ring", m[0], "Mid", Up=45, Radius="0.22R", Count=10, Speed=820, Size=70, Life=0.42), r("Burst", "Streak", "Core", Up=50, Angle=180, Count=14, Speed=900, Size=70, Life=0.24),
           r("Mark", m[2], "Alt", Size="1.6R", Life=0.6, Spin=50, Bright=0.45), r("Rise", m[1], "Mid", Radius="0.8R", Count=8, Speed=130, Size=32, Life=0.8, Delay=0.1, Bright=0.8)]
    if u: out += [r("Cracks", col="Mid", Radius="0.8R", Count=9, Life=1.0), r("Pillar", col="Core", Size=80, Lift=620, Life=0.45), r("Shake", Size=0.45)]
    return out
def c_circle(m, u):
    out = [r("Mark", m[2], "Alt", "T", Size="2R", Life=0.8, Spin=30, Bright=0.45), r("Wave", col="Core", at="T", Up=8, Radius="1R", Life=0.32),
           r("Burst", m[0], "Alt", "T", Up=20, Angle=180, Count=8, Speed=380, Size=46, Life=0.55, Lift=0.9, Grav=-700), r("Rise", m[1], "Mid", "T", Radius="0.8R", Count=6, Speed=130, Size=30, Life=0.7),
           r("Flash", col="Core", at="T", Up=50, Size=120, Life=0.12)]
    if u: out += [r("Pillar", col="Core", at="T", Size=70, Lift=520, Life=0.45), r("Cracks", col="Mid", at="T", Radius="0.8R", Count=8, Life=1.0), r("Shake", Size=0.35)]
    return out
def c_line(m, u):
    k = 1.3 if u else 1.0
    return [r("Glint", col="Core", at="M", Size=90), r("Pop", "Spikes", "Core", "M", Size=150 * k, Life=0.12),
            r("Lance", col="Mid", at="M", Radius="0.95L", Size=26 * k, Life=0.3), r("Pillars", col="Alt", Fwd=70, Radius="0.9L", Count=6, Size=16 * k, Lift=170 * k, Life=0.34, Sweep=0.028),
            r("Line", "Chevron", "Mid", Up=12, Fwd=60, Radius="0.9L", Count=5, Size=190 * k, Life=0.45, Sweep=0.03, Bright=0.8),
            r("Line", m[0], "Alt", Up=110, Fwd=60, Radius="0.9L", Count=6, Size=90 * k, Life=0.4, Sweep=0.028, Lift=60),
            r("Pop", "Spikes", "Core", "T", Up=80, Size=190 * k, Life=0.14, Delay=0.16), r("Wave", "Teeth", "Mid", "T", Up=8, Radius="0.6R", Life=0.3, Delay=0.16, Spin=90),
            r("Pillar", col="Core", at="T", Size=22 * k, Lift=300 * k, Life=0.3, Delay=0.16)] + ([r("Shake", Size=0.3)] if u else [])
def c_self(m, u):
    k = 1.3 if u else 1.0
    return [r("Mark", m[2], "Alt", Size=280 * k, Life=1.0, Spin=90, Bright=0.45), r("Ring", m[0], "Alt", Up=30, Radius=95 * k, Count=5, Size=60 * k, Life=0.9, Spin=300, Lift=150),
            r("Rise", m[1], "Mid", Radius=80, Count=8, Speed=220, Size=30, Life=0.8), r("Pillar", col="Core", Size=46 * k, Lift=400 * k, Life=0.45),
            r("Wave", col="Mid", Up=8, Radius=170 * k, Life=0.35), r("Flash", col="Core", Up=90, Size=110 * k, Life=0.12)] + ([r("Shake", Size=0.3)] if u else [])
def c_totem(m, u):
    return [r("Pillar", col="Core", at="T", Size=70, Lift=540, Life=0.5), r("Mark", m[2], "Alt", "T", Size="2R", Life=1.4, Spin=25, Bright=0.45),
            r("Ring", m[0], "Alt", "T", Up=50, Radius="0.7R", Count=6, Size=64, Life=1.3, Spin=-120), r("Rise", m[1], "Mid", "T", Radius="1R", Count=10, Speed=120, Size=32, Life=1.0),
            r("Wave", col="Core", at="T", Up=8, Radius="1R", Life=0.4), r("Burst", "Shard", "Dark", "T", Up=10, Angle=180, Count=6, Speed=360, Size=36, Life=0.6, Lift=1.0, Grav=-900),
            r("Cracks", col="Mid", at="T", Radius="0.6R", Count=7, Life=1.2), r("Shake", Size=0.35)]
def c_barrage(m, u):
    return [r("Mark", m[2], "Alt", "T", Size="2R", Life=1.1, Spin=20, Bright=0.45), r("Rain", m[0], "Alt", "T", Radius="0.9R", Count=10, Size=90, Speed=2400, Lift=620, Sweep=0.5),
            r("Wave", col="Core", at="T", Up=8, Radius="1R", Life=0.4), r("Rise", m[1], "Mid", "T", Radius="0.9R", Count=8, Speed=120, Size=30, Life=0.9, Delay=0.2), r("Shake", Size=0.35, Delay=0.15)]
def c_blink(m, u):
    return [r("Flash", col="Core", Up=70, Size=110, Life=0.1), r("Rise", m[1], "Mid", Radius=60, Count=6, Speed=320, Size=34, Life=0.45), r("Wave", col="Mid", Up=8, Radius=130, Life=0.28, Bright=0.7),
            r("Dust", col="Dark", Count=3, Size=40, Speed=120, Angle=180, Life=0.45),
            r("Pillar", col="Core", at="T", Size=50, Lift=320, Life=0.3, Delay=0.08), r("Flash", col="Core", at="T", Up=70, Size=130, Life=0.12, Delay=0.08),
            r("Wave", col="Mid", at="T", Up=8, Radius="1R", Life=0.35, Delay=0.08), r("Burst", m[0], "Alt", "T", Up=40, Angle=180, Count=8, Speed=420, Size=50, Life=0.45, Lift=0.5, Delay=0.08),
            r("Mark", m[2], "Alt", "T", Size="1.4R", Life=0.6, Delay=0.08, Spin=60, Bright=0.45)]
def c_global(m, u):
    return [r("Pillar", col="Core", Size=90, Lift=900, Life=0.6), r("Wave", col="Core", Up=8, Radius=900, Life=0.7), r("Wave", col="Mid", Up=10, Radius=700, Life=0.9, Delay=0.1, Bright=0.6),
            r("Ring", m[0], "Alt", Up=60, Radius=120, Count=12, Speed=700, Size=70, Life=0.8), r("Rise", m[1], "Mid", Radius=500, Count=16, Speed=200, Size=36, Life=1.2),
            r("Mark", m[2], "Alt", Size=520, Life=1.2, Spin=30, Bright=0.45), r("Flash", col="Core", Up=90, Size=170, Life=0.14), r("Shake", Size=0.4)]
def c_chain(m, u):
    k = 1.3 if u else 1.0
    return [r("Glint", col="Core", at="M", Size=90), r("Pop", "Spikes", "Core", "M", Size=140 * k, Life=0.12),
            r("Lance", col="Mid", at="M", Radius="0.95L", Size=20 * k, Life=0.22), r("Bolt", col="Mid", at="T", Lift=340 * k, Count=5, Size=14 * k, Life=0.24, Delay=0.05),
            r("Bolt", col="Alt", at="H", Lift=260 * k, Count=4, Size=10 * k, Life=0.2, Delay=0.02),
            r("Line", m[0], "Alt", Up=110, Fwd=40, Radius="0.95L", Count=5, Size=80 * k, Life=0.34, Sweep=0.02, Lift=70),
            r("Wave", "Teeth", "Mid", "T", Up=8, Radius=150 * k, Life=0.24, Delay=0.07, Spin=140), r("Pop", "Spikes", "Core", "T", Up=70, Size=170 * k, Life=0.13, Delay=0.07),
            r("Burst", "Streak", "Core", "T", Up=70, Angle=180, Count=8, Speed=620, Size=60, Life=0.22, Lift=0.3, Delay=0.07), r("Rise", m[1], "Mid", "T", Radius=60, Count=4, Speed=140, Size=34, Life=0.6, Delay=0.1)]
CAST = dict(Dash=c_dash, Volley=c_volley, Cone=c_cone, Zone=c_zone, Nova=c_nova, Circle=c_circle, Line=c_line, Self=c_self, Totem=c_totem, Barrage=c_barrage, Blink=c_blink,
            Global=c_global, Chain=c_chain)

# ---------------------------------------------------------------- bespoke layers (what makes each reaper its own)
EXTRA = {}
def extra(rid, ev, *rows): EXTRA.setdefault((rid, ev), []).extend(rows)

def build():
    ROWS.clear()
    for rid, m in MOTIF.items():
        w, e, u = KIT[rid]
        put(rid, "Attack", ATTACK[w](m) + EXTRA.get((rid, "Attack"), []))
        put(rid, "E", CAST[e](m, False) + EXTRA.get((rid, "E"), []))
        put(rid, "R", CAST[u](m, True) + EXTRA.get((rid, "R"), []))
    for (rid, ev), rows in EXTRA.items():
        if rid == "*": put(rid, ev, rows)

def write(path="/home/claude/sq/Content/Data/FxRecipes.csv"):
    with open(path, "w", newline="") as f:
        f.write("# Effect recipes (Nov 19 2026). One row = one layer. Id = reaper id or * for shared events. Events: Attack (signature weapon accent, Delay 0 = the blow lands),\n")
        f.write("# E, R (casts), and shared: Impact ImpactLight ImpactHeavy Death LevelUp Collect SoulCollect GoldCollect ChestOpen LanternLit LanternSpent Bank ShrinePulse HazardPulse Dash Jump Land. Edit and replay: no rebuild needed.\n")
        f.write("# Col: Core Mid Dark Alt (the reaper's palette / the event colour) White or #RRGGBB. At: O feet, H hit point, T target, M muzzle. Values may end in R (x radius) or L (x distance to target).\n")
        f.write("# Verbs: Flash Glint Wave Swoosh Mark Pop Pillar Pillars Lance Burst Dust Rise Ring Fan Line Shot Bolt Rain Cracks Shake (see SQFxRecipe.cpp for what each column means per verb).\n")
        w = csv.DictWriter(f, COLS, extrasaction="ignore"); w.writeheader()
        for x in ROWS: w.writerow({k: x.get(k, "") for k in COLS})
    print("recipes", len(ROWS))

if __name__ == "__main__":
    import gen as G, bespoke  # noqa: bespoke fills gen.EXTRA (run through the imported module, not __main__, so both share one EXTRA)
    G.build(); G.write()
