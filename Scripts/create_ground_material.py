"""
Soul Quota - creates /Game/Materials/M_Ground: the arena's surface material.
  BaseColor = VertexColor * Color * (subtle world-aligned noise: big soft patches + fine grain)
  Emissive  = BaseColor * Glow        (lamp bulbs, jump pads, the moon, the lantern ring)
  Roughness 0.9, low specular.
The C++ (ASQArena) paints the terrain's look into vertex colours and uses this for the ground, walls, rocks,
tufts and glowing bits; meshes without vertex colours read as white, so Color alone tints them.
Missing -> the game falls back to M_Clay (flat colours). Safe to re-run (skips if it exists;
delete the asset to rebuild it).
"""
import os
import unreal

PATH = "/Game/Materials"
NAME = "M_Ground"
FULL = PATH + "/" + NAME

mel = unreal.MaterialEditingLibrary
assets = unreal.AssetToolsHelpers.get_asset_tools()


def try_set(obj, prop, value):
    try:
        obj.set_editor_property(prop, value)
    except Exception as err:  # property / enum names can shift between engine versions
        unreal.log_warning("[SoulQuota ground] could not set {}: {}".format(prop, err))


def noise_function(*names):
    for n in names:
        if hasattr(unreal.NoiseFunction, n):
            return getattr(unreal.NoiseFunction, n)
    return None


if unreal.EditorAssetLibrary.does_asset_exist(FULL):
    unreal.log_warning(FULL + " already exists - leaving it alone.")
else:
    mat = assets.create_asset(NAME, PATH, unreal.Material, unreal.MaterialFactoryNew())

    def node(cls, x, y):
        return mel.create_material_expression(mat, cls, x, y)

    # --- Colour: vertex colour * Color parameter ---
    vcol = node(unreal.MaterialExpressionVertexColor, -1400, -300)
    color = node(unreal.MaterialExpressionVectorParameter, -1400, -150)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    tint = node(unreal.MaterialExpressionMultiply, -1100, -250)
    mel.connect_material_expressions(vcol, "", tint, "A")
    mel.connect_material_expressions(color, "", tint, "B")

    # --- Pattern: two cheap ALU gradient noises in world space (no textures) ---
    world = node(unreal.MaterialExpressionWorldPosition, -1700, 150)
    gradient = noise_function("NOISEFUNCTION_GRADIENT_ALU", "NOISEFUNCTION_GRADIENTALU", "NOISEFUNCTION_VALUE_ALU")

    big = node(unreal.MaterialExpressionNoise, -1400, 100)       # soft ~6 m patches
    try_set(big, "scale", 0.0016)
    try_set(big, "levels", 2)
    try_set(big, "quality", 1)
    try_set(big, "output_min", 0.0)
    try_set(big, "output_max", 1.0)
    try_set(big, "turbulence", False)
    if gradient is not None:
        try_set(big, "noise_function", gradient)
    mel.connect_material_expressions(world, "", big, "Position")

    fine = node(unreal.MaterialExpressionNoise, -1400, 350)      # ~40 cm grain / grass tufts
    try_set(fine, "scale", 0.025)
    try_set(fine, "levels", 1)
    try_set(fine, "quality", 1)
    try_set(fine, "output_min", 0.0)
    try_set(fine, "output_max", 1.0)
    try_set(fine, "turbulence", False)
    if gradient is not None:
        try_set(fine, "noise_function", gradient)
    mel.connect_material_expressions(world, "", fine, "Position")

    big_l = node(unreal.MaterialExpressionLinearInterpolate, -1100, 100)
    big_l.set_editor_property("const_a", 0.86)
    big_l.set_editor_property("const_b", 1.10)
    mel.connect_material_expressions(big, "", big_l, "Alpha")
    fine_l = node(unreal.MaterialExpressionLinearInterpolate, -1100, 350)
    fine_l.set_editor_property("const_a", 0.92)
    fine_l.set_editor_property("const_b", 1.06)
    mel.connect_material_expressions(fine, "", fine_l, "Alpha")
    pattern = node(unreal.MaterialExpressionMultiply, -850, 200)
    mel.connect_material_expressions(big_l, "", pattern, "A")
    mel.connect_material_expressions(fine_l, "", pattern, "B")

    base = node(unreal.MaterialExpressionMultiply, -600, -100)
    mel.connect_material_expressions(tint, "", base, "A")
    mel.connect_material_expressions(pattern, "", base, "B")
    mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)

    # --- Glow -> emissive ---
    glow = node(unreal.MaterialExpressionScalarParameter, -600, 150)
    glow.set_editor_property("parameter_name", "Glow")
    glow.set_editor_property("default_value", 0.0)
    emissive = node(unreal.MaterialExpressionMultiply, -350, 100)
    mel.connect_material_expressions(base, "", emissive, "A")
    mel.connect_material_expressions(glow, "", emissive, "B")
    mel.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    rough = node(unreal.MaterialExpressionConstant, -350, 300)
    rough.set_editor_property("r", 0.9)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    spec = node(unreal.MaterialExpressionConstant, -350, 400)
    spec.set_editor_property("r", 0.2)
    mel.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)

    # Used on instanced meshes (rocks, tufts, walls, lamp bulbs); procedural meshes need no usage flag
    try_set(mat, "used_with_instanced_static_meshes", True)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(FULL)
    unreal.log("Created " + FULL)


