# Soul Quota arena preview: builds the generated arena (sq_arena_gen.py port of SQArena.cpp) in Blender and renders.
# usage: python3 sq_arena_render.py <Biome> <outdir> <shot>... [env: SQ_SCALE=0.5 SQ_SAMPLES=16 SQ_SAVE=1]
# shots: overview | gamecam | gamecam90
import os, sys, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import bpy, bmesh
from mathutils import Vector, Matrix, Quaternion, Euler
from sq_arena_gen import Arena, hue, soft_tint

BIOME, OUTDIR = sys.argv[1], sys.argv[2]
SHOTS = sys.argv[3:] or ["overview"]
SCALE = float(os.environ.get("SQ_SCALE", "1"))
SAMPLES = int(os.environ.get("SQ_SAMPLES", "48"))
PROPDIR = "/mnt/user-data/outputs/ArtSource/Props/"
TEXPATH = PROPDIR + "halloweenbits_texture.png"
REAPER = "/home/claude/sq/ArtSource/Characters/Reaper_Custom.blend"

A = Arena(BIOME)
Pal = A.Pal
print(A.summary())

bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene


def U(x, y, z=0.0):
    """UE cm (left-handed) -> Blender m (right-handed): mirror Y."""
    return Vector((x / 100.0, -y / 100.0, z / 100.0))


def yawq(ue_yaw_deg):
    return Quaternion((0, 0, 1), -math.radians(ue_yaw_deg))


def coll(name, hidden=False):
    c = bpy.data.collections.new(name)
    if not hidden:
        sc.collection.children.link(c)
    return c


C_TERRAIN, C_PROPS, C_DRESS, C_FX, C_CHAR = (coll(n) for n in ("Terrain", "Props", "Dressing", "FX", "Characters"))
C_LIB = coll("Lib", hidden=True)


# ---------------------------------------------------------------- materials
TEX = bpy.data.images.load(TEXPATH)


def principled(nt):
    return nt.nodes.get("Principled BSDF") or nt.nodes.new("ShaderNodeBsdfPrincipled")


def mat_flat(name, color, glow=0.0, rough=0.9):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    p = principled(m.node_tree)
    c = (float(color[0]), float(color[1]), float(color[2]), 1)
    p.inputs["Base Color"].default_value = c
    p.inputs["Roughness"].default_value = rough
    p.inputs["Specular IOR Level"].default_value = 0.25
    if glow > 0:
        p.inputs["Emission Color"].default_value = c
        p.inputs["Emission Strength"].default_value = glow
    return m


_texmats = {}


def mat_tex(name, tint, glow=0.0):
    key = (name, tuple(np.round(tint, 4)), glow)
    if key in _texmats:
        return _texmats[key]
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    p = principled(nt)
    t = nt.nodes.new("ShaderNodeTexImage"); t.image = TEX; t.interpolation = 'Closest'
    mix = nt.nodes.new("ShaderNodeMix"); mix.data_type = 'RGBA'; mix.blend_type = 'MULTIPLY'
    mix.inputs[0].default_value = 1.0
    mix.inputs[7].default_value = (float(tint[0]), float(tint[1]), float(tint[2]), 1)
    nt.links.new(t.outputs["Color"], mix.inputs[6])
    nt.links.new(mix.outputs[2], p.inputs["Base Color"])
    p.inputs["Roughness"].default_value = 0.85
    p.inputs["Specular IOR Level"].default_value = 0.25
    if glow > 0:
        nt.links.new(mix.outputs[2], p.inputs["Emission Color"])
        p.inputs["Emission Strength"].default_value = glow
    _texmats[key] = m
    return m


