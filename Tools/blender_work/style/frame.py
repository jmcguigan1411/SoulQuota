# Style frame for the "Midnight Fairground" direction: a Blender mock of the target look (not an Unreal screenshot).
import bpy, math, random, os, sys
from mathutils import Vector
random.seed(7)
ARM=float(os.environ.get('ARM','1.1'))
RES=float(os.environ.get('RES','1'))
U='/mnt/user-data/uploads/Projects/SoulQuota/ArtSource/Props/'
C='/home/claude/sq/ArtSource/Characters/'
bpy.ops.wm.read_factory_settings(use_empty=True)
sc=bpy.context.scene
def srgb(h):
    h=h.lstrip('#'); v=[int(h[i:i+2],16)/255 for i in (0,2,4)]
    return tuple(((c/12.92) if c<=0.04045 else ((c+0.055)/1.055)**2.4) for c in v)+(1,)
def mat(name,col,emit=0.0,rough=0.9):
    m=bpy.data.materials.new(name); m.use_nodes=True
    b=m.node_tree.nodes['Principled BSDF']; b.inputs['Base Color'].default_value=col; b.inputs['Roughness'].default_value=rough
    if emit>0:
        b.inputs['Emission Color'].default_value=col; b.inputs['Emission Strength'].default_value=emit
    return m
def texmat(name,png,rim=(0.45,0.75,1.0),rimamt=0.6):
    m=bpy.data.materials.new(name); m.use_nodes=True; nt=m.node_tree
    b=nt.nodes['Principled BSDF']; t=nt.nodes.new('ShaderNodeTexImage'); t.image=bpy.data.images.load(png); t.interpolation='Closest'
    nt.links.new(t.outputs['Color'],b.inputs['Base Color']); b.inputs['Roughness'].default_value=0.85
    lw=nt.nodes.new('ShaderNodeLayerWeight'); lw.inputs['Blend'].default_value=0.35
    mx=nt.nodes.new('ShaderNodeMixRGB'); mx.blend_type='MULTIPLY'; mx.inputs['Fac'].default_value=1
    mx.inputs['Color1'].default_value=rim+(1,); nt.links.new(lw.outputs['Facing'],mx.inputs['Color2'])
    nt.links.new(mx.outputs['Color'],b.inputs['Emission Color']); b.inputs['Emission Strength'].default_value=rimamt
    return m
# world: gradient sky
w=bpy.data.worlds.new('W'); sc.world=w; w.use_nodes=True; nt=w.node_tree
bg=nt.nodes['Background']; tc=nt.nodes.new('ShaderNodeTexCoord'); sep=nt.nodes.new('ShaderNodeSeparateXYZ')
ramp=nt.nodes.new('ShaderNodeValToRGB'); nt.links.new(tc.outputs['Generated'],sep.inputs[0]); nt.links.new(sep.outputs['Z'],ramp.inputs[0])
e=ramp.color_ramp.elements; e[0].position=0.0; e[0].color=srgb('#C2418F'); e[1].position=0.55; e[1].color=srgb('#0B0A2A')
m=ramp.color_ramp.elements.new(0.16); m.color=srgb('#4B2A8C')
nt.links.new(ramp.outputs[0],bg.inputs[0]); bg.inputs[1].default_value=1.0
# ground
bpy.ops.mesh.primitive_plane_add(size=400); g=bpy.context.object
gm=bpy.data.materials.new('ground'); gm.use_nodes=True; nt=gm.node_tree; b=nt.nodes['Principled BSDF']
n=nt.nodes.new('ShaderNodeTexNoise'); n.inputs['Scale'].default_value=0.035; n.inputs['Detail'].default_value=1
r=nt.nodes.new('ShaderNodeValToRGB'); r.color_ramp.interpolation='CONSTANT'
el=r.color_ramp.elements; el[0].position=0; el[0].color=srgb('#2E8F7A'); el[1].position=0.42; el[1].color=srgb('#3F9A4A')
x=r.color_ramp.elements.new(0.56); x.color=srgb('#8FD05A')
nt.links.new(n.outputs['Fac'],r.inputs[0]); nt.links.new(r.outputs[0],b.inputs['Base Color']); b.inputs['Roughness'].default_value=0.95
g.data.materials.append(gm)
# path strip
bpy.ops.mesh.primitive_plane_add(size=1,location=(0,18,0.02)); p=bpy.context.object; p.scale=(3.2,60,1); p.data.materials.append(mat('path',srgb('#B07A4A')))
def imp(f):
    before=set(bpy.data.objects); bpy.ops.import_scene.fbx(filepath=f); return [o for o in bpy.data.objects if o not in before]
