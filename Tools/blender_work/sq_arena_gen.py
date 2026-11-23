# Faithful Python port of ASQArena terrain/layout generation (SQArena.cpp) for preview renders.
# Units: UE centimetres, UE axes (X fwd, Y right, Z up). The renderer converts to Blender metres (Y flipped).
import math, zlib, csv
import numpy as np

SQ = "/home/claude/sq"
f32 = np.float32
PI = float(f32(math.pi))


# ---------------------------------------------------------------- UE FRandomStream (exact)
class FRandomStream:
    def __init__(self, seed):
        self.seed = int(seed) & 0xFFFFFFFF

    def _mutate(self):
        self.seed = (self.seed * 196314165 + 907633515) & 0xFFFFFFFF

    def frac(self):
        self._mutate()
        bits = 0x3F800000 | (self.seed >> 9)
        return f32(np.frombuffer(np.uint32(bits).tobytes(), dtype=np.float32)[0] - f32(1.0))

    def FRand(self):
        return float(self.frac())

    def FRandRange(self, lo, hi):
        lo = f32(lo); hi = f32(hi)
        return float(lo + (hi - lo) * self.frac())

    def RandRange(self, lo, hi):
        rng = (hi - lo) + 1
        return lo + (int(self.frac() * f32(rng)) if rng > 0 else 0)


def stable_seed(name):
    # FCrc::StrCrc32 treats every char as 4 bytes -> standard CRC32 over UTF-32LE
    return zlib.crc32(name.lower().encode("utf-32-le")) & 0x7FFFFFFF


def to_i32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


# ---------------------------------------------------------------- Perlin (UE's algorithm; permutation table is
# Ken Perlin's reference table - UE ships its own shuffled table, so the noise PATTERN differs, the character is the same)
_P = [151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,8,99,37,240,21,10,23,190,6,148,
247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,
74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,55,46,245,40,244,102,143,54,
65,25,63,161,1,216,80,73,209,76,132,187,208,89,18,169,200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,
52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,189,28,42,223,183,170,213,
119,248,152,2,44,154,163,70,221,153,101,155,167,43,172,9,129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,
218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,241,81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,
184,84,204,176,115,121,50,45,127,4,150,254,138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180]
PERM = np.array(_P + _P, dtype=np.int64)


def _grad2(h, x, y):
    h = h & 7
    return np.select([h == 0, h == 1, h == 2, h == 3, h == 4, h == 5, h == 6],
                     [x, x + y, y, -x + y, -x, -x - y, -y], x - y)


def perlin2d(x, y):
    x = np.asarray(x, dtype=np.float64); y = np.asarray(y, dtype=np.float64)
    xf = np.floor(x); yf = np.floor(y)
    xi = xf.astype(np.int64) & 255; yi = yf.astype(np.int64) & 255
    X = x - xf; Y = y - yf
    aa = PERM[xi] + yi; ab = aa + 1
    ba = PERM[xi + 1] + yi; bb = ba + 1
    u = X * X * X * (X * (X * 6 - 15) + 10)
    v = Y * Y * Y * (Y * (Y * 6 - 15) + 10)
    l1 = _grad2(PERM[aa], X, Y) + u * (_grad2(PERM[ba], X - 1, Y) - _grad2(PERM[aa], X, Y))
    l2 = _grad2(PERM[ab], X, Y - 1) + u * (_grad2(PERM[bb], X - 1, Y - 1) - _grad2(PERM[ab], X, Y - 1))
    return l1 + v * (l2 - l1)


def smooth01(t):
    c = np.clip(t, 0.0, 1.0)
    return c * c * (3 - 2 * c)


def sm(t):
    c = min(max(t, 0.0), 1.0)
    return c * c * (3 - 2 * c)


def dist_seg(px, py, ax, ay, bx, by):
    sx = bx - ax; sy = by - ay
    l2 = sx * sx + sy * sy
    t = min(max(((px - ax) * sx + (py - ay) * sy) / l2, 0.0), 1.0) if l2 > 1 else 0.0
    qx = ax + sx * t - px; qy = ay + sy * t - py
    return math.sqrt(qx * qx + qy * qy)


def dist_seg_np(px, py, ax, ay, bx, by):
    sx = bx - ax; sy = by - ay
    l2 = sx * sx + sy * sy
    if l2 > 1:
        t = np.clip(((px - ax) * sx + (py - ay) * sy) / l2, 0, 1)
    else:
        t = 0.0
    return np.hypot(ax + sx * t - px, ay + sy * t - py)


def round_to_int(v):
    return int(math.floor(v + 0.5))


# ---------------------------------------------------------------- data
def load_tuning():
    t = {}
    with open(f"{SQ}/Content/Data/Tuning.csv", newline="") as f:
        for row in csv.reader(f):
            if not row or row[0].startswith("#") or row[0] == "Key" or len(row) < 2:
                continue
            try:
                t[row[0].strip()] = float(row[1])
            except ValueError:
                pass
    return t