def mat_terrain():
    m = bpy.data.materials.new("M_Ground_Terrain")
    m.use_nodes = True
    nt = m.node_tree
    p = principled(nt)
    ca = nt.nodes.new("ShaderNodeVertexColor"); ca.layer_name = "Col"
    # M_Ground: vertex colour * subtle world-space noise (big soft patches + fine grain)
    tc = nt.nodes.new("ShaderNodeTexCoord")
    nb = nt.nodes.new("ShaderNodeTexNoise"); nb.inputs["Scale"].default_value = 0.16; nb.inputs["Detail"].default_value = 1
    nf = nt.nodes.new("ShaderNodeTexNoise"); nf.inputs["Scale"].default_value = 2.5; nf.inputs["Detail"].default_value = 2
    nt.links.new(tc.outputs["Object"], nb.inputs["Vector"]); nt.links.new(tc.outputs["Object"], nf.inputs["Vector"])
    mb = nt.nodes.new("ShaderNodeMapRange"); mb.inputs[3].default_value = 0.88; mb.inputs[4].default_value = 1.12
    mf = nt.nodes.new("ShaderNodeMapRange"); mf.inputs[3].default_value = 0.93; mf.inputs[4].default_value = 1.07
    nt.links.new(nb.outputs["Fac"], mb.inputs[0]); nt.links.new(nf.outputs["Fac"], mf.inputs[0])
    mul = nt.nodes.new("ShaderNodeMath"); mul.operation = 'MULTIPLY'
    nt.links.new(mb.outputs[0], mul.inputs[0]); nt.links.new(mf.outputs[0], mul.inputs[1])
    mix = nt.nodes.new("ShaderNodeMix"); mix.data_type = 'RGBA'; mix.blend_type = 'MULTIPLY'; mix.inputs[0].default_value = 1
    nt.links.new(ca.outputs["Color"], mix.inputs[6])
    comb = nt.nodes.new("ShaderNodeCombineColor")
    for i in range(3):
        nt.links.new(mul.outputs[0], comb.inputs[i])
    nt.links.new(comb.outputs[0], mix.inputs[7])
    nt.links.new(mix.outputs[2], p.inputs["Base Color"])
    p.inputs["Roughness"].default_value = 0.9
    p.inputs["Specular IOR Level"].default_value = 0.2
    return m


# ---------------------------------------------------------------- terrain mesh
def build_terrain():
    N = A.GridN; R = N + 1
    verts = [tuple(U(A.VX[iy, ix], A.VY[iy, ix], A.H[iy, ix])) for iy in range(R) for ix in range(R)]
    faces = []
    for iy in range(N):
        for ix in range(N):
            v00 = iy * R + ix; v10 = v00 + 1; v01 = v00 + R; v11 = v01 + 1
            faces.append((v00, v11, v10)); faces.append((v00, v01, v11))
    me = bpy.data.meshes.new("Terrain")
    me.from_pydata(verts, [], faces)
    col = me.color_attributes.new("Col", 'FLOAT_COLOR', 'POINT')
    cols = np.concatenate([A.Colors.reshape(-1, 3), np.ones((R * R, 1))], 1).astype(np.float32)
    col.data.foreach_set("color", cols.ravel())
    me.shade_smooth()
    me.materials.append(mat_terrain())
    ob = bpy.data.objects.new("Terrain", me)
    C_TERRAIN.objects.link(ob)
    return ob


# ---------------------------------------------------------------- prop library
LIB = {}


def lib(name):
    if name in LIB:
        return LIB[name]
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=PROPDIR + name + ".fbx")
    new = [o for o in bpy.data.objects if o not in before]
    meshes = [o for o in new if o.type == 'MESH']
    # bake every part's world transform into one mesh
    bm = bmesh.new()
    for o in meshes:
        tmp = o.data.copy()
        tmp.transform(o.matrix_world)
        bm.from_mesh(tmp)
        bpy.data.meshes.remove(tmp)
    me = bpy.data.meshes.new("LIB_" + name)
    bm.to_mesh(me); bm.free()
    for o in new:
        bpy.data.objects.remove(o, do_unlink=True)
    co = np.array([v.co for v in me.vertices])
    mn, mx = co.min(0), co.max(0)
    LIB[name] = dict(mesh=me, mn=Vector(mn), mx=Vector(mx), h=float(mx[2] - mn[2]))
    return LIB[name]


def sit(name, base, rotq, scale3, mat, coll_=None, objname=None):
    """ArenaSitTransform: the mesh's bounds bottom-centre lands on base."""
    L = lib(name)
    lb = Vector(((L["mn"].x + L["mx"].x) / 2, (L["mn"].y + L["mx"].y) / 2, L["mn"].z))
    s = Vector(scale3)
    loc = base - rotq @ Vector((lb.x * s.x, lb.y * s.y, lb.z * s.z))
    ob = bpy.data.objects.new(objname or name, L["mesh"])
    ob.data.materials.clear() if False else None
    ob.material_slots  # noqa
    ob.matrix_world = Matrix.Translation(loc) @ rotq.to_matrix().to_4x4() @ Matrix.Diagonal((s.x, s.y, s.z, 1))
    if mat is not None:
        if len(ob.data.materials) == 0:
            ob.data.materials.append(None)
        ob.material_slots[0].link = 'OBJECT'
        ob.material_slots[0].material = mat
    (coll_ or C_PROPS).objects.link(ob)
    return ob


