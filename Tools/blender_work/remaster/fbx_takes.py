# List FBX take (AnimStack) names without Blender: python3 fbx_takes.py file.fbx [...] > out.json
import re, sys, json
out = {}
for p in sys.argv[1:]:
    d = open(p, "rb").read()
    out[p.split("/")[-1]] = sorted(set(m.decode() for m in re.findall(rb"([\x20-\x7e]{1,80})\x00\x01AnimStack", d)))
json.dump(out, sys.stdout, indent=0)
