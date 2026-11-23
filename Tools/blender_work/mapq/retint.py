# Per-biome recolour of the two KayKit atlases (CC0) so the borrowed village pieces sit in each map's palette.
# Output: ArtSource/Props/kit{m,d}_{graveyard,harbour,clock}.png (imported as T_KitM_Graveyard ... by import_art.py)
import numpy as np, colorsys, sys
from PIL import Image
P = "/home/claude/sq/ArtSource/Props/"

def rgb2hsv(a):
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    mx, mn = a.max(-1), a.min(-1)
    d = mx - mn
    h = np.zeros_like(mx)
    m = d > 1e-6
    rc = np.where(m, (mx - r) / np.where(m, d, 1), 0); gc = np.where(m, (mx - g) / np.where(m, d, 1), 0); bc = np.where(m, (mx - b) / np.where(m, d, 1), 0)
    h = np.where(mx == r, bc - gc, np.where(mx == g, 2 + rc - bc, 4 + gc - rc))
    h = (h / 6.0) % 1.0
    s = np.where(mx > 1e-6, d / np.where(mx > 1e-6, mx, 1), 0)
    return h * 360, s, mx

def hsv2rgb(h, s, v):
    h = (h % 360) / 60.0
    i = np.floor(h).astype(int) % 6
    f = h - np.floor(h)
    p, q, t = v * (1 - s), v * (1 - s * f), v * (1 - s * (1 - f))
    r = np.choose(i, [v, q, p, p, t, v]); g = np.choose(i, [t, v, v, q, p, p]); b = np.choose(i, [p, p, t, v, v, q])
    return np.stack([r, g, b], -1)

# class -> (hue, sat, value multiplier, value floor)
SCHEMES = {
    # Graveyard: plum-slate roofs, bone / lavender stone, mauve timber, teal-black pines
    "graveyard": {"red": (286, 0.46, 0.62), "blue": (252, 0.40, 0.50), "green": (176, 0.50, 0.34), "yellow": (40, 0.22, 0.86),
                  "brown": (338, 0.26, 0.60), "pink": (300, 0.42, 0.62), "grey": (262, 0.13, 0.92), "skin": (30, 0.14, 0.88)},
    # Drowned Harbour: weathered sea-grey timber, slate-blue roofs, rust accents, kelp-dark trees
    "harbour": {"red": (12, 0.50, 0.58), "blue": (212, 0.40, 0.56), "green": (168, 0.30, 0.36), "yellow": (38, 0.30, 0.78),
                "brown": (24, 0.24, 0.52), "pink": (350, 0.34, 0.60), "grey": (205, 0.14, 0.80), "skin": (36, 0.20, 0.74)},
    # Clocktower: copper roofs, night-indigo trim, sandstone walls, bronze-dark trees
    "clock": {"red": (22, 0.62, 0.74), "blue": (246, 0.46, 0.44), "green": (58, 0.34, 0.32), "yellow": (36, 0.52, 0.78),
              "brown": (18, 0.36, 0.50), "pink": (330, 0.36, 0.56), "grey": (34, 0.16, 0.86), "skin": (32, 0.26, 0.82)},
}

def classify(h, s, v):
    c = np.full(h.shape, "grey", dtype=object)
    sat = s > 0.16
    c[sat & ((h >= 345) | (h < 22)) & (s > 0.55)] = "red"
    c[sat & ((h >= 345) | (h < 45)) & (s <= 0.55)] = "brown"
    c[sat & (h >= 22) & (h < 45) & (s > 0.55) & (v > 0.75)] = "yellow"
    c[sat & (h >= 22) & (h < 45) & (s > 0.55) & (v <= 0.75)] = "brown"
    c[sat & (h >= 45) & (h < 72)] = "yellow"
    c[sat & (h >= 72) & (h < 175)] = "green"
    c[sat & (h >= 175) & (h < 265)] = "blue"
    c[sat & (h >= 265) & (h < 345)] = "pink"
    c[sat & (h >= 15) & (h < 45) & (s < 0.42) & (v > 0.7)] = "skin"
    return c

def retint(src, scheme):
    im = np.asarray(Image.open(P + src).convert("RGBA")).astype(np.float32) / 255
    h, s, v = rgb2hsv(im[..., :3])
    cls = classify(h, s, v)
    # one class per atlas swatch (8 x 4 gradient cells), from the cell's upper half, so a gradient never flips class
    H, W = h.shape
    ch, cw = H // 4, W // 8
    for cy in range(4):
        for cx in range(8):
            blk = (slice(cy * ch, cy * ch + ch // 2), slice(cx * cw + cw // 4, cx * cw + cw))
            rgbm = im[..., :3][blk].reshape(-1, 3).mean(0)
            bh, bs, bv = rgb2hsv(rgbm[None, None, :])
            cls[cy * ch:(cy + 1) * ch, cx * cw:(cx + 1) * cw] = classify(bh, bs, bv)[0, 0]
    oh, os_, ov = h.copy(), s.copy(), v.copy()
    for name, (th, ts, vm) in SCHEMES[scheme].items():
        m = cls == name
        oh[m] = th + (h[m] - np.mean(h[m]) if m.any() else 0) * 0.15
        os_[m] = np.clip(ts * (0.75 + 0.5 * s[m]), 0, 1) if name != "grey" else ts
        ov[m] = np.clip(v[m] * vm, 0, 1)
    out = hsv2rgb(oh, os_, ov)
    res = np.concatenate([out, im[..., 3:4]], -1)
    return Image.fromarray((np.clip(res, 0, 1) * 255 + 0.5).astype(np.uint8), "RGBA")

if __name__ == "__main__":
    for scheme in SCHEMES:
        for pack in ("kitm", "kitd"):
            retint(pack + "_texture.png", scheme).save(P + "%s_%s.png" % (pack, scheme))
            print("wrote", pack, scheme)
    # glow mask for the Fair_ / Set_ palettes (emissive swatches): same layout, white where the swatch glows
    sys.path.insert(0, "/home/claude/blender_work/fair")
