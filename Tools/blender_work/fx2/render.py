# Effects preview renderer: chase camera (8.5 m back, 18 degrees down, FOV 90), Midnight Fairground light, dark grass on one side and a
# pale plaza on the other, a reaper and a few enemies for scale. Draws sim.Sys snapshots with the real kit meshes; additive shapes use
# an emission+transparent material (M_SQGlow), solid ones a lit + emissive material (M_ArtTint Glow). Bloom / tone map in numpy.
import bpy, bmesh, math, os, sys, numpy as np
from mathutils import Vector, Quaternion, Matrix
from PIL import Image, ImageDraw
sys.path.insert(0, os.path.dirname(__file__))
import sim
ART = "/home/claude/sq/ArtSource/"
SC = None; MESH = {}; MATS = {}; LIVE = []; ACTORS = {}
GLOW = dict(add_sphere=2.6, add_kit=1.8, solid_sphere=4.0, solid_kit=3.0, solid_basic=2.5, exposure=1.0, bloom=0.55)

def setup(res=(640, 360), fov=90.0, cam_back=850.0, cam_pitch=18.0, look=(120, 0, 60), plaza=True, samples=6):
    global SC
    bpy.ops.wm.read_factory_settings(use_empty=True)
    SC = bpy.context.scene
    SC.render.engine = 'BLENDER_EEVEE'; SC.render.resolution_x, SC.render.resolution_y = res
    SC.view_settings.view_transform = 'Raw'; SC.render.image_settings.file_format = 'OPEN_EXR'; SC.render.image_settings.color_depth = '16'
    try:
        SC.eevee.taa_render_samples = samples; SC.eevee.use_shadows = True; SC.eevee.use_raytracing = False
    except Exception as ex: print("eevee", ex)
    W = bpy.data.worlds.new("w"); W.use_nodes = True; W.node_tree.nodes["Background"].inputs[0].default_value = (0.045, 0.02, 0.085, 1); W.node_tree.nodes["Background"].inputs[1].default_value = 1.0
    SC.world = W
    def lit(name, col, rough=0.9):
        m = bpy.data.materials.new(name); m.use_nodes = True; b = m.node_tree.nodes["Principled BSDF"]
        b.inputs["Base Color"].default_value = (*col, 1); b.inputs["Roughness"].default_value = rough; return m
    def quad(name, x0, x1, y0, y1, z, mat):
        me = bpy.data.meshes.new(name); me.from_pydata([(x0, y0, z), (x1, y0, z), (x1, y1, z), (x0, y1, z)], [], [(0, 1, 2, 3)])
        o = bpy.data.objects.new(name, me); o.data.materials.append(mat); SC.collection.objects.link(o); return o
    quad("grass", -3000, 6000, -6000, 6000, 0, lit("grass", sim.hx(0x2A5C44)))
    if plaza: quad("plaza", -3000, 6000, 0, 6000, 0.5, lit("plaza", sim.hx(0x8C86A2)))
    for (name, col, e, d) in (("moon", (0.62, 0.72, 1.0), 2.2, (0.45, 0.35, -0.82)), ("fill", (0.9, 0.5, 0.75), 0.6, (-0.6, -0.5, -0.4))):
        L = bpy.data.lights.new(name, 'SUN'); L.energy = e; L.color = col; L.angle = math.radians(4)
        o = bpy.data.objects.new(name, L); SC.collection.objects.link(o); o.rotation_euler = Vector(d).to_track_quat('-Z', 'Y').to_euler()
    c = bpy.data.cameras.new("cam"); c.sensor_fit = 'HORIZONTAL'; c.angle = math.radians(fov); c.clip_start = 5; c.clip_end = 20000
    co = bpy.data.objects.new("cam", c); SC.collection.objects.link(co); SC.camera = co
    p = math.radians(cam_pitch); pivot = Vector((0, 0, 90))
    co.location = pivot + Vector((-math.cos(p) * cam_back, 0, math.sin(p) * cam_back))
    co.rotation_euler = (Vector(look) - co.location).to_track_quat('-Z', 'Y').to_euler()
    # materials
    a = bpy.data.materials.new("add"); a.use_nodes = True; nt = a.node_tree; nt.nodes.clear()
    oi = nt.nodes.new("ShaderNodeObjectInfo"); em = nt.nodes.new("ShaderNodeEmission"); tr = nt.nodes.new("ShaderNodeBsdfTransparent")
    ad = nt.nodes.new("ShaderNodeAddShader"); out = nt.nodes.new("ShaderNodeOutputMaterial")
    nt.links.new(oi.outputs["Color"], em.inputs["Color"]); nt.links.new(oi.outputs["Alpha"], em.inputs["Strength"])
    nt.links.new(em.outputs[0], ad.inputs[0]); nt.links.new(tr.outputs[0], ad.inputs[1]); nt.links.new(ad.outputs[0], out.inputs[0])
    try: a.surface_render_method = 'BLENDED'
    except Exception: a.blend_method = 'BLEND'
    a.use_backface_culling = False
    MATS["add"] = a
    sf = bpy.data.materials.new("addsoft"); sf.use_nodes = True; nt = sf.node_tree; nt.nodes.clear()   # Soft = 0.8: bright middle, edge fades
    oi = nt.nodes.new("ShaderNodeObjectInfo"); em = nt.nodes.new("ShaderNodeEmission"); tr = nt.nodes.new("ShaderNodeBsdfTransparent")
    lw = nt.nodes.new("ShaderNodeLayerWeight"); lw.inputs["Blend"].default_value = 0.5; inv = nt.nodes.new("ShaderNodeMath"); inv.operation = 'SUBTRACT'; inv.inputs[0].default_value = 1.0
    sq = nt.nodes.new("ShaderNodeMath"); sq.operation = 'POWER'; sq.inputs[1].default_value = 2.0
    mx = nt.nodes.new("ShaderNodeMapRange"); mx.inputs[3].default_value = 0.2; mx.inputs[4].default_value = 1.0
    mu = nt.nodes.new("ShaderNodeMath"); mu.operation = 'MULTIPLY'
    ad = nt.nodes.new("ShaderNodeAddShader"); out = nt.nodes.new("ShaderNodeOutputMaterial")
    nt.links.new(lw.outputs["Facing"], inv.inputs[1]); nt.links.new(inv.outputs[0], sq.inputs[0]); nt.links.new(sq.outputs[0], mx.inputs[0])
    nt.links.new(mx.outputs[0], mu.inputs[0]); nt.links.new(oi.outputs["Alpha"], mu.inputs[1])
    nt.links.new(oi.outputs["Color"], em.inputs["Color"]); nt.links.new(mu.outputs[0], em.inputs["Strength"])
    nt.links.new(em.outputs[0], ad.inputs[0]); nt.links.new(tr.outputs[0], ad.inputs[1]); nt.links.new(ad.outputs[0], out.inputs[0])
    try: sf.surface_render_method = 'BLENDED'
    except Exception: sf.blend_method = 'BLEND'
    MATS["addsoft"] = sf
    so = bpy.data.materials.new("solid"); so.use_nodes = True; nt = so.node_tree; b = nt.nodes["Principled BSDF"]; b.inputs["Roughness"].default_value = 0.85
    oi = nt.nodes.new("ShaderNodeObjectInfo"); nt.links.new(oi.outputs["Color"], b.inputs["Base Color"]); nt.links.new(oi.outputs["Color"], b.inputs["Emission Color"])
    nt.links.new(oi.outputs["Alpha"], b.inputs["Emission Strength"])
    MATS["solid"] = so
    MESH.clear(); LIVE.clear(); ACTORS.clear()

