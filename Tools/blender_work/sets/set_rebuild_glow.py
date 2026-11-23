# Re-export the Set_* landmark pieces with their glowing swatches (windows, lantern heads) on a second material slot
# named M_SetGlow, so SQArt::ApplyGlowSlots can light them. Geometry and hulls are unchanged. usage: python3 set_rebuild_glow.py
import sys
sys.path.insert(0, "/home/claude/blender_work/sets"); sys.path.insert(0, "/home/claude/blender_work/park")
import bpy
import set_build as sb
pb = sb.pb
def glow_slot(ob, layout):
    gm = bpy.data.materials.get("M_SetGlow") or bpy.data.materials.new("M_SetGlow")
    if len(ob.data.materials) < 2:
        ob.data.materials.append(gm)
    uv = ob.data.uv_layers[0].data
    n = 0
    for poly in ob.data.polygons:
        u, v = uv[poly.loop_indices[0]].uv
        idx = int(u * 8) + int(v * 8) * 8
        if idx < len(layout) and layout[idx].startswith("glow"):
            poly.material_index = 1; n += 1
    return n
for fn in sb.PIECES:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    img = bpy.data.images.load(sb.PAL)
    mat = pb.make_material(img)
    P = fn()
    ob, hulls, off = pb.build_objects(P, mat)
    n = glow_slot(ob, sb.LAYOUT)
    pb.export(ob, hulls, sb.OUT + P.name + ".fbx")
    print("SET", P.name, "glow faces", n)
