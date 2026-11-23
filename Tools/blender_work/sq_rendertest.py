import bpy, math, mathutils, time
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath="/mnt/user-data/outputs/ArtSource/Characters/Skeleton_Mage.fbx")
bpy.ops.import_scene.fbx(filepath="/mnt/user-data/outputs/ArtSource/Characters/Skeleton_Axe.fbx")
rig=bpy.data.objects['Rig']; axe=bpy.data.objects['Skeleton_Axe']
rig.animation_data.action=bpy.data.actions['Rig|Idle']
if hasattr(rig.animation_data,'action_slot') and rig.animation_data.action_slot is None:
    rig.animation_data.action_slot=bpy.data.actions['Rig|Idle'].slots[0]
axe.parent=rig; axe.parent_type='BONE'; axe.parent_bone='handslot.r'
# bone parent places at tail; we want at head: offset -length along Y
axe.matrix_parent_inverse=mathutils.Matrix.Identity(4)
axe.location=(0,-rig.data.bones['handslot.r'].length,0)
sc=bpy.context.scene; sc.frame_set(10)
cam=bpy.data.cameras.new('c'); co=bpy.data.objects.new('cam',cam); sc.collection.objects.link(co); sc.camera=co
co.location=(2.5,-3.5,2.6); 
d=mathutils.Vector((0,0,0.9))-co.location; co.rotation_euler=d.to_track_quat('-Z','Y').to_euler()
cam.lens=45
l=bpy.data.lights.new('s','SUN'); lo=bpy.data.objects.new('s',l); sc.collection.objects.link(lo); lo.rotation_euler=(0.8,0.2,0.6); l.energy=3
w=bpy.data.worlds.new('w'); sc.world=w; w.use_nodes=True; w.node_tree.nodes['Background'].inputs[0].default_value=(0.05,0.05,0.07,1)
sc.render.resolution_x=sc.render.resolution_y=400
for eng in ['BLENDER_EEVEE','BLENDER_WORKBENCH']:
    try:
        sc.render.engine=eng; t=time.time()
        sc.render.filepath=f"/tmp/test_{eng}.png"; bpy.ops.render.render(write_still=True); print("OK",eng,time.time()-t)
    except Exception as e: print("FAIL",eng,e)
