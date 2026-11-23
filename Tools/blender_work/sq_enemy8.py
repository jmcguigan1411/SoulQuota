# Soul Quota - enemy and boss takes rebuilt on KayKit's hand-animated takes (Nov 19 2026), same builder as Remaster 8.
# Works on /tmp/enemy8.blend (a copy of the pre-8 reaper file: same rig, all KayKit takes). Never touches Reaper_Custom.
# sq_export_bodies8.py then writes these takes into Skeleton_<Body>.fbx.
import sys
WORK = "/tmp/enemy8.blend"
_s8 = open("/home/claude/blender_work/sq_anims8.py").read()
_s8 = _s8.replace('shutil.copyfile(PRE8, "/home/claude/sq/ArtSource/Characters/Reaper_Custom.blend")', 'shutil.copyfile(PRE8, WORK)')
_s8 = _s8.replace('_src = open("/home/claude/blender_work/sq_anims2.py").read()', '_src = open("/home/claude/blender_work/sq_anims2.py").read().replace("/home/claude/sq/ArtSource/Characters/Reaper_Custom.blend", WORK)')
exec(compile(_s8, "sq_anims8", "exec"))
for _p in ("b_attack", "c_kit", "d_idle", "e_state"):
    exec(open("/home/claude/blender_work/sq_anims8_%s.py" % _p).read())
OLDN = {a.name: int(a.frame_range[1]) for a in bpy.data.actions}
import bpy as _b
with _b.data.libraries.load("/home/claude/blender_work/sq_enemy_anims.blend") as (_src_e, _dst_e):
    _old_enemy = list(_src_e.actions)
POST["_N"] = dict(NEUT); POST["_H"] = dict(NEUT); POST["_Q"] = dict(NEUT)
HEAVY.add("_H"); QUICK.add("_Q")
ENEMY8 = []
_PN = from_basis(NEUT)
SIDE_X = {"l": 1.0 if head_w(_PN, "upperarm.l").x > 0 else -1.0}; SIDE_X["r"] = -SIDE_X["l"]
def _raise(P, sd, fwd, out=0.0, bend=0.0):
    sx = -1.0 if sd == "l" else 1.0
    rot_subtree(P, "upperarm." + sd, Quaternion((1, 0, 0), math.radians(-fwd)) @ Quaternion((0, 1, 0), math.radians(sx * out)))
    if bend: rot_subtree(P, "lowerarm." + sd, Quaternion((1, 0, 0), math.radians(-bend)))

def _aim(P, sd, tgt, w=1.0, bend=0.0):
    """swing the whole arm so the upper arm points along tgt (armature space: -Y is forward, +Z up, +X the character's left... sign by side)"""
    a = head_w(P, "upperarm." + sd); b = head_w(P, "lowerarm." + sd)
    d = (b - a).normalized(); t = Vector(tgt).normalized()
    q = d.rotation_difference(t)
    rot_subtree(P, "upperarm." + sd, Quaternion().slerp(q, w))
    if bend:
        a = head_w(P, "lowerarm." + sd); b = head_w(P, "wrist." + sd)
        ax = (b - a).normalized().cross(Vector((0, 0, 1)))
        if ax.length > 1e-4: rot_subtree(P, "lowerarm." + sd, Quaternion(ax.normalized(), math.radians(bend if sd == "l" else -bend)))
def _sx(sd):
    return SIDE_X[sd]

def gait(name, base, N, k, layer, stride=1.0, lift=1.0, width=1.0, warp=0.3, spring=True, post="_N"):
    C = dict(k=k, ref=mean_ref(base), post={}, lag={"chest": 0.5}, stride=stride, lift=lift, width=width,
             spring=({"head": (2.6, 0.4), "lowerarm.l": (3.5, 0.4), "lowerarm.r": (3.5, 0.4), "wrist.l": (4.0, 0.35), "wrist.r": (4.0, 0.35)} if spring else None))
    lm = loopmap(base, N); L8 = len(seq(base)[1]) - 1
    tm = lambda f: lm(f) - warp * L8 / (4 * math.pi) * math.sin(4 * math.pi * (f - 1) / (N - 1) + 0.6)
    build8(name, N, True, base, tm, C, post, layer)
    ENEMY8.append(name)

