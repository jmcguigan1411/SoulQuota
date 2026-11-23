# Soul Quota monsters - shared building helpers (original work).
# Same approach as sq_build.py (Reaper): faces carry a palette swatch index, UVs sit in the swatch centre,
# parts are built in world space (Z up, facing -Y) and skinned with per-part weight functions.
import bpy, bmesh, math
from mathutils import Vector, Matrix, Quaternion

KAYKIT = "/mnt/user-data/outputs/ArtSource/Characters/"     # pristine KayKit sources (read only)
ENEMY_BLEND = "/home/claude/blender_work/sq_enemy_anims.blend"  # SQ_Enemy_* takes (read only)
CH = "/home/claude/sq/ArtSource/Characters/"
PREV = "/home/claude/sq/ArtSource/Previews/"

# ---------------------------------------------------------------- palette (shared layout, per-model colours)
LAYOUT = ["skin", "skin_dark", "skin_light", "shirt", "shirt_dark", "pants", "pants_dark", "cavity",
          "bone", "bone_shade", "eye_white", "pupil", "glow", "mouth", "tooth", "hair",
          "stitch", "belt", "metal", "flesh", "pustule", "robe", "robe_dark", "robe_inner",
          "membrane", "membrane_dark", "glow2", "shoe", "tongue", "accent", "accent_dark", "nail"]
SW = {n: i for i, n in enumerate(LAYOUT)}
GLOW = {"glow", "glow2"}
GRID, CELL = 8, 16

