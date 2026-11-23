# Chase-camera contact sheets / GIF frames on the real monster meshes (R_<Id>.blend + actions from Reaper_Custom.blend).
#   python3 sq_preview7.py <out.jpg> <tile> <cols> row [row...]     row = Label|Id|before/after|Action   (cols frames per row)
#   gif mode: python3 sq_preview7.py --gif <out.gif> <tile> row [row...]  (all rows side by side, every frame)
import bpy, sys, math, os
from mathutils import Vector
CH = "/home/claude/sq/ArtSource/Characters/"
SRC = {"after": CH + "Reaper_Custom.blend", "before": "/home/claude/blender_work/backup/Reaper_Custom_pre7.blend"}
args = sys.argv[1:]
gif = args[0] == "--gif"
if gif:
    args = args[1:]
out, tile = args[0], int(args[1])
cols = 0 if gif else int(args[2])
rows = [r.split("|") for r in args[(2 if gif else 3):]]
tmp = "/tmp/a7/"
shots = []
for ri, (label, rid, which, action) in enumerate(rows):
    bpy.ops.wm.open_mainfile(filepath=CH + "R_%s.blend" % rid)
    with bpy.data.libraries.load(SRC[which]) as (src, dst):
        if action not in src.actions:
            print("MISSING", action, which); continue
        dst.actions = [action]
    act = bpy.data.actions[action]
    rig = bpy.data.objects["Rig"]
    if not rig.animation_data:
        rig.animation_data_create()
    rig.animation_data.action = act
    if len(act.slots):
        rig.animation_data.action_slot = act.slots[0]
    N = int(act.frame_range[1])
    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_WORKBENCH'
    sh = sc.display.shading
    sh.light = 'STUDIO'; sh.color_type = 'TEXTURE'; sh.show_shadows = True; sh.show_object_outline = True
    sc.render.resolution_x = tile; sc.render.resolution_y = tile; sc.render.film_transparent = False
    sc.world = sc.world or bpy.data.worlds.new("W")
    sc.display.shading.background_type = 'VIEWPORT'; sc.display.shading.background_color = (0.16, 0.14, 0.2)
    bpy.ops.mesh.primitive_plane_add(size=8, location=(0, 0, 0))
    cam_d = bpy.data.cameras.new("C"); cam = bpy.data.objects.new("C", cam_d); sc.collection.objects.link(cam)
    cam.location = Vector((0.9, 2.9, 2.0))           # behind (+Y) and above, slightly to one side: the chase view
    tgt = Vector((0, -0.15, 0.55))
    cam.rotation_euler = (tgt - cam.location).to_track_quat('-Z', 'Y').to_euler()
    cam_d.lens = 38
    sc.camera = cam
    frames = list(range(1, N + 1)) if gif else [max(1, min(N, round(1 + (N - 1) * i / (cols - 1)))) for i in range(cols)]
    for f in frames:
        sc.frame_set(f)
        p = tmp + "r%d_f%03d.png" % (ri, f)
        sc.render.filepath = p
        bpy.ops.render.render(write_still=True)
        shots.append((ri, f, p, "%s  %s (%d f)" % (label, action, N)))
from PIL import Image, ImageDraw, ImageEnhance
_open = Image.open
def _bright(p):
    return ImageEnhance.Brightness(_open(p).convert("RGB")).enhance(1.7)
if gif:
    per = {}
    for ri, f, p, lab in shots:
        per.setdefault(ri, []).append((f, p, lab))
    L = max(len(v) for v in per.values())
    ims = []
    for i in range(L):
        fr = Image.new("RGB", (tile * len(per), tile + 16), (20, 18, 26))
        d = ImageDraw.Draw(fr)
        for c, ri in enumerate(sorted(per)):
            v = per[ri]
            f, p, lab = v[min(i, len(v) - 1)]
            fr.paste(_bright(p), (c * tile, 16))
            d.text((c * tile + 4, 2), lab[:int(tile / 6.5)], fill=(255, 220, 150))
        ims.append(fr)
    ims[0].save(out, save_all=True, append_images=ims[1:], duration=50, loop=0)
else:
    nr = len(rows)
    sheet = Image.new("RGB", (tile * cols, (tile + 18) * nr), (20, 18, 26))
    d = ImageDraw.Draw(sheet)
    seen = {}
    for ri, f, p, lab in shots:
        c = seen.get(ri, 0); seen[ri] = c + 1
        sheet.paste(_bright(p), (c * tile, ri * (tile + 18) + 18))
        if c == 0:
            d.text((4, ri * (tile + 18) + 3), lab, fill=(150, 255, 190) if lab.startswith("AFTER") or lab.startswith("NEW") else (255, 200, 140))
        d.text((c * tile + 4, ri * (tile + 18) + 20), "f%d" % f, fill=(230, 230, 230))
    sheet.save(out, quality=88)
print("WROTE", out)
