# Park_Pieces.png: each piece re-imported from its FBX; left = shaded visual with a 180 cm reaper for scale,
# right = its UCX hulls (ride hulls in alternating greens, decoration hulls orange). Ride direction +X = left->right arrow.
import bpy, sys, os, math, json
from mathutils import Vector
OUT = "/home/claude/sq/ArtSource/Props/"
T = "/tmp/park_tiles/"; os.makedirs(T, exist_ok=True)
names = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
px = 420
info = {}
for name in names:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    bpy.ops.import_scene.fbx(filepath=OUT + name + ".fbx")
    objs = [o for o in bpy.data.objects if o.type == 'MESH']
    vis = [o for o in objs if not o.name.startswith("UCX_")][0]
    ucx = sorted([o for o in objs if o.name.startswith("UCX_")], key=lambda o: o.name)
    img = bpy.data.images.load(OUT + "park_palette.png")
    m = bpy.data.materials.new("pal"); m.use_nodes = True
    t = m.node_tree.nodes.new("ShaderNodeTexImage"); t.image = img; t.interpolation = 'Closest'
    b = m.node_tree.nodes["Principled BSDF"]; m.node_tree.links.new(t.outputs["Color"], b.inputs["Base Color"]); b.inputs["Roughness"].default_value = 0.85
    vis.data.materials.clear(); vis.data.materials.append(m)
    cos = [vis.matrix_world @ v.co for v in vis.data.vertices]
    mn = Vector([min(c[i] for c in cos) for i in range(3)]); mx = Vector([max(c[i] for c in cos) for i in range(3)])
    # reaper for scale (rest pose, 180 cm), beside the piece at -Y
    with bpy.data.libraries.load("/home/claude/sq/ArtSource/Characters/Reaper_Custom.blend", link=False) as (src, dst):
        dst.objects = [n for n in src.objects if n == "Reaper_Body"]
    rp = dst.objects[0]; sc.collection.objects.link(rp)
    rp.parent = None; rp.modifiers.clear()
    rc = [v.co for v in rp.data.vertices]; rh = max(c.y for c in rc) - min(c.y for c in rc)   # rig space is Y-up
    s = 1.8 / rh; rp.scale = (s, s, s); rp.rotation_euler = (math.pi / 2, 0, 0)
    rp.location = (mn.x - 0.1, mn.y - 0.45, -min(c.y for c in rc) * s)
    for sl in rp.material_slots:
        rm = bpy.data.materials.new("r"); rm.use_nodes = True
        rt = rm.node_tree.nodes.new("ShaderNodeTexImage"); rt.image = bpy.data.images.load("/home/claude/sq/ArtSource/Characters/reaper_palette.png"); rt.interpolation = 'Closest'
        rm.node_tree.links.new(rt.outputs["Color"], rm.node_tree.nodes["Principled BSDF"].inputs["Base Color"]); sl.material = rm
    # hull materials
    greens = []
    for k, (r, g, bb) in enumerate([(0.15, 0.75, 0.35), (0.10, 0.45, 0.25), (1.0, 0.45, 0.1)]):
        hm = bpy.data.materials.new("h%d" % k); hm.use_nodes = True
        hm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (r, g, bb, 1); greens.append(hm)
    # ground
    bpy.ops.mesh.primitive_plane_add(size=30, location=(0, 0, -0.002)); gpl = bpy.context.active_object
    gm = bpy.data.materials.new("g"); gm.use_nodes = True
    gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.16, 0.22, 0.13, 1); gpl.data.materials.append(gm)
    # 1 m grid lines on the ground for scale
    for i in range(-14, 15):
        for horiz in (0, 1):
            bpy.ops.mesh.primitive_plane_add(size=1, location=(i if not horiz else 0, i if horiz else 0, 0.0))
            ln = bpy.context.active_object; ln.scale = ((0.008, 15, 1) if not horiz else (15, 0.008, 1)); ln.data.materials.append(greens[1])
    # arrow showing +X ride direction
    bpy.ops.mesh.primitive_cone_add(vertices=3, radius1=0.25, depth=0.05, location=(mx.x + 0.45, 0, 0.03), rotation=(0, 0, -math.pi / 2))
    ar = bpy.context.active_object
    am = bpy.data.materials.new("a"); am.use_nodes = True; am.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.95, 0.85, 0.1, 1)
    am.node_tree.nodes["Principled BSDF"].inputs["Emission Color"].default_value = (0.95, 0.85, 0.1, 1)
    am.node_tree.nodes["Principled BSDF"].inputs["Emission Strength"].default_value = 1.0
    ar.data.materials.append(am)
    # camera / lights
    cam_d = bpy.data.cameras.new("c"); cam_d.type = 'ORTHO'; cam = bpy.data.objects.new("c", cam_d); sc.collection.objects.link(cam); sc.camera = cam
    ctr = (mn + mx) / 2; ctr.z = (mx.z) * 0.35
    loc = ctr + Vector((-0.55, -1.0, 1.05)).normalized() * 30
    cam.location = loc; cam.rotation_euler = (ctr - loc).to_track_quat('-Z', 'Y').to_euler()
    ext = max(mx.x - mn.x + 1.6, mx.y - mn.y + 1.8, 4.0)
    cam_d.ortho_scale = ext * 1.08; cam_d.clip_end = 100
    for nm, rot, e in (("k", (math.radians(45), 0, math.radians(-30)), 3.5), ("f", (math.radians(65), 0, math.radians(120)), 1.0)):
        ld = bpy.data.lights.new(nm, 'SUN'); ld.energy = e; lo = bpy.data.objects.new(nm, ld); sc.collection.objects.link(lo); lo.rotation_euler = rot
    w = bpy.data.worlds.new("w"); sc.world = w; w.use_nodes = True; w.node_tree.nodes["Background"].inputs[0].default_value = (0.06, 0.06, 0.08, 1)
    sc.render.engine = 'BLENDER_EEVEE'; sc.view_settings.view_transform = 'AgX'
    sc.render.resolution_x = sc.render.resolution_y = px
    for u in ucx: u.hide_render = True
    sc.render.filepath = T + name + "_vis.png"; bpy.ops.render.render(write_still=True)
    vis.hide_render = True
    deco = set(json.load(open("/home/claude/blender_work/park/report.json"))[name]["deco_ucx"])
    for u in ucx:
        u.hide_render = False
        u.data.materials.clear()
    sc.render.filepath = T + name + "_ucx.png"
    for k, u in enumerate(ucx):
        u.data.materials.append(greens[2] if u.name in deco else greens[k % 2])
    bpy.ops.render.render(write_still=True)
    info[name] = {"ucx": [u.name for u in ucx], "vis": vis.name, "bbox_cm": [round(v * 100, 1) for v in list(mn) + list(mx)]}
    print("RENDERED", name, len(ucx), info[name]["bbox_cm"])
json.dump(info, open(T + "info_%s.json" % names[0], "w"))
