# Soul Quota map-quality mirror: one district of a map as ASQArena builds it (set pieces, scenery, ground art,
# Midnight Fairground lighting), rendered from the real chase camera and a wider beauty angle.
# usage: python3 scene.py <graveyard|harbour|clock> <iter> [chase|beauty|both]
import sys, os, math, json, random
import numpy as np
import bpy
from mathutils import Vector
from PIL import Image, ImageFilter
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import recipes

MAP, IT = sys.argv[1], int(sys.argv[2])
WHICH = sys.argv[3] if len(sys.argv) > 3 else "both"
DIST = int(os.environ.get("DIST", "0"))
PROPS = "/home/claude/sq/ArtSource/Props/"
HKIT = "/home/claude/blender_work/mapq/kit/"
OUTD = "/mnt/user-data/outputs/maps_v2/"
TMP = "/tmp/mapq/"
os.makedirs(TMP, exist_ok=True); os.makedirs(OUTD, exist_ok=True)
CFG = recipes.config(MAP, IT)
rng = random.Random(1234 + hash(MAP) % 1000)
bpy.ops.wm.read_factory_settings(use_empty=True)
SC = bpy.context.scene

# ------------------------------------------------------------------ materials
MATS = {}
def tex_mat(key, path, closest=False, glow_mask=None, glow=0.0, tint=None):
    if key in MATS:
        return MATS[key]
    m = bpy.data.materials.new(key); m.use_nodes = True
    nt = m.node_tree; b = nt.nodes["Principled BSDF"]
    t = nt.nodes.new("ShaderNodeTexImage"); t.image = bpy.data.images.load(path)
    t.interpolation = 'Closest' if closest else 'Linear'
    if tint:   # M_ArtTint: Tex * Color
        mx = nt.nodes.new("ShaderNodeMixRGB"); mx.blend_type = 'MULTIPLY'; mx.inputs[0].default_value = 1.0; mx.inputs[2].default_value = (*tint, 1)
        nt.links.new(t.outputs["Color"], mx.inputs[1]); nt.links.new(mx.outputs[0], b.inputs["Base Color"])
    else:
        nt.links.new(t.outputs["Color"], b.inputs["Base Color"])
    b.inputs["Roughness"].default_value = 0.9
    if glow_mask and glow > 0:
        g = nt.nodes.new("ShaderNodeTexImage"); g.image = bpy.data.images.load(glow_mask); g.interpolation = 'Closest'
        g.image.colorspace_settings.name = 'Non-Color'
        mul = nt.nodes.new("ShaderNodeMath"); mul.operation = 'MULTIPLY'; mul.inputs[1].default_value = glow
        nt.links.new(g.outputs["Color"], mul.inputs[0])
        nt.links.new(t.outputs["Color"], b.inputs["Emission Color"])
        nt.links.new(mul.outputs[0], b.inputs["Emission Strength"])
    MATS[key] = m
    return m

def glow_mask_for(palette_png, names, out):
    """white on the palette swatches that glow (same 8x8 x 16 px layout as park_build.write_palette)"""
    im = Image.new("L", (128, 128), 0); px = im.load()
    for i, n in enumerate(names):
        if n.startswith("glow"):
            cx, cy = i % 8, i // 8
            for y in range((7 - cy) * 16, (8 - cy) * 16):
                for x in range(cx * 16, cx * 16 + 16):
                    px[x, y] = 255
    im.save(out)
    return out

import importlib
sys.path.insert(0, "/home/claude/blender_work/fair"); sys.path.insert(0, "/home/claude/blender_work/sets"); sys.path.insert(0, "/home/claude/blender_work/park")
FAIR_NAMES = json.load(open("/home/claude/blender_work/mapq/fair_names.json"))
SET_NAMES = ["stone", "stone_dark", "stone_light", "moss", "wood", "wood_dark", "wood_light", "iron", "roof", "roof_dark", "glow", "plaster", "brass", "brass_dark", "teal", "red",
             "rope", "leaf_dark", "leaf", "bark", "white", "night", "plaster_dark", "glow_cool"]
