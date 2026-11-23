"""
Soul Quota - style materials for the "Midnight Fairground" art direction (Docs/ArtDirection.md), Nov 12 2026.

  /Game/Materials/M_SQSky        unlit sky-dome gradient (params Horizon / Mid / Zenith colours, Brightness, MidHeight)
  /Game/Materials/M_SQInk        post-process ink outline (depth + normal edges, fades with distance)
                                 params: InkStrength, InkThickness, InkFadeDistance, InkDepthThreshold, InkNormalThreshold, InkColor
  /Game/Art/Materials/M_ArtTintV2  M_ArtTint (Tex * Color, Glow) + a Fresnel rim light (params RimColor, Rim)
  /Game/Materials/M_SQGlow       unlit ADDITIVE glow for souls, halos, shockwaves and telegraph fills
                                 params: Color, Glow, Soft (1 = bright centre fading to the silhouette, 0 = even)

Every one is optional: the C++ looks each up and carries on without it (no sky dome / no outlines / plain M_ArtTint).
A material that fails half-way is deleted so nothing renders black. Safe to re-run (skips what exists; delete an asset
to rebuild it).
"""
import unreal

mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
assets = unreal.AssetToolsHelpers.get_asset_tools()


def log(msg):
    unreal.log("[SoulQuota style] " + msg)


def try_set(obj, prop, value):
    try:
        obj.set_editor_property(prop, value)
        return True
    except Exception as err:  # property / enum names shift between engine versions
        unreal.log_warning("[SoulQuota style] could not set {}: {}".format(prop, err))
        return False


def enum_value(enum_cls, *names):
    for n in names:
        if hasattr(enum_cls, n):
            return getattr(enum_cls, n)
    return None


class Graph:
    """Small helper round MaterialEditingLibrary so the builders below read like formulas."""

    def __init__(self, mat):
        self.mat = mat
        self.x = -2600
        self.y = -800

    def node(self, cls):
        self.y += 110
        if self.y > 900:
            self.y = -800
            self.x += 260
        return mel.create_material_expression(self.mat, cls, self.x, self.y)

    def link(self, src, src_pin, dst, dst_pin):
        if not mel.connect_material_expressions(src, src_pin, dst, dst_pin):
            raise RuntimeError("could not connect %s.%s -> %s.%s" % (src.get_name(), src_pin, dst.get_name(), dst_pin))

    def scalar(self, name, default):
        p = self.node(unreal.MaterialExpressionScalarParameter)
        p.set_editor_property("parameter_name", name)
        p.set_editor_property("default_value", default)
        return p

    def vector(self, name, r, g, b):
        p = self.node(unreal.MaterialExpressionVectorParameter)
        p.set_editor_property("parameter_name", name)
        p.set_editor_property("default_value", unreal.LinearColor(r, g, b, 1.0))
        return p

    def const(self, value):
        c = self.node(unreal.MaterialExpressionConstant)
        c.set_editor_property("r", value)
        return c

    def const2(self, r, g):
        c = self.node(unreal.MaterialExpressionConstant2Vector)
        c.set_editor_property("r", r)
        c.set_editor_property("g", g)
        return c

    def binary(self, cls, a, b, a_pin="", b_pin=""):
        n = self.node(cls)
        self.link(a, a_pin, n, "A")
        self.link(b, b_pin, n, "B")
        return n

    def add(self, a, b, a_pin="", b_pin=""):
        return self.binary(unreal.MaterialExpressionAdd, a, b, a_pin, b_pin)

    def sub(self, a, b, a_pin="", b_pin=""):
        return self.binary(unreal.MaterialExpressionSubtract, a, b, a_pin, b_pin)

    def mul(self, a, b, a_pin="", b_pin=""):
        return self.binary(unreal.MaterialExpressionMultiply, a, b, a_pin, b_pin)

    def div(self, a, b, a_pin="", b_pin=""):
        return self.binary(unreal.MaterialExpressionDivide, a, b, a_pin, b_pin)

    def vmax(self, a, b):
        return self.binary(unreal.MaterialExpressionMax, a, b)

    def unary(self, cls, a, a_pin=""):
        n = self.node(cls)
        self.link(a, a_pin, n, "")
        return n

    def saturate(self, a):
        return self.unary(unreal.MaterialExpressionSaturate, a)

    def abs(self, a):
        return self.unary(unreal.MaterialExpressionAbs, a)

    def mask(self, a, r=False, g=False, b=False, a_pin=""):
        m = self.node(unreal.MaterialExpressionComponentMask)
        m.set_editor_property("r", r)
        m.set_editor_property("g", g)
        m.set_editor_property("b", b)
        m.set_editor_property("a", False)
        self.link(a, a_pin, m, "")
        return m

    def lerp(self, a, b, alpha):
        l = self.node(unreal.MaterialExpressionLinearInterpolate)
        self.link(a, "", l, "A")
        self.link(b, "", l, "B")
        self.link(alpha, "", l, "Alpha")
        return l