def load_biome(bid):
    with open(f"{SQ}/Content/Data/Biomes.csv", newline="") as f:
        rows = [r for r in csv.reader(f) if r and r[0] and not r[0].startswith("#")]
    head = [h.lower() for h in rows[0]]
    for r in rows[1:]:
        if r[0].lower() == bid.lower():
            return dict(zip(head, [c.strip() for c in r]))
    return dict(zip(head, rows[1]))


def srgb_hex_to_linear(hx):
    # FLinearColor::FromSRGBColor uses the exact sRGB curve (via a 256 LUT)
    hx = hx.lstrip("#")
    out = []
    for i in range(3):
        c = int(hx[2 * i:2 * i + 2], 16) / 255.0
        out.append(c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4)
    return np.array(out)


def palette(bid):
    b = load_biome(bid)
    P = {}
    for key, col in [("Grass", "groundcolor"), ("Prop", "propcolor"), ("Fog", "fogcolor"), ("GrassAlt", "grassaltcolor"),
                     ("Path", "pathcolor"), ("Cliff", "cliffcolor"), ("Top", "topcolor"), ("Basin", "basincolor"),
                     ("Wall", "wallcolor"), ("Moon", "mooncolor"), ("Sky", "skycolor"), ("Lamp", "lampcolor"),
                     ("Backdrop", "backdropcolor")]:
        P[key] = srgb_hex_to_linear(b[col])
    names = lambda k: [s.strip() for s in b.get(k, "").split("|") if s.strip()]
    P["Props"] = names("props"); P["Decor"] = names("decor"); P["RimProps"] = names("rimprops")
    P["LampProps"] = names("lamps"); P["BackdropProps"] = names("backdrop")
    P["PropCount"] = int(b["propcount"]); P["DecorCount"] = int(b["decorcount"])
    P["Gimmick"] = b["gimmick"]; P["Name"] = b["name"]
    return P


def hue(c):
    m = max(c)
    return c / m if m > 0.001 else np.ones(3)


def soft_tint(c, s):
    m = max(c)
    n = c / m if m > 0.01 else np.ones(3)
    return 1 + (n - 1) * s


# ---------------------------------------------------------------- arena
class Plateau:
    def radius_at(self, a):
        return self.Radius * (1 + self.Wobble * math.sin(self.Lobes * a + self.WobblePhase))