def _imp(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=path)
    return [o for o in bpy.data.objects if o not in before]

def kit_mesh(shape):
    """mesh + normalising scale so the longest side is 100 (engine basic shapes are already 100 across)"""
    if shape in MESH: return MESH[shape]
    if shape in sim.KIT:
        obs = _imp(ART + "FX/SM_FX_%s.fbx" % shape)
        ob = [o for o in obs if o.type == 'MESH'][0]
        me = ob.data.copy(); me.transform(ob.matrix_world)
        for o in obs: bpy.data.objects.remove(o)
        xs = [v.co for v in me.vertices]; mn = Vector((min(c.x for c in xs), min(c.y for c in xs), min(c.z for c in xs))); mx = Vector((max(c.x for c in xs), max(c.y for c in xs), max(c.z for c in xs)))
        norm = 100.0 / max((mx - mn)[i] for i in range(3))
    else:
        bm = bmesh.new()
        if shape == "Sphere": bmesh.ops.create_uvsphere(bm, u_segments=16, v_segments=10, radius=50)
        elif shape == "Cube": bmesh.ops.create_cube(bm, size=100)
        elif shape == "Cylinder": bmesh.ops.create_cone(bm, cap_ends=True, segments=20, radius1=50, radius2=50, depth=100)
        else: bmesh.ops.create_cone(bm, cap_ends=True, segments=20, radius1=50, radius2=0, depth=100)
        me = bpy.data.meshes.new(shape); bm.to_mesh(me); bm.free(); norm = 1.0
        if shape == "Sphere":
            for p in me.polygons: p.use_smooth = True
    MESH[shape] = (me, norm); return MESH[shape]

