# District recipes + ground art maths shared by the Blender mirror; ASQArena::PlanScenery / BuildScenery / the
# terrain paint implement the same rules (angles from "out" = away from the hub, distances in metres here, cm in game).
import math, random
import numpy as np

def hexrgb(h):
    h = h.lstrip('#'); return tuple(int(h[i:i + 2], 16) / 255 for i in (0, 2, 4))
def lin(c):   # sRGB -> linear, for Blender colour inputs
    return tuple((x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4) for x in c)

BIOME = {
    "graveyard": dict(ground="#2A5C44", alt="#275747", path="#6A5448", top="#3A7350", basin="#33305E", wall="#5D5866", moon="#9FD0FF", lamp="#FFA53A",
                      road="#75604F", cap="#8C86A0", fog="#3A2466", sky_h="#C2418F", sky_m="#4B2A8C", sky_z="#0B0A2A", backdrop="#140F2E", pattern="flag"),
    "harbour": dict(ground="#3F6B5E", alt="#4E8270", path="#8A7A60", top="#77A07A", basin="#2F4F6A", wall="#4F5A63", moon="#BFE3FF", lamp="#FFC870",
                    road="#857457", cap="#A89F88", fog="#1C3F52", sky_h="#2FB7A6", sky_m="#1F4F8C", sky_z="#06142E", backdrop="#0F1E28", pattern="plank"),
    "clock": dict(ground="#5C5236", alt="#4F5A3A", path="#85694A", top="#8E7E50", basin="#3F3326", wall="#5A4A36", moon="#FFE2A8", lamp="#FFC46B",
                  road="#886C48", cap="#B0965A", fog="#3A2A12", sky_h="#E0A040", sky_m="#5A2E5E", sky_z="#120A1E", backdrop="#1C150B", pattern="tile"),
}
BIOME6 = {   # iteration 6: the Harbour's dry ground is wet shingle and sea-grass (not lawn); the Clocktower's is dark umber so brass and tile carry it
    "harbour": dict(ground="#44605E", alt="#3A6660", top="#8C8A74", basin="#274A60"),
    "clock": dict(ground="#4A4034", alt="#55492F", top="#7C6A44", basin="#33291F"),
}
def biome(m, it):
    B = dict(BIOME[m])
    if it >= 6: B.update(BIOME6.get(m, {}))
    return B

R = 13.0          # landmark zone radius (MapLandmarkRadius 1300)
HUB_D = 62.0      # hub centre is this far back down the lane
LANE_HW = 2.2     # MapLaneWidth 440 / 2
OUTNAME = {"graveyard": "Graveyard", "harbour": "Harbour", "clock": "Clocktower"}

# ------------------------------------------------------------------ per-iteration config
def config(m, it):
    B = biome(m, it)
    c = dict(retint=False, glow=0.0, new_recipe=False, ground_v=1,
             sky_h=lin(hexrgb(B["sky_h"])), sky_m=lin(hexrgb(B["sky_m"])), sky_z=lin(hexrgb(B["sky_z"])), sky_strength=0.7,
             moon_col=lin(hexrgb(B["moon"])), moon_size=34, moon_energy=1.6, moon_light=hexrgb(B["moon"]),
             rim_energy=0.5, rim_col=hexrgb(B["sky_m"]), lamp_gain=1.0, lamp_shadows=False,
             hill=lin(tuple(x * 1.0 for x in hexrgb(B["backdrop"]))), hill_far=lin(tuple(min(1, x * 1.9 + 0.03) for x in hexrgb(B["backdrop"]))),
             ground_far=lin(hexrgb(B["ground"])),
             fog_col=tuple(min(1, x * 1.05) for x in hexrgb(B["fog"])), fog_start=30.0, fog_density=0.006, fog_max=0.75,
             bloom=0.6, bloom_thresh=0.72, contrast=1.12, saturation=1.06, vignette=0.38,
             district_lights=False, podium=False, density=1.0,
             skyline=lin(hexrgb(B["backdrop"])), prop_scale=3.2, skyline_scale=4.0, moon_yaw=28.0,
             skyline_props={"graveyard": ["tree_dead_large", "tree_dead_medium", "tree_dead_large", "tree_pine_orange_large"],
                            "harbour": ["post", "tree_dead_medium", "post_skull", "tree_dead_large"],
                            "clock": ["tree_dead_large", "tree_dead_medium"]}[m])
    if it >= 2:
        c.update(retint=True, new_recipe=True)
    if it >= 3:
        c.update(ground_v=2, glow=6.0, district_lights=True, podium=True, moon_energy=3.2, rim_energy=1.2, sky_strength=1.0,
                 fog_start=22.0, fog_density=0.010, fog_max=0.8, bloom=0.9, bloom_thresh=0.62, lamp_gain=1.8, shoulders3=True)
    if it >= 4:
        c.update(ground_v=2, tweak4=True, lamp_shadows=True, contrast=1.14, saturation=1.1, glow=3.2, bloom=0.7, bloom_thresh=0.68, stars=True)
    if it >= 5:
        c.update(tweak5=True, sky_v2=True, ground_detail=True, gres=3600)
        c.update(wall_tint={"graveyard": (0.50, 0.47, 0.62), "harbour": (0.5, 0.6, 0.62), "clock": (0.62, 0.42, 0.26)}[m])
        bd, gr, sh = hexrgb(B["backdrop"]), hexrgb(B["ground"]), hexrgb(B["sky_h"])
        near = tuple(0.5 * bd[i] + 0.5 * gr[i] * 0.25 for i in range(3))
        c.update(hill=lin(near), hill_far=lin(tuple(0.55 * near[i] + 0.45 * sh[i] for i in range(3))))
    if it >= 8:
        c.update(tweak8=True)
    if it >= 7:
        c.update(tweak7=True)
    if it >= 6:
        c.update(tweak6=True, glow=2.6, ground_far=lin(hexrgb(B["ground"])), gres=2800)
    return c

def pol(ang_deg, r):
    a = math.radians(ang_deg); return (r * math.cos(a), r * math.sin(a))
def face_centre(x, y):
    return math.degrees(math.atan2(-y, -x))