class Arena:
    def __init__(self, biome):
        self.biome = biome
        self.T = load_tuning()
        self.Pal = palette(biome)
        tune = lambda k, d: self.T.get(k, d)
        self.tune = tune
        self.Half = tune("ArenaHalfSize", 6000.0)
        self.Seed = stable_seed(biome)
        self.TerrainSeed = (self.Seed ^ 0x1B873593) & 0x7FFFFFFF
        self.build_height_grid()
        self.build_jump_pads()
        self.build_paths()
        self.build_colors()
        self.build_props()
        self.build_dressing()
        self.build_walls()
        self.build_backdrop()

    # ---- height grid
    def build_height_grid(self):
        tune = self.tune; Half = self.Half
        TR = FRandomStream(self.TerrainSeed)
        cell = min(max(tune("TerrainCell", 100.0), 50.0), 400.0)
        self.GridN = N = min(max(round_to_int(Half * 2 / cell), 16), 256)
        self.Cell = Half * 2 / N
        self.GridMin = -Half
        PlazaR = self.PlazaR = tune("TerrainPlazaRadius", 1600.0)
        PlazaBlend = 900.0
        NumPl = min(max(round_to_int(tune("TerrainPlateaus", 4)), 0), 8)
        PlH = max(0.0, tune("TerrainPlateauHeight", 450.0))
        HillH = max(0.0, tune("TerrainHillHeight", 300.0))
        NumHills = min(max(round_to_int(tune("TerrainHills", 3)), 0), 8)
        BasinDepth = max(0.0, tune("TerrainBasinDepth", 150.0))
        NumBasins = min(max(round_to_int(tune("TerrainBasins", 3)), 0), 8)
        NumRidges = min(max(round_to_int(tune("TerrainRidges", 1)), 0), 2)
        EdgeRise = tune("TerrainEdgeRise", 200.0); EdgeBand = 1000.0
        MaxSlope = min(max(tune("TerrainMaxSlope", 30.0), 10.0), 40.0)
        RampTan = math.tan(math.radians(MaxSlope - 4))
        RampPerH = 1.5 / max(0.05, RampTan)

        self.Plateaus = []
        AngOff = TR.FRandRange(0, 2 * PI)
        for i in range(NumPl):
            p = Plateau()
            p.Height = PlH * TR.FRandRange(0.6, 1.3)
            p.Ramp = max(400.0, p.Height * RampPerH)
            p.Radius = TR.FRandRange(700, 1300)
            p.Wobble = TR.FRandRange(0.06, 0.18)
            p.WobblePhase = TR.FRandRange(0, 2 * PI)
            p.Lobes = TR.RandRange(2, 4)
            ang = AngOff + (i + TR.FRandRange(0.25, 0.75)) * 2 * PI / NumPl
            mind = PlazaR + 300 + p.Radius * (1 + p.Wobble) + p.Ramp
            maxd = max(mind, Half - p.Radius * 0.6)
            d = TR.FRandRange(mind, maxd)
            p.CX = math.cos(ang) * d; p.CY = math.sin(ang) * d
            self.Plateaus.append(p)

        hills = []
        hmin = PlazaR + 1400
        for i in range(NumHills):
            hx = hy = 0.0
            for _ in range(12):
                hx = TR.FRandRange(-Half * 0.85, Half * 0.85)
                hy = TR.FRandRange(-Half * 0.85, Half * 0.85)
                if hx * hx + hy * hy > hmin * hmin:
                    break
            sig = TR.FRandRange(800, 1400)
            hh = HillH * TR.FRandRange(0.5, 1.0)
            hills.append((hx, hy, sig, hh))
        self.Hills = hills

        basins = []
        for i in range(NumBasins):
            r = TR.FRandRange(500, 900); dep = BasinDepth * TR.FRandRange(0.7, 1.0); ramp = 700.0
            bmin = PlazaR + r + ramp + 200
            bx = by = 0.0
            for _ in range(12):
                bx = TR.FRandRange(-Half * 0.8, Half * 0.8)
                by = TR.FRandRange(-Half * 0.8, Half * 0.8)
                if bx * bx + by * by <= bmin * bmin:
                    continue
                clear = True
                for p in self.Plateaus:
                    c = p.Radius * (1 + p.Wobble) + p.Ramp + r + ramp
                    if (p.CX - bx) ** 2 + (p.CY - by) ** 2 < c * c:
                        clear = False
                if clear:
                    break
            basins.append((bx, by, r, dep, ramp))
        self.Basins = basins

        ridges = []
        P = self.Plateaus
        if NumRidges > 0 and len(P) >= 2:
            pairs = []
            for i in range(len(P)):
                j = (i + 1) % len(P)
                if j == i or (len(P) == 2 and i == 1):
                    continue
                pairs.append((math.hypot(P[i].CX - P[j].CX, P[i].CY - P[j].CY), i, j))
            pairs.sort(key=lambda t: t[0])  # TArray::Sort is not stable but distances differ
            for _, a, b in pairs:
                if len(ridges) >= NumRidges:
                    break
                h = min(P[a].Height, P[b].Height)
                ramp = max(400.0, h * RampPerH)
                if dist_seg(0, 0, P[a].CX, P[a].CY, P[b].CX, P[b].CY) < PlazaR + ramp + 450:
                    continue
                ridges.append((P[a].CX, P[a].CY, P[b].CX, P[b].CY, h, ramp))
        self.Ridges = ridges

        R = N + 1
        xs = self.GridMin + np.arange(R) * self.Cell
        VX, VY = np.meshgrid(xs, xs)  # [iy, ix]
        H = np.zeros_like(VX)
        for hx, hy, sig, hh in hills:
            H += hh * np.exp(-((VX - hx) ** 2 + (VY - hy) ** 2) / (2 * sig * sig))
        sunk = np.zeros_like(VX)
        for bx, by, r, dep, ramp in basins:
            bd = np.hypot(VX - bx, VY - by)
            sunk = np.minimum(sunk, -dep * (1 - smooth01((bd - r) / ramp)))
        H += sunk
        for p in P:
            px = VX - p.CX; py = VY - p.CY
            pd = np.hypot(px, py)
            topr = p.Radius * (1 + p.Wobble * np.sin(p.Lobes * np.arctan2(py, px) + p.WobblePhase))
            pv = p.Height * (1 - smooth01((pd - topr) / p.Ramp))
            H += np.maximum(0, pv - np.maximum(H, 0))
        for ax, ay, bx, by, h, ramp in ridges:
            rd = dist_seg_np(VX, VY, ax, ay, bx, by)
            rv = h * (1 - smooth01((rd - 250.0) / ramp))
            H += np.maximum(0, rv - np.maximum(H, 0))
        H *= smooth01((np.hypot(VX, VY) - PlazaR) / PlazaBlend)
        ed = Half - np.maximum(np.abs(VX), np.abs(VY))
        H += EdgeRise * (1 - smooth01(ed / EdgeBand))
        H = H.astype(np.float32).astype(np.float64)

        # Walkability relaxation: Gauss-Seidel sweeps in the same order as the C++
        maxstep = self.Cell * math.tan(math.radians(MaxSlope))
        h = H.tolist()
        for _ in range(64):
            changed = False
            for iy in range(R):
                row = h[iy]; prev = h[iy - 1] if iy > 0 else None
                for ix in range(R):
                    v = row[ix]
                    if ix > 0 and row[ix - 1] + maxstep < v: v = row[ix - 1] + maxstep
                    if prev is not None and prev[ix] + maxstep < v: v = prev[ix] + maxstep
                    if v < row[ix]:
                        if v < row[ix] - 0.01: changed = True
                        row[ix] = v
            for iy in range(R - 1, -1, -1):
                row = h[iy]; nxt = h[iy + 1] if iy < N else None
                for ix in range(R - 1, -1, -1):
                    v = row[ix]
                    if ix < N and row[ix + 1] + maxstep < v: v = row[ix + 1] + maxstep
                    if nxt is not None and nxt[ix] + maxstep < v: v = nxt[ix] + maxstep
                    if v < row[ix]:
                        if v < row[ix] - 0.01: changed = True
                        row[ix] = v
            if not changed:
                break
        self.H = np.array(h)
        self.VX, self.VY = VX, VY
        self.MinZ = float(self.H.min()); self.MaxZ = float(self.H.max())

    # ---- queries
    def ground_z(self, x, y):
        N = self.GridN
        gx = min(max((x - self.GridMin) / self.Cell, 0.0), float(N))
        gy = min(max((y - self.GridMin) / self.Cell, 0.0), float(N))
        ix = min(int(gx), N - 1); iy = min(int(gy), N - 1)
        fx = gx - ix; fy = gy - iy
        H = self.H
        h00 = H[iy, ix]; h10 = H[iy, ix + 1]; h01 = H[iy + 1, ix]; h11 = H[iy + 1, ix + 1]
        if fx >= fy:
            return float(h00 + fx * (h10 - h00) + fy * (h11 - h10))
        return float(h00 + fy * (h01 - h00) + fx * (h11 - h01))

    def normal(self, x, y):
        e = self.Cell * 0.5
        dzx = (self.ground_z(x + e, y) - self.ground_z(x - e, y)) / (2 * e)
        dzy = (self.ground_z(x, y + e) - self.ground_z(x, y - e)) / (2 * e)
        n = np.array([-dzx, -dzy, 1.0])
        return n / np.linalg.norm(n)

    def slope_deg(self, x, y):
        return math.degrees(math.acos(min(max(self.normal(x, y)[2], -1), 1)))

    def dist_pad(self, x, y):
        return min((math.hypot(p["pos"][0] - x, p["pos"][1] - y) for p in self.Pads), default=1e30)

    def path_mask(self, x, y):
        if not self.PathA:
            return 0.0
        best = min(dist_seg(x, y, a[0], a[1], b[0], b[1]) for a, b in zip(self.PathA, self.PathB))
        if best > self.PathHW * 1.5:
            return 0.0
        best += 28 * float(perlin2d(x * 0.011, y * 0.011))
        return 1 - sm((best - self.PathHW * 0.55) / (self.PathHW * 0.45))

    def path_mask_np(self, X, Y):
        best = np.full(X.shape, 1e30)
        for a, b in zip(self.PathA, self.PathB):
            best = np.minimum(best, dist_seg_np(X, Y, a[0], a[1], b[0], b[1]))
        far = best > self.PathHW * 1.5
        best = best + 28 * perlin2d(X * 0.011, Y * 0.011)
        m = 1 - smooth01((best - self.PathHW * 0.55) / (self.PathHW * 0.45))
        return np.where(far, 0.0, m)

    # ---- jump pads
    def build_jump_pads(self):
        self.Pads = []
        cnt = min(max(round_to_int(self.tune("JumpPadCount", 6)), 0), 16)
        if cnt == 0 or not self.Plateaus:
            return
        PR = FRandomStream(self.TerrainSeed ^ 0x51A7F00D)
        Half = self.Half; PlazaR = self.PlazaR
        NumP = len(self.Plateaus)
        att = 0
        while att < cnt * 8 and len(self.Pads) < cnt:
            pl = self.Plateaus[att % NumP]
            inang = math.atan2(-pl.CY, -pl.CX)
            spread = 0.0 if att < NumP else math.radians(PR.FRandRange(-110, 110))
            att += 1
            ang = inang + spread
            topr = pl.radius_at(ang)
            d = topr + pl.Ramp + 200
            px = pl.CX + math.cos(ang) * d; py = pl.CY + math.sin(ang) * d
            if abs(px) > Half - 500 or abs(py) > Half - 500: continue
            if px * px + py * py < (PlazaR + 250) ** 2: continue
            if self.slope_deg(px, py) > 12: continue
            if self.dist_pad(px, py) < 900: continue
            pz = self.ground_z(px, py)
            tx = pl.CX + math.cos(ang) * topr * 0.45; ty = pl.CY + math.sin(ang) * topr * 0.45
            tz = self.ground_z(tx, ty)
            if tz - pz < 150: continue
            self.Pads.append({"pos": (px, py, pz), "target": (tx, ty, tz), "normal": tuple(self.normal(px, py))})

    # ---- paths + lamp spots
    def build_paths(self):
        self.PathA = []; self.PathB = []; self.Lamps = []
        self.PathHW = min(max(self.tune("PathWidth", 340) * 0.5, 50), 600)
        PlazaR = self.PlazaR; Half = self.Half
        MaxLamps = min(max(round_to_int(self.tune("MapLamps", 10)), 0), 24)
        PR = FRandomStream(self.TerrainSeed ^ 0x6D2B79F5)
        ends = [np.array(p["pos"][:2]) for p in self.Pads]
        for pl in self.Plateaus:
            has = any(math.hypot(pl.CX - p["target"][0], pl.CY - p["target"][1]) < pl.Radius * 1.5 for p in self.Pads)
            if has: continue
            inang = math.atan2(-pl.CY, -pl.CX)
            fr = pl.radius_at(inang) + pl.Ramp
            fx = min(max(pl.CX + math.cos(inang) * fr, -Half * 0.9), Half * 0.9)
            fy = min(max(pl.CY + math.sin(inang) * fr, -Half * 0.9), Half * 0.9)
            ends.append(np.array([fx, fy]))
        cands = []
        for e, end in enumerate(ends):
            n = np.linalg.norm(end)
            start = end / n * (PlazaR * 0.4) if n > 1e-8 else np.zeros(2)
            delta = end - start
            L = float(np.linalg.norm(delta))
            bend = min(max(PR.FRandRange(-0.22, 0.22) * L, -900), 900)
            side_s = 1.0 if e % 2 == 0 else -1.0
            if L < 300: continue
            side = np.array([-delta[1] / L, delta[0] / L])
            ctrl = start + delta * 0.5 + side * bend
            prev = start
            for s in range(1, 9):
                t = s / 8
                p = start * (1 - t) ** 2 + ctrl * (2 * (1 - t) * t) + end * t * t
                self.PathA.append(prev); self.PathB.append(p); prev = p
            lt = 0.55
            lp = start * (1 - lt) ** 2 + ctrl * (2 * (1 - lt) * lt) + end * lt * lt
            tg = (ctrl - start) * (2 * (1 - lt)) + (end - ctrl) * (2 * lt)
            tg = tg / np.linalg.norm(tg)
            cands.append(lp + np.array([-tg[1], tg[0]]) * (side_s * (self.PathHW + 150)))
        for pl in self.Plateaus:
            c = np.array([pl.CX, pl.CY]); tc = -c / np.linalg.norm(c)
            cands.append(c + tc * (pl.Radius * 0.35))
        for c in cands:
            if len(self.Lamps) >= MaxLamps: break
            cx, cy = float(c[0]), float(c[1])
            if abs(cx) > Half - 400 or abs(cy) > Half - 400: continue
            if cx * cx + cy * cy < (PlazaR * 0.75) ** 2: continue
            if self.slope_deg(cx, cy) > 22: continue
            if any(dist_seg(cx, cy, p["pos"][0], p["pos"][1], p["target"][0], p["target"][1]) < 350 for p in self.Pads): continue
            self.Lamps.append((cx, cy, self.ground_z(cx, cy)))
        self.PathA = [tuple(map(float, a)) for a in self.PathA]
        self.PathB = [tuple(map(float, b)) for b in self.PathB]

    # ---- vertex colours (BuildTerrainMesh)
    def build_colors(self):
        Pal = self.Pal; H = self.H; N = self.GridN; c = self.Cell
        VX, VY = self.VX, self.VY
        TopRef = max(150.0, self.MaxZ * 0.6)
        NoiseOff = float(self.TerrainSeed % 997) * 1.37
        PoolR = 560.0; K = 3
        idx = np.arange(N + 1)
        xl = np.maximum(idx - 1, 0); xr = np.minimum(idx + 1, N)
        DZX = (H[:, xr] - H[:, xl]) / ((xr - xl) * c)[None, :]
        DZY = (H[xr, :] - H[xl, :]) / ((xr - xl) * c)[:, None]
        nrm = np.stack([-DZX, -DZY, np.ones_like(H)], -1)
        self.Normals = nrm / np.linalg.norm(nrm, axis=-1, keepdims=True)
        slope = np.sqrt(DZX ** 2 + DZY ** 2)
        km = np.maximum(idx - K, 0); kp = np.minimum(idx + K, N)
        navg = 0.25 * (H[:, km] + H[:, kp] + H[km, :] + H[kp, :])
        curv = navg - H
        N1 = perlin2d(VX * 0.00045 + NoiseOff, VY * 0.00045 - NoiseOff)
        N2 = perlin2d(VX * 0.0023 - NoiseOff, VY * 0.0023 + NoiseOff * 0.5)
        lerp = lambda a, b, t: a + (b - a) * t[..., None]
        VC = lerp(Pal["Grass"], Pal["GrassAlt"], smooth01(0.5 + N1 * 1.6))
        VC = VC * (1 + 0.12 * N2)[..., None]
        VC = lerp(VC, Pal["Top"] * (1 + 0.08 * N2)[..., None], 0.9 * smooth01((H - 60) / TopRef))
        VC = lerp(VC, np.broadcast_to(Pal["Basin"], VC.shape), smooth01((-H - 10) / 110))
        self.PathMaskGrid = pm = self.path_mask_np(VX, VY)
        pt = pm * (1 - smooth01((slope - 0.30) / 0.25))
        VC = lerp(VC, Pal["Path"] * (1 + 0.10 * N2)[..., None], pt)
        cliff = lerp(np.broadcast_to(Pal["Cliff"], VC.shape), np.broadcast_to(Pal["Cliff"] * 0.6, VC.shape), smooth01((slope - 0.42) / 0.2))
        VC = lerp(VC, cliff * (1 + 0.08 * N2)[..., None], smooth01((slope - 0.20) / 0.26))
        VC = VC * np.clip(1 - curv * 0.012, 0.72, 1.3)[..., None]
        for lx, ly, _ in self.Lamps:
            ld2 = (lx - VX) ** 2 + (ly - VY) ** 2
            pool = np.where(ld2 < PoolR * PoolR, (1 - np.sqrt(ld2) / PoolR) ** 2, 0.0)
            VC = VC * (1 + 0.55 * pool)[..., None] + Pal["Lamp"] * (0.05 * pool)[..., None]
        ed = self.Half - np.maximum(np.abs(VX), np.abs(VY))
        VC = VC * (0.72 + 0.28 * smooth01(ed / 900))[..., None]
        self.Colors = VC

    # ---- props (Rebuild): returns instances as dicts {mesh, x, y, z, yaw_deg, scale, kind}
    def can_place(self, x, y):
        if self.slope_deg(x, y) > self.tune("TerrainPropMaxSlope", 20): return False
        if self.path_mask(x, y) > 0.25: return False
        for p in self.Pads:
            if dist_seg(x, y, p["pos"][0], p["pos"][1], p["target"][0], p["target"][1]) < 450: return False
        for l in self.Lamps:
            if (l[0] - x) ** 2 + (l[1] - y) ** 2 < 200 * 200: return False
        return True

    def build_props(self):
        Half = self.Half; Pal = self.Pal
        self.Props = []; self.Decor = []
        Rand = FRandomStream(self.Seed)
        self.ArtRand = ArtRand = FRandomStream(self.Seed ^ 0x2A5F17)
        PlazaClear = self.tune("TerrainPlazaRadius", 1600) + 100
        meshes = Pal["Props"]

        def place(x, y, yaw):
            m = meshes[ArtRand.RandRange(0, len(meshes) - 1)]
            ps = ArtRand.FRandRange(0.85, 1.2)
            self.Props.append(dict(mesh=m, x=x, y=y, z=self.ground_z(x, y) - 6, yaw=yaw, s=ps))

        for i in range(Pal["PropCount"]):
            px = Rand.FRandRange(-Half * 0.92, Half * 0.92); py = Rand.FRandRange(-Half * 0.92, Half * 0.92)
            Rand.FRandRange(0.8, 1.15); Rand.FRandRange(0.9, 1.6); Rand.FRandRange(-6, 6)
            yaw = Rand.FRandRange(0, 360)
            if px * px + py * py < PlazaClear * PlazaClear: continue
            if not self.can_place(px, py): continue
            place(px, py, yaw)
        RimRand = FRandomStream(self.TerrainSeed ^ 0x3C6EF372)
        per = min(max(round_to_int(self.tune("TerrainRimProps", 5)), 0), 16)
        for pl in self.Plateaus:
            for k in range(per):
                a = RimRand.FRandRange(0, 2 * PI)
                r = pl.radius_at(a) * RimRand.FRandRange(0.8, 0.92)
                rx = pl.CX + math.cos(a) * r; ry = pl.CY + math.sin(a) * r
                RimRand.FRandRange(0.8, 1.15); RimRand.FRandRange(0.9, 1.6); RimRand.FRandRange(-6, 6)
                yaw = RimRand.FRandRange(0, 360)
                if abs(rx) > Half * 0.94 or abs(ry) > Half * 0.94: continue
                if not self.can_place(rx, ry): continue
                place(rx, ry, yaw)
        # decor clusters
        dm = Pal["Decor"]
        if dm:
            PlazaR = self.PlazaR
            cx = cy = 0.0; left = 0
            for i in range(Pal["DecorCount"]):
                if left <= 0:
                    cx = ArtRand.FRandRange(-Half * 0.95, Half * 0.95)
                    cy = ArtRand.FRandRange(-Half * 0.95, Half * 0.95)
                    left = ArtRand.RandRange(1, 4)
                left -= 1
                dx = min(max(cx + ArtRand.FRandRange(-220, 220), -Half * 0.97), Half * 0.97)
                dy = min(max(cy + ArtRand.FRandRange(-220, 220), -Half * 0.97), Half * 0.97)
                dyaw = ArtRand.FRandRange(0, 2 * PI)
                keep = ArtRand.FRand() < 0.35
                m = dm[ArtRand.RandRange(0, len(dm) - 1)]
                ds = ArtRand.FRandRange(0.85, 1.25)
                if self.dist_pad(dx, dy) < 250: continue
                if self.slope_deg(dx, dy) > 32: continue
                if dx * dx + dy * dy < PlazaR * PlazaR * 0.36 and not keep: continue
                self.Decor.append(dict(mesh=m, x=dx, y=dy, z=self.ground_z(dx, dy) - 3, yaw=math.degrees(dyaw), s=ds,
                                       normal=tuple(self.normal(dx, dy))))

    # ---- BuildDressing (rim stones, cliff rocks, grass tufts)
    def build_dressing(self):
        Half = self.Half; Pal = self.Pal
        DR = FRandomStream(self.Seed ^ 0x2545F491)
        self.RimArt = []; self.Rocks = []; self.Tufts = []

        def near_flight(x, y, clear):
            return any(dist_seg(x, y, p["pos"][0], p["pos"][1], p["target"][0], p["target"][1]) < clear for p in self.Pads)

        def add_rock(x, y, size, shade):
            rot = (DR.FRandRange(-25, 25), DR.FRandRange(0, 360), DR.FRandRange(-25, 25))  # pitch, yaw, roll
            sc = (size * DR.FRandRange(0.8, 1.2), size * DR.FRandRange(0.8, 1.2), size * DR.FRandRange(0.5, 0.8))
            self.Rocks.append(dict(x=x, y=y, z=self.ground_z(x, y) + sc[2] * 15, rot=rot, s=sc, shade=shade))

        rim_meshes = Pal["RimProps"]
        RS = min(max(round_to_int(self.tune("RimStones", 16)), 0), 64)
        CR = min(max(round_to_int(self.tune("CliffRocks", 60)), 0), 400)
        for pl in self.Plateaus:
            for k in range(RS):
                a = (k + DR.FRandRange(0.2, 0.8)) * 2 * PI / RS
                r = pl.radius_at(a) + DR.FRandRange(-10, 90)
                rx = pl.CX + math.cos(a) * r; ry = pl.CY + math.sin(a) * r
                use_art = len(rim_meshes) > 0 and DR.FRand() < 0.45
                pick = DR.RandRange(0, len(rim_meshes) - 1) if rim_meshes else 0
                asz = DR.FRandRange(0.75, 1.1); rsz = DR.FRandRange(0.45, 0.9); yaw = DR.FRandRange(0, 360)
                if abs(rx) > Half - 150 or abs(ry) > Half - 150: continue
                if near_flight(rx, ry, 380) or self.path_mask(rx, ry) > 0.15: continue
                if use_art:
                    self.RimArt.append(dict(mesh=rim_meshes[pick], x=rx, y=ry, z=self.ground_z(rx, ry) - 5, yaw=yaw, s=asz))
                else:
                    add_rock(rx, ry, rsz, 0)
            here = CR // len(self.Plateaus)
            for k in range(here):
                a = DR.FRandRange(0, 2 * PI)
                r = pl.radius_at(a) + pl.Ramp * DR.FRandRange(0.1, 0.95)
                rx = pl.CX + math.cos(a) * r; ry = pl.CY + math.sin(a) * r
                size = DR.FRandRange(0.25, 0.55)
                shade = 0 if DR.FRand() < 0.5 else 1
                if abs(rx) > Half - 150 or abs(ry) > Half - 150: continue
                if near_flight(rx, ry, 300) or self.path_mask(rx, ry) > 0.15: continue
                add_rock(rx, ry, size, shade)
        nt = min(max(round_to_int(self.tune("GrassTufts", 700)), 0), 5000)
        for i in range(nt):
            tx = DR.FRandRange(-Half * 0.96, Half * 0.96); ty = DR.FRandRange(-Half * 0.96, Half * 0.96)
            shade = 0 if DR.FRand() < 0.5 else 1
            tyaw = DR.FRandRange(0, 360); tsz = DR.FRandRange(0.8, 1.3)
            if self.path_mask(tx, ty) > 0.1 or self.slope_deg(tx, ty) > 20: continue
            tz = self.ground_z(tx, ty) - 5
            blades = []
            for b in range(3):
                byaw = tyaw + b * 120
                off = (math.cos(math.radians(byaw)) * 14 * tsz, math.sin(math.radians(byaw)) * 14 * tsz)
                pitch = DR.FRandRange(-22, -8)
                h = DR.FRandRange(0.28, 0.45) * tsz
                blades.append(dict(x=tx + off[0], y=ty + off[1], z=tz, yaw=byaw, pitch=pitch, sxy=0.13 * tsz, sz=h))
            self.Tufts.append(dict(shade=shade, blades=blades))

    # ---- BuildWalls (geometry list; fence pieces are laid out by the renderer)
    def build_walls(self):
        Half = self.Half
        WallH = 150.0; Thick = 140.0; Line = Half + Thick * 0.5
        Segs = max(1, round_to_int(Half * 2 / 600)); Step = Half * 2 / Segs
        self.WallBlocks = []; self.Columns = []; self.Skirts = []; self.WallSegs = []
        for side in range(4):
            along_y = side < 2; sign = 1.0 if side % 2 == 0 else -1.0
            exy = (lambda a, c: (sign * c, a)) if along_y else (lambda a, c: (a, sign * c))
            eg = lambda a: self.ground_z(*exy(a, Half - 20))
            segtop = []; sidemin = 1e30
            for k in range(Segs):
                a0 = -Half + k * Step
                z0, zm, z1 = eg(a0), eg(a0 + Step * 0.5), eg(a0 + Step)
                lo = min(z0, zm, z1) - 90; top = max(z0, zm, z1) + WallH
                segtop.append(top); sidemin = min(sidemin, lo + 90)
                c = exy(a0 + Step * 0.5, Line)
                ext = (Thick, Step + 4, top - lo) if along_y else (Step + 4, Thick, top - lo)
                self.WallBlocks.append(dict(c=(c[0], c[1], (lo + top) / 2), size=ext))
                self.WallSegs.append(dict(a=exy(a0 + 30, Line), b=exy(a0 + Step - 30, Line), top=top, along_y=along_y))
            for k in range(Segs + 1):
                a = -Half + k * Step
                ct = max(segtop[max(k - 1, 0)], segtop[min(k, Segs - 1)]) + 45
                cl = eg(a) - 120
                c = exy(a, Line)
                self.Columns.append(dict(c=(c[0], c[1], (cl + ct) / 2), size=(190, 190, ct - cl), top=ct))
            SkW = 7000.0
            sc = exy(0, Half + Thick + SkW * 0.5)
            ln = (Half + Thick + SkW) * 2
            st = sidemin - 10
            self.Skirts.append(dict(c=(sc[0], sc[1], st - 50), size=(SkW, ln, 100) if along_y else (ln, SkW, 100)))

    def build_backdrop(self):
        Half = self.Half
        BR = FRandomStream(self.Seed ^ 0x68E31DA4)
        bm = self.Pal["BackdropProps"]
        self.Backdrop = []
        cnt = min(max(round_to_int(self.tune("BackdropCount", 80)), 0), 400)
        for i in range(cnt):
            side = BR.RandRange(0, 3)
            along = BR.FRandRange(-Half - 2500, Half + 2500)
            out = Half + BR.FRandRange(450, 3600)
            bx = out if side == 0 else (-out if side == 1 else along)
            by = along if side < 2 else (out if side == 2 else -out)
            yaw = BR.FRandRange(0, 360); size = BR.FRandRange(1.4, 2.6)
            pick = BR.RandRange(0, len(bm) - 1) if bm else 0
            self.Backdrop.append(dict(mesh=bm[pick] if bm else None, x=bx, y=by, z=self.ground_z(bx, by) - 30, yaw=yaw, s=size))

    def summary(self):
        s = [f"{self.biome}: seed {self.Seed} terrain seed {self.TerrainSeed}",
             f"  grid {self.GridN}x{self.GridN} cell {self.Cell:.0f}, Z {self.MinZ:.0f}..{self.MaxZ:.0f}",
             f"  plateaus {len(self.Plateaus)}, hills {len(self.Hills)}, basins {len(self.Basins)}, ridges {len(self.Ridges)}",
             f"  pads {len(self.Pads)}, path segs {len(self.PathA)}, lamps {len(self.Lamps)}",
             f"  props {len(self.Props)}, decor {len(self.Decor)}, rim art {len(self.RimArt)}, rocks {len(self.Rocks)}, tufts {len(self.Tufts)}"]
        for i, p in enumerate(self.Plateaus):
            s.append(f"  plateau {i}: c=({p.CX:.0f},{p.CY:.0f}) r={p.Radius:.0f} h={p.Height:.0f} ramp={p.Ramp:.0f} lobes={p.Lobes}")
        for p in self.Pads:
            s.append(f"  pad {tuple(round(v) for v in p['pos'])} -> {tuple(round(v) for v in p['target'])}")
        for l in self.Lamps:
            s.append(f"  lamp {tuple(round(v) for v in l)}")
        return "\n".join(s)


if __name__ == "__main__":
    import sys, time
    for b in sys.argv[1:] or ["Graveyard"]:
        t = time.time()
        a = Arena(b)
        print(a.summary(), f"\n  ({time.time() - t:.1f}s)")