# ---------------------------------------------------------------- horde locomotion (seen from the front and above, coming at you)
def L_shamble(P, f, u):            # zombie: both arms reaching forward at different heights, head lolling, lurching onto each step
    ph = 2 * math.pi * u
    body8(P, hr=(9, 5 * math.sin(ph), 0), ch=(8 + 3 * math.sin(2 * ph), -7 * math.sin(ph), 6 * math.sin(ph)), hd=(-10, 20 * math.sin(ph + 0.9), 0))
    _aim(P, "l", (_sx("l") * 0.38, -1, 0.55 + 0.14 * math.sin(ph + 0.5)), 0.92, bend=12)
    _aim(P, "r", (_sx("r") * 0.45, -1, 0.12 + 0.16 * math.sin(ph + 2.2)), 0.92, bend=30)

def L_lurch(P, f, u):              # one bad leg: drops hard onto it, the far arm hangs dead, the near one gropes ahead
    ph = 2 * math.pi * u
    d = max(0.0, math.sin(ph)) ** 2
    body8(P, hip=(0.03 * d, 0, -0.07 * d), hr=(6, 12 * d, 0), ch=(14 + 6 * d, -9 * d, 0), hd=(16, -12 * d, 10 * math.sin(ph * 0.5)))
    _aim(P, "l", (_sx("l") * 0.2, -1, -0.05 + 0.2 * d), 0.9, bend=18)
    _aim(P, "r", (_sx("r") * 0.12, 0.12 * math.sin(ph - 0.8), -1), 0.85)

def L_scuttle(P, f, u):            # ghoul: bent double, clawing at the ground ahead one hand after the other, eyes on you
    ph = 2 * math.pi * u
    body8(P, hip=(0, 0, -0.13), hr=(26, 0, 6 * math.sin(ph)), ch=(12, 0, -10 * math.sin(ph)), hd=(-34, 0, 0))
    _aim(P, "l", (_sx("l") * 0.35, -1, -0.15 + 0.45 * math.sin(ph)), 0.95, bend=35)
    _aim(P, "r", (_sx("r") * 0.35, -1, -0.15 - 0.45 * math.sin(ph)), 0.95, bend=35)

def L_sneak(P, f, u):              # gravedigger: knees bent, hands up by the chest, head swivelling
    ph = 2 * math.pi * u
    body8(P, hip=(0, 0, -0.08), hr=(10, 0, 0), ch=(8, 0, 5 * math.sin(ph)), hd=(-8, 0, 22 * math.sin(ph * 0.5)))
    _aim(P, "l", (_sx("l") * 0.45, -0.6, -0.65), 0.9, bend=95); _aim(P, "r", (_sx("r") * 0.45, -0.6, -0.65), 0.9, bend=95)

def L_waddle(P, f, u):             # the fat ones: rock from foot to foot with a bounce, arms held off the belly and swinging
    ph = 2 * math.pi * u
    body8(P, hip=(0.05 * math.sin(ph), 0, 0.02 * abs(math.sin(ph))), hr=(0, 13 * math.sin(ph), 0), ch=(-4, -8 * math.sin(ph), 5 * math.sin(ph)), hd=(0, -8 * math.sin(ph), 0))
    arms_out(P, 26 + 8 * math.sin(2 * ph))

def L_limp(P, f, u):
    ph = 2 * math.pi * u
    d = max(0.0, math.sin(ph)) ** 2
    body8(P, hip=(0, 0, -0.05 * d), hr=(4, 8 * d, 0), ch=(8 * d, -6 * d, 0), hd=(6 * d, 0, 0))
    arms_out(P, 14)
def L_swagger(P, f, u):            # boss strut: chest out, shoulders rolling, arms wide
    ph = 2 * math.pi * u
    body8(P, hr=(-4, 4 * math.sin(ph), -6 * math.sin(ph)), ch=(-10, 0, 13 * math.sin(ph)), hd=(-4, 0, -8 * math.sin(ph)))
    arms_out(P, 30)
def L_shuffle(P, f, u):
    ph = 2 * math.pi * u
    body8(P, hr=(8, 0, 0), ch=(10, 0, 3 * math.sin(ph)), hd=(8 + 4 * math.sin(2 * ph), 0, 0))
    arms_out(P, 6)
def L_gallop(P, f, u):             # hound-like: thrown forward, both arms reaching then raking back together
    ph = 2 * math.pi * u
    body8(P, hip=(0, 0, -0.05 + 0.05 * math.sin(2 * ph)), hr=(24, 0, 0), ch=(8 * math.sin(2 * ph), 0, 0), hd=(-26, 0, 0))
    z = -0.35 + 0.55 * math.sin(2 * ph)
    _aim(P, "l", (_sx("l") * 0.3, -1, z), 0.95, bend=20); _aim(P, "r", (_sx("r") * 0.3, -1, z), 0.95, bend=20)