# ------------------------------------------------------------------ baseline = what f7e3b21 builds today
def build_v1(m, rng):
    P = []
    lights = []
    lm = {"graveyard": "Set_Chapel", "harbour": "Set_Lighthouse", "clock": "Set_ClockTower"}[m]
    foot = {"Set_Chapel": 4.1, "Set_Lighthouse": 3.0, "Set_ClockTower": 3.2}[lm]
    x = R + foot + 0.7
    P.append(dict(name=lm, x=x, y=0, face_deg=180, foot=foot))
    if m != "harbour":
        P.append(dict(name="Set_Gate", x=-R, y=0, face_deg=0))
        ring = R + 1.1; step = (4.0 + 0.26) / ring; skip = (LANE_HW + 4.2) / ring
        for s in (-1, 1):
            run = 0; sw = skip + step / 2
            while sw < math.pi * 0.86:
                if run % 4 != 3:
                    ang = math.pi + s * sw
                    if abs(math.atan2(math.sin(ang), math.cos(ang))) > 0.42:
                        P.append(dict(name="Set_Wall", x=ring * math.cos(ang), y=ring * math.sin(ang), face_deg=math.degrees(ang) + 90, foot=1.2, kind="wall"))
                sw += step; run += 1
    tree = {"graveyard": "Set_Yew", "harbour": "Set_Pilings", "clock": "Set_Yew"}[m]
    slot = 0; xx = -R - 5
    while xx > -HUB_D + 20:
        for s in (-1, 1):
            if slot % 2 == 0:
                P.append(dict(name=tree, x=xx, y=s * (LANE_HW + 3.3), face_deg=rng.uniform(0, 360), scale=rng.uniform(0.88, 1.18), foot=0.6, kind="tree"))
            else:
                P.append(dict(name="Set_LampPost", x=xx, y=s * (LANE_HW + 1.5), face_deg=(-90 if s > 0 else 90) - 90))
                lights.append((xx, s * (LANE_HW + 1.2), 3.5, hexrgb(BIOME[m]["lamp"]), 500, 9))
        xx -= 8.2; slot += 1
    H = {"graveyard": [("KitM_church_red", 11.5), ("KitM_home_A_blue", 7.0), ("KitM_home_B_red", 8.2), ("KitM_tavern_red", 9.0), ("KitM_destroyed", 6.2), ("KitM_well_blue", 3.8)],
         "harbour": [("KitM_lumbermill_red", 10.5), ("KitM_watermill_blue", 9.8), ("KitM_market_blue", 6.6), ("KitM_blacksmith_blue", 7.2), ("KitM_barracks_blue", 10.0), ("KitM_home_B_red", 8.2)],
         "clock": [("KitM_blacksmith_blue", 7.2), ("KitM_scaffolding", 8.6), ("KitM_stage_B", 4.3), ("KitM_tavern_red", 9.0), ("KitM_barracks_blue", 10.0), ("KitM_home_A_blue", 7.0)]}[m]
    T = {"graveyard": [("KitM_tower_A_blue", 17), ("KitM_tower_B_red", 19), ("KitM_windmill_red", 15)], "harbour": [("KitM_tower_B_red", 19), ("KitM_tower_A_blue", 17)],
         "clock": [("KitM_castle_red", 26), ("KitM_tower_B_red", 22), ("KitM_windmill_red", 17), ("KitM_tower_A_blue", 18)]}[m]
    Y = {"graveyard": [("KitD_barrel_small_stack", 1.75), ("KitD_crates_stacked", 2.15), ("KitM_tent", 3.3), ("KitM_weaponrack", 1.9), ("KitM_wheelbarrow", 1.2)],
         "harbour": [("KitD_box_stacked", 3.3), ("KitD_crates_stacked", 2.15), ("KitD_keg_decorated", 2.05), ("KitD_barrel_large", 2.0), ("KitD_barrel_small_stack", 1.75)],
         "clock": [("KitD_crates_stacked", 2.15), ("KitD_keg_decorated", 2.05), ("KitM_weaponrack", 1.9), ("KitD_pillar", 4.0), ("KitD_barrel_large", 2.0)]}[m]
    a0 = rng.uniform(0, 360); made = 0
    for t in range(25):
        if made >= 5: break
        name, h = rng.choice(H)
        ang = a0 + 360 * t / 25 + rng.uniform(-4, 4)
        a = (ang + 180) % 360 - 180
        if abs(abs(a) - 180) < 28 or abs(a) < 30: continue
        f = h * 0.45
        r = R + f + rng.uniform(4.2, 7.6)
        x, y = pol(ang, r)
        P.append(dict(name=name, x=x, y=y, face_deg=face_centre(x, y) + rng.uniform(-7, 7), height=h, foot=f)); made += 1
        for k in range(rng.randint(1, 2)):
            cn, ch = rng.choice(Y)
            s = 1 if k == 0 else -1
            cx, cy = x - math.sin(math.radians(ang)) * s * (f + ch * 0.45 + 0.3), y + math.cos(math.radians(ang)) * s * (f + ch * 0.45 + 0.3)
            P.append(dict(name=cn, x=cx, y=cy, face_deg=face_centre(x, y) + rng.uniform(-20, 20), height=ch))
    for k in range(2):
        name, h = rng.choice(T)
        x, y = pol(rng.uniform(-63, 63), R + rng.uniform(15, 20))
        P.append(dict(name=name, x=x, y=y, face_deg=face_centre(x, y), height=h, foot=h * 0.25))
    walls = ["KitD_wall", "KitD_wall_arched", "KitD_wall_window_open", "KitD_wall_broken", "KitD_wall_doorway", "KitD_wall_scaffold"]
    for k in range(2):
        ang = rng.uniform(40, 150) * (1 if k == 0 else -1)
        mx, my = pol(ang, R + rng.uniform(11, 21))
        tx, ty = -math.sin(math.radians(ang)), math.cos(math.radians(ang))
        for seg in (-1, 0, 1):
            P.append(dict(name=rng.choice(walls), x=mx + tx * 4.2 * seg, y=my + ty * 4.2 * seg, face_deg=ang, height=4.2, foot=1.5, kind="wall"))
    xx = -R - 9
    while xx > -HUB_D + 23:
        for s in (-1, 1):
            P.append(dict(name="KitD_pillar_decorated", x=xx, y=s * (LANE_HW + 2.6), face_deg=0, height=4.8))
        xx -= 15
    for g in range(3):
        cx, cy = pol(rng.uniform(100, 260), R + rng.uniform(14, 26))
        for k in range(rng.randint(4, 6)):
            dx, dy = pol(rng.uniform(0, 360), rng.uniform(0, 5.2))
            P.append(dict(name=rng.choice(["KitM_tree_single_A", "KitM_tree_single_B"]), x=cx + dx, y=cy + dy, face_deg=rng.uniform(0, 360), height=rng.uniform(5.5, 9), foot=0.7, kind="tree"))
    return P, lights

