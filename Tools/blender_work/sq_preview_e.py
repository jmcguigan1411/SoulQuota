# Review renders on the real monster meshes. python3 sq_preview8.py [--gif] <out> <tile> <cols> row...
#   row = Label|Id|src|Action|cam     src: new | pre8 | pre7     cam: chase | front
import bpy, sys, math, os
from mathutils import Vector, Matrix
CH = "/home/claude/sq/ArtSource/Characters/"
SRC = {"new": "/tmp/enemy8.blend", "old": "/home/claude/blender_work/sq_enemy_anims.blend", "pre8": "/home/claude/blender_work/backup/Reaper_Custom_pre8.blend",
       "pre7": "/home/claude/blender_work/backup/Reaper_Custom_pre7.blend"}
WLEN = {"Grim": 150, "Mort": 68, "Bonesmith": 133, "Intern": 125, "Femur": 95, "Collector": 117, "Beak": 170, "Vexa": 63, "Actuary": 60,
        "Solace": 68, "Pallbearers": 104, "Ectoplasm": 53, "Neckless": 109, "Tickwell": 172}
args = sys.argv[1:]
gif = args[0] == "--gif"
if gif: args = args[1:]
out, tile, cols = args[0], int(args[1]), int(args[2])
rows = [r.split("|") for r in args[3:]]
tmp = "/tmp/a8/p%d_" % os.getpid()
shots = []
for ri, (label, rid, which, action, camn) in enumerate(rows):
    bpy.ops.wm.open_mainfile(filepath=CH + "%s.blend" % rid)
    with bpy.data.libraries.load(SRC[which]) as (src, dst):
        if action not in src.actions:
            print("MISSING", action, which); continue
        dst.actions = [action]
    act = bpy.data.actions[action]
    rig = bpy.data.objects["Rig"]
    if not rig.animation_data: rig.animation_data_create()
    rig.animation_data.action = act
    if len(act.slots): rig.animation_data.action_slot = act.slots[0]
    w = bpy.data.objects.get("W_" + rid)
    if w:
        L = max(w.dimensions)
        s = (WLEN.get(rid, 100) / 100.0) / max(0.01, L)
        w.parent = rig; w.parent_type = 'BONE'; w.parent_bone = "handslot.r"
        w.matrix_parent_inverse = Matrix.Identity(4)
        w.location = (0, -rig.data.bones["handslot.r"].length, 0); w.rotation_euler = (math.radians(-90), 0, 0); w.scale = (s, s, s)
    N = int(act.frame_range[1])
    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_WORKBENCH'
    sh = sc.display.shading
    sh.light = 'STUDIO'; sh.color_type = 'TEXTURE'; sh.show_shadows = True; sh.show_object_outline = True
    sc.render.resolution_x = tile; sc.render.resolution_y = tile
    sh.background_type = 'VIEWPORT'; sh.background_color = (0.16, 0.14, 0.2)
    bpy.ops.mesh.primitive_grid_add(size=10, x_subdivisions=20, y_subdivisions=20, location=(0, 0, 0))
    g = bpy.context.object; m = g.modifiers.new("w", "WIREFRAME"); m.thickness = 0.01; m.use_replace = False
    cam_d = bpy.data.cameras.new("C"); cam = bpy.data.objects.new("C", cam_d); sc.collection.objects.link(cam)
    if camn == "enemy":      # how the player sees the horde: from in front and well above
        cam.location = Vector((1.3, -4.8, 2.0)); tgt = Vector((0, 0, 0.6))
    elif camn == "front":
        cam.location = Vector((1.9, -2.6, 1.3)); tgt = Vector((0, 0, 0.6))
    elif camn == "side":
        cam.location = Vector((3.4, 0.0, 0.9)); tgt = Vector((0, 0, 0.6))
    else:  # chase: behind (+Y) and above, 18 deg down, framed closer than the game's 8.5 m so poses can be judged
        d = 3.6; a = math.radians(22)
        cam.location = Vector((0.5, d * math.cos(a), 0.6 + d * math.sin(a))); tgt = Vector((0, -0.1, 0.6))
    cam.rotation_euler = (tgt - cam.location).to_track_quat('-Z', 'Y').to_euler()
    cam_d.lens = 40
    sc.camera = cam
    frames = list(range(1, N + (0 if act.get("sq_loop") else 1))) if gif else [max(1, min(N, round(1 + (N - 1) * i / (cols - 1)))) for i in range(cols)]
    for f in frames:
        sc.frame_set(f)
        p = tmp + "r%d_f%03d.png" % (ri, f)
        sc.render.filepath = p
        bpy.ops.render.render(write_still=True)
        shots.append((ri, f, p, "%s %s (%df) %s" % (label, action, N, camn)))
from PIL import Image, ImageDraw, ImageEnhance
def _b(p): return ImageEnhance.Brightness(Image.open(p).convert("RGB")).enhance(1.7)
if gif:
    per = {}
    for ri, f, p, lab in shots: per.setdefault(ri, []).append((f, p, lab))
    L = max(len(v) for v in per.values()); ims = []
    ncol = cols if cols > 0 else len(per)
    nrow = (len(per) + ncol - 1) // ncol
    for i in range(L):
        fr = Image.new("RGB", (tile * ncol, (tile + 16) * nrow), (20, 18, 26)); d = ImageDraw.Draw(fr)
        for c, ri in enumerate(sorted(per)):
            v = per[ri]; f, p, lab = v[i % len(v)] if v[0][2] else v[min(i, len(v) - 1)]
            x, y = (c % ncol) * tile, (c // ncol) * (tile + 16)
            fr.paste(_b(p), (x, y + 16)); d.text((x + 4, y + 2), lab[:int(tile / 6.3)], fill=(255, 220, 150))
        ims.append(fr.quantize(96))
    ims[0].save(out, save_all=True, append_images=ims[1:], duration=33, loop=0)
else:
    nr = len(rows)
    sheet = Image.new("RGB", (tile * cols, (tile + 16) * nr), (20, 18, 26)); d = ImageDraw.Draw(sheet); seen = {}
    for ri, f, p, lab in shots:
        c = seen.get(ri, 0); seen[ri] = c + 1
        sheet.paste(_b(p), (c * tile, ri * (tile + 16) + 16))
        if c == 0: d.text((4, ri * (tile + 16) + 2), lab, fill=(150, 255, 190))
        d.text((c * tile + 3, ri * (tile + 16) + 18), "f%d" % f, fill=(230, 230, 230))
    sheet.save(out, quality=85)
for ri, f, p, lab in shots:
    try: os.remove(p)
    except OSError: pass
print("WROTE", out)