GLOW = CFG["glow"]
SCHEME = {"graveyard": "graveyard", "harbour": "harbour", "clock": "clock"}[MAP]

def mat_for(name):
    if name.startswith("Fair_"):
        return tex_mat("fair", PROPS + "fair_palette.png", True, glow_mask_for(None, FAIR_NAMES, TMP + "fair_glow.png"), GLOW)
    if name == "Set_Wall" and CFG.get("wall_tint"):
        return tex_mat("setwall", PROPS + "set_palette.png", True, tint=CFG["wall_tint"])
    if name.startswith("Set_"):
        return tex_mat("set", PROPS + "set_palette.png", True, glow_mask_for(None, SET_NAMES, TMP + "set_glow.png"), GLOW)
    if name.startswith("KitM_"):
        return tex_mat("kitm", PROPS + ("kitm_%s.png" % SCHEME if CFG["retint"] else "kitm_texture.png"))
    if name.startswith("KitD_"):
        return tex_mat("kitd", PROPS + ("kitd_%s.png" % SCHEME if CFG["retint"] else "kitd_texture.png"))
    return tex_mat("hkit", HKIT + "halloweenbits_texture.png")

# ------------------------------------------------------------------ mesh library
LIB = {}
def lib(name):
    if name in LIB:
        return LIB[name]
    path = (HKIT if os.path.isfile(HKIT + name + ".fbx") else PROPS) + name + ".fbx"
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=path)
    new = [o for o in bpy.data.objects if o not in before]
    ms = [o for o in new if o.type == 'MESH' and not o.name.startswith("UCX")]
    for o in new:
        if o not in ms:
            bpy.data.objects.remove(o)
    bpy.ops.object.select_all(action='DESELECT')
    for o in ms:
        o.select_set(True)
    bpy.context.view_layer.objects.active = ms[0]
    if len(ms) > 1:
        bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    ob.data.materials.clear(); ob.data.materials.append(mat_for(name))
    zs = [v.co.z for v in ob.data.vertices]
    ob["h"] = max(zs) - min(zs)
    ob["zmin"] = min(zs)
    ob.hide_render = True; ob.hide_viewport = True
    LIB[name] = ob
    return ob

SITES = []   # (x, y, radius, kind) for the ground art
def place(name, x, y, face_deg=0.0, height=None, z=0.0, scale=None, foot=None, kind="solid", jitter=0.0):
    src = lib(name)
    ob = src.copy(); ob.hide_render = False; ob.hide_viewport = False
    SC.collection.objects.link(ob)
    s = scale if scale is not None else (height / src["h"] if height else 1.0)
    kitfront = name.startswith("Kit") or not (name.startswith("Set_") or name.startswith("Fair_"))
    yaw = math.radians(face_deg) + (math.radians(90) if kitfront else 0.0)   # kit fronts face -Y in Blender; Set_/Fair_ face +X
    ob.rotation_euler = (0, 0, yaw)
    ob.scale = (s, s, s)
    ob.location = (x, y, z - src["zmin"] * s)
    if foot:
        SITES.append((x, y, foot, kind))
    return ob

# ------------------------------------------------------------------ build the district (recipes.py mirrors ASQArena)
PLAN = recipes.build(MAP, IT, rng, DIST)
for it in PLAN["pieces"]:
    place(**it)
R = PLAN["R"]

# reaper stand-in (cloak + head + scythe) where the chase camera follows it
def reaper(x, y, yaw):
    def prim(fn, col, **kw):
        fn(**kw); o = bpy.context.object
        m = bpy.data.materials.new("r"); m.use_nodes = True; m.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (*col, 1)
        o.data.materials.append(m); return o
    prim(bpy.ops.mesh.primitive_cone_add, (0.05, 0.04, 0.09), radius1=0.55, radius2=0.2, depth=1.5, location=(x, y, 0.75))
    prim(bpy.ops.mesh.primitive_uv_sphere_add, (0.07, 0.05, 0.12), radius=0.42, location=(x, y, 1.75), segments=12, ring_count=8)
    prim(bpy.ops.mesh.primitive_cylinder_add, (0.3, 0.22, 0.2), radius=0.04, depth=2.3, location=(x + 0.5 * math.sin(yaw), y - 0.5 * math.cos(yaw), 1.3))
