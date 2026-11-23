import json, sys
from PIL import Image, ImageDraw, ImageFont
T="/tmp/imp_tiles/"; px=260; lab=22; left=150
models=["Minion","Warrior","Rogue","Mage","Zombie","ZombieBloat","ZombieRunner","Wraith","Gravebat"]
rep=json.load(open("/home/claude/blender_work/impostors/report.json"))
f=ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",15)
fs=ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",13)
head=46
W=Image.new("RGB",(left+5*px,head+len(models)*(px+lab)),(18,18,24)); d=ImageDraw.Draw(W)
d.text((10,8),"Soul Quota - far impostors: full-res skeletal pose (ref) vs SM_<Model>_Walk_0..3 (re-imported FBX, game 3/4 view, ortho)",fill=(235,235,240),font=f)
for c,t in enumerate(["skeletal (ref)","Walk_0","Walk_1","Walk_2","Walk_3"]): d.text((left+c*px+8,28),t,fill=(200,200,120),font=f)
for r,m in enumerate(models):
    y=head+r*(px+lab); info=json.load(open(T+m+".json")); R=rep[m]
    d.text((8,y+lab+10),m,fill=(235,235,240),font=f)
    d.text((8,y+lab+32),R["take"],fill=(170,170,190),font=fs)
    d.text((8,y+lab+50),"cycle %.2fs"%R["cycle_s"],fill=(170,170,190),font=fs)
    for c,n in enumerate(["full","imp0","imp1","imp2","imp3"]):
        W.paste(Image.open(T+"%s_%s.png"%(m,n)).convert("RGB"),(left+c*px,y+lab))
        if c: d.text((left+c*px+8,y+3),"f%s  %d tris"%(R["poses"][c-1]["frame"],info[str(c-1)]["tris"]),fill=(220,220,225),font=fs)
        else: d.text((left+8,y+3),"f%s  %d tris"%(R["frames"][0],R["poses"][0]["full_tris"]),fill=(220,220,225),font=fs)
W.save(sys.argv[1]); print(W.size)