PROPSCALE = 1.10 / lib("gravestone")["h"] * A.tune("PropScale", 1.6)   # ComputeArtScale * PropScale
print("PropScale (blender units / mesh unit):", PROPSCALE)


# ---------------------------------------------------------------- scene build
terrain = build_terrain()

prop_mat = mat_tex("M_Prop", soft_tint(Pal["Prop"], 0.3))
for p in A.Props:
    s = PROPSCALE * p["s"]
    sit(p["mesh"], U(p["x"], p["y"], p["z"]), yawq(p["yaw"]), (s, s, s), prop_mat)
for d in A.Decor:
    n = d["normal"]
    nb = Vector((n[0], -n[1], n[2]))
    q = Vector((0, 0, 1)).rotation_difference(nb) @ yawq(d["yaw"])
    s = PROPSCALE * A.tune("DecorScale", 1.25) * d["s"]
    sit(d["mesh"], U(d["x"], d["y"], d["z"]), q, (s, s, s), prop_mat, C_DRESS)
rim_mat = mat_tex("M_Rim", soft_tint(Pal["Cliff"], 0.5))
for r in A.RimArt:
    s = PROPSCALE * r["s"]
    sit(r["mesh"], U(r["x"], r["y"], r["z"]), yawq(r["yaw"]), (s, s, s), rim_mat, C_DRESS)


def cube_mesh(name, items, mat):
    """items: list of (center Vector, rot Matrix3, size Vector) -> one merged mesh."""
    bm = bmesh.new()
    for c, rot, size in items:
        geom = bmesh.ops.create_cube(bm, size=1.0)
        vs = geom["verts"]
        M = Matrix.Translation(c) @ rot.to_4x4() @ Matrix.Diagonal((size.x, size.y, size.z, 1))
        bmesh.ops.transform(bm, matrix=M, verts=vs)
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    me.materials.append(mat)
    ob = bpy.data.objects.new(name, me); C_DRESS.objects.link(ob)
    return ob


def ue_rot(pitch, yaw, roll):
    """Approximate UE FRotator -> Blender rotation matrix (mirror-converted)."""
    return (Euler((math.radians(roll), math.radians(pitch), -math.radians(yaw)), 'XYZ')).to_matrix()


# rocks (UE cube = 100 cm)
for shade, mul in ((0, 1.15), (1, 0.8)):
    items = [(U(r["x"], r["y"], r["z"]), ue_rot(*r["rot"]), Vector(r["s"])) for r in A.Rocks if r["shade"] == shade]
    if items:
        cube_mesh(f"Rocks{shade}", items, mat_flat(f"M_Rock{shade}", Pal["Cliff"] * mul))

# grass tufts: three leaning cones each (UE cone 100 cm wide, 100 tall)
for shade, colr in ((0, (Pal["Grass"] + (Pal["Top"] - Pal["Grass"]) * 0.5) * 1.25), (1, Pal["GrassAlt"] * 1.15)):
    bm = bmesh.new()
    for t in A.Tufts:
        if t["shade"] != shade:
            continue
        for b in t["blades"]:
            g = bmesh.ops.create_cone(bm, cap_ends=False, segments=5, radius1=0.5, radius2=0.0, depth=1.0)
            lean = Matrix.Rotation(math.radians(-b["pitch"]), 4, 'Y')   # tilt the blade outwards
            M = (Matrix.Translation(U(b["x"], b["y"], b["z"])) @ yawq(b["yaw"]).to_matrix().to_4x4() @ lean
                 @ Matrix.Diagonal((b["sxy"], b["sxy"], b["sz"], 1)) @ Matrix.Translation((0, 0, 0.5)))
            bmesh.ops.transform(bm, matrix=M, verts=g["verts"])
    me = bpy.data.meshes.new(f"Tufts{shade}"); bm.to_mesh(me); bm.free()
    me.materials.append(mat_flat(f"M_Tuft{shade}", colr))
    C_DRESS.objects.link(bpy.data.objects.new(f"Tufts{shade}", me))