# ---------------------------------------------------------------------------------------------------------------
# M_GroundV2 (map art pass, Nov 10 2026). Same parameters as M_Ground (Color, Glow, vertex colour), richer surface:
#   BaseColor = VertexColor * Color * macro patches (~16 m) * mid mottling (~2.5 m) * fine grain (~35 cm) * damp blotches
#   Roughness = 0.92 dry -> 0.55 in the damp blotches (they catch the moon and the lamps), specular follows
#   Emissive  = VertexColor * Color * Glow   (no pattern, so glowing rings / bulbs / flames stay clean)
# ASQArena loads M_GroundV2 first and falls back to M_Ground. Safe to re-run (skips if it exists; delete to rebuild).
V2_NAME = "M_GroundV2"
V2_FULL = PATH + "/" + V2_NAME


def build_ground_v2():
    mat = assets.create_asset(V2_NAME, PATH, unreal.Material, unreal.MaterialFactoryNew())

    def node(cls, x, y):
        return mel.create_material_expression(mat, cls, x, y)

    def const(value, x, y):
        c = node(unreal.MaterialExpressionConstant, x, y)
        c.set_editor_property("r", value)
        return c

    def lerp_consts(alpha, a, b, x, y):
        l = node(unreal.MaterialExpressionLinearInterpolate, x, y)
        l.set_editor_property("const_a", a)
        l.set_editor_property("const_b", b)
        mel.connect_material_expressions(alpha, "", l, "Alpha")
        return l

    def mul(a, b, x, y):
        m = node(unreal.MaterialExpressionMultiply, x, y)
        mel.connect_material_expressions(a, "", m, "A")
        mel.connect_material_expressions(b, "", m, "B")
        return m

    gradient = noise_function("NOISEFUNCTION_GRADIENT_ALU", "NOISEFUNCTION_GRADIENTALU", "NOISEFUNCTION_VALUE_ALU")
    world = node(unreal.MaterialExpressionWorldPosition, -2100, 200)

    def noise(scale, levels, lo, hi, x, y):
        n = node(unreal.MaterialExpressionNoise, x, y)
        try_set(n, "scale", scale)
        try_set(n, "levels", levels)
        try_set(n, "quality", 1)
        try_set(n, "output_min", lo)
        try_set(n, "output_max", hi)
        try_set(n, "turbulence", False)
        if gradient is not None:
            try_set(n, "noise_function", gradient)
        mel.connect_material_expressions(world, "", n, "Position")
        return n

    vcol = node(unreal.MaterialExpressionVertexColor, -1500, -500)
    color = node(unreal.MaterialExpressionVectorParameter, -1500, -350)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    tint = mul(vcol, color, -1200, -450)

    macro = lerp_consts(noise(0.0006, 2, 0.0, 1.0, -1800, -100), 0.84, 1.10, -1400, -100)
    mid = lerp_consts(noise(0.004, 2, 0.0, 1.0, -1800, 100), 0.90, 1.06, -1400, 100)
    fine = lerp_consts(noise(0.03, 1, 0.0, 1.0, -1800, 300), 0.94, 1.05, -1400, 300)

    # Damp blotches: stretch a noise past 0..1 and clamp it, which gives patches with a soft but definite edge
    sat_cls = getattr(unreal, "MaterialExpressionSaturate", None) or getattr(unreal, "MaterialExpressionClamp", None)
    if sat_cls is not None:
        raw = noise(0.0022, 3, -1.4, 2.0, -1800, 520)
        damp = node(sat_cls, -1550, 520)
        mel.connect_material_expressions(raw, "", damp, "")
    else:
        damp = noise(0.0022, 3, 0.0, 1.0, -1800, 520)
    damp_dark = lerp_consts(damp, 0.80, 1.0, -1400, 520)

    pattern = mul(mul(macro, mid, -1100, 0), mul(fine, damp_dark, -1100, 400), -850, 200)
    base = mul(tint, pattern, -600, -200)
    mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)

    glow = node(unreal.MaterialExpressionScalarParameter, -900, -600)
    glow.set_editor_property("parameter_name", "Glow")
    glow.set_editor_property("default_value", 0.0)
    emissive = mul(tint, glow, -600, -550)
    mel.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    rough = lerp_consts(damp, 0.55, 0.92, -900, 600)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    spec = lerp_consts(damp, 0.40, 0.15, -900, 750)
    mel.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)

    try_set(mat, "used_with_instanced_static_meshes", True)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(V2_FULL)
    unreal.log("Created " + V2_FULL)