RP = PLAN["reaper"]
reaper(RP[0], RP[1], RP[2])

# ------------------------------------------------------------------ ground art (mirrors BuildTerrainMesh paint + M_GroundV3 maths)
GSIZE, GRES = 150.0, CFG.get("gres", 2600)
gimg = recipes.ground(MAP, IT, PLAN, SITES, GSIZE, GRES)
Image.fromarray(gimg[..., :3]).save(TMP + "ground_%s_%d.png" % (MAP, IT))
if gimg.shape[2] == 4:
    Image.fromarray(gimg[..., 3]).save(TMP + "rough_%s_%d.png" % (MAP, IT))
bpy.ops.mesh.primitive_plane_add(size=GSIZE, location=(PLAN["gcentre"][0], PLAN["gcentre"][1], 0))
gp = bpy.context.object
gm = bpy.data.materials.new("ground"); gm.use_nodes = True
gt = gm.node_tree.nodes.new("ShaderNodeTexImage"); gt.image = bpy.data.images.load(TMP + "ground_%s_%d.png" % (MAP, IT))
gb = gm.node_tree.nodes["Principled BSDF"]; gm.node_tree.links.new(gt.outputs["Color"], gb.inputs["Base Color"]); gb.inputs["Roughness"].default_value = 0.95
if gimg.shape[2] == 4:
    rt = gm.node_tree.nodes.new("ShaderNodeTexImage"); rt.image = bpy.data.images.load(TMP + "rough_%s_%d.png" % (MAP, IT)); rt.image.colorspace_settings.name = 'Non-Color'
    mr = gm.node_tree.nodes.new("ShaderNodeMapRange"); mr.inputs[1].default_value = 0.08; mr.inputs[2].default_value = 1.0; mr.inputs[3].default_value = 0.12; mr.inputs[4].default_value = 0.95
    gm.node_tree.links.new(rt.outputs["Color"], mr.inputs[0]); gm.node_tree.links.new(mr.outputs[0], gb.inputs["Roughness"])
gp.data.materials.append(gm)
# far ground skirt in the fog colour so there is no void
bpy.ops.mesh.primitive_plane_add(size=1600, location=(0, 0, -0.05))
sk = bpy.context.object; sm = bpy.data.materials.new("skirt"); sm.use_nodes = True
sm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (*[c * 0.5 for c in CFG["ground_far"]], 1); sk.data.materials.append(sm)

# ------------------------------------------------------------------ sky, hills, moon (mirrors ASQArena::BuildSky)
W = bpy.data.worlds.new("w"); SC.world = W; W.use_nodes = True
nt = W.node_tree; bg = nt.nodes["Background"]
tc = nt.nodes.new("ShaderNodeTexCoord"); sep = nt.nodes.new("ShaderNodeSeparateXYZ"); ramp = nt.nodes.new("ShaderNodeValToRGB")
nt.links.new(tc.outputs["Generated"], sep.inputs[0]); nt.links.new(sep.outputs["Z"], ramp.inputs[0])
el = ramp.color_ramp.elements
el[0].position = 0.0; el[0].color = (*CFG["sky_h"], 1)
el[1].position = 0.55; el[1].color = (*CFG["sky_z"], 1)
e = ramp.color_ramp.elements.new(0.16); e.color = (*CFG["sky_m"], 1)
nt.links.new(ramp.outputs[0], bg.inputs[0]); bg.inputs[1].default_value = CFG["sky_strength"]

def emis(col, strength):
    m = bpy.data.materials.new("e"); m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]; b.inputs["Base Color"].default_value = (*col, 1)
    b.inputs["Emission Color"].default_value = (*col, 1); b.inputs["Emission Strength"].default_value = strength
    return m