prop_m=texmat('props',U+'halloweenbits_texture.png')
def place(f,loc,rot=0,s=1,m=None):
    obs=imp(U+f)
    for o in obs:
        if o.type=='MESH':
            o.data.materials.clear(); o.data.materials.append(m or prop_m)
        if o.parent is None:
            o.location=loc; o.rotation_euler[2]=rot; o.scale=[c*s for c in o.scale]
    return obs
# kit pieces are metres already?  measure once
t=place('gravestone.fbx',(0,0,0)); bpy.context.view_layer.update()
h=max((o.matrix_world@Vector(c)).z for o in t if o.type=='MESH' for c in o.bound_box); print('gravestone h',h)
K=1.4/max(h,1e-3)
for o in t:
    if o.parent is None: o.scale=[c*K for c in o.scale]; o.location=(-5,6,0); o.rotation_euler[2]=0.3
for i,(x,y) in enumerate([(-7.5,9),(-5.5,12),(-8,15),(6,8),(8.5,11),(6.5,14.5),(9,18)]):
    place('gravestone.fbx' if i%2 else 'grave_A.fbx',(x,y,0),random.uniform(-0.4,0.4)+(3.14 if x>0 else 0),K)
place('crypt.fbx',(-14,24,0),0.5,K); place('crypt.fbx',(15,30,0),-0.6,K)
for x,y in [(-4.2,5),(4.2,12),(-4.2,20),(4.2,28)]:
    place('post_lantern.fbx',(x,y,0),0,K)
    bpy.ops.object.light_add(type='POINT',location=(x,y,2.4)); L=bpy.context.object; L.data.energy=900; L.data.color=srgb('#FFA53A')[:3]; L.data.shadow_soft_size=0.4
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.16,location=(x,y,2.3)); bpy.context.object.data.materials.append(mat('flame',srgb('#FFA53A'),14))
for x,y,s in [(-12,14,1.2),(12,20,1.4),(-18,34,1.6),(20,40,1.5),(-3,44,1.3)]:
    place('tree_dead_large.fbx',(x,y,0),random.uniform(0,6),K*s)
# skyline: hills + giant silhouettes
sil=mat('sil',srgb('#140F2E')); far=mat('far',srgb('#5A2A70'))
for ring,(R,mm,hh) in enumerate([(120,sil,10),(190,far,22)]):
    for k in range(14):
        a=math.radians(40+k*100/14*1.0+ring*4)
        bpy.ops.mesh.primitive_uv_sphere_add(radius=1,location=(R*math.cos(a),R*math.sin(a),-hh*0.25)); o=bpy.context.object
        o.scale=(random.uniform(28,45)*(1+ring*0.8),random.uniform(20,30),random.uniform(hh*0.7,hh*1.3)); o.data.materials.append(mm)
        if ring==0:
            place('tree_dead_large.fbx' if k%3 else 'crypt.fbx',(R*math.cos(a)+random.uniform(-8,8),R*math.sin(a),o.scale[2]*0.6),random.uniform(0,6),K*random.uniform(2.5,4.5),sil)
# moon
bpy.ops.mesh.primitive_uv_sphere_add(radius=26,location=(-60,330,62)); bpy.context.object.data.materials.append(mat('moon',(0.8,0.92,1,1),3.0))
# pickups: xp crystals + coins
xp=mat('xp',srgb('#2BFF5E'),1.6); gold=mat('gold',srgb('#FFC61A'),1.6); soul=mat('soul',srgb('#3DF5E0'),2.5)
for i in range(9):
    bpy.ops.mesh.primitive_cone_add(vertices=6,radius1=0.16,depth=0.5,location=(random.uniform(-3,3),random.uniform(6,16),0.35)); o=bpy.context.object; o.rotation_euler=(0.4,0.3,random.uniform(0,6)); o.data.materials.append(xp)