if unreal.EditorAssetLibrary.does_asset_exist(V2_FULL):
    unreal.log_warning(V2_FULL + " already exists - leaving it alone.")
else:
    try:
        build_ground_v2()
    except Exception as err:
        # A half-built material would render black: remove it so the game falls back to M_Ground
        unreal.log_error("[SoulQuota ground] M_GroundV2 failed (%s) - removing it, the game keeps using M_Ground" % err)
        if unreal.EditorAssetLibrary.does_asset_exist(V2_FULL):
            unreal.EditorAssetLibrary.delete_asset(V2_FULL)


# ---------------------------------------------------------------------------------------------------------------
# M_GroundV3 (ground art pass, Nov 15 2026). TERRAIN ONLY (walls / rocks / bulbs keep M_GroundV2): the V2 surface plus
# flat, procedural ground art drawn in world space by one small Custom (HLSL) node. Nothing here is geometry, so it can
# never be mistaken for something to pick up.
#   UV0.x  "stone" weight  -> staggered flagstones / square tiles (StoneW x StoneH cm, StoneStagger 0..1, StoneChecker)
#   UV0.y  "wood" weight   -> long staggered planks (PlankL x PlankW cm)
#   UV1    plaza-local position / plaza radius (|UV1| < 1 inside a plaza) -> a radial mosaic emblem (EmblemSpokes)
#   UV2    world XY in cm (the pattern space; a UV so the Custom node gets plain floats)
#   UV3    lane edging (map quality pass, Nov 19 2026): x = cm outside the lane's edge (capped), y = 1 where a lane runs
#          in the open -> a crisp edging course along every lane: flat edging stones (EdgeStone 1), a dark stringer beam
#          on the plank quays or a dark border tile (EdgeStart / EdgeEnd cm, EdgeTone = brightness of the course)
#   Vertex alpha: 1 = dry, lower = wet (tidal mud): darker and glossy
#   Joints widen with distance and the whole pattern fades out between ~18 m and ~45 m (no moire / shimmer).
# Parameters (set per map by ASQArena): StoneW, StoneH, StoneStagger, StoneChecker, PlankL, PlankW, EmblemSpokes,
# JointDark, plus Color / Glow as before. ASQArena uses V3 for the terrain when it exists and Tuning MapGroundArt > 0.
# Safe to re-run (skips if it exists; delete the asset to rebuild it). Any failure removes the asset -> V2 is used.
V3_NAME = "M_GroundV3"
V3_FULL = PATH + "/" + V3_NAME