def swatch_uv(i):
    return ((i % GRID + 0.5) / GRID, (i // GRID + 0.5) / GRID)

def make_palette(model, colours, path):
    W = GRID * CELL
    img = bpy.data.images.new(model.lower() + "_palette", W, W, alpha=False)
    px = [0.0] * (W * W * 4)
    for i in range(GRID * GRID):
        c = colours.get(LAYOUT[i], (0.5, 0.5, 0.5)) if i < len(LAYOUT) else (0.5, 0.5, 0.5)
        cx, cy = i % GRID, i // GRID
        for y in range(cy * CELL, cy * CELL + CELL):
            for x in range(cx * CELL, cx * CELL + CELL):
                k = (y * W + x) * 4
                px[k:k + 4] = [c[0], c[1], c[2], 1.0]
    img.pixels = px
    img.filepath_raw = path
    img.file_format = 'PNG'
    img.save()
    return img

def make_materials(model, img):
    mats = []
    for name, emis in (("M_" + model, 0.0), ("M_" + model + "_Glow", 4.0)):
        m = bpy.data.materials.new(name)
        m.use_nodes = True
        nt = m.node_tree
        bsdf = nt.nodes.get("Principled BSDF")
        tex = nt.nodes.new("ShaderNodeTexImage")
        tex.image = img
        tex.interpolation = 'Closest'
        nt.nodes.active = tex
        nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
        bsdf.inputs["Roughness"].default_value = 0.8
        if emis:
            nt.links.new(tex.outputs["Color"], bsdf.inputs["Emission Color"])
            bsdf.inputs["Emission Strength"].default_value = emis
        mats.append(m)
    return mats

# ---------------------------------------------------------------- parts
PARTS = []
BONES = {}  # name -> (head, tail) in world space at rest

class Part:
    def __init__(self, name, weights, recalc=True):
        self.name = name
        self.bm = bmesh.new()
        self.sw = self.bm.faces.layers.int.new("sw")
        self.weights = weights
        self.recalc = recalc
        PARTS.append(self)

    def v(self, co):
        return self.bm.verts.new(Vector(co))

    def face(self, verts, sw):
        f = self.bm.faces.new(verts)
        f[self.sw] = SW[sw]
        return f

    def tag(self, faces, sw):
        for f in faces:
            f[self.sw] = SW[sw]

    def recolor(self, pred, sw):
        """pred(face_centre, face_normal, current_swatch_name) -> bool"""
        self.bm.normal_update()
        for f in self.bm.faces:
            if pred(f.calc_center_median(), f.normal, LAYOUT[f[self.sw]]):
                f[self.sw] = SW[sw]

def _tag_verts(p, verts, sw):
    for f in {f for v in verts for f in v.link_faces}:
        f[p.sw] = SW[sw]

def ring(center, u, v, ru, rv, n, phase=0.0):
    c, u, v = Vector(center), Vector(u), Vector(v)
    return [c + u * (ru * math.cos(phase + 2 * math.pi * k / n)) + v * (rv * math.sin(phase + 2 * math.pi * k / n)) for k in range(n)]

def hring(z, rx, ry, n, cx=0.0, cy=0.0, phase=None):
    """horizontal ring, vertex 0 at the front (-Y)"""
    ph = -math.pi / 2 if phase is None else phase
    return [Vector((cx + rx * math.cos(ph + 2 * math.pi * k / n), cy + ry * math.sin(ph + 2 * math.pi * k / n), z)) for k in range(n)]

def loft(p, rings, sw, closed=True, cap0=True, cap1=True, sw_cap=None):
    grid = [[p.v(co) for co in r] for r in rings]
    faces = []
    n = len(rings[0])
    m = n if closed else n - 1
    for i in range(len(grid) - 1):
        for j in range(m):
            a, b = grid[i][j], grid[i][(j + 1) % n]
            c, d = grid[i + 1][(j + 1) % n], grid[i + 1][j]
            faces.append(p.face([a, b, c, d], sw))
    caps = []
    if closed and cap0:
        caps.append(p.face(grid[0][::-1], sw_cap or sw))
    if closed and cap1:
        caps.append(p.face(grid[-1], sw_cap or sw))
    return grid, faces, caps

def shell(p, faces, inward, inner_sw, rim_sw):
    fset = set(faces)
    vmap = {}
    for f in faces:
        for v in f.verts:
            if v not in vmap:
                vmap[v] = p.v(inward(v.co))
    new = []
    for f in faces:
        new.append(p.face([vmap[v] for v in reversed(f.verts)], inner_sw))
    for f in faces:
        for l in f.loops:
            e = l.edge
            lf = [x for x in e.link_faces if x in fset]
            if len(lf) == 1:
                a, b = l.vert, l.link_loop_next.vert
                new.append(p.face([b, a, vmap[a], vmap[b]], rim_sw))
    return new

def box(p, center, size, sw, bevel=0.0, rot=None, segs=1):
    mat = Matrix.Translation(Vector(center))
    if rot is not None:
        mat = mat @ (rot.to_4x4() if rot.__class__.__name__ == "Matrix" and len(rot) == 3 else rot)
    mat = mat @ Matrix.Diagonal((size[0], size[1], size[2], 1.0))
    r = bmesh.ops.create_cube(p.bm, size=1.0, matrix=mat)
    verts = r["verts"]
    if bevel > 0:
        edges = list({e for v in verts for e in v.link_edges})
        br = bmesh.ops.bevel(p.bm, geom=edges + verts, offset=bevel, segments=segs, affect='EDGES', profile=0.5, clamp_overlap=True)
        verts = list({v for f in br["faces"] for v in f.verts} | {v for v in verts if v.is_valid})
    _tag_verts(p, verts, sw)
    return verts

def ell(p, center, radii, useg, vseg, sw, rot=None):
    mat = Matrix.Translation(Vector(center))
    if rot is not None:
        mat = mat @ rot.to_4x4()
    mat = mat @ Matrix.Diagonal((radii[0], radii[1], radii[2], 1.0))
    r = bmesh.ops.create_uvsphere(p.bm, u_segments=useg, v_segments=vseg, radius=1.0, matrix=mat)
    _tag_verts(p, r["verts"], sw)
    return r["verts"]

def cyl(p, a, b, r1, r2, seg, sw, caps=True):
    a, b = Vector(a), Vector(b)
    d = b - a
    rot = Vector((0, 0, 1)).rotation_difference(d.normalized()).to_matrix().to_4x4()
    mat = Matrix.Translation((a + b) / 2) @ rot
    r = bmesh.ops.create_cone(p.bm, cap_ends=caps, cap_tris=False, segments=seg, radius1=r1, radius2=r2, depth=d.length, matrix=mat)
    _tag_verts(p, r["verts"], sw)
    return r["verts"]

def cone(p, a, b, r, seg, sw):
    """pointed cone from base a to tip b"""
    a, b = Vector(a), Vector(b)
    d = (b - a).normalized()
    u = d.orthogonal().normalized()
    w = d.cross(u)
    base = [p.v(a + u * (r * math.cos(2 * math.pi * k / seg)) + w * (r * math.sin(2 * math.pi * k / seg))) for k in range(seg)]
    tip = p.v(b)
    fs = [p.face([base[k], base[(k + 1) % seg], tip], sw) for k in range(seg)]
    fs.append(p.face(base[::-1], sw))
    return fs

def tube(p, pts, radii, seg, sw, cap0=True, cap1=True, up=Vector((0, 0, 1))):
    """lofted tube along a polyline"""
    rings = []
    for i, c in enumerate(pts):
        c = Vector(c)
        if i == 0:
            t = Vector(pts[1]) - c
        elif i == len(pts) - 1:
            t = c - Vector(pts[i - 1])
        else:
            t = Vector(pts[i + 1]) - Vector(pts[i - 1])
        t.normalize()
        u = t.cross(up)
        if u.length < 1e-4:
            u = t.orthogonal()
        u.normalize()
        w = u.cross(t)
        r = radii[i] if isinstance(radii, (list, tuple)) else radii
        rx, rz = (r, r) if not isinstance(r, tuple) else r
        rings.append([c + u * (rx * math.cos(2 * math.pi * k / seg)) + w * (rz * math.sin(2 * math.pi * k / seg)) for k in range(seg)])
    return loft(p, rings, sw, True, cap0, cap1)

def disc(p, center, rx, rz, n, sw, normal=Vector((0, -1, 0)), phase=0.0):
    """flat polygon facing `normal`"""
    c = Vector(center)
    nrm = Vector(normal).normalized()
    u = Vector((0, 0, 1)).cross(nrm)
    if u.length < 1e-4:
        u = Vector((1, 0, 0))
    u.normalize()
    w = nrm.cross(u)
    vs = [p.v(c + u * (rx * math.cos(phase + 2 * math.pi * k / n)) + w * (rz * math.sin(phase + 2 * math.pi * k / n))) for k in range(n)]
    f = p.face(vs, sw)
    f.normal_update()
    if f.normal.dot(nrm) < 0:
        f.normal_flip()
    return f

def stitch_line(p, a, b, n, sw, nrm, w=0.012, cross=0.05):
    """surface stitch: a thin bar a->b plus n cross ticks; nrm = surface normal"""
    a, b, nrm = Vector(a), Vector(b), Vector(nrm).normalized()
    d = b - a
    L = d.length
    R = frame(d.normalized(), nrm)
    box(p, (a + b) / 2, (w, L, w * 0.6), sw, 0, R)
    for k in range(n):
        t = (k + 0.5) / n
        box(p, a + d * t + nrm * 0.002, (cross, w * 0.9, w * 0.6), sw, 0, R)

def frame(ydir, zdir):
    y = ydir.normalized()
    x = y.cross(zdir).normalized()
    z = x.cross(y).normalized()
    return Matrix((x, y, z)).transposed()

# ---------------------------------------------------------------- weights
def rigid(b):
    return lambda co: {b: 1.0}

def ablend(axis, keys, s=1.0):
    """blend weight dicts along a world axis (0=x,1=y,2=z); s mirrors the axis (for x on the right side)"""
    keys = sorted(keys, key=lambda k: k[0])
    def fn(co):
        x = co[axis] * s
        if x <= keys[0][0]:
            return dict(keys[0][1])
        if x >= keys[-1][0]:
            return dict(keys[-1][1])
        for (x0, w0), (x1, w1) in zip(keys, keys[1:]):
            if x0 <= x <= x1:
                t = (x - x0) / (x1 - x0)
                t = t * t * (3 - 2 * t)
                out = {}
                for k in set(w0) | set(w1):
                    out[k] = w0.get(k, 0) * (1 - t) + w1.get(k, 0) * t
                return out
    return fn

def zblend(keys):
    return ablend(2, keys)

def seg_dist(co, b):
    h, t = BONES[b]
    d = t - h
    L2 = d.length_squared
    u = 0.0 if L2 < 1e-12 else max(0.0, min(1.0, (co - h).dot(d) / L2))
    return (co - (h + d * u)).length

def segw(bones, k=6.0, prune=0.04, bias=None):
    """smooth weights from distance to the listed bone segments"""
    def fn(co):
        w = {}
        for b in bones:
            d = seg_dist(co, b) + 0.01
            if bias and b in bias:
                d *= bias[b]
            w[b] = 1.0 / d ** k
        s = sum(w.values())
        w = {b: x / s for b, x in w.items()}
        w = {b: x for b, x in w.items() if x >= prune}
        s = sum(w.values())
        return {b: x / s for b, x in w.items()}
    return fn

def mix(fa, fb, t_fn):
    """blend two weight functions with factor t_fn(co) in [0,1] (0 = fa)"""
    def fn(co):
        t = max(0.0, min(1.0, t_fn(co)))
        a, b = fa(co), fb(co)
        out = {}
        for k in set(a) | set(b):
            out[k] = a.get(k, 0) * (1 - t) + b.get(k, 0) * t
        return out
    return fn

# ---------------------------------------------------------------- rig + assembly
def load_kaykit_rig(body, keep_actions=()):
    """import pristine KayKit Skeleton_<body>.fbx, delete its meshes/materials, keep chosen actions (rest pose)"""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.render.fps = 30
    bpy.ops.import_scene.fbx(filepath=KAYKIT + "Skeleton_%s.fbx" % body)
    rig = bpy.data.objects["Rig"]
    for o in list(bpy.data.objects):
        if o.type == 'MESH':
            me = o.data
            bpy.data.objects.remove(o)
            bpy.data.meshes.remove(me)
    for m in list(bpy.data.materials):
        bpy.data.materials.remove(m)
    for img in list(bpy.data.images):
        bpy.data.images.remove(img)
    rig.animation_data.action = None
    for a in list(bpy.data.actions):
        nm = a.name[4:] if a.name.startswith("Rig|") else a.name
        if nm in keep_actions:
            a.name = nm
            a.use_fake_user = True
        else:
            bpy.data.actions.remove(a)
    for pb in rig.pose.bones:
        pb.matrix_basis = Matrix.Identity(4)
    read_bones(rig)
    return rig

def read_bones(rig):
    BONES.clear()
    for b in rig.data.bones:
        BONES[b.name] = (rig.matrix_world @ b.head_local, rig.matrix_world @ b.tail_local)

def part_to_object(p, mats):
    bm = p.bm
    if p.recalc:
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    uv = bm.loops.layers.uv.new("UVMap")
    for f in bm.faces:
        u = swatch_uv(f[p.sw])
        for l in f.loops:
            l[uv].uv = u
        f.material_index = 1 if LAYOUT[f[p.sw]] in GLOW else 0
    me = bpy.data.meshes.new(p.name)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(p.name, me)
    bpy.context.scene.collection.objects.link(ob)
    for m in mats:
        me.materials.append(m)
    groups = {}
    for v in me.vertices:
        for b, val in p.weights(v.co).items():
            if val <= 1e-4:
                continue
            if b not in groups:
                groups[b] = ob.vertex_groups.new(name=b)
            groups[b].add([v.index], val, 'REPLACE')
    return ob

def assemble(model, rig, mats):
    objs = [part_to_object(p, mats) for p in PARTS]
    PARTS.clear()
    bpy.ops.object.select_all(action='DESELECT')
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.join()
    body = bpy.context.view_layer.objects.active
    body.name = model + "_Body"
    body.data.name = model + "_Body"
    body.data.transform(rig.matrix_world.inverted())
    body.parent = rig
    body.matrix_parent_inverse = Matrix.Identity(4)
    body.matrix_basis = Matrix.Identity(4)
    mod = body.modifiers.new("Armature", 'ARMATURE')
    mod.object = rig
    bpy.ops.object.select_all(action='DESELECT')
    body.select_set(True)
    bpy.context.view_layer.objects.active = body
    bpy.ops.object.shade_flat()
    # limit to 4 influences, then normalise
    bpy.ops.object.mode_set(mode='WEIGHT_PAINT')
    bpy.ops.object.vertex_group_limit_total(group_select_mode='ALL', limit=4)
    bpy.ops.object.vertex_group_normalize_all(lock_active=False)
    bpy.ops.object.mode_set(mode='OBJECT')
    return body

def tri_count(ob):
    return sum(len(p.vertices) - 2 for p in ob.data.polygons)

def check_weights(ob):
    bad = [v.index for v in ob.data.vertices if sum(g.weight for g in v.groups) < 0.99]
    return len(bad)

# ---------------------------------------------------------------- FBX export (same settings as the Reaper)
def patch_take_names():
    from io_scene_fbx import export_fbx_bin as _efb
    if getattr(_efb, "_sq_patched", False):
        return
    _orig = _efb.get_blenderID_name
    def _take_name(bid):
        if isinstance(bid, (tuple, list)) and len(bid) == 2 and isinstance(bid[1], bpy.types.Action):
            return bid[1].name
        return _orig(bid)
    _efb.get_blenderID_name = _take_name
    _efb._sq_patched = True

def export_fbx(path, objs, anims):
    patch_take_names()
    bpy.ops.object.select_all(action='DESELECT')
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.export_scene.fbx(
        filepath=path, use_selection=True, object_types={'ARMATURE', 'MESH'},
        apply_unit_scale=True, global_scale=1.0, apply_scale_options='FBX_SCALE_UNITS',
        axis_forward='-Z', axis_up='Y', use_space_transform=True, bake_space_transform=False,
        mesh_smooth_type='FACE', use_mesh_modifiers=False, add_leaf_bones=False,
        primary_bone_axis='Y', secondary_bone_axis='X', armature_nodetype='NULL',
        bake_anim=anims, bake_anim_use_all_bones=True, bake_anim_use_nla_strips=False,
        bake_anim_use_all_actions=True, bake_anim_force_startend_keying=True,
        bake_anim_step=1.0, bake_anim_simplify_factor=1.0, path_mode='STRIP')

def sbox(p, center, size, useg, vseg, sw, power=0.55, rot=None, taper=None):
    """soft cube (superellipsoid): uv sphere pushed toward a box. taper(zn)->(sx,sy) scales by height (-1..1)"""
    r = bmesh.ops.create_uvsphere(p.bm, u_segments=useg, v_segments=vseg, radius=1.0)
    c = Vector(center)
    R = rot.to_4x4() if rot is not None else Matrix.Identity(4)
    for v in r["verts"]:
        d = v.co.copy()
        q = Vector([math.copysign(abs(x) ** power, x) for x in d])
        q = Vector((q.x * size[0] / 2, q.y * size[1] / 2, q.z * size[2] / 2))
        if taper:
            sx, sy = taper(d.z)
            q.x *= sx; q.y *= sy
        v.co = c + (R @ q.to_4d()).to_3d()
    _tag_verts(p, r["verts"], sw)
    return r["verts"]

def slab(p, pts, offset, sw, side_sw=None):
    """closed prism from a polygon (pts) extruded by vector offset"""
    off = Vector(offset)
    a = [p.v(Vector(co)) for co in pts]
    b = [p.v(Vector(co) + off) for co in pts]
    n = len(pts)
    fs = [p.face(a, sw), p.face(b[::-1], sw)]
    for k in range(n):
        fs.append(p.face([a[(k + 1) % n], a[k], b[k], b[(k + 1) % n]], side_sw or sw))
    return fs
