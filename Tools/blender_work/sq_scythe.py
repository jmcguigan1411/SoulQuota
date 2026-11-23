# Soul Quota - reaper scythe (static mesh, pivot at the grip, pole along +Z, blade toward +Y)
import bpy, bmesh, math, sys
sys.path.insert(0, "/home/claude/blender_work")
import sq_build as B
from mathutils import Vector, Matrix

OUT = "/home/claude/sq/ArtSource/Characters/"

def bez(p0, p1, p2, p3, t):
    u = 1 - t
    return p0 * u ** 3 + p1 * 3 * u * u * t + p2 * 3 * u * t * t + p3 * t ** 3

def build():
    p = B.Part("Reaper_Scythe", lambda co: {})
    # pole
    B.cyl(p, (0, 0, -0.40), (0, 0, 1.12), .033, .03, 6, "wood")
    # grip wrap + bands
    B.cyl(p, (0, 0, -0.13), (0, 0, 0.15), .04, .04, 6, "rope")
    for z in (-0.15, 0.17):
        B.cyl(p, (0, 0, z - 0.018), (0, 0, z + 0.018), .045, .045, 6, "wood_dark")
    # pommel
    B.ellipsoid(p, (0, 0, -0.43), (.052, .052, .055), 6, 4, "metal")
    # collar + cap
    B.box(p, (0, 0.01, 1.10), (.085, .11, .14), "metal", 0.012)
    B.ellipsoid(p, (0, 0.0, 1.19), (.045, .045, .04), 6, 4, "metal")
    B.cyl(p, (0, 0, 0.995), (0, 0, 1.03), .05, .05, 6, "gold")
    # soul gems on both sides of the collar
    for s in (1, -1):
        vs = B.ellipsoid(p, (0.045 * s, 0.015, 1.10), (.02, .03, .03), 4, 2, "glow")
    # blade
    P0, P1, P2, P3 = Vector((0, 0.03, 1.13)), Vector((0, 0.42, 1.24)), Vector((0, 0.86, 1.10)), Vector((0, 1.00, 0.80))
    N = 11
    rings = []
    for i in range(N):
        t = i / (N - 1)
        S = bez(P0, P1, P2, P3, t)
        S2 = bez(P0, P1, P2, P3, min(1, t + 0.01)) if t < 1 else S
        S1 = bez(P0, P1, P2, P3, max(0, t - 0.01))
        tang = (S2 - S1).normalized()
        nrm = Vector((1, 0, 0)).cross(tang).normalized()  # points to concave/down side
        if nrm.z > 0 and t < 0.3:
            nrm = -nrm
        w = 0.20 * (1 - t) ** 0.75 + 0.004
        th = 0.024 * (1 - t) + 0.006
        E = S + nrm * w
        M = S + nrm * (w * 0.45)
        rings.append([S + Vector((th * 0.6, 0, 0)), M + Vector((th, 0, 0)), E, M - Vector((th, 0, 0)), S - Vector((th * 0.6, 0, 0))])
    grid = [[p.v(c) for c in r] for r in rings]
    for i in range(N - 1):
        for j in range(5):
            a, b = grid[i][j], grid[i][(j + 1) % 5]
            c, d = grid[i + 1][(j + 1) % 5], grid[i + 1][j]
            sw = "edge" if j in (1, 2) else "steel"
            if j == 4:
                sw = "metal"  # spine strip
            p.face([a, b, c, d], sw)
    p.face(grid[0][::-1], "metal")
    p.face(grid[-1], "edge")
    # hanging quota tag (behind the pole, flat in the YZ plane)
    B.cyl(p, (0, -0.045, 1.04), (0, -0.075, 0.93), .006, .006, 4, "rope")
    tagc = Vector((0, -0.085, 0.86))
    rot = Matrix.Rotation(math.radians(8), 3, 'X')
    B.box(p, tagc, (.012, .085, .12), "card", 0.006, rot)
    B.box(p, tagc + rot @ Vector((0, 0, 0.038)), (.016, .088, .028), "card_red", 0.0, rot)
    for z in (-0.005, -0.028):
        B.box(p, tagc + rot @ Vector((0, 0, z)), (.016, .055, .008), "ink", 0.0, rot)
    return p

def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    img = bpy.data.images.load(OUT + "reaper_palette.png")
    mats = B.make_materials(img)
    p = build()
    ob = B.part_to_object(p, None, mats)
    bpy.context.view_layer.objects.active = ob
    ob.select_set(True)
    bpy.ops.object.shade_flat()
    tris = sum(len(f.vertices) - 2 for f in ob.data.polygons)
    print("SCYTHE_TRIS", tris, "dims", tuple(round(x, 3) for x in ob.dimensions))
    bpy.ops.wm.save_as_mainfile(filepath=OUT + "Reaper_Scythe.blend")
    bpy.ops.export_scene.fbx(filepath=OUT + "Reaper_Scythe.fbx", use_selection=True, object_types={'MESH'},
                             apply_unit_scale=True, global_scale=1.0, apply_scale_options='FBX_SCALE_NONE',
                             mesh_smooth_type='FACE', add_leaf_bones=False, bake_anim=False, path_mode='STRIP')
    print("EXPORTED scythe")

if __name__ == "__main__":
    main()