md = PLAN["moon_dir"]
hm = bpy.data.materials.new("hill"); hm.use_nodes = True; hm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (*CFG["hill"], 1)
hm2 = bpy.data.materials.new("hill2"); hm2.use_nodes = True; hm2.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (*CFG["hill_far"], 1)
hr = random.Random(7)
if CFG.get("sky_v2"):
    # exactly what ASQArena::BuildSky builds: a big low moon (15 deg up), two rings of squashed-sphere hills round the
    # arena (centre = the hub, 62 m back down the lane), kit silhouettes standing on the near hills
    HUBX = -recipes.HUB_D
    ma, me_ = math.radians(CFG.get("moon_yaw", 28.0)), math.radians(15.0)
    mdir = Vector((math.cos(ma) * math.cos(me_), math.sin(ma) * math.cos(me_), math.sin(me_)))
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.085 * 900, location=mdir * 837, segments=32, ring_count=16)
    bpy.context.object.data.materials.append(emis(CFG["moon_col"], CFG.get("moon_glow", 2.2)))
    tops = []
    for ring, (ringr, wmul, tmul, hmat) in enumerate(((229.0, 1.0, 1.0, hm), (349.0, 1.8, 2.1, hm2))):
        for k in range(26):
            a = (k + hr.uniform(-0.3, 0.3)) / 26 * 2 * math.pi + ring * 0.4
            r = ringr + hr.uniform(-15, 25); wide = hr.uniform(55, 95) * wmul; tall = hr.uniform(11, 26) * tmul
            bpy.ops.mesh.primitive_uv_sphere_add(radius=1, segments=20, ring_count=10, location=(HUBX + math.cos(a) * r, math.sin(a) * r, -tall * 0.25))
            o = bpy.context.object; o.scale = (wide * 0.6, wide, tall); o.rotation_euler = (0, 0, a); o.data.materials.append(hmat)
            if ring == 0: tops.append((HUBX + math.cos(a) * r, math.sin(a) * r, -tall * 0.25 + tall * 0.9))
    skm = bpy.data.materials.new("skyline"); skm.use_nodes = True; skm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (*CFG["skyline"], 1)
    for (tx, ty, tz) in tops:
        for k in range(2):
            nm = hr.choice(CFG["skyline_props"])
            src = lib(nm); o = src.copy(); o.data = src.data.copy(); o.data.materials.clear(); o.data.materials.append(skm)
            o.hide_render = False; o.hide_viewport = False; SC.collection.objects.link(o)
            sc_ = hr.uniform(0.6, 1.0) * CFG.get("skyline_scale", 4.0) * CFG.get("prop_scale", 3.2)
            o.scale = (sc_, sc_, sc_); o.rotation_euler = (0, 0, hr.uniform(0, 6.28))
            o.location = (tx + hr.uniform(-22, 22), ty + hr.uniform(-22, 22), tz - 5 - src["zmin"] * sc_)
else:
    bpy.ops.mesh.primitive_uv_sphere_add(radius=CFG["moon_size"], location=(md[0] * 520, md[1] * 520, md[2] * 520), segments=24, ring_count=12)
    bpy.context.object.data.materials.append(emis(CFG["moon_col"], 3.0))
    for ringr, n, hmat, hh in ((300, 18, hm, 20), (430, 16, hm2, 36)):
        for k in range(n):
            a = 2 * math.pi * (k + hr.uniform(-0.3, 0.3)) / n
            bpy.ops.mesh.primitive_cone_add(vertices=7, radius1=hr.uniform(70, 120), radius2=hr.uniform(30, 60), depth=hh * hr.uniform(0.5, 1.0),
                                            location=(ringr * math.cos(a), ringr * math.sin(a), hh * 0.25))
            bpy.context.object.data.materials.append(hmat)