def build(full_path, builder):
    """Create one material with `builder(mat, Graph)`; on any error delete the half-built asset."""
    if eal.does_asset_exist(full_path):
        unreal.log_warning("[SoulQuota style] " + full_path + " already exists - leaving it alone.")
        return
    folder, name = full_path.rsplit("/", 1)
    try:
        mat = assets.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
        builder(mat, Graph(mat))
        mel.recompile_material(mat)
        eal.save_asset(full_path)
        log("created " + full_path)
    except Exception as err:
        unreal.log_error("[SoulQuota style] %s failed (%s) - removing it, the game runs without it" % (full_path, err))
        if eal.does_asset_exist(full_path):
            eal.delete_asset(full_path)


# ------------------------------------------------------------------------------------------------ sky dome
def build_sky(mat, g):
    unlit = enum_value(unreal.MaterialShadingModel, "MSM_UNLIT")
    if unlit is None or not try_set(mat, "shading_model", unlit):
        raise RuntimeError("no unlit shading model")
    try_set(mat, "two_sided", True)
    try_set(mat, "used_with_instanced_static_meshes", True)
    try_set(mat, "is_sky", True)  # keeps height fog / aerial perspective off the dome and lets the sky light capture it

    # Height of this pixel on the dome: direction from the dome's centre, z up. 0 at the horizon, 1 overhead.
    world = g.node(unreal.MaterialExpressionWorldPosition)
    centre = g.node(unreal.MaterialExpressionObjectPositionWS)
    direction = g.unary(unreal.MaterialExpressionNormalize, g.sub(world, centre))
    height = g.saturate(g.mask(direction, b=True))

    horizon = g.vector("Horizon", 0.42, 0.10, 0.36)
    mid = g.vector("Mid", 0.13, 0.06, 0.32)
    zenith = g.vector("Zenith", 0.02, 0.02, 0.10)
    mid_height = g.scalar("MidHeight", 0.22)
    low = g.lerp(horizon, mid, g.saturate(g.div(height, mid_height)))
    one = g.const(1.0)
    upper = g.saturate(g.div(g.sub(height, mid_height), g.sub(one, mid_height)))
    colour = g.lerp(low, zenith, upper)
    out = g.mul(colour, g.scalar("Brightness", 1.0))
    if not mel.connect_material_property(out, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError("emissive not connected")


# ------------------------------------------------------------------------------------------------ ink outline
def build_ink(mat, g):
    domain = enum_value(unreal.MaterialDomain, "MD_POST_PROCESS")
    if domain is None or not try_set(mat, "material_domain", domain):
        raise RuntimeError("no post-process domain")
    # Before DOF / translucency: the lines get anti-aliased with the scene and effects (sparks, glows) draw over them
    where = enum_value(unreal.BlendableLocation, "BL_SCENE_COLOR_BEFORE_DOF", "BL_BEFORE_TRANSLUCENCY", "BL_SCENE_COLOR_BEFORE_BLOOM",
                       "BL_BEFORE_TONEMAPPING")
    if where is not None:
        try_set(mat, "blendable_location", where)

    def scene_texture(*ids):
        tex_id = enum_value(unreal.SceneTextureId, *ids)
        if tex_id is None:
            raise RuntimeError("scene texture id missing: %s" % (ids,))
        n = g.node(unreal.MaterialExpressionSceneTexture)
        n.set_editor_property("scene_texture_id", tex_id)
        return n

    def sample(tex_node, uv):
        # uv is either the ScreenPosition node (first output = ViewportUV) or an Add of it and an offset
        if not (mel.connect_material_expressions(uv, "", tex_node, "UVs")
                or mel.connect_material_expressions(uv, "", tex_node, "Coordinates")):
            raise RuntimeError("scene texture UV pin not found")
        return tex_node

    # Every lookup (centre and neighbours) uses the same viewport UV, so they always line up whatever the screen percentage
    uv0 = g.node(unreal.MaterialExpressionScreenPosition)
    depth_c_node = sample(scene_texture("PPI_SCENE_DEPTH"), uv0)
    depth_c = g.mask(depth_c_node, r=True, a_pin="Color")
    normal_c = g.mask(sample(scene_texture("PPI_WORLD_NORMAL"), uv0), r=True, g=True, b=True, a_pin="Color")
    colour = g.mask(sample(scene_texture("PPI_POST_PROCESS_INPUT0"), uv0), r=True, g=True, b=True, a_pin="Color")

    thickness = g.scalar("InkThickness", 1.5)
    # One pixel in UV units * thickness
    step = g.mul(depth_c_node, thickness, a_pin="InvSize")

    depth_sum = None
    normal_sum = None
    for dx, dy in ((1.0, 0.0), (-1.0, 0.0), (0.0, 1.0), (0.0, -1.0)):
        uv = g.add(uv0, g.mul(g.const2(dx, dy), step))
        d = g.mask(sample(scene_texture("PPI_SCENE_DEPTH"), uv), r=True, a_pin="Color")
        n = g.mask(sample(scene_texture("PPI_WORLD_NORMAL"), uv), r=True, g=True, b=True, a_pin="Color")
        depth_sum = d if depth_sum is None else g.add(depth_sum, d)
        normal_sum = n if normal_sum is None else g.add(normal_sum, n)

    four = g.const(4.0)
    one = g.const(1.0)
    # Depth edge: how far the neighbours' depth is from this pixel's, relative to its own depth
    depth_lap = g.abs(g.sub(depth_sum, g.mul(depth_c, four)))
    depth_rel = g.div(depth_lap, g.vmax(depth_c, one))
    depth_edge = g.saturate(g.mul(g.sub(depth_rel, g.scalar("InkDepthThreshold", 0.035)), g.const(40.0)))
    # Normal edge: creases and silhouettes where the surface direction changes
    normal_diff = g.sub(normal_sum, g.mul(normal_c, four))
    normal_len = g.unary(unreal.MaterialExpressionSquareRoot, g.binary(unreal.MaterialExpressionDotProduct, normal_diff, normal_diff))
    normal_edge = g.saturate(g.mul(g.sub(normal_len, g.scalar("InkNormalThreshold", 0.9)), g.const(2.5)))
    edge = g.vmax(depth_edge, normal_edge)

    # Fade with distance: near things get a crisp line, the far horde and the sky get none (no black noise)
    fade = g.saturate(g.sub(one, g.div(depth_c, g.scalar("InkFadeDistance", 6500.0))))
    amount = g.saturate(g.mul(g.mul(edge, fade), g.scalar("InkStrength", 0.8)))

    # Ink multiplies the scene colour (works at any exposure): a deep plum line, never pure black
    ink = g.mul(colour, g.vector("InkColor", 0.10, 0.04, 0.16))
    out = g.lerp(colour, ink, amount)
    if not mel.connect_material_property(out, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError("emissive not connected")


# ------------------------------------------------------------------------------------------------ art tint + rim light
def build_art_tint_v2(mat, g):
    tex = g.node(unreal.MaterialExpressionTextureSampleParameter2D)
    tex.set_editor_property("parameter_name", "Tex")
    default_tex = None
    for candidate in ("/Game/Art/Textures/T_Skeleton", "/Game/Art/Textures/T_HalloweenBits"):
        if eal.does_asset_exist(candidate):
            default_tex = eal.load_asset(candidate)
            break
    if default_tex:
        tex.set_editor_property("texture", default_tex)
    colour = g.vector("Color", 1.0, 1.0, 1.0)
    base = g.mul(tex, colour, a_pin="RGB")
    if not mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR):
        raise RuntimeError("base colour not connected")

    glow = g.mul(base, g.scalar("Glow", 0.0))
    # Rim light: brightest where the surface turns away from the camera, so bodies pop from the dark ground
    fresnel = g.node(unreal.MaterialExpressionFresnel)
    try_set(fresnel, "exponent", 3.0)
    try_set(fresnel, "base_reflect_fraction", 0.0)
    rim = g.mul(g.mul(fresnel, g.vector("RimColor", 0.45, 0.75, 1.0)), g.scalar("Rim", 0.0))
    if not mel.connect_material_property(g.add(glow, rim), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError("emissive not connected")

    rough = g.const(0.85)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    for flag in ("used_with_skeletal_mesh", "used_with_instanced_static_meshes"):
        try_set(mat, flag, True)


# ------------------------------------------------------------------------------------------------ additive glow
def build_glow(mat, g, per_instance=False):
    """Light, not a solid: adds Color * Glow to whatever is behind it. On a sphere with Soft = 1 the middle is bright and
    the edge fades to nothing, so it reads as a soft orb / wisp instead of a hard ball."""
    unlit = enum_value(unreal.MaterialShadingModel, "MSM_UNLIT")
    if unlit is None or not try_set(mat, "shading_model", unlit):
        raise RuntimeError("no unlit shading model")
    additive = enum_value(unreal.BlendMode, "BLEND_ADDITIVE")
    if additive is None or not try_set(mat, "blend_mode", additive):
        raise RuntimeError("no additive blend mode")
    try_set(mat, "two_sided", False)
    try_set(mat, "used_with_instanced_static_meshes", True)

    colour = g.vector("Color", 0.2, 1.0, 0.85)
    glow = g.scalar("Glow", 1.0)
    soft = g.scalar("Soft", 1.0)
    # Fresnel is 0 where the surface faces the camera and 1 at the silhouette: 1 - fresnel = a soft-edged blob
    fresnel = g.node(unreal.MaterialExpressionFresnel)
    try_set(fresnel, "exponent", 1.6)
    try_set(fresnel, "base_reflect_fraction", 0.0)
    one = g.const(1.0)
    facing = g.saturate(g.sub(one, fresnel))
    falloff = g.mul(facing, facing)
    amount = g.lerp(one, falloff, soft)
    out = g.mul(g.mul(colour, glow), amount)
    if per_instance:
        # Nov 19 2026: each instance carries one custom float (0..1+) = its brightness, so a spark can fade out instead of only shrinking.
        # ASQSparks writes it every frame (SetCustomDataValue index 0); 1 when nothing is written.
        cls = getattr(unreal, "MaterialExpressionPerInstanceCustomData", None)
        if cls is None:
            raise RuntimeError("no per-instance custom data node in this engine build")
        fade = g.node(cls)
        if not try_set(fade, "data_index", 0):
            raise RuntimeError("per-instance data index not settable")
        try_set(fade, "const_default_value", 1.0)
        out = g.mul(out, fade)
    if not mel.connect_material_property(out, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError("emissive not connected")


def build_glow2(mat, g):
    """M_SQGlow with a per-instance fade (the effects kit, Nov 19 2026). A new asset name, because build() never touches a material that exists."""
    build_glow(mat, g, per_instance=True)


build("/Game/Materials/M_SQGlow", build_glow)
build("/Game/Materials/M_SQGlow2", build_glow2)
build("/Game/Materials/M_SQSky", build_sky)
build("/Game/Materials/M_SQInk", build_ink)
build("/Game/Art/Materials/M_ArtTintV2", build_art_tint_v2)