# walls, columns, skirt
I3 = Matrix.Identity(3)
cube_mesh("Walls", [(U(*w["c"]), I3, Vector(w["size"]) / 100) for w in A.WallBlocks], mat_flat("M_Wall", Pal["Wall"]))
cube_mesh("Columns", [(U(*c["c"]), I3, Vector(c["size"]) / 100) for c in A.Columns], mat_flat("M_Column", Pal["Wall"] * 1.3))
cube_mesh("Skirt", [(U(*s["c"]), I3, Vector(s["size"]) / 100) for s in A.Skirts], mat_flat("M_Skirt", Pal["Grass"] * 0.45 + Pal["Fog"] * 0.5))
fence_mat = mat_tex("M_Fence", soft_tint(Pal["Wall"], 0.35))
F = lib("fence")
fence_len = (F["mx"].x - F["mn"].x) * PROPSCALE    # long axis is local X
for seg in A.WallSegs:
    a = U(*seg["a"]); b = U(*seg["b"])
    run = (b - a); L = run.length
    pieces = max(1, int(math.floor(L / fence_len + 0.5)))
    plen = L / pieces
    yaw = math.atan2(run.y, run.x)
    for k in range(pieces):
        c = a + run.normalized() * (plen * (k + 0.5))
        c.z = seg["top"] / 100 - 0.02
        sit("fence", c, Quaternion((0, 0, 1), yaw), (PROPSCALE * plen / fence_len, PROPSCALE, PROPSCALE), fence_mat, C_DRESS)
for c in A.Columns:
    sit("fence_pillar", U(c["c"][0], c["c"][1], c["top"] - 2), Quaternion(), (PROPSCALE * 1.15,) * 3, fence_mat, C_DRESS)

# backdrop silhouettes
back_mat = mat_flat("M_Backdrop", Pal["Backdrop"])
for b in A.Backdrop:
    s = PROPSCALE * b["s"]
    sit(b["mesh"], U(b["x"], b["y"], b["z"]), yawq(b["yaw"]), (s, s, s), back_mat, C_DRESS)

# ---------------------------------------------------------------- lights
def point(name, loc, color, watts, radius=0.1, cutoff=None, shadow=False):
    ld = bpy.data.lights.new(name, 'POINT')
    ld.color = tuple(float(c) for c in color)
    ld.energy = watts
    ld.shadow_soft_size = radius
    ld.use_shadow = shadow
    if cutoff:
        ld.use_custom_distance = True
        ld.cutoff_distance = cutoff
    ob = bpy.data.objects.new(name, ld); ob.location = loc
    C_FX.objects.link(ob)
    return ob


LAMP_W = float(os.environ.get("SQ_LAMPW", "900"))
PAD_W = float(os.environ.get("SQ_PADW", "400"))
LANTERN_W = float(os.environ.get("SQ_LANTERNW", "15000"))

# jump pads: rim + glowing disc + chevron + cyan light
rim_m = mat_flat("M_PadRim", (0.04, 0.2, 0.4))
disc_m = mat_flat("M_PadDisc", (0.4, 1, 1), glow=1.2 * 4)
chev_m = mat_flat("M_PadChevron", (0.9, 1, 1), glow=2.0 * 4)
for i, p in enumerate(A.Pads):
    pos = U(*p["pos"]); n = Vector((p["normal"][0], -p["normal"][1], p["normal"][2]))
    q = Vector((0, 0, 1)).rotation_difference(n)
    for nm, r, off, m in (("PadRim", 1.7, 0.03, rim_m), ("PadDisc", 1.25, 0.06, disc_m)):
        bpy.ops.mesh.primitive_cylinder_add(vertices=40, radius=r, depth=0.06, location=pos + n * off)
        ob = bpy.context.active_object; ob.name = f"{nm}{i}"; ob.rotation_mode = 'QUATERNION'; ob.rotation_quaternion = q
        ob.data.materials.append(m)
        for c in ob.users_collection: c.objects.unlink(ob)
        C_FX.objects.link(ob)
    t = U(*p["target"]); d = Vector((t.x - pos.x, t.y - pos.y, 0)).normalized()
    tip = pos + d * 1.0 + Vector((0, 0, 0.18))
    dyaw = math.atan2(d.y, d.x)
    items = []
    for arm in (1, -1):
        ay = dyaw + math.radians(140) * arm
        ad = Vector((math.cos(ay), math.sin(ay), 0))
        items.append((tip + ad * 0.45, Matrix.Rotation(ay, 3, 'Z'), Vector((0.9, 0.22, 0.05))))
    cube_mesh(f"Chevron{i}", items, chev_m)
    point(f"PadLight{i}", pos + Vector((0, 0, 0.9)), (0.3, 0.9, 1), PAD_W, 0.3, 7.0)

