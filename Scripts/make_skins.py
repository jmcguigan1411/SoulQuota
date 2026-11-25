# Soul Quota skins (Nov 25 2026, Docs/RunStructure.md). Builds every reaper's skin palettes from its own palette and writes
# the Kind=Skin rows of Content/Data/Cosmetics.csv. Run in the cloud / any Python with Pillow:  python3 Scripts/make_skins.py
#
# A skin is a GRADIENT MAP over the reaper's own palette: each swatch's luminance (after a per-theme value curve) picks a colour
# on the theme's ramp, so the painted shading and value structure of the body survive while the colour story changes
# completely; the theme also moves the value key (Bone Moon is high key, Abyssal low key, Ember Wake high contrast). Bright
# saturated swatches (eyes, lantern glow, runes) become the theme's accent so the glow slots still read.
# Three skins per reaper: two themes chosen to sit far from the reaper's own hue (so a skin never looks like the default),
# plus Spectral (the premium glowing one). Original names only.
import csv, colorsys, io, os, sys
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CHAR = os.path.join(ROOT, "ArtSource", "Characters")
OUT = os.path.join(CHAR, "skins")
DATA = os.path.join(ROOT, "Content", "Data")

# Each theme: name, value gamma (<1 lifts / >1 crushes), accent, card colour, line, and four ramps (dark -> light, sRGB):
#   main   = the reaper's most used colour family (robe / skin / fur)
#   second = its next colour family
#   trim   = every other colour family (belts, metal, details)
#   neutral= its greys and whites (bone, cloth, steel)
# so a skin keeps the material separation of the original (robe vs bone vs trim) while the whole colour story changes.
THEMES = {
    "BoneMoon": ("Bone Moon", 0.8, (170, 222, 255), "#C9D3E6", "Moonlit bone and cold blue shadow.",
                 [(12, 18, 46), (36, 62, 132), (108, 150, 222), (222, 234, 255)],
                 [(30, 24, 22), (110, 96, 84), (196, 184, 160), (250, 244, 228)],
                 [(40, 40, 60), (120, 120, 160), (200, 200, 235), (255, 255, 255)],
                 [(30, 24, 22), (110, 96, 84), (196, 184, 160), (250, 244, 228)]),
    "EmberWake": ("Ember Wake", 1.15, (255, 150, 40), "#E0612A", "Charcoal and coals, still glowing.",
                  [(12, 6, 6), (88, 20, 12), (200, 70, 24), (255, 180, 90)],
                  [(8, 8, 10), (40, 36, 36), (90, 82, 78), (160, 150, 140)],
                  [(40, 14, 0), (160, 80, 0), (250, 170, 40), (255, 240, 180)],
                  [(8, 8, 10), (40, 36, 36), (90, 82, 78), (170, 160, 150)]),
    "Abyssal": ("Abyssal", 1.2, (60, 255, 228), "#1C8C94", "From the drowned deep: black water and lantern-fish light.",
                [(4, 10, 22), (12, 54, 90), (30, 128, 156), (150, 236, 234)],
                [(6, 8, 14), (26, 30, 52), (62, 72, 112), (136, 156, 206)],
                [(0, 30, 30), (0, 120, 110), (60, 230, 200), (200, 255, 240)],
                [(6, 8, 14), (30, 36, 60), (80, 92, 130), (170, 186, 220)]),
    "Verdigris": ("Verdigris", 1.0, (190, 255, 130), "#5FAE8C", "Old copper gone green in the rain.",
                  [(14, 24, 20), (40, 96, 80), (104, 170, 140), (214, 240, 214)],
                  [(30, 20, 12), (100, 64, 36), (170, 120, 70), (236, 200, 150)],
                  [(20, 30, 10), (90, 120, 30), (180, 220, 90), (240, 255, 190)],
                  [(20, 20, 22), (80, 82, 86), (150, 152, 156), (230, 230, 228)]),
    "GraveRose": ("Grave Rose", 1.05, (255, 120, 180), "#C2588A", "Funeral flowers left out too long.",
                  [(24, 8, 16), (96, 28, 58), (196, 96, 138), (255, 214, 230)],
                  [(10, 12, 10), (34, 52, 36), (80, 110, 80), (170, 200, 160)],
                  [(40, 30, 10), (140, 110, 50), (220, 190, 120), (255, 245, 215)],
                  [(20, 16, 18), (90, 80, 84), (170, 160, 162), (245, 238, 236)]),
    "Spectral": ("Spectral", 0.7, (226, 186, 255), "#B9A6F5", "Barely there: a ghost of a ghost, glowing from within.",
                 [(30, 22, 60), (100, 86, 180), (190, 180, 250), (255, 255, 255)],
                 [(10, 40, 50), (40, 140, 160), (130, 230, 240), (240, 255, 255)],
                 [(60, 20, 70), (170, 90, 200), (240, 190, 255), (255, 250, 255)],
                 [(40, 40, 60), (140, 140, 180), (220, 220, 250), (255, 255, 255)]),
}
COMMON = ["BoneMoon", "EmberWake", "Abyssal", "Verdigris", "GraveRose"]
THEME_HUE = {"BoneMoon": 0.62, "EmberWake": 0.05, "Abyssal": 0.5, "Verdigris": 0.4, "GraveRose": 0.92}
PRICES = (2500, 6000, 12000)


def lum(c):
    r, g, b = [x / 255.0 for x in c]
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def ramp(stops, t):
    t = min(1.0, max(0.0, t))
    seg = t * (len(stops) - 1)
    i = min(len(stops) - 2, int(seg))
    f = seg - i
    a, b = stops[i], stops[i + 1]
    return tuple(int(round(a[k] + (b[k] - a[k]) * f)) for k in range(3))