def L_scurry(P, f, u):             # bomber: tiny frantic steps, arms hugging the bomb, head jerking round to look at the fuse
    ph = 2 * math.pi * u
    body8(P, hip=(0, 0, 0.02 * abs(math.sin(ph))), hr=(3, 5 * math.sin(ph), 4 * math.sin(ph)), ch=(-4, 0, 0), hd=(-6, 0, 24 * math.sin(ph * 0.5)))
    _aim(P, "l", (_sx("l") * 0.35, -0.75, -0.6), 0.9, bend=90); _aim(P, "r", (_sx("r") * 0.35, -0.75, -0.6), 0.9, bend=90)

def L_heavy(P, f, u):              # brute: shoulders rolling like a bull, head low
    ph = 2 * math.pi * u
    body8(P, hr=(12, 7 * math.sin(ph), -5 * math.sin(ph)), ch=(8, -5 * math.sin(ph), 12 * math.sin(ph)), hd=(-10, 0, -6 * math.sin(ph)))
    arms_out(P, 36)
def L_sprint(P, f, u):             # runner: flat out, arms pumping, leaning into it
    ph = 2 * math.pi * u
    body8(P, hr=(20, 0, -6 * math.sin(ph)), ch=(4, 0, 10 * math.sin(ph)), hd=(-18, 0, -5 * math.sin(ph)))
    arms_out(P, 16)
def L_hop(P, f, u):
    a = math.sin(math.pi * u)
    body8(P, ch=(-8 * a, 0, 0), hd=(-6 * a, 0, 0)); arms_out(P, 10 + 40 * a)

def build_all():
    gait("SQ_Enemy_Shamble", "Walking_D_Skeletons", OLDN.get("Walking_D_Skeletons", 41), dict(up=1.4, arm=0.5, head=1.2, hipl=1.9), L_shamble, stride=0.95, lift=0.8, width=1.25, warp=0.55)
    gait("SQ_Enemy_Lurch", "Walking_C", 39, dict(up=1.5, arm=0.4, head=1.0, hipl=2.2), L_lurch, stride=0.9, lift=0.8, width=1.4, warp=0.6)
    gait("SQ_Enemy_Scuttle", "Running_C", 13, dict(up=1.2, arm=0.3, head=0.8, hipl=1.2), L_scuttle, stride=1.1, lift=0.9, width=1.5, warp=0.3)
    gait("SQ_Walk_Sneak", "Walking_B", 33, dict(up=1.3, arm=0.2, head=1.0, hipl=1.6), L_sneak, stride=0.9, lift=1.5, width=1.2, warp=0.5)
    gait("SQ_Walk_Waddle", "Walking_C", 31, dict(up=1.6, arm=1.2, head=0.8, hipl=2.4), L_waddle, stride=0.85, lift=0.9, width=1.9, warp=0.4)
    gait("SQ_Walk_Limp", "Walking_C", 37, dict(up=1.5, arm=1.0, head=0.9, hipl=2.2), L_limp, stride=0.9, lift=0.8, width=1.3, warp=0.6)
    gait("SQ_Walk_Swagger", "Walking_B", 31, dict(up=1.6, arm=1.5, head=0.8, hipl=2.0), L_swagger, stride=1.2, lift=1.3, width=1.4, warp=0.35)
    gait("SQ_Walk_Shuffle", "Walking_A", 25, dict(up=1.2, arm=0.6, head=1.0, hipl=1.2), L_shuffle, stride=0.6, lift=0.5, width=1.0, warp=0.2)
    gait("SQ_Run_Gallop", "Running_C", 15, dict(up=1.3, arm=0.2, head=0.8, hipl=1.6), L_gallop, stride=1.35, lift=1.2, width=1.2, warp=0.5)
    gait("SQ_Run_Scurry", "Running_C", 11, dict(up=1.2, arm=0.2, head=0.9, hipl=1.2), L_scurry, stride=0.8, lift=0.9, width=0.9, warp=0.2)
    gait("SQ_Run_Heavy", "Running_A", 23, dict(up=1.7, arm=1.4, head=0.8, hipl=2.6), L_heavy, stride=1.15, lift=1.0, width=1.7, warp=0.5)
    gait("SQ_Run_Sprint", "Running_A", 15, dict(up=1.4, arm=1.5, head=0.8, hipl=1.5), L_sprint, stride=1.3, lift=1.3, width=1.0, warp=0.3)
    gait("SQ_Enemy_Hop", "Jump_Full_Short", OLDN.get("Jump_Full_Short", 25), dict(up=1.3, arm=1.2, head=1.0, hipl=1.1), L_hop, warp=0.0)
    # ---- attacks: the wind-up is a held, readable pose (the horde's attack clip loops on a shared leader, so the pose is the tell)
    def atk(name, rid, base, N, I, big=1.2, O=None):
        o = dict(O or {}); o.setdefault("hold_anti", 3)
        shot8(name, rid, base, N, I, o, big=big); ENEMY8.append(name)
    atk("SQ_Enemy_Swipe", "_N", "1H_Melee_Attack_Slice_Horizontal", OLDN.get("SQ_Enemy_Swipe", 31) if "SQ_Enemy_Swipe" in OLDN else 31, 12)
    atk("SQ_Attack_Chop_R2", "_H", "1H_Melee_Attack_Chop", OLDN.get("SQ_Attack_Chop_R2", 31), 13, big=1.3)
    atk("SQ_Attack_Slash_R2", "_Q", "2H_Melee_Attack_Slice", OLDN.get("SQ_Attack_Slash_R2", 24), 9)
    atk("SQ_Attack_Punch_R2", "_Q", "Unarmed_Melee_Attack_Punch_B", OLDN.get("SQ_Attack_Punch_R2", 24), 9)
    atk("SQ_Cast_Shoot_R2", "_N", "Spellcast_Shoot", OLDN.get("SQ_Cast_Shoot_R2", 24), 11)
    atk("SQ_Ability_Stampede", "_H", "2H_Melee_Attack_Chop", OLDN.get("SQ_Ability_Stampede", 36), 15, big=1.7)
    # ---- hits: enemies are thrown further than the reapers flinch
    for nm, base, kk in (("SQ_Hit_A_R2", "Hit_A", 2.1), ("SQ_Hit_B_R2", "Hit_B", 1.9)):
        N = OLDN.get(nm, 14)
        keys = [(1, 1, "s"), (3, 6, "o"), (6, 9, "s"), (N, 21 if base == "Hit_A" else 27, "x")]
        C = dict(k=dict(up=kk, arm=kk, head=1.0, hipl=1.8), post={}, spring={"head": (3.5, 0.5), "lowerarm.l": (4.5, 0.4), "lowerarm.r": (4.5, 0.4)})
        def layer(P, f, u, N=N):
            d = K8([(1, 0, "s"), (3, 1, "o"), (7, 0.5, "s"), (N, 0, "s")], f)
            body8(P, hip=(0, 0.09 * d, 0), ch=(-16 * d, 0, 0), hd=(-10 * d, 0, 0)); arms_out(P, 30 * d)
        a = build8(nm, N, False, base, lambda f, keys=keys: K8(keys, f), C, "_N", layer)
        set_scale_keys(a, "head", [(1, (1, 1, 1)), (2, (1.14, 0.84, 1.14)), (4, (0.93, 1.1, 0.93)), (8, (1, 1, 1)), (N, (1, 1, 1))], N)
        ENEMY8.append(nm)
    # the boss roar stays the Nov 7 take: the hand-animated long taunt is a jump-spin and read worse as a roar (iteration 1)