if CFG.get("stars"):   # the game's star field (BuildSky: 260 stars on the dome)
    sr = random.Random(5); smat = emis((1.0, 0.95, 0.9), 6.0)
    import bmesh
    bm = bmesh.new()
    for k in range(260):
        az, el = sr.uniform(0, 2 * math.pi), math.asin(sr.uniform(0.12, 1.0))
        p = Vector((math.cos(az) * math.cos(el), math.sin(az) * math.cos(el), math.sin(el))) * 900
        r = sr.uniform(0.9, 2.2)
        bmesh.ops.create_icosphere(bm, subdivisions=1, radius=r, matrix=__import__("mathutils").Matrix.Translation(p))
    me = bpy.data.meshes.new("stars"); bm.to_mesh(me); bm.free()
    so_ = bpy.data.objects.new("stars", me); so_.data.materials.append(smat); SC.collection.objects.link(so_)

# ------------------------------------------------------------------ lights
sun = bpy.data.lights.new("moon", 'SUN'); sun.energy = CFG["moon_energy"]; sun.color = CFG["moon_light"]; sun.angle = math.radians(3)
so = bpy.data.objects.new("moon", sun); SC.collection.objects.link(so)
so.rotation_euler = Vector((-md[0], -md[1], -md[2])).to_track_quat('-Z', 'Y').to_euler()
if CFG.get("rim_energy", 0) > 0:   # cool rim / fill from the opposite side (the game's rim light + sky fill)
    f = bpy.data.lights.new("fill", 'SUN'); f.energy = CFG["rim_energy"]; f.color = CFG["rim_col"]
    fo = bpy.data.objects.new("fill", f); SC.collection.objects.link(fo)
    fo.rotation_euler = Vector((md[0], md[1], -0.5)).to_track_quat('-Z', 'Y').to_euler()
for (lx, ly, lz, col, power, rad) in PLAN["lights"]:
    L = bpy.data.lights.new("lamp", 'POINT'); L.energy = power * CFG["lamp_gain"]; L.color = col; L.shadow_soft_size = 0.4
    L.use_shadow = CFG.get("lamp_shadows", False)
    lo = bpy.data.objects.new("lamp", L); lo.location = (lx, ly, lz); SC.collection.objects.link(lo)

# ------------------------------------------------------------------ render
SC.render.engine = 'BLENDER_EEVEE'
SC.render.resolution_x, SC.render.resolution_y = 1280, 720
SC.view_settings.view_transform = 'Standard'
try: SC.view_settings.look = 'None'
except Exception: pass
SC.render.image_settings.file_format = 'PNG'
try:
    SC.eevee.taa_render_samples = 8
    SC.eevee.use_shadows = True
    SC.eevee.use_raytracing = False
except Exception as ex:
    print("eevee opt", ex)

def camera(name, pos, look, lens=None, fov=None):
    c = bpy.data.cameras.new(name); c.sensor_fit = 'HORIZONTAL'
    if fov: c.angle = math.radians(fov)
    if lens: c.lens = lens
    c.clip_end = 2000
    o = bpy.data.objects.new(name, c); SC.collection.objects.link(o)
    o.location = pos
    o.rotation_euler = (Vector(look) - Vector(pos)).to_track_quat('-Z', 'Y').to_euler()
    return o

def render(cam, tag):
    SC.camera = cam
    beauty = TMP + "%s_%d_%s_b.png" % (MAP, IT, tag)
    SC.render.filepath = beauty
    bpy.ops.render.render(write_still=True)
    # depth pass: every material -> emission of camera distance / 400
    dm = bpy.data.materials.new("depth"); dm.use_nodes = True
    n = dm.node_tree; n.nodes.clear()
    cd = n.nodes.new("ShaderNodeCameraData"); mp = n.nodes.new("ShaderNodeMath"); mp.operation = 'DIVIDE'; mp.inputs[1].default_value = 400.0
    em = n.nodes.new("ShaderNodeEmission"); out = n.nodes.new("ShaderNodeOutputMaterial")
    n.links.new(cd.outputs["View Distance"], mp.inputs[0]); n.links.new(mp.outputs[0], em.inputs[0]); n.links.new(em.outputs[0], out.inputs[0])
    vl = bpy.context.view_layer; vl.material_override = dm
    old = (bg.inputs[1].default_value, SC.view_settings.view_transform)
    for l in list(nt.links):
        if l.to_node == bg: nt.links.remove(l)
    bg.inputs[0].default_value = (1, 1, 1, 1); bg.inputs[1].default_value = 1.0
    SC.view_settings.view_transform = 'Raw'
    depth = TMP + "%s_%d_%s_d.png" % (MAP, IT, tag)
    SC.render.filepath = depth; SC.render.image_settings.color_depth = '16'
    bpy.ops.render.render(write_still=True)
    SC.render.image_settings.color_depth = '8'
    vl.material_override = None
    nt.links.new(ramp.outputs[0], bg.inputs[0]); bg.inputs[1].default_value = old[0]; SC.view_settings.view_transform = old[1]
    post(beauty, depth, OUTD + PLAN["outname"] % tag, 0.45 if tag == "beauty" else 1.0)