V3_CODE = """
float2 gP = WXY;
float fadeJ = 1.0 - saturate((CDist - 1800.0) / 2600.0);
float fadeT = 1.0 - saturate((CDist - 2600.0) / 2400.0);
float jw = 2.5 + CDist * 0.004;

float rowA = floor(gP.y / StH);
float parA = frac(rowA * 0.5) * 2.0;
float ua = gP.x / StW + Stg * 0.5 * parA;
float2 cellA = float2(floor(ua), rowA);
float2 fa = float2(frac(ua), frac(gP.y / StH));
float2 da = min(fa, 1.0 - fa) * float2(StW, StH);
float jointA = 1.0 - smoothstep(jw * 0.5, jw * 1.6, min(da.x, da.y));
float hA = frac(sin(dot(cellA, float2(12.9898, 78.233))) * 43758.5453);
float chkA = frac((cellA.x + cellA.y) * 0.5) * 2.0;
float toneA = 1.0 + (hA - 0.5) * 0.24 + (chkA - 0.5) * Chk;

float rowB = floor(gP.y / PlW);
float hRowB = frac(sin(rowB * 91.7) * 43758.5453);
float ub = gP.x / PlL + hRowB;
float2 cellB = float2(floor(ub), rowB);
float2 fb = float2(frac(ub), frac(gP.y / PlW));
float2 db = min(fb, 1.0 - fb) * float2(PlL, PlW);
float jointB = 1.0 - smoothstep(jw * 0.4, jw * 1.3, min(db.x, db.y));
float hB = frac(sin(dot(cellB, float2(39.346, 11.135))) * 43758.5453);
float toneB = 1.0 + (hB - 0.5) * 0.34;

float er = length(Emb);
float inE = 1.0 - smoothstep(0.985, 1.0, er);
float seg = atan2(Emb.y, Emb.x + 0.00001) * Spk / 6.2831853;
float fseg = frac(seg);
float wedge = floor(frac(seg * 0.5) * 2.0);
float band = step(0.42, er) * step(er, 0.88);
float ringD = min(min(abs(er - 0.42), abs(er - 0.88)), min(abs(er - 0.96), abs(er - 0.16)));
float spokeD = min(fseg, 1.0 - fseg) * (6.2831853 / Spk) * er;
float lineD = min(ringD, lerp(1.0, spokeD, band));
float lineE = (1.0 - smoothstep(0.007, 0.016, lineD)) * inE;
float toneE = 1.0 + (band * (wedge - 0.5) * 0.30 + step(er, 0.16) * 0.14) * inE;

float eb = smoothstep(EdA - 5.0, EdA + 5.0, Edg.x) * (1.0 - smoothstep(EdB - 5.0, EdB + 5.0, Edg.x)) * saturate(Edg.y);
float wA = max(saturate(Msk.x), eb * EdS);
float wB = saturate(Msk.y);
float tone = lerp(1.0, toneA, wA);
tone = lerp(tone, toneB, wB);
tone = lerp(1.0, tone * toneE, fadeT);
float joint = max(max(jointA * wA, jointB * wB) * (1.0 - inE * band * 0.6), lineE) * fadeJ;
tone = tone * lerp(1.0, EdT, eb * fadeT);
return float3(tone * (1.0 - joint * JDk), joint, max(wA, wB));
"""


