# Soul Quota - small clipboard prop for Reaper_Taunt_Clipboard (static mesh, attach to handslot.l)
# board extends along +Z from the grip (bottom edge), paper faces +Y
import bpy, math, sys
sys.path.insert(0, "/home/claude/blender_work")
import sq_build as B
from mathutils import Vector
OUT = "/home/claude/sq/ArtSource/Characters/"
def build():
    p = B.Part("Reaper_Clipboard", lambda co: {})
    B.box(p, (0, 0, 0.15), (.25, .018, .34), "wood", 0.01)
    B.box(p, (0, 0.01, 0.14), (.21, .006, .28), "card", 0.0)
    B.box(p, (0, 0.012, 0.25), (.212, .006, .045), "card_red", 0.0)
    for k, z in enumerate((0.19, 0.145, 0.10, 0.055)):
        B.box(p, (-0.02 + 0.01 * (k % 2), 0.0135, z), (.11 - 0.02 * (k % 2), .004, .01), "ink", 0.0)
        B.box(p, (0.06, 0.0135, z), (.018, .004, .018), "ink" if k != 2 else "card_red", 0.0)
    B.box(p, (0, 0.012, 0.315), (.09, .02, .035), "metal", 0.006)
    B.cyl(p, (0, 0.022, 0.295), (0, 0.022, 0.33), .012, .012, 5, "gold")
    return p
bpy.ops.wm.read_factory_settings(use_empty=True)
img = bpy.data.images.load(OUT + "reaper_palette.png")
mats = B.make_materials(img)
ob = B.part_to_object(build(), None, mats)
bpy.context.view_layer.objects.active = ob; ob.select_set(True)
bpy.ops.object.shade_flat()
print("CLIP_TRIS", sum(len(f.vertices) - 2 for f in ob.data.polygons))
bpy.ops.wm.save_as_mainfile(filepath=OUT + "Reaper_Clipboard.blend")
bpy.ops.export_scene.fbx(filepath=OUT + "Reaper_Clipboard.fbx", use_selection=True, object_types={'MESH'},
    apply_unit_scale=True, global_scale=1.0, apply_scale_options='FBX_SCALE_NONE', mesh_smooth_type='FACE', bake_anim=False, path_mode='STRIP')