# ------------------------------------------------------------------ the new district recipe (PlanScenery in game)
TABLE = {
    "graveyard": dict(
        landmark=("Set_Chapel", 4.1), gate="Fair_LanternArch", span="Fair_Bunting",
        flank=[("Set_Mausoleum", None, 3.0), ("KitM_church_red", 12.5, 4.2), ("crypt", 5.2, 2.6), ("KitM_destroyed", 7.0, 3.2), ("KitM_home_B_red", 8.6, 3.0), ("crypt", 4.6, 2.4)],
        back=[("Set_BellTower", None, 2.6), ("KitM_tower_B_red", 21.0, 3.4), ("KitM_tower_A_blue", 18.0, 3.2), ("KitM_windmill_red", 16.0, 3.4)],
        sig=[("Fair_CoffinWheel", 128.0, 13.5, 5.5), ("Fair_TentCrypt", -127.0, 11.5, 5.0)], tree=("Fair_LanternTree", 158.0, 9.5),
        stalls=("Fair_Stall", [-152.0, -164.0]), trees=["KitM_tree_single_A", "KitM_tree_single_B", "tree_dead_large", "Set_Yew", "tree_dead_medium"],
        avenue="Set_Yew", plots=True, cargo=None),
    "harbour": dict(
        landmark=("Set_Lighthouse", 3.0), gate="Fair_NetRack", span="Fair_NetRack",
        flank=[("Fair_StiltHouse", None, 4.6), ("KitM_lumbermill_red", 11.0, 4.0), ("Set_Boathouse", None, 3.6), ("KitM_watermill_blue", 10.5, 3.8), ("KitM_market_blue", 7.2, 3.4), ("Fair_StiltHouse", None, 4.6)],
        back=[("Set_Crane", None, 3.0), ("KitM_tower_B_red", 20.0, 3.2), ("KitM_barracks_blue", 11.0, 3.8), ("KitM_tower_A_blue", 17.0, 3.0)],
        sig=[("Fair_Wreck", 126.0, 15.0, 8.0), ("Set_Crane", -128.0, 11.5, 3.2)], tree=("Set_Pilings", 158.0, 9.0),
        stalls=("Fair_CargoPile", [-152.0, -166.0]), trees=["Set_Pilings", "tree_dead_medium", "post"],
        avenue="Set_Pilings", plots=False, cargo="Fair_CargoPile"),
    "clock": dict(
        landmark=("Set_ClockTower", 3.2), gate="Fair_GearBridge", span="Fair_Bunting",
        flank=[("Fair_ClockWall", None, 3.0), ("KitM_blacksmith_blue", 8.0, 3.2), ("KitM_scaffolding", 9.5, 3.0), ("KitM_barracks_blue", 10.5, 3.8), ("Fair_ClockWall", None, 3.0), ("KitM_tavern_red", 9.4, 3.4)],
        back=[("KitM_castle_red", 27.0, 5.0), ("KitM_tower_B_red", 23.0, 3.6), ("Set_GearArch", None, 3.0), ("KitM_tower_A_blue", 19.0, 3.2)],
        sig=[("Fair_Pendulum", 127.0, 12.0, 3.4), ("Fair_Pipes", -127.0, 11.5, 4.0)], tree=("Set_GearStack", 158.0, 9.0),
        stalls=("Fair_Pipes", [-150.0]), trees=["KitM_tree_single_A", "tree_dead_medium", "Set_Yew"],
        avenue="Set_Yew", plots=False, cargo=None),
}
LANDMARKS = {"graveyard": ["Set_Chapel", "Set_Mausoleum", "Set_BellTower"], "harbour": ["Set_Lighthouse", "Set_Boathouse", "Set_Crane"],
             "clock": ["Set_ClockTower", "Set_GearArch", "Set_GearStack"]}
SUBST = {"graveyard": ("KitM_tavern_red", 9.0, 3.2), "harbour": ("KitM_blacksmith_blue", 7.6, 3.2), "clock": ("KitM_home_A_blue", 7.4, 3.0)}
SIG_ALT = {"graveyard": ("Fair_Carousel", 13.5, 5.0), "harbour": ("Fair_DivingBell", 12.5, 4.2), "clock": ("Fair_Orrery", 12.5, 4.6)}
FLANK_ANG = [34.0, -36.0, 64.0, -66.0, 95.0, -97.0]
BACK_ANG = [50.0, -51.0, 82.0, -80.0]