def build_ground_v3():
    mat = assets.create_asset(V3_NAME, PATH, unreal.Material, unreal.MaterialFactoryNew())

    def node(cls, x, y):
        return mel.create_material_expression(mat, cls, x, y)

    def link(a, a_out, b, b_in):
        if not mel.connect_material_expressions(a, a_out, b, b_in):
            raise RuntimeError("could not connect %s.%s -> %s.%s" % (a.get_name(), a_out, b.get_name(), b_in))

    def lerp_consts(alpha, a, b, x, y):
        l = node(unreal.MaterialExpressionLinearInterpolate, x, y)
        l.set_editor_property("const_a", a)
        l.set_editor_property("const_b", b)
        link(alpha, "", l, "Alpha")
        return l

    def mul(a, b, x, y, a_out="", b_out=""):
        m = node(unreal.MaterialExpressionMultiply, x, y)
        link(a, a_out, m, "A")
        link(b, b_out, m, "B")
        return m

    def scalar(name, default, x, y):
        s = node(unreal.MaterialExpressionScalarParameter, x, y)
        s.set_editor_property("parameter_name", name)
        s.set_editor_property("default_value", default)
        return s

    gradient = noise_function("NOISEFUNCTION_GRADIENT_ALU", "NOISEFUNCTION_GRADIENTALU", "NOISEFUNCTION_VALUE_ALU")
    world = node(unreal.MaterialExpressionWorldPosition, -2500, 200)

    def noise(scale, levels, lo, hi, x, y):
        n = node(unreal.MaterialExpressionNoise, x, y)
        try_set(n, "scale", scale)
        try_set(n, "levels", levels)
        try_set(n, "quality", 1)
        try_set(n, "output_min", lo)
        try_set(n, "output_max", hi)
        try_set(n, "turbulence", False)
        if gradient is not None:
            try_set(n, "noise_function", gradient)
        link(world, "", n, "Position")
        return n

    vcol = node(unreal.MaterialExpressionVertexColor, -1500, -500)
    color = node(unreal.MaterialExpressionVectorParameter, -1500, -350)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    tint = mul(vcol, color, -1200, -450)

    # Painterly breakup: wider swings than V2 on the big shapes, a touch less on the grain
    macro = lerp_consts(noise(0.0005, 2, 0.0, 1.0, -1800, -100), 0.80, 1.14, -1400, -100)
    mid = lerp_consts(noise(0.0035, 2, 0.0, 1.0, -1800, 100), 0.88, 1.08, -1400, 100)
    fine = lerp_consts(noise(0.03, 1, 0.0, 1.0, -1800, 300), 0.95, 1.04, -1400, 300)
    sat_cls = getattr(unreal, "MaterialExpressionSaturate", None)
    if sat_cls is None:
        raise RuntimeError("no Saturate expression")
    raw = noise(0.0022, 3, -1.4, 2.0, -1800, 520)
    damp = node(sat_cls, -1550, 520)
    link(raw, "", damp, "")
    damp_dark = lerp_consts(damp, 0.80, 1.0, -1400, 520)
    pattern = mul(mul(macro, mid, -1100, 0), mul(fine, damp_dark, -1100, 400), -850, 200)

    # --- the ground art node ---
    uv0 = node(unreal.MaterialExpressionTextureCoordinate, -2500, 900)
    uv0.set_editor_property("coordinate_index", 0)
    uv1 = node(unreal.MaterialExpressionTextureCoordinate, -2500, 1050)
    uv1.set_editor_property("coordinate_index", 1)
    uv2 = node(unreal.MaterialExpressionTextureCoordinate, -2500, 1125)   # world XY in cm (plain floats, no LWC types)
    uv2.set_editor_property("coordinate_index", 2)
    cam = node(unreal.MaterialExpressionCameraPositionWS, -2500, 1200)
    dist = node(unreal.MaterialExpressionDistance, -2250, 1200)
    link(world, "", dist, "A")
    link(cam, "", dist, "B")

    art = node(unreal.MaterialExpressionCustom, -1900, 900)
    art.set_editor_property("description", "SQGroundArt")
    art.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    names = ["WXY", "Msk", "Emb", "CDist", "StW", "StH", "Stg", "Chk", "PlL", "PlW", "Spk", "JDk", "Edg", "EdA", "EdB", "EdS", "EdT"]
    inputs = []
    for n in names:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        inputs.append(ci)
    art.set_editor_property("inputs", inputs)
    art.set_editor_property("code", V3_CODE)
    link(uv2, "", art, "WXY")
    link(uv0, "", art, "Msk")
    link(uv1, "", art, "Emb")
    link(dist, "", art, "CDist")
    uv3 = node(unreal.MaterialExpressionTextureCoordinate, -2500, 1275)   # lane edging: (cm outside the lane edge, on/off)
    uv3.set_editor_property("coordinate_index", 3)
    link(uv3, "", art, "Edg")
    params = [("StW", "StoneW", 130.0), ("StH", "StoneH", 85.0), ("Stg", "StoneStagger", 1.0), ("Chk", "StoneChecker", 0.0),
              ("PlL", "PlankL", 420.0), ("PlW", "PlankW", 44.0), ("Spk", "EmblemSpokes", 8.0), ("JDk", "JointDark", 0.42),
              ("EdA", "EdgeStart", 10.0), ("EdB", "EdgeEnd", 60.0), ("EdS", "EdgeStone", 0.0), ("EdT", "EdgeTone", 1.0)]
    for i, (pin, pname, default) in enumerate(params):
        link(scalar(pname, default, -2250, 1350 + i * 90), "", art, pin)

    def mask(src, channel, x, y):
        m = node(unreal.MaterialExpressionComponentMask, x, y)
        for c in ("r", "g", "b", "a"):
            m.set_editor_property(c, c == channel)
        link(src, "", m, "")
        return m

    art_tone = mask(art, "r", -1600, 850)
    art_joint = mask(art, "g", -1600, 950)
    art_paved = mask(art, "b", -1600, 1050)

    # Paved ground loses most of the grassy mottling (stone and wood read as made surfaces)
    one = node(unreal.MaterialExpressionConstant, -1100, 650)
    one.set_editor_property("r", 1.0)
    paved_mix = node(unreal.MaterialExpressionMultiply, -1100, 750)
    link(art_paved, "", paved_mix, "A")
    paved_mix.set_editor_property("const_b", 0.6)
    pattern_soft = node(unreal.MaterialExpressionLinearInterpolate, -850, 650)
    link(pattern, "", pattern_soft, "A")
    link(one, "", pattern_soft, "B")
    link(paved_mix, "", pattern_soft, "Alpha")

    # Wet (tidal mud): vertex alpha below 1 darkens and glosses
    wet = node(unreal.MaterialExpressionOneMinus, -1200, -650)
    if not mel.connect_material_expressions(vcol, "A", wet, ""):
        # no alpha pin by that name on this engine version: no wet look (everything dry), the rest still works
        unreal.log_warning("[SoulQuota ground] vertex alpha pin not found - tidal mud will not look wet")
        dry = node(unreal.MaterialExpressionConstant, -1350, -650)
        dry.set_editor_property("r", 1.0)
        link(dry, "", wet, "")
    wet_dark = lerp_consts(wet, 1.0, 0.72, -950, -650)

    base = mul(mul(tint, pattern_soft, -600, -200), mul(art_tone, wet_dark, -600, 0), -350, -100)
    mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)

    glow = node(unreal.MaterialExpressionScalarParameter, -900, -800)
    glow.set_editor_property("parameter_name", "Glow")
    glow.set_editor_property("default_value", 0.0)
    emissive = mul(tint, glow, -600, -750)
    mel.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    # Roughness: damp blotches as V2, joints fully rough, wet mud glossy
    rough_dry = lerp_consts(damp, 0.55, 0.92, -900, 1250)
    rough_joint = node(unreal.MaterialExpressionLinearInterpolate, -650, 1250)
    link(rough_dry, "", rough_joint, "A")
    rough_joint.set_editor_property("const_b", 0.98)
    link(art_joint, "", rough_joint, "Alpha")
    rough = node(unreal.MaterialExpressionLinearInterpolate, -400, 1250)
    link(rough_joint, "", rough, "A")
    rough.set_editor_property("const_b", 0.22)
    link(wet, "", rough, "Alpha")
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    spec = lerp_consts(damp, 0.40, 0.15, -900, 1450)
    mel.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)

    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(V3_FULL)
    unreal.log("Created " + V3_FULL)


