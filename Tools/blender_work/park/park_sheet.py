import json, sys
from PIL import Image, ImageDraw, ImageFont
T="/tmp/park_tiles/"; px=420; lab=40
P=["Park_RibKicker","Park_FemurRamp","Park_SpineWave","Park_PelvisQuarter","Park_SkullHip","Park_CoffinFunbox","Park_TombPad","Park_JawBank","Park_GraveRoller","Park_BoneBridge"]
rep=json.load(open("report.json"))
f=ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",15); fs=ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",12)
head=30; W=Image.new("RGB",(4*px,head+5*(px+lab)),(18,18,24)); d=ImageDraw.Draw(W)
d.text((8,7),"Soul Quota park pieces (re-imported FBX). Left: visual + 180 cm reaper, 1 m grid, yellow arrow = +X ride dir.  Right: UCX hulls (green = ride, orange = deco)",fill=(235,235,240),font=fs)
for i,n in enumerate(P):
    r,c=i//2,(i%2)*2; x,y=c*px,head+r*(px+lab); R=rep[n]; s=R["size_cm"]
    d.text((x+8,y+4),"%s  %dx%dx%d cm"%(n,s[0],s[1],s[2]),fill=(235,235,240),font=f)
    d.text((x+8,y+22),"%d tris, %d UCX (%d deco), steepest ride face %.1f deg"%(R["tris"],R["hulls"],R["deco_hulls"],R["steepest_ride_face_deg"]),fill=(170,170,190),font=fs)
    for k,t in enumerate(["vis","ucx"]): W.paste(Image.open(T+"%s_%s.png"%(n,t)).convert("RGB"),(x+k*px,y+lab))
W.save(sys.argv[1]); print(W.size)