def is_accent(c):
    h, s, v = colorsys.rgb_to_hsv(*[x / 255.0 for x in c])
    return s > 0.45 and v > 0.82


def dominant_hue(img):
    """Mean hue (weighted by saturation x value) of the reaper's own palette."""
    px = list(img.convert("RGB").get_flattened_data()) if hasattr(img, "get_flattened_data") else list(img.convert("RGB").getdata())
    sx = sy = wt = 0.0
    import math
    for c in px:
        h, s, v = colorsys.rgb_to_hsv(*[x / 255.0 for x in c])
        w = s * v
        sx += math.cos(2 * math.pi * h) * w
        sy += math.sin(2 * math.pi * h) * w
        wt += w
    if wt <= 1e-6:
        return 0.0
    return (math.atan2(sy, sx) / (2 * math.pi)) % 1.0


def hue_dist(a, b):
    d = abs(a - b) % 1.0
    return min(d, 1.0 - d)


def hue_bin(c):
    h, sat, v = colorsys.rgb_to_hsv(*[x / 255.0 for x in c])
    return None if sat < 0.18 or v < 0.08 else int(h * 12) % 12


def skin_image(base, theme):
    _, gamma, accent, _, _, main, second, trim, neutral = THEMES[theme]
    src = base.convert("RGB")
    sp = src.load()
    # colour families of this palette: hue bins ranked by how many pixels of the palette they cover
    counts = {}
    for y in range(src.size[1]):
        for x in range(src.size[0]):
            c = sp[x, y]
            if c == (128, 128, 128) or is_accent(c):
                continue
            hb = hue_bin(c)
            if hb is not None:
                counts[hb] = counts.get(hb, 0) + 1
    order = sorted(counts, key=lambda k: -counts[k])
    family = {}
    for rank, hb in enumerate(order):
        family[hb] = main if rank == 0 else (second if rank == 1 else trim)
    out = Image.new("RGB", src.size)
    op = out.load()
    cache = {}
    for y in range(src.size[1]):
        for x in range(src.size[0]):
            c = sp[x, y]
            if c not in cache:
                if c == (128, 128, 128):
                    cache[c] = c  # unused filler cells stay neutral
                elif is_accent(c):
                    v = colorsys.rgb_to_hsv(*[k / 255.0 for k in c])[2]
                    cache[c] = tuple(int(min(255, a * (0.75 + 0.3 * v))) for a in accent)
                else:
                    hb = hue_bin(c)
                    stops = neutral if hb is None else family.get(hb, trim)
                    cache[c] = ramp(stops, lum(c) ** gamma)
            op[x, y] = cache[c]
    return out


def pick_themes(idx, hue):
    """Two common themes far from the reaper's own hue (rotated through the four most distant by roster position so the
    lineup does not wear one theme), then Spectral."""
    ranked = sorted(COMMON, key=lambda t: -hue_dist(THEME_HUE[t], hue))[:4]
    first = ranked[idx % 4]
    second = ranked[(idx + 2) % 4]
    return [first, second, "Spectral"]


def main():
    os.makedirs(OUT, exist_ok=True)
    reapers = list(csv.DictReader(open(os.path.join(DATA, "Reapers.csv"), newline="")))
    rows = []
    for idx, r in enumerate(reapers):
        rid = r["Id"]
        model = r.get("Model") or ("R_" + rid)
        src = os.path.join(CHAR, "%s_palette.png" % model.lower())
        if not os.path.isfile(src):
            print("no palette for", rid, src)
            continue
        base = Image.open(src)
        themes = pick_themes(idx, dominant_hue(base))
        for tier, theme in enumerate(themes):
            name, _, _, card, line = THEMES[theme][:5]
            img = skin_image(base, theme)
            img.save(os.path.join(OUT, "%s_%s.png" % (model, theme)))
            rows.append(dict(Id="Skin_%s_%s" % (rid, theme), Kind="Skin", Name=name, Color=card, Reaper=rid, Price=str(PRICES[tier]), Tex=theme,
                             Desc=line))
    # rewrite Cosmetics.csv: keep comments and non-skin rows, replace every Kind=Skin row
    path = os.path.join(DATA, "Cosmetics.csv")
    raw = open(path, newline="").read().splitlines()
    header = ["Id", "Kind", "Name", "Color", "Reaper", "Price", "Tex", "Desc"]
    out = []
    hdr_done = False
    for ln in raw:
        if ln.startswith("#") or not ln.strip():
            out.append(ln)
            continue
        cells = next(csv.reader([ln]))
        if not hdr_done:
            out.append(",".join(header))
            old_header = cells
            hdr_done = True
            continue
        d = dict(zip(old_header, cells))
        if d.get("Kind") == "Skin":
            continue
        buf = io.StringIO()
        csv.writer(buf, lineterminator="").writerow([d.get(h, "") for h in header])
        out.append(buf.getvalue())
    out.append("# Nov 25 2026 skins (Scripts/make_skins.py writes these rows; edit THEMES / PRICES there, never by hand). Kind Skin = bought with")
    out.append("# banked souls on the SKINS page (main menu); Reaper = who wears it; Tex = texture suffix (T_<Model>_<Tex>); Color = card swatch / fallback tint.")
    for d in rows:
        buf = io.StringIO()
        csv.writer(buf, lineterminator="").writerow([d[h] for h in header])
        out.append(buf.getvalue())
    open(path, "w", newline="").write("\n".join(out) + "\n")
    print("skins:", len(rows), "->", OUT)


if __name__ == "__main__":
    main()