# lamps: post_lantern sat at the spot facing the centre, glowing bulb at 0.88 * 270 cm, light on the first N
lamp_name = next((n for n in Pal["LampProps"] if os.path.exists(PROPDIR + n + ".fbx")), None)
lamp_scale = 2.70 / lib(lamp_name)["h"]
lamp_mat = mat_tex("M_Lamp", np.ones(3), glow=0.25)
bulb_m = mat_flat("M_Bulb", hue(Pal["Lamp"]), glow=5 * 3)
max_lights = int(A.tune("MaxArenaLights", 11))
lamp_lights = max(0, max_lights - len(A.Pads))
for i, l in enumerate(A.Lamps):
    yaw = math.degrees(math.atan2(-l[1], -l[0]))
    sit(lamp_name, U(l[0], l[1], l[2] - 4), yawq(yaw), (lamp_scale,) * 3, lamp_mat, C_PROPS)
    bulb = U(l[0], l[1], l[2] + 270 * 0.88)
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.16, location=bulb, segments=16, ring_count=8)
    ob = bpy.context.active_object; ob.data.materials.append(bulb_m)
    for c in ob.users_collection: c.objects.unlink(ob)
    C_FX.objects.link(ob)
    if i < lamp_lights:
        point(f"LampLight{i}", bulb + Vector((0, 0, 0.3)), hue(Pal["Lamp"]), LAMP_W, 0.2, 10.0)

# central lantern (SQLantern): lantern_standing fit to 340 cm, flame, banking ring + 8 candles at r=350
lz = A.ground_z(0, 0)
lant_s = 3.40 / lib("lantern_standing")["h"]
sit("lantern_standing", U(0, 0, lz), Quaternion(), (lant_s,) * 3, mat_tex("M_Lantern", np.ones(3), glow=0.4), C_PROPS, "Lantern")
bpy.ops.mesh.primitive_uv_sphere_add(radius=0.15, location=U(0, 0, lz + 272), segments=16, ring_count=8)
fl = bpy.context.active_object; fl.data.materials.append(mat_flat("M_Flame", (1, 0.7, 0.3), glow=6 * 3))
bpy.ops.mesh.primitive_cylinder_add(vertices=64, radius=3.5, depth=0.01, location=U(0, 0, lz + 2))
ring = bpy.context.active_object; ring.data.materials.append(mat_flat("M_Ring", (0.75, 0.52, 0.16), glow=0.6))
for o in (fl, ring):
    for c in o.users_collection: c.objects.unlink(o)
    C_FX.objects.link(o)
cand_s = 0.60 / lib("candle_triple")["h"]
for i in range(8):
    a = i * 2 * math.pi / 8
    sit("candle_triple", U(math.cos(a) * 350, math.sin(a) * 350, lz), yawq(math.degrees(a)), (cand_s,) * 3,
        mat_tex("M_Candle", np.ones(3), glow=0.6), C_PROPS)
point("LanternLight", U(0, 0, lz + 300), (1, 0.75, 0.35), LANTERN_W, 0.3, 18.0, shadow=True)

