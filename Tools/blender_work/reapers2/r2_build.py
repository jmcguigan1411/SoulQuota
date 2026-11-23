# Build, save and export batch-2 reapers. usage: python3 r2_build.py Femur Digby ...
import bpy, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r2_lib import *
import r2_chars

def build(rid):
    model = "R_" + rid
    rig = load_reaper_rig()
    img = make_palette(model, r2_chars.PALETTES[rid], CH + "r_%s_palette.png" % rid.lower())
    mats = make_materials(model, img)
    r2_chars.BUILDERS[rid]()
    body = assemble(model, rig, mats)
    zs = [(rig.matrix_world @ v.co).z for v in body.data.vertices]
    print("MODEL", model, "TRIS", tri_count(body), "VERTS", len(body.data.vertices), "UNWEIGHTED", check_weights(body),
          "HEIGHT_CM %.1f" % ((max(zs) - min(zs)) * 100), "ZMIN %.3f" % min(zs), "GROUPS", sorted(g.name for g in body.vertex_groups))
    img.filepath = CH + "r_%s_palette.png" % rid.lower()
    bpy.ops.wm.save_as_mainfile(filepath=CH + model + ".blend", relative_remap=True)
    export_fbx(CH + model + ".fbx", [rig, body], anims=False)
    print("EXPORTED", CH + model + ".fbx")

if __name__ == "__main__":
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    for rid in (args or list(r2_chars.BUILDERS)):
        build(rid)