for i in range(4):
    bpy.ops.mesh.primitive_cylinder_add(radius=0.2,depth=0.05,location=(random.uniform(-3,3),random.uniform(7,14),0.3)); o=bpy.context.object; o.rotation_euler=(1.3,0,random.uniform(0,6)); o.data.materials.append(gold)
for i in range(3):
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.2,location=(random.uniform(-2.5,2.5),random.uniform(9,13),0.7)); bpy.context.object.data.materials.append(soul)
# characters
def char(fbx,png,loc,rot,rim):
    ZX=float(os.environ.get('ZX','1.25')) if 'Zombie' in fbx else 0.0
    obs=imp(C+fbx); m=texmat(fbx,C+png,rim,0.9)
    zmax=0
    bpy.context.view_layer.update()
    for o in obs:
        if o.type=='MESH':
            o.data.materials.clear(); o.data.materials.append(m)
            zmax=max(zmax,max((o.matrix_world@Vector(c)).z for c in o.bound_box))
    for o in obs:
        if o.type=='ARMATURE':
            for pb in o.pose.bones:
                n=pb.name.lower()
                if 'upperarm' in n:
                    pb.rotation_mode='XYZ'; sign=1 if n.endswith(('l','left')) else -1
                    pb.rotation_euler=(ZX,0,sign*(0.25 if ZX else ARM))
    s=1.9/max(zmax,1e-3)
    for o in obs:
        if o.parent is None: o.scale=[c*s for c in o.scale]; o.location=loc; o.rotation_euler[2]=rot
try:
    char('R_Grim.fbx','r_grim_palette.png',(0,3.2,0),3.14,(0.45,0.75,1.0))
    for i in range(7):
        char('Zombie.fbx','zombie_palette.png',(random.uniform(-6,6),random.uniform(12,24),0),random.uniform(-0.5,0.5),(1.0,0.35,0.3))
except Exception as ex: print('char fail',ex)
# lights
bpy.ops.object.light_add(type='SUN',rotation=(math.radians(55),0,math.radians(200))); s=bpy.context.object; s.data.energy=5.0; s.data.color=srgb('#9FD0FF')[:3]; s.data.angle=0.05
# camera: chase cam behind the reaper
bpy.ops.object.camera_add(location=(0,-3.6,3.3)); cam=bpy.context.object; cam.data.lens=22
d=Vector((0,14,2.2))-cam.location; cam.rotation_euler=d.to_track_quat('-Z','Y').to_euler(); sc.camera=cam
sc.render.engine='BLENDER_EEVEE' if 'BLENDER_EEVEE' in [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items] else 'BLENDER_EEVEE_NEXT'
sc.render.resolution_x=int(1280*RES); sc.render.resolution_y=int(720*RES)
sc.view_settings.view_transform='Standard'; sc.view_settings.look='None'
ink = '--ink' in sys.argv
if ink:
    sc.render.use_freestyle=True; sc.render.line_thickness=1.6
    vl=sc.view_layers[0]; vl.use_freestyle=True
    ls=vl.freestyle_settings.linesets[0] if vl.freestyle_settings.linesets else vl.freestyle_settings.linesets.new('ink')
    ls.select_silhouette=True; ls.select_border=True; ls.select_crease=True
    if ls.linestyle is None: ls.linestyle=bpy.data.linestyles.new('ink')
    ls.linestyle.color=(0.06,0.02,0.10)
    vl.freestyle_settings.crease_angle=math.radians(120)
    # only near objects: far silhouettes off
    for o in bpy.data.objects:
        if o.type=='MESH' and (o.matrix_world.translation.y>60): 
            pass
try:
    sc.eevee.use_bloom=True
except Exception: pass
sc.render.filepath=os.environ.get('OUT','/mnt/user-data/outputs/style/StyleFrame_MidnightFairground.png')
bpy.ops.render.render(write_still=True)