def build_v2(m, rng, c, it=5, dist=0):
    T = TABLE[m]; B = biome(m, it)
    P = []; lights = []
    lamp = hexrgb(B["lamp"])
    pod_h = 1.4 if c["podium"] else 0.0
    lm, lfoot = T["landmark"]
    flank_list, sig_list = list(T["flank"]), list(T["sig"])
    if c.get("tweak8"):
        # no two districts alike: the landmark rotates through the map's three, the fronts rotate round the ring, the
        # signature pair alternates, and every other district is mirrored (applied at the end of build())
        lm = LANDMARKS[m][dist % 3]
        flank_list = flank_list[(2 * dist) % 6:] + flank_list[:(2 * dist) % 6]
        flank_list = [(SUBST[m] if f[0] == lm else f) for f in flank_list]
        if dist % 2 == 1:
            sig_list = [(SIG_ALT[m][0], sig_list[0][1], SIG_ALT[m][1], SIG_ALT[m][2]), (sig_list[0][0], sig_list[1][1], sig_list[0][2], sig_list[0][3])]
    lx = R + 6.6
    if c["podium"]:
        P.append(dict(name="Fair_Podium", x=lx - 0.86, y=0, face_deg=180, foot=7.2, kind="podium"))
    P.append(dict(name=lm, x=lx, y=0, face_deg=180, z=pod_h, foot=None if c["podium"] else lfoot))
    # flank houses, shoulder to shoulder on a ring just outside the yard wall, facing in
    fr = c.get("flank_ring", 5.6)
    for k, ang in enumerate(FLANK_ANG):
        name, h, f = flank_list[k]
        r = R + f + fr + (1.2 if k % 2 else 0)
        x, y = pol(ang, r)
        P.append(dict(name=name, x=x, y=y, face_deg=face_centre(x, y), height=h, foot=f))
        if T["cargo"] and k in (1, 3, 4):   # big netted cargo, hard against the warehouse's side wall
            tx, ty = -math.sin(math.radians(ang)), math.cos(math.radians(ang))
            s = 1 if ang > 0 else -1
            P.append(dict(name=T["cargo"], x=x + tx * s * (f + 1.6), y=y + ty * s * (f + 1.6), face_deg=face_centre(x, y), foot=2.0))
    # second row: towers and tall pieces behind the gaps (the skyline you run toward)
    for k, ang in enumerate(BACK_ANG):
        name, h, f = T["back"][k]
        if c.get("tweak8") and name == lm: name, h, f = "KitM_tower_A_blue", 18.0, 3.2
        x, y = pol(ang, R + c.get("back_ring", 17.5) + (2.5 if k >= 2 else 0))
        P.append(dict(name=name, x=x, y=y, face_deg=face_centre(x, y), height=h, foot=f))
    # signature set pieces at the district's shoulders, the lantern tree by the gate
    sig = sig_list; gt = T["tree"]; stall_name, stall_angs = T["stalls"]
    if c.get("shoulders3"):
        sig = [(sig[0][0], 124.0, sig[0][2] + 0.5, sig[0][3]), (sig[1][0], -133.0, sig[1][2] + 1.5, sig[1][3])]
        gt = (gt[0], -157.0, 11.0) if not c.get("tweak4") else (gt[0], -150.0, 13.5)
        stall_angs = [140.0, 153.0][:len(stall_angs)]
    for name, ang, sdist, f in sig:
        if c.get("tweak8") and name == lm: name, sdist, f = SIG_ALT[m]
        x, y = pol(ang, R + sdist)
        P.append(dict(name=name, x=x, y=y, face_deg=face_centre(x, y) + (25 if name == "Fair_Wreck" else 0), foot=f))
        if c["district_lights"]:
            lights.append((x * 0.86, y * 0.86, 4.5, lamp, 900, 12))
    name, ang, dist = gt
    x, y = pol(ang, R + dist)
    P.append(dict(name=name, x=x, y=y, face_deg=face_centre(x, y), foot=1.3, kind="tree"))
    if c["district_lights"]:
        lights.append((x, y + 1.5, 5.0, lamp, 700, 10))
    # yard wall ring with gaps; stalls take the place of wall lengths (they ARE structure, never loose)
    ring = R + 1.1; step = 4.26 / ring; skip = (LANE_HW + 4.2) / ring
    if m != "harbour":
        for s in (-1, 1):
            run = 0; sw = skip + step / 2
            while sw < math.pi * 0.86:
                ang = math.pi + s * sw
                an = math.degrees(math.atan2(math.sin(ang), math.cos(ang)))
                near_stall = any(abs(an - sa) < 9 for sa in stall_angs)
                if run % 4 != 3 and abs(an) > 26 and not near_stall:
                    P.append(dict(name="Set_Wall", x=ring * math.cos(ang), y=ring * math.sin(ang), face_deg=math.degrees(ang) + 90, foot=1.2, kind="wall"))
                sw += step; run += 1
    for sa in stall_angs:
        x, y = pol(sa, ring + (0.6 if stall_name == "Fair_Stall" else 1.6))
        P.append(dict(name=stall_name, x=x, y=y, face_deg=face_centre(x, y), foot=2.4))
    # gate arch where the lane arrives, bunting / net spans over the lane, lamp posts between, avenue trees
    gate = T["gate"]
    if c.get("shoulders3") and m == "harbour": gate = "Fair_LanternArch"
    span6 = "Fair_Bunting"
    if c.get("tweak6"):   # every map has its own gate and its own lane span
        gate = {"graveyard": "Fair_LanternArch", "harbour": "Fair_BuoyArch", "clock": "Fair_GearBridge"}[m]
        span6 = {"graveyard": "Fair_Bunting", "harbour": "Fair_BuoyLine", "clock": "Fair_CogLine"}[m]
    P.append(dict(name=gate, x=-R - 1.5, y=0, face_deg=0, foot=None))
    lights.append((-R - 1.5, 0, 5.5, lamp, 900 if c["district_lights"] else 0, 10))
    if c.get("tweak4"):
        xx = -R - 6.0; slot = 0
        while xx > -HUB_D + 20:
            if slot % 2 == 0:
                for s in (-1, 1):
                    P.append(dict(name="Set_LampPost", x=xx, y=s * (LANE_HW + 1.5), face_deg=(-90 if s > 0 else 90) - 90))
                    lights.append((xx, s * (LANE_HW + 1.2), 3.5, lamp, 500, 9))
            else:
                for s in (-1, 1):
                    if m == "clock" and c.get("tweak5") and not c.get("tweak7"): continue   # Biomes.csv: the Clocktower has no avenue trees
                    if c.get("tweak5") and xx > -R - 30: continue     # keep the last 30 m of the approach open: the arch frames the landmark
                    av = "tree_dead_medium" if m == "clock" else T["avenue"]
                    if c.get("tweak7") and m == "clock":
                        P.append(dict(name="Fair_GearPost", x=xx, y=s * (LANE_HW + 6.5), face_deg=90, foot=0.5, kind="tree")); continue
                    P.append(dict(name=av, x=xx, y=s * (LANE_HW + (6.5 if c.get("tweak5") else 4.8)), face_deg=rng.uniform(0, 360),
                                  scale=None if m == "clock" else rng.uniform(0.75, 0.95) if c.get("tweak5") else rng.uniform(0.9, 1.2),
                                  height=rng.uniform(6.0, 8.0) if m == "clock" else None, foot=0.7, kind="tree"))
            xx -= 4.5 if False else 5.5; slot += 1
        xx = -R - (31.0 if not c.get("tweak7") else 14.5)
        while xx > -HUB_D + 20:
            P.append(dict(name=span6, x=xx, y=0, face_deg=0))
            lights.append((xx, 0, 4.2, lamp, 500, 8))
            if m == "harbour":
                for s in (-1, 1):
                    P.append(dict(name="Fair_NetRack", x=xx - 6.0, y=s * (LANE_HW + (12.5 if c.get("tweak5") else 8.5)), face_deg=90, foot=None))
            xx -= 18.0
        if m == "harbour" and not c.get("tweak5"):
            for s in (-1, 1):
                P.append(dict(name="Fair_NetRack", x=-R - 13.0, y=s * (LANE_HW + 8.5), face_deg=90, foot=None))
    else:
        xx = -R - (16.5 if c.get("shoulders3") else 10.5); slot = 0
        while xx > -HUB_D + 20:
            if slot % 2 == 0:
                P.append(dict(name="Fair_Bunting" if c.get("shoulders3") else T["span"], x=xx, y=0, face_deg=0))
                if c.get("shoulders3") and m == "harbour":
                    for s in (-1, 1):
                        P.append(dict(name="Fair_NetRack", x=xx - 4.5, y=s * (LANE_HW + 7.5), face_deg=90, foot=None))
                if c["district_lights"]: lights.append((xx, 0, 4.2, lamp, 500, 8))
            else:
                for s in (-1, 1):
                    P.append(dict(name="Set_LampPost", x=xx, y=s * (LANE_HW + 1.5), face_deg=(-90 if s > 0 else 90) - 90))
                    lights.append((xx, s * (LANE_HW + 1.2), 3.5, lamp, 500, 9))
            for s in (-1, 1):
                if slot % 2 == 1:
                    P.append(dict(name=T["avenue"], x=xx + 3.5, y=s * (LANE_HW + 4.6), face_deg=rng.uniform(0, 360), scale=rng.uniform(0.9, 1.2), foot=0.7, kind="tree"))
            xx -= 9.0; slot += 1
    # tree line: a dense arc behind the second row, groves at the shoulders
    trees = T["trees"]
    if c.get("tweak4") and m == "clock":
        trees = ["KitM_tree_single_A", "tree_dead_medium", "tree_dead_large"]   # no leafy green in the brass town
    if c.get("tweak4") and m == "harbour":
        for (ba, bd, yaw) in ((38.0, 31.0, 70.0), (-72.0, 30.0, -40.0), (150.0, 27.0, 20.0), (-116.0, 28.0, 110.0), (4.0, 36.0, 95.0)):
            x, y = pol(ba, R + bd)
            P.append(dict(name="Fair_Boat", x=x, y=y, face_deg=yaw, foot=3.8, kind="wet"))
            if c["district_lights"]: lights.append((x, y, 3.2, lamp, 260, 7))
        for k in range(-5, 6):   # mooring pilings along the quay edge
            if k == 0: continue
            x, y = pol(k * 21.0, R + 8.6 + (0.5 if k % 2 else 0))
            P.append(dict(name="Set_Pilings", x=x, y=y, face_deg=rng.uniform(0, 360), scale=rng.uniform(0.85, 1.1), foot=0.7, kind="wet"))
    if c.get("tweak7") and m == "clock":
        trees = ["Fair_Chimney", "Fair_GearPost", "Fair_Chimney", "Set_GearStack"]
    if c.get("tweak7") and m == "harbour":
        trees = ["Set_Pilings", "Fair_Mast", "Set_Pilings", "post"]
    def tree(x, y, big=1.0):
        n = rng.choice(trees)
        h = {"KitM_tree_single_A": 9.0, "KitM_tree_single_B": 8.0, "tree_dead_large": 10.0, "tree_dead_medium": 7.0, "post": 3.2}.get(n)
        if n.startswith("Fair_") or n == "Set_GearStack": big = min(big, 1.0) * rng.uniform(0.8, 1.0) / rng.uniform(0.9, 1.35)
        P.append(dict(name=n, x=x, y=y, face_deg=rng.uniform(0, 360), height=(h * rng.uniform(0.85, 1.3) * big) if h else None,
                      scale=None if h else rng.uniform(0.9, 1.35) * big, foot=0.7, kind="tree"))
    ntl = int((26 if m == "graveyard" else 12) * c["density"])
    for k in range(ntl):
        ang = -118 + 236 * (k + rng.uniform(-0.3, 0.3)) / max(1, ntl - 1)
        x, y = pol(ang, R + rng.uniform(24.5, 31.0))
        tree(x, y, 1.25)
    for (ga, gd) in ((143, 19.0), (-143, 18.0), (172, 17.0), (-172, 17.5)):
        gx, gy = pol(ga, R + gd)
        if abs(gy) < LANE_HW + 6: gy = math.copysign(LANE_HW + 8, gy)
        for k in range(5 if m != "harbour" else 3):
            dx, dy = pol(rng.uniform(0, 360), rng.uniform(0.8, 4.2))
            tree(gx + dx, gy + dy)
    # graveyard: fenced grave plots between the flank houses and the shoulders (standing headstones inside a fence)
    if T["plots"]:
        for (pa, pd) in ((112, 9.5), (-112, 9.0)) + (((148, 12.0),) if c.get("tweak4") else ()):
            px, py = pol(pa, R + pd)
            fa = math.radians(pa)
            ux, uy = math.cos(fa), math.sin(fa); vx, vy = -uy, ux
            for i in range(-1, 2):
                for j in (-1, 1):
                    gx, gy = px + ux * i * 1.9 + vx * j * 1.3, py + uy * i * 1.9 + vy * j * 1.3
                    P.append(dict(name=rng.choice(["gravestone", "grave_A", "grave_B"]), x=gx, y=gy, face_deg=pa + 180 + rng.uniform(-8, 8), height=rng.uniform(1.5, 2.0)))
            for i in range(-2, 3):
                for j in (-1, 1):
                    P.append(dict(name="fence", x=px + ux * i * 1.6 + vx * j * 2.9, y=py + uy * i * 1.6 + vy * j * 2.9, face_deg=pa + 90, height=1.5))
            for j in (-1, 0, 1):
                for i in (-1, 1):
                    P.append(dict(name="fence", x=px + ux * i * 4.0 + vx * j * 1.9, y=py + uy * i * 4.0 + vy * j * 1.9, face_deg=pa, height=1.5))
            for i in (-1, 1):
                for j in (-1, 1):
                    P.append(dict(name="fence_pillar", x=px + ux * i * 4.0 + vx * j * 2.9, y=py + uy * i * 4.0 + vy * j * 2.9, face_deg=pa, height=2.1))
            P.append(dict(name="post_lantern", x=px + ux * 4.0, y=py + uy * 4.0, face_deg=pa, height=3.2))
            SITE_EXTRA.append((px, py, 5.2, "plot"))
            if c["district_lights"]: lights.append((px + ux * 3.6, py + uy * 3.6, 2.8, lamp, 300, 6))
    if c.get("tweak7"):
        # wayside pieces half way down the lane, set well back (the game: role Wayside, alternating sides)
        ways = {"graveyard": [("Set_Statue", 1.2, "solid"), ("Set_Mausoleum", 3.0, "solid")], "harbour": [("Fair_Boat", 3.8, "wet"), ("Fair_Mast", 0.7, "tree")],
                "clock": [("Fair_Chimney", 1.0, "tree"), ("Set_GearStack", 1.6, "solid")]}[m]
        for k, (wn, wf, wk) in enumerate(ways):
            s_ = 1 if k == 0 else -1
            wx, wy = -R - 21.0 - 9.0 * k, s_ * (LANE_HW + 9.5 + wf)
            P.append(dict(name=wn, x=wx, y=wy, face_deg=-90 * s_, foot=wf, kind=wk))
            lights.append((wx, wy - s_ * (wf + 1.0), 3.0, lamp, 260, 7))
        # Lantern Square: the lantern, statues between the lanes, lamp posts down the other five lanes
        hx = -HUB_D
        P.append(dict(name="lantern_standing", x=hx, y=0, face_deg=0, height=4.4))
        lights.append((hx, 0, 4.0, lamp, 2200, 16))
        st = {"graveyard": "Set_Statue", "harbour": "Set_Pilings", "clock": "Set_GearStack"}[m]
        for k in range(6):
            a = math.radians(30 + 60 * k)
            P.append(dict(name=st, x=hx + 20.2 * math.cos(a), y=20.2 * math.sin(a), face_deg=math.degrees(a) + 180, foot=1.2))
        for la in (60, 120, 180, 240, 300):
            ca, sa = math.cos(math.radians(la)), math.sin(math.radians(la))
            for dd, lamp_slot in ((23.5, True), (29.0, False), (34.5, True), (40.0, False)):
                for s_ in (-1, 1):
                    off = LANE_HW + (1.5 if lamp_slot else 6.5)
                    px, py = hx + ca * dd - sa * s_ * off, sa * dd + ca * s_ * off
                    if lamp_slot:
                        P.append(dict(name="Set_LampPost", x=px, y=py, face_deg=la + (-90 if s_ > 0 else 90) - 90))
                        lights.append((px, py, 3.5, lamp, 400, 8))
                    elif m != "clock":
                        P.append(dict(name=T["avenue"], x=px, y=py, face_deg=rng.uniform(0, 360), scale=rng.uniform(0.75, 0.95), foot=0.7, kind="tree"))
                    else:
                        P.append(dict(name="Fair_GearPost", x=px, y=py, face_deg=la + 90, foot=0.5, kind="tree"))
            P.append(dict(name=span6, x=hx + ca * 31.0, y=sa * 31.0, face_deg=la))
    if c["district_lights"]:   # the landmark's door light and two pools on the plaza edge
        lights.append((R + 0.5, 0, 3.6, lamp, 1100, 12))
        for s in (-1, 1):
            lights.append((2.0, s * (R - 2.0), 3.2, lamp, 500, 9))
    return P, lights