# The material is rebuilt once whenever its recipe changes (V3_REV): a marker in Saved/SQImport records the revision
V3_REV = "r2"
_v3_marker = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "SQImport", "groundv3_%s.done" % V3_REV)
if unreal.EditorAssetLibrary.does_asset_exist(V3_FULL) and not os.path.isfile(_v3_marker):
    unreal.log("[SoulQuota ground] M_GroundV3 recipe changed (%s) - rebuilding it" % V3_REV)
    unreal.EditorAssetLibrary.delete_asset(V3_FULL)
if unreal.EditorAssetLibrary.does_asset_exist(V3_FULL):
    unreal.log_warning(V3_FULL + " already exists - leaving it alone.")
else:
    try:
        build_ground_v3()
        try:
            os.makedirs(os.path.dirname(_v3_marker), exist_ok=True)
            with open(_v3_marker, "w") as _f:
                _f.write("M_GroundV3 %s\n" % V3_REV)
        except Exception as err2:
            unreal.log_warning("[SoulQuota ground] could not write %s: %s" % (_v3_marker, err2))
    except Exception as err:
        unreal.log_error("[SoulQuota ground] M_GroundV3 failed (%s) - removing it, the terrain keeps using M_GroundV2" % err)
        if unreal.EditorAssetLibrary.does_asset_exist(V3_FULL):
            unreal.EditorAssetLibrary.delete_asset(V3_FULL)
