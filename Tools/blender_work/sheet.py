import sys
from PIL import Image
out=sys.argv[1]; fs=sys.argv[2:]
ims=[Image.open(f).convert('RGB') for f in fs]; s=ims[0].size[0]; n=len(ims); c=2 if n<=4 else 3; r=(n+c-1)//c
W=Image.new('RGB',(s*c,s*r))
for i,im in enumerate(ims): W.paste(im,(s*(i%c),s*(i//c)))
W.save(out)