SITE_EXTRA = []

def build(m, it, rng, dist=0):
    c = config(m, it)
    del SITE_EXTRA[:]
    P, lights = build_v2(m, rng, c, it, dist) if c["new_recipe"] else build_v1(m, rng)
    if c.get("ground_v", 1) >= 2:   # the game's moon: 50 degrees up; here it lights the fronts from the left of the lane
        az, el = math.radians(205), math.radians(50)
        md = (math.cos(az) * math.cos(el), math.sin(az) * math.cos(el), math.sin(el))
    else:
        az, el = math.radians(-128), math.radians(24)
        md = (math.cos(az) * math.cos(el) * -1, math.sin(az) * math.cos(el) * -1, math.sin(el))
    if c.get("tweak8") and dist % 2 == 1:
        for q in P:
            q["y"] = -q["y"]; q["face_deg"] = -q["face_deg"]
        lights = [(lx_, -ly_, lz_, col_, pw_, rd_) for (lx_, ly_, lz_, col_, pw_, rd_) in lights]
        for k in range(len(SITE_EXTRA)):
            SITE_EXTRA[k] = (SITE_EXTRA[k][0], -SITE_EXTRA[k][1], SITE_EXTRA[k][2], SITE_EXTRA[k][3])
    views = dict(lane=(-R - 33.0, 0.6, 0.0),                       # mid-lane, running toward the district
                 hub=(-HUB_D + 21.0, 0.0, math.pi),                # leaving the lane into Lantern Square
                 side=(3.0, -3.0, math.radians(125.0)),            # on the plaza, looking at a shoulder (the fight view)
                 back=(R + 1.0, -9.5, math.radians(200.0)),        # at the far side of the plaza looking back at the gate
                 top=((-22.0, 0.0, 120.0), (-22.0, 0.01, 0.0), 26))
    return dict(pieces=P, lights=lights, R=R, reaper=(-R - 12.0, 0.0, 0.0), gcentre=(-10.0, 0.0), moon_dir=md, views=views,
                beauty=((-R - 24.0, -30.0, 19.0), (6.0, 3.0, 3.5)), outname=OUTNAME[m] + "_iter%d_%%s.png" % it, extra=list(SITE_EXTRA))