build_all()
bpy.ops.wm.save_as_mainfile(filepath=WORK)
# ---- audit: loop seams and feet under the ground
nz = min(head_w(from_basis(NEUT), "foot.l").z, head_w(from_basis(NEUT), "foot.r").z)
bad = []
for n in ENEMY8:
    a = bpy.data.actions[n]; N = int(a.frame_range[1])
    Ps = [eval_pose(a, f) for f in range(1, N + 1)]
    mz = min(min(head_w(P, "foot.l").z, head_w(P, "foot.r").z) for P in Ps)
    m = ""
    if mz < nz - 0.03: m += " feet %.2f under" % (nz - mz)
    if a.get("sq_loop"):
        g = sum((head_w(Ps[-1], b) - head_w(Ps[0], b)).length for b in ("hand.l", "hand.r", "head", "hips"))
        d0 = sum((head_w(Ps[1], b) - head_w(Ps[0], b)).length for b in ("hand.l", "hand.r", "head", "hips"))
        d1 = sum((head_w(Ps[-1], b) - head_w(Ps[-2], b)).length for b in ("hand.l", "hand.r", "head", "hips"))
        if g > 0.01: m += " seam gap %.3f" % g
        if abs(d0 - d1) > 0.6 * max(d0, d1, 0.02): m += " seam speed %.3f/%.3f" % (d0, d1)
    if m: bad.append(n + ":" + m)
print("EAUDIT", len(bad), bad)
print("ENEMY8", len(ENEMY8), ENEMY8)
print("OLD ENEMY", _old_enemy)