# moon (directional light from build_graveyard_map.py: pitch -38, yaw 35) + moon disc
cp, sp = math.cos(math.radians(-38)), math.sin(math.radians(-38))
fwd_ue = Vector((cp * math.cos(math.radians(35)), cp * math.sin(math.radians(35)), sp))
travel = Vector((fwd_ue.x, -fwd_ue.y, fwd_ue.z))
MOON_E = float(os.environ.get("SQ_MOON", "3.2"))
md = bpy.data.lights.new("Moon", 'SUN'); md.energy = MOON_E; md.color = tuple(hue(Pal["Moon"])); md.angle = math.radians(3)
mo = bpy.data.objects.new("Moon", md); mo.rotation_mode = 'QUATERNION'
mo.rotation_quaternion = travel.to_track_quat('-Z', 'Y'); C_FX.objects.link(mo)
to_moon = -travel
flat = Vector((to_moon.x, to_moon.y, 0)).normalized()
elev = min(max(math.asin(to_moon.z), math.radians(14)), math.radians(30))
moon_pos = (flat * math.cos(elev) + Vector((0, 0, 1)) * math.sin(elev)) * (A.tune("MoonDistance", 45000) / 100)
bpy.ops.mesh.primitive_uv_sphere_add(radius=A.tune("MoonSize", 2600) / 100, location=moon_pos, segments=32, ring_count=16)
mdisc = bpy.context.active_object
mcol = np.array([0.9, 0.92, 1.0]) + (hue(Pal["Moon"]) - np.array([0.9, 0.92, 1.0])) * 0.35
mdisc.data.materials.append(mat_flat("M_MoonDisc", mcol, glow=A.tune("MoonGlow", 4) * 2))
mdisc.visible_shadow = False
for c in mdisc.users_collection: c.objects.unlink(mdisc)
C_FX.objects.link(mdisc)

# world: sky-tinted ambient (the sky light) + fog-coloured background and light volumetric fog
w = bpy.data.worlds.new("W"); sc.world = w; w.use_nodes = True
bg = w.node_tree.nodes["Background"]
sky = np.array([1, 1, 1]) + (hue(Pal["Sky"]) - 1) * 0.6
AMB = float(os.environ.get("SQ_AMB", "0.22"))
lp = w.node_tree.nodes.new("ShaderNodeLightPath")
mixw = w.node_tree.nodes.new("ShaderNodeMixShader")
bg2 = w.node_tree.nodes.new("ShaderNodeBackground")
bg.inputs[0].default_value = (*sky, 1); bg.inputs[1].default_value = AMB      # lighting
fogc = Pal["Fog"] * A.tune("FogBrightness", 1.6)
bg2.inputs[0].default_value = (*fogc, 1); bg2.inputs[1].default_value = 1.0   # what the camera sees
out = w.node_tree.nodes["World Output"]
w.node_tree.links.new(lp.outputs["Is Camera Ray"], mixw.inputs[0])
w.node_tree.links.new(bg.outputs[0], mixw.inputs[1]); w.node_tree.links.new(bg2.outputs[0], mixw.inputs[2])
w.node_tree.links.new(mixw.outputs[0], out.inputs["Surface"])
FOG = float(os.environ.get("SQ_FOG", "0.005"))
if FOG > 0:
    # Height fog stand-in: a big low volume box (a world volume in EEVEE Next would also swallow the moonlight)
    bpy.ops.mesh.primitive_cube_add(size=1, location=(0, 0, 10))
    fb = bpy.context.active_object; fb.name = "Fog"; fb.scale = (330, 330, 34)
    fm = bpy.data.materials.new("M_Fog"); fm.use_nodes = True
    fnt = fm.node_tree
    fnt.nodes.remove(principled(fnt))
    vol = fnt.nodes.new("ShaderNodeVolumePrincipled")
    vol.inputs["Color"].default_value = (*(fogc / max(fogc.max(), 1e-3)), 1)
    vol.inputs["Density"].default_value = FOG
    vol.inputs["Emission Strength"].default_value = 0.0
    fnt.links.new(vol.outputs[0], fnt.nodes["Material Output"].inputs["Volume"])
    fb.data.materials.append(fm)
    for c in fb.users_collection: c.objects.unlink(fb)
    C_FX.objects.link(fb)