# ------------------------------------------------------------------ ground art (terrain paint + M_GroundV3 patterns)
def vnoise(x, y, scale, seed):
    r = np.random.RandomState(seed)
    g = r.rand(96, 96).astype(np.float32)
    xs, ys = (x / scale) % 95, (y / scale) % 95
    x0, y0 = np.floor(xs).astype(int), np.floor(ys).astype(int)
    fx, fy = xs - x0, ys - y0
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy)
    return (g[y0, x0] * (1 - fx) + g[y0, x0 + 1] * fx) * (1 - fy) + (g[y0 + 1, x0] * (1 - fx) + g[y0 + 1, x0 + 1] * fx) * fy

def smooth(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1); return t * t * (3 - 2 * t)

def ground(m, it, plan, sites, gsize, gres):
    c = config(m, it); B = biome(m, it)
    v = c["ground_v"]
    half = gsize / 2
    xs = np.linspace(plan["gcentre"][0] - half, plan["gcentre"][0] + half, gres, dtype=np.float32)
    ys = np.linspace(plan["gcentre"][1] + half, plan["gcentre"][1] - half, gres, dtype=np.float32)
    X, Y = np.meshgrid(xs, ys)
    col = lambda k, mul=1.0: np.array(hexrgb(B[k]), np.float32) * mul
    bright = 0.85
    n1 = vnoise(X, Y, 22.0, 1); n2 = vnoise(X, Y, 4.3, 2); n3 = vnoise(X, Y, 1.6, 3); n4 = vnoise(X, Y, 0.45, 4)
    G = col("ground")[None, None, :] * (0.9 + 0.2 * n2[..., None])
    G = G + (col("alt") - col("ground"))[None, None, :] * smooth(0.45, 0.7, n1)[..., None]
    earth = smooth(0.66, 0.8, n2 * 0.6 + n1 * 0.4)
    G = G * (1 - earth[..., None] * 0.6) + col("path", 0.8)[None, None, :] * earth[..., None] * 0.6
    if v >= 2:   # bolder painterly blotches: lighter top-colour clearings and dark basin hollows, bigger shapes
        clear = smooth(0.58, 0.74, vnoise(X, Y, 13.0, 9) * 0.7 + n3 * 0.3)
        G = G * (1 - clear[..., None] * 0.55) + col("top", 0.92)[None, None, :] * clear[..., None] * 0.55
        hollow = smooth(0.62, 0.8, vnoise(X, Y, 17.0, 11))
        G = G * (1 - hollow[..., None] * 0.5) + col("basin", 0.8)[None, None, :] * hollow[..., None] * 0.5
        tuft = smooth(0.72, 0.9, n4) * (1 - earth)
        G = G * (1 - 0.16 * tuft[..., None])
    stone = np.zeros_like(X); wood = np.zeros_like(X); pave_col = np.zeros(X.shape + (3,), np.float32)
    def pave(mask, kind, tone):
        nonlocal stone, wood, pave_col, G
        mask = np.clip(mask, 0, 1)
        G = G * (1 - mask[..., None]) + tone[None, None, :] * mask[..., None]
        if kind == "wood": wood = np.maximum(wood, mask)
        else: stone = np.maximum(stone, mask)
    ragged = (n3 - 0.5) * 1.2 + (n4 - 0.5) * 0.5
    # the lanes (worn dirt / planks / tiles): the district's own lane, and from iteration 7 the hub's other five
    lane_kind = {"flag": None, "plank": "wood", "tile": "stone"}[B["pattern"]]
    road = col("road", 0.95)
    lane_angs = [0.0] + ([60.0, 120.0, 180.0, 240.0, 300.0] if c.get("tweak7") else [])
    for la in lane_angs:
        ca, sa = math.cos(math.radians(la)), math.sin(math.radians(la))
        U = (X + HUB_D) * ca + Y * sa            # along the lane, from the hub centre outward
        Vv = -(X + HUB_D) * sa + Y * ca          # across it
        ay = np.abs(Vv)
        run = ((U > 0) & (U < HUB_D + 2)).astype(np.float32)
        lane_d = ay + ragged * 0.5
        lane_m = (1 - smooth(LANE_HW - 0.2, LANE_HW + 0.7, lane_d)) * run
        if lane_kind:
            pave(lane_m, lane_kind, road if lane_kind == "wood" else col("cap", 0.9))
        else:
            mid = 1 - smooth(0.0, LANE_HW * 0.7, ay)
            G = G * (1 - lane_m[..., None]) + (col("path") * (0.92 + 0.14 * mid[..., None])) * lane_m[..., None]
            if v >= 2:   # darker ragged verge so the path sits in the grass
                verge = (smooth(LANE_HW - 0.3, LANE_HW + 0.5, lane_d) - smooth(LANE_HW + 0.5, LANE_HW + 1.7, lane_d)) * run
                G = G * (1 - 0.3 * verge[..., None])
        if c.get("tweak6"):   # the lane is BUILT: a flat edging course either side (edging stones / stringer beam / border tiles)
            on = ((U < HUB_D - R + 0.5) & (U > 17)).astype(np.float32) if la == 0.0 else ((U > 17) & (U < HUB_D + 2)).astype(np.float32)
            if B["pattern"] == "flag":
                soft = (smooth(LANE_HW - 0.6, LANE_HW + 0.1, ay) - smooth(LANE_HW + 0.4, LANE_HW + 1.3, ay)) * on
                G = G * (1 - 0.55 * soft[..., None]) + col("cap", 0.78)[None, None, :] * 0.55 * soft[..., None]
                band = (smooth(LANE_HW + 0.03, LANE_HW + 0.13, ay) - smooth(LANE_HW + 0.57, LANE_HW + 0.67, ay)) * on
                jx = smooth(0.44, 0.49, np.abs((U / 1.35 + (Vv > 0) * 0.5) % 1 - 0.5))
                tone = 1.2 * (0.9 + 0.2 * vnoise(np.floor(U / 1.35 + (Vv > 0) * 0.5) * 3.3 + la, (Vv > 0) * 5.0, 1.0, 51)) * (1 - 0.42 * jx)
                G = G * (1 + band[..., None] * (tone[..., None] - 1))
            elif B["pattern"] == "plank":
                band = (smooth(LANE_HW - 0.25, LANE_HW - 0.15, ay) - smooth(LANE_HW + 0.11, LANE_HW + 0.21, ay)) * on
                G = G * (1 - band[..., None] * 0.45)
            else:
                e0, e1 = (-0.48, -0.04) if not c.get("tweak7") else (-0.34, -0.06)
                band = (smooth(LANE_HW + e0 - 0.05, LANE_HW + e0 + 0.05, ay) - smooth(LANE_HW + e1 - 0.05, LANE_HW + e1 + 0.05, ay)) * on
                G = G * (1 - band[..., None] * (0.40 if not c.get("tweak7") else 0.30))
    hub_d = np.hypot(X + HUB_D, Y)
    pave(1 - smooth(17.6, 18.4, hub_d + ragged * 0.4), "wood" if B["pattern"] == "plank" else "stone", col("cap") if B["pattern"] != "plank" else road)
    # the plaza
    pd = np.hypot(X, Y)
    plaza = 1 - smooth(R - 0.4, R + 0.5, pd + ragged * 0.35)
    pave(plaza, "wood" if B["pattern"] == "plank" else "stone", col("cap") if B["pattern"] != "plank" else road)
    if v >= 2:
        # promenade: a paved crescent between the yard wall and the house fronts; aprons round every structure
        prom = (smooth(R + 0.4, R + 1.2, pd) - smooth(R + 4.6, R + 6.2, pd + ragged)) * (np.abs(np.degrees(np.arctan2(Y, X))) < 118)
        pave(prom * 0.92, "wood" if B["pattern"] == "plank" else "stone", (col("cap", 0.84) if B["pattern"] != "plank" else road * 0.9))
        for (sx, sy, sr, kind) in list(sites) + list(plan["extra"]):
            d = np.hypot(X - sx, Y - sy)
            if kind in ("solid", "podium", "plot"):
                ap = 1 - smooth(sr + 1.0, sr + 2.6, d + ragged * 0.9)
                pave(ap * (0.9 if kind != "plot" else 0.0), "wood" if B["pattern"] == "plank" else "stone", (col("cap", 0.8) if B["pattern"] != "plank" else road * 0.86))
                if kind == "plot":
                    G = G * (1 - 0.45 * ap[..., None]) + col("basin", 0.55)[None, None, :] * 0.45 * ap[..., None]
        # worn trails from the gate out to the shoulders (signature pieces) and round the back of the houses
        for sign in (-1, 1):
            ang = np.degrees(np.arctan2(Y * sign, X))
            trail_r = R + 8.4 + 1.8 * np.sin(np.radians(ang * 2.0)) + ragged * 0.8
            tm = (1 - smooth(0.7, 1.5, np.abs(pd - trail_r))) * (ang > 104) * (ang < 176)
            G = G * (1 - 0.8 * tm[..., None]) + col("path", 0.9)[None, None, :] * 0.8 * tm[..., None]
    if c.get("tweak7") and B["pattern"] == "tile":
        stone = np.maximum(stone, 0.34)
    if c.get("ground_detail"):
        soft = 1 - np.maximum(stone, wood)
        f1 = vnoise(X, Y, 0.55, 41); f2 = vnoise(X, Y, 0.17, 42); f3 = vnoise(X, Y, 0.06, 43)
        G = G * (1 + soft[..., None] * ((f1[..., None] - 0.5) * 0.22 + (f2[..., None] - 0.5) * 0.16 + (f3[..., None] - 0.5) * 0.12))
        G = G * (1 + (1 - soft[..., None]) * ((f2[..., None] - 0.5) * 0.08))
    # --- M_GroundV3 patterns on the paved masks
    joint = 0.42
    if B["pattern"] == "flag":
        row = np.floor(Y / 0.9); u = X / 1.5 + (row % 2) * 0.5
        fx, fy = np.abs((u % 1) - 0.5) * 1.5, np.abs((Y / 0.9) % 1 - 0.5) * 0.9
        j = np.maximum(smooth(0.71, 0.745, fx), smooth(0.41, 0.445, fy))
        tone = 0.9 + 0.2 * vnoise(np.floor(u) * 3.1 + row * 7.7, row * 1.3, 1.0, 21)
        G = G * (1 - stone[..., None]) + G * stone[..., None] * tone[..., None] * (1 - joint * j[..., None])
    if B["pattern"] == "tile":
        tx, ty = np.floor(X / 1.2), np.floor(Y / 1.2)
        chk = ((tx + ty) % 2) * 0.16 + 0.92
        j = np.maximum(smooth(0.565, 0.595, np.abs((X / 1.2) % 1 - 0.5) * 1.2), smooth(0.565, 0.595, np.abs((Y / 1.2) % 1 - 0.5) * 1.2))
        G = G * (1 - stone[..., None]) + G * stone[..., None] * chk[..., None] * (1 - joint * j[..., None])
    if B["pattern"] == "plank":
        # boards run across the direction of travel: radial on the plaza, across the lane
        ang = np.arctan2(Y, X)
        on_plaza = pd < R + 7
        across = np.where(on_plaza, pd, X)               # coordinate ALONG the board gaps
        along = np.where(on_plaza, ang * np.maximum(pd, 1.0), Y)
        bw = 0.32
        brow = np.floor(across / bw)
        j = smooth(0.42, 0.5, np.abs((across / bw) % 1 - 0.5))
        ends = smooth(0.47, 0.5, np.abs(((along / 3.2) + (brow % 3) * 0.37) % 1 - 0.5))
        tone = 0.86 + 0.26 * vnoise(brow * 5.3, np.floor(along / 3.2 + (brow % 3) * 0.37) * 3.7, 1.0, 22)
        G = G * (1 - wood[..., None]) + G * wood[..., None] * tone[..., None] * (1 - joint * np.maximum(j, ends)[..., None])
    # plaza emblem (V3 mosaic): rings + spokes inside 0.5 R
    er = pd / (R * 0.5)
    a = np.arctan2(Y, X)
    nsp = {"flag": 8, "plank": 16, "tile": 12}[B["pattern"]]
    spoke = smooth(0.80, 0.93, np.abs(np.cos(a * nsp / 2)))
    ringm = np.maximum(1 - smooth(0.0, 0.045, np.abs(er - 0.95)), 1 - smooth(0.0, 0.035, np.abs(er - 0.42)))
    emb = np.clip(np.maximum(ringm, spoke * (er < 0.95) * (er > 0.42)) + (er < 0.14), 0, 1) * (er < 1.0)
    acc = col("basin", 0.75) if B["pattern"] != "plank" else road * 0.55
    G = G * (1 - 0.62 * emb[..., None]) + acc[None, None, :] * 0.62 * emb[..., None]
    if v >= 3:   # a second, larger flat emblem: a dark inlaid border ring with pointed studs round the plaza rim
        rim = (1 - smooth(0.0, 0.22, np.abs(pd - (R - 1.3)))) + (1 - smooth(0.0, 0.5, np.abs(pd - (R - 1.3)))) * smooth(0.6, 0.95, np.cos(a * 24)) * 0.8
        G = G * (1 - 0.5 * np.clip(rim, 0, 1)[..., None]) + acc[None, None, :] * 0.5 * np.clip(rim, 0, 1)[..., None]
    # moss at walls, lantern wear, contact shade
    for (sx, sy, sr, kind) in list(sites) + list(plan["extra"]):
        d = np.hypot(X - sx, Y - sy)
        if kind == "wall":
            mm = (1 - smooth(sr - 0.2, sr + 1.6, d + ragged * 0.6)) * (1 - np.maximum(stone, wood)) * 0.5
            G = G * (1 - mm[..., None]) + (col("alt") * 0.45 + col("basin") * 0.3)[None, None, :] * mm[..., None]
        if v >= 2:
            if kind == "tree":
                mm = (1 - smooth(sr + 0.4, sr + 2.4, d + ragged * 0.8)) * 0.55
                G = G * (1 - mm[..., None]) + (col("alt") * 0.4 + col("basin") * 0.25)[None, None, :] * mm[..., None]
            sh = (1 - smooth(sr - 0.3, sr + 1.5, d)) * (0.42 if kind != "tree" else 0.3)
            G = G * (1 - sh[..., None])
    wd = np.hypot(X + 7.5, Y)
    wear = (1 - smooth(1.2, 3.4, wd + ragged * 0.6)) * 0.45
    G = G * (1 - wear[..., None]) + np.array([0.09, 0.07, 0.08], np.float32)[None, None, :] * wear[..., None]
    if B["pattern"] == "plank":   # the tidal flats: dark, wet mud outside the quays
        quay = np.maximum(np.maximum(wood, stone), (pd < R + 10).astype(np.float32) * 0.0)
        flat = (1 - np.clip(quay * 3, 0, 1)) * smooth(R + (7.5 if v >= 2 else 3.0), R + (10.5 if v >= 2 else 5.0), pd) * smooth(LANE_HW + 2.5, LANE_HW + 5.0, np.abs(Y) + (X > 0) * 99)
        flat = np.maximum(flat, (1 - np.clip(quay * 3, 0, 1)) * smooth(R + 8.0, R + 11.0, pd) * (X > -R))
        mud = col("path") * 0.35 + col("basin") * 0.42
        G = G * (1 - 0.85 * flat[..., None]) + mud[None, None, :] * (0.8 + 0.3 * n3[..., None]) * 0.85 * flat[..., None]
        WET = 0.55 * flat
        if c.get("tweak4"):   # the tide: shallow water over the outer flats (the game's water sheet at mid tide), rippled
            sea = flat * smooth(0.42, 0.6, vnoise(X, Y, 19.0, 31) * 0.55 + smooth(R + 14.0, R + 30.0, pd) * 0.6)
            ripple = 0.85 + 0.3 * vnoise(X * 0.35, Y * 2.2, 1.1, 33)
            water = np.array(hexrgb("#1E5A78"), np.float32) * 0.62
            G = G * (1 - 0.9 * sea[..., None]) + water[None, None, :] * ripple[..., None] * 0.9 * sea[..., None]
            WET = np.maximum(WET, sea)
        rgb = (np.clip(G * bright, 0, 1) * 255 + 0.5).astype(np.uint8)
        return np.dstack([rgb, (255 - WET * 235).astype(np.uint8)])
    return (np.clip(G * bright, 0, 1) * 255 + 0.5).astype(np.uint8)