def actor(key, fbx, pos, yaw=0.0, height=170.0, tint=None):
    """a character mesh for scale (rest pose)"""
    obs = _imp(fbx); ms = [o for o in obs if o.type == 'MESH']
    zs = [(o.matrix_world @ Vector(c)).z for o in ms for c in o.bound_box]; k = height / max(1e-3, max(zs) - min(zs))
    root = bpy.data.objects.new(key, None); SC.collection.objects.link(root)
    for o in obs:
        if o.parent is None: o.parent = root
    root.scale = (k, k, k); root.location = Vector(pos) - Vector((0, 0, min(zs) * k)); root.rotation_euler = (0, 0, math.radians(yaw))
    if tint is not None:
        m = bpy.data.materials.new(key); m.use_nodes = True; m.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (*tint, 1)
        for o in ms:
            o.data.materials.clear(); o.data.materials.append(m)
    ACTORS[key] = root; return root

def draw(snapshot):
    for o in LIVE: bpy.data.objects.remove(o)
    LIVE.clear()
    fa, fg = sim.TUNE["StyleFxAdditive"], sim.TUNE["StyleFxGlow"]
    for (shape, pos, q, sc, col, alpha) in snapshot:
        me, norm = kit_mesh(shape)
        lum = 0.3 * col[0] + 0.59 * col[1] + 0.11 * col[2]
        o = bpy.data.objects.new("p", me); SC.collection.objects.link(o); LIVE.append(o)
        o.location = pos; o.rotation_mode = 'QUATERNION'; o.rotation_quaternion = q; o.scale = sc * norm
        if fa > 0 and shape in sim.LIGHT and lum > 0.14:
            g = fa * fg * (GLOW["add_sphere"] if shape == "Sphere" else GLOW["add_kit"]) * alpha
            o.data = me; o.active_material_index = 0
            mat = MATS["addsoft"] if shape == "Sphere" else MATS["add"]
            o.color = (*col, g)
        else:
            g = 0.6 if shape in sim.MATTE else fg * (GLOW["solid_sphere"] if shape == "Sphere" else (GLOW["solid_kit"] if shape in sim.KIT else GLOW["solid_basic"]))
            mat = MATS["solid"]; o.color = (*col, g * 0.35)
        if not o.material_slots: 
            if not me.materials: me.materials.append(None)
        o.material_slots[0].link = 'OBJECT'; o.material_slots[0].material = mat

def shoot(path):
    SC.render.filepath = path; bpy.ops.render.render(write_still=True)
    img = bpy.data.images.load(path if path.endswith(".exr") else path + ".exr")
    w, h = img.size; a = np.array(img.pixels[:], dtype=np.float32).reshape(h, w, 4)[::-1, :, :3]
    bpy.data.images.remove(img)
    return a

def grade(a):
    """bloom on what is brighter than white-ish, then sRGB"""
    a = a * GLOW["exposure"]
    br = np.clip(a - 0.85, 0, 8)
    def blur(x, r):
        k = np.exp(-0.5 * (np.arange(-3 * r, 3 * r + 1) / r) ** 2); k /= k.sum()
        x = np.apply_along_axis(lambda m: np.convolve(m, k, mode='same'), 0, x)
        return np.apply_along_axis(lambda m: np.convolve(m, k, mode='same'), 1, x)
    h = a.shape[0]
    bl = blur(br, max(1, h // 90)) * 0.6 + blur(br, max(2, h // 30)) * 0.4
    a = a + bl * GLOW["bloom"]
    a = np.clip(a, 0, 1)
    s = np.where(a <= 0.0031308, a * 12.92, 1.055 * np.power(a, 1 / 2.4) - 0.055)
    return Image.fromarray((np.clip(s, 0, 1) * 255).astype(np.uint8))

def run(emit_fn, times, tag, seed=3, dt=1 / 60.0):
    """emit at t=0, render the listed times; returns PIL tiles"""
    s = sim.Sys(seed); emit_fn(s); tiles = []; t = 0.0
    for want in times:
        while t + 1e-6 < want: s.step(dt); t += dt
        draw(s.snapshot())
        tiles.append(grade(shoot("/tmp/fx2/%s_%03d.exr" % (tag, round(want * 1000)))))
    return tiles

def sheet(rows, out, label_h=16, pad=3):
    """rows: [(label, [tiles])]"""
    tw, th = rows[0][1][0].size; n = max(len(r[1]) for r in rows)
    S = Image.new("RGB", (n * (tw + pad) + pad, len(rows) * (th + label_h + pad) + pad), (16, 14, 22)); d = ImageDraw.Draw(S)
    for ri, (label, tiles) in enumerate(rows):
        y = pad + ri * (th + label_h + pad)
        d.text((pad + 2, y + 2), label, fill=(235, 225, 190))
        for ci, t in enumerate(tiles): S.paste(t, (pad + ci * (tw + pad), y + label_h))
    S.save(out, quality=88); print("WROTE", out, S.size)