# ---------------------------------------------------------------- reapers
def add_reapers(spots):
    with bpy.data.libraries.load(REAPER) as (src, dst):
        dst.objects = ["Rig", "Reaper_Body"]
    rig0, body0 = dst.objects
    # measure the rest height once
    C_CHAR.objects.link(rig0); C_CHAR.objects.link(body0)
    rig0.animation_data_create(); rig0.animation_data.action = None
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    be = body0.evaluated_get(dg)
    zs = [(be.matrix_world @ v.co).z for v in be.data.vertices]
    h0 = max(zs) - min(zs)
    k = 1.9 / h0 * rig0.scale.x
    print("reaper rest height", h0, "-> scale", k)
    tints = [(0.35, 0.85, 0.8), (0.95, 0.35, 0.3), (0.7, 0.5, 1.0), (1.0, 0.82, 0.3)]
    mask = bpy.data.images.load("/home/claude/sq/ArtSource/Characters/reaper_palette_tintmask.png")
    rigs = []
    for i, (pos, yaw, action, frame) in enumerate(spots):
        rig = rig0.copy(); body = body0.copy(); body.data = body0.data.copy()
        C_CHAR.objects.link(rig); C_CHAR.objects.link(body)
        body.parent = rig
        for md_ in body.modifiers:
            if md_.type == 'ARMATURE':
                md_.object = rig
        rig.location = pos; rig.scale = (k, k, k)
        rig.rotation_mode = 'XYZ'; rig.rotation_euler = (rig0.rotation_euler.x, rig0.rotation_euler.y, yaw)
        rig.animation_data_create()
        act = bpy.data.actions.get(action)
        rig.animation_data.action = act
        if act and len(act.slots):
            rig.animation_data.action_slot = act.slots[0]
        # robe tint: lerp(Tex, Tex*Color, Mask)
        for si, slot in enumerate(body.material_slots):
            m = slot.material
            if m is None or not m.name.startswith("M_Reaper") or "Glow" in m.name:
                continue
            m2 = m.copy(); nt = m2.node_tree
            tex = [n for n in nt.nodes if n.type == 'TEX_IMAGE'][0]
            mk = nt.nodes.new("ShaderNodeTexImage"); mk.image = mask; mk.interpolation = 'Closest'
            for l in tex.inputs["Vector"].links:
                nt.links.new(l.from_socket, mk.inputs["Vector"])
            mul = nt.nodes.new("ShaderNodeMix"); mul.data_type = 'RGBA'; mul.blend_type = 'MULTIPLY'
            mul.inputs[7].default_value = (*tints[i % 4], 1)
            nt.links.new(mk.outputs["Color"], mul.inputs[0])
            targets = [l.to_socket for l in tex.outputs["Color"].links]
            nt.links.new(tex.outputs["Color"], mul.inputs[6])
            for ts in targets:
                nt.links.new(mul.outputs[2], ts)
            slot.link = 'OBJECT'; slot.material = m2
        rigs.append((rig, frame))
    C_CHAR.objects.unlink(rig0); C_CHAR.objects.unlink(body0)
    return rigs


# reapers by the lantern (+ the game-cam hero is placed per shot)
LZ = A.ground_z(0, 0)
spots = [(U(420, -260, LZ), math.radians(200), "Idle", 12),
         (U(-380, -330, LZ), math.radians(-30), "Reaper_Bank_Channel", 20),
         (U(-150, 470, LZ), math.radians(110), "Cheer", 15),
         (U(560, 330, LZ), math.radians(160), "Running_A", 6)]

# game camera hero: in front of the chosen jump pad, looking along the pad's launch direction
def choose_pad():
    # a pad near the plaza whose ramp faces the moon (so the ramp reads in the shot)
    tm = (-fwd_ue.x, -fwd_ue.y)
    def score(p):
        dx, dy = p["target"][0] - p["pos"][0], p["target"][1] - p["pos"][1]
        l = math.hypot(dx, dy)
        return -(dx * tm[0] + dy * tm[1]) / l - math.hypot(p["pos"][0], p["pos"][1]) / 8000
    return max(A.Pads, key=score) if not os.environ.get("SQ_PAD") else A.Pads[int(os.environ["SQ_PAD"])]


GP = choose_pad()
gdx, gdy = GP["target"][0] - GP["pos"][0], GP["target"][1] - GP["pos"][1]
gl = math.hypot(gdx, gdy); gdx /= gl; gdy /= gl
HX, HY = GP["pos"][0] - gdx * 650, GP["pos"][1] - gdy * 650
HZ = A.ground_z(HX, HY)
hero_yaw_ue = math.degrees(math.atan2(gdy, gdx))
spots.append((U(HX, HY, HZ), -math.radians(hero_yaw_ue) + math.pi / 2, "Running_A", 4))
spots.append((U(HX + 260 * gdy - 200 * gdx, HY - 260 * gdx - 200 * gdy, A.ground_z(HX + 260 * gdy, HY - 260 * gdx)),
              -math.radians(hero_yaw_ue) + math.pi / 2, "Running_B", 10))
