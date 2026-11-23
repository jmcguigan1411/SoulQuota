import bpy
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath="/home/claude/sq/ArtSource/Characters/Zombie.fbx")
o=bpy.data.objects["Zombie_Body"]
from collections import Counter
print([m.name for m in o.data.materials], Counter(p.material_index for p in o.data.polygons))