def post(beauty, depth, out, fog_scale=1.0):
    """height fog by distance, bloom and vignette (what ApplyAtmosphere + the grade do in game)"""
    b = np.asarray(Image.open(beauty).convert("RGB")).astype(np.float32) / 255
    d = np.asarray(Image.open(depth)).astype(np.float32)
    d = (d[..., 0] if d.ndim == 3 else d) / (65535.0 if d.max() > 255 else 255.0)
    dist = d * 400.0
    sky = d > 0.995
    fog = 1 - np.exp(-np.maximum(dist - CFG["fog_start"] / fog_scale, 0) * CFG["fog_density"] * fog_scale)
    fog = np.clip(fog, 0, CFG["fog_max"]); fog[sky] = 0
    H = b.shape[0]
    fc = np.array(CFG["fog_col"], np.float32)[None, None, :]
    img = b * (1 - fog[..., None]) + fc * fog[..., None]
    # bloom
    lum = img.max(-1)
    bright = np.clip((lum - CFG["bloom_thresh"]) / (1 - CFG["bloom_thresh"]), 0, 1)[..., None] * img
    bi = Image.fromarray((np.clip(bright, 0, 1) * 255).astype(np.uint8))
    glow = sum(np.asarray(bi.filter(ImageFilter.GaussianBlur(r))).astype(np.float32) / 255 for r in (6, 18, 40)) / 3
    img = img + glow * CFG["bloom"]
    # grade: contrast, saturation, vignette
    img = (img - 0.5) * CFG["contrast"] + 0.5
    g = img.mean(-1, keepdims=True); img = g + (img - g) * CFG["saturation"]
    yy, xx = np.mgrid[0:H, 0:b.shape[1]]
    r2 = ((xx / b.shape[1] - 0.5) ** 2 + (yy / H - 0.5) ** 2) * 2
    img = img * (1 - CFG["vignette"] * r2[..., None])
    Image.fromarray((np.clip(img, 0, 1) * 255 + 0.5).astype(np.uint8)).save(out)
    print("WROTE", out)

def chase_cam(name, cx, cy, yaw):
    d = Vector((math.cos(yaw), math.sin(yaw), 0))
    pivot = Vector((cx, cy, 1.8))
    arm, pitch = 8.5, math.radians(18)
    cpos = pivot - d * arm * math.cos(pitch) + Vector((0, 0, arm * math.sin(pitch)))
    return camera(name, cpos, pivot + (pivot - cpos).normalized() * 10, fov=90)

TAGS = ["chase", "beauty"] if WHICH == "both" else WHICH.split(",")
SUF = "" if DIST == 0 else "d%d" % (DIST + 1)
for tag in TAGS:
    if tag == "chase":
        render(chase_cam("chase", *RP), SUF + "chase")
    elif tag == "beauty":
        bp, bl = PLAN["beauty"]
        render(camera("beauty", bp, bl, lens=30), SUF + "beauty")
    elif tag in PLAN.get("views", {}):
        v = PLAN["views"][tag]
        if len(v) == 3:
            reaper(*v); render(chase_cam(tag, *v), SUF + tag)
        else:
            render(camera(tag, v[0], v[1], lens=v[2]), SUF + tag)