rigs = add_reapers(spots)

# ---------------------------------------------------------------- render setup
sc.render.engine = 'BLENDER_EEVEE'
ee = sc.eevee
ee.taa_render_samples = SAMPLES
for attr, val in (("use_shadows", True), ("shadow_ray_count", 1), ("shadow_step_count", 6),
                  ("volumetric_start", 18.0), ("volumetric_end", 400.0), ("volumetric_tile_size", '8'),
                  ("use_volumetric_shadows", False), ("use_gtao", True), ("gtao_distance", 1.0),
                  ("use_bloom", True), ("bloom_intensity", 0.05)):
    try:
        setattr(ee, attr, val)
    except Exception as e:
        print("eevee attr skip", attr, e)
sc.view_settings.view_transform = 'AgX'
sc.view_settings.look = os.environ.get("SQ_LOOK", 'AgX - Medium High Contrast')
sc.view_settings.exposure = float(os.environ.get("SQ_EXPO", "0.0"))
sc.render.image_settings.file_format = 'PNG'

cam_d = bpy.data.cameras.new("cam"); cam_d.clip_start = 0.5; cam_d.clip_end = 1200
cam = bpy.data.objects.new("cam", cam_d); sc.collection.objects.link(cam); sc.camera = cam


def look(loc, tgt):
    cam.location = loc
    cam.rotation_euler = (tgt - loc).to_track_quat('-Z', 'Y').to_euler()


os.makedirs(OUTDIR, exist_ok=True)
for shot in SHOTS:
    if shot == "overview":
        sc.render.resolution_x, sc.render.resolution_y = int(1600 * SCALE), int(1000 * SCALE)
        frames = {r: f for r, f in rigs}
        ov = os.environ.get("SQ_OVCAM", "-55,-80,78")
        loc = Vector(tuple(float(v) for v in ov.split(",")))
        look(loc, Vector((1, -7, -4)))
        cam_d.sensor_fit = 'HORIZONTAL'; cam_d.lens = float(os.environ.get("SQ_OVLENS", "24"))
        name = f"Map_{BIOME}_Overview.png"
    elif shot == "plaza":
        sc.render.resolution_x, sc.render.resolution_y = int(1600 * SCALE), int(900 * SCALE)
        look(Vector((-9.5, -14.5, 7.5)), Vector((0.5, 0, 1.0)))
        cam_d.sensor_fit = 'HORIZONTAL'; cam_d.lens = 30
        name = f"Map_{BIOME}_Plaza.png"
    else:
        sc.render.resolution_x, sc.render.resolution_y = int(1600 * SCALE), int(900 * SCALE)
        # Spring arm: pivot = capsule centre (ground + 90), pitch -45, arm 1400, socket offset (0,0,120) in arm space
        pitch = math.radians(-45); yaw = math.radians(hero_yaw_ue)
        fwd = Vector((math.cos(pitch) * math.cos(yaw), math.cos(pitch) * math.sin(yaw), math.sin(pitch)))
        up = Vector((-math.sin(pitch) * math.cos(yaw), -math.sin(pitch) * math.sin(yaw), math.cos(pitch)))
        piv = Vector((HX, HY, HZ + 90))
        cpos = piv - fwd * 1400 + up * 120
        loc = U(*cpos); tgt = U(*(cpos + fwd * 1000))
        look(loc, tgt)
        cam_d.sensor_fit = 'HORIZONTAL'
        cam_d.angle = math.radians(90 if shot == "gamecam90" else 75)
        name = f"Map_{BIOME}_GameCam{'_FOV90' if shot == 'gamecam90' else ''}.png"
    sc.frame_set(rigs[0][1])
    # per-rig frames: bake by evaluating each action at its own frame via NLA-free offset
    for r, f in rigs:
        if r.animation_data and r.animation_data.action:
            r.animation_data.action_extrapolation = 'HOLD'
    sc.render.filepath = os.path.join(OUTDIR, name)
    bpy.ops.render.render(write_still=True)
    print("WROTE", sc.render.filepath)

if os.environ.get("SQ_SAVE"):
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUTDIR, f"Map_{BIOME}_Preview.blend"))
