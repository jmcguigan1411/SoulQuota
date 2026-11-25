"""Imports the CC0 KayKit art in ArtSource/ into /Game/Art (run inside the editor via setup_project.py).

Result (paths the C++ looks for; anything missing just falls back to primitive shapes):
  /Game/Art/Textures/T_Skeleton, T_HalloweenBits, T_<Model> (palette of each original model, see MODEL_TEXTURES)
  /Game/Art/Materials/M_ArtTint            (texture param Tex * vector param Color, scalar Glow -> emissive)
  /Game/Art/Characters/<Model>/SK_<Model>  (+ A_<AnimName> animations, only the ones the game uses)
  /Game/Art/Characters/<Model>/SK_<Model>  for MESH_ONLY_MODELS: mesh only, on the owner's skeleton (no A_ copies)
  /Game/Art/Weapons/SM_Axe, SM_Blade, SM_Staff
  /Game/Art/Props/SM_<name>

Safe to re-run: existing assets are skipped. Delete /Game/Art to force a full re-import.
"""
import os
import re
import unreal

ROOT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "ArtSource")
CHAR_DIR = os.path.join(ROOT, "Characters")
PROP_DIR = os.path.join(ROOT, "Props")

MODELS = ["Minion", "Warrior", "Rogue", "Mage", "Reaper", "Gravebat"]
# Source file per model (Reaper = our original character, rigged to the KayKit skeleton;
# Gravebat = original flying enemy with its own small rig and 6 takes)
MODEL_FILES = {"Reaper": "Reaper_Custom.fbx", "Gravebat": "Gravebat.fbx"}
# Original meshes skinned to an existing KayKit skeleton: imported mesh-only onto the owner's Skeleton asset
# (/Game/Art/Characters/<Owner>/), so they play every animation of the owner. Model -> (file, owner model)
MESH_ONLY_MODELS = {
    "R_Grim": ("R_Grim.fbx", "Reaper"),
    "R_Mort": ("R_Mort.fbx", "Reaper"),
    "R_Widow": ("R_Widow.fbx", "Reaper"),
    "R_Bonesmith": ("R_Bonesmith.fbx", "Reaper"),
    "R_Keeper": ("R_Keeper.fbx", "Reaper"),
    "R_Monk": ("R_Monk.fbx", "Reaper"),
    "R_Intern": ("R_Intern.fbx", "Reaper"),
    "R_Femur": ("R_Femur.fbx", "Reaper"),
    "R_Digby": ("R_Digby.fbx", "Reaper"),
    "R_Collector": ("R_Collector.fbx", "Reaper"),
    "R_Beak": ("R_Beak.fbx", "Reaper"),
    "R_Tallow": ("R_Tallow.fbx", "Reaper"),
    "R_Lookout": ("R_Lookout.fbx", "Reaper"),
    "R_Vexa": ("R_Vexa.fbx", "Reaper"),
    "R_Actuary": ("R_Actuary.fbx", "Reaper"),
    "R_Sweep": ("R_Sweep.fbx", "Reaper"),
    "R_Solace": ("R_Solace.fbx", "Reaper"),
    "R_Pallbearers": ("R_Pallbearers.fbx", "Reaper"),
    "R_Ectoplasm": ("R_Ectoplasm.fbx", "Reaper"),
    "R_Neckless": ("R_Neckless.fbx", "Reaper"),
    "R_Tickwell": ("R_Tickwell.fbx", "Reaper"),
    "Zombie": ("Zombie.fbx", "Minion"),
    "ZombieBloat": ("ZombieBloat.fbx", "Minion"),
    "ZombieRunner": ("ZombieRunner.fbx", "Minion"),
    "Wraith": ("Wraith.fbx", "Mage"),
}
# Palette textures of the original models -> /Game/Art/Textures/T_<Model>
MODEL_TEXTURES = {
    "R_Grim": "r_grim_palette.png",
    "R_Mort": "r_mort_palette.png",
    "R_Widow": "r_widow_palette.png",
    "R_Bonesmith": "r_bonesmith_palette.png",
    "R_Keeper": "r_keeper_palette.png",
    "R_Monk": "r_monk_palette.png",
    "R_Intern": "r_intern_palette.png",
    "R_Femur": "r_femur_palette.png",
    "R_Digby": "r_digby_palette.png",
    "R_Collector": "r_collector_palette.png",
    "R_Beak": "r_beak_palette.png",
    "R_Tallow": "r_tallow_palette.png",
    "R_Lookout": "r_lookout_palette.png",
    "R_Vexa": "r_vexa_palette.png",
    "R_Actuary": "r_actuary_palette.png",
    "R_Sweep": "r_sweep_palette.png",
    "R_Solace": "r_solace_palette.png",
    "R_Pallbearers": "r_pallbearers_palette.png",
    "R_Ectoplasm": "r_ectoplasm_palette.png",
    "R_Neckless": "r_neckless_palette.png",
    "R_Tickwell": "r_tickwell_palette.png",
    "Reaper": "reaper_palette.png",
    "Zombie": "zombie_palette.png",
    "ZombieBloat": "zombiebloat_palette.png",
    "ZombieRunner": "zombierunner_palette.png",
    "Wraith": "wraith_palette.png",
    "Gravebat": "gravebat_palette.png",
}
# A take only the latest remastered FBX files contain: if a model's FBX has it but its folder has no
# A_<marker> yet, the animations are re-imported onto the existing skeleton.
REIMPORT_MARKER = "SQ_Marker_Remaster9"  # Nov 19 2026: all-21 character layers, stride matching, victory/defeat/bank/landing/dash on hand-animated bases, enemy takes rebuilt (Remaster 8: quality pass on the hand-animated KayKit takes)
MIN_ANIMS = {"Gravebat": 6}  # models with fewer takes than the KayKit sets (default 20)
KEEP_ALL_ANIMS = True  # Jack wants more animations: keep every take
WEAPONS = ["Axe", "Blade", "Staff", "Scythe"]
WEAPON_FILES = {"Scythe": "Reaper_Scythe.fbx"}
# Monster roster (Nov 9 2026): one original weapon per monster, UV'd to its R_<Id> palette (SQCharacter: W_<Id> -> T_R_<Id>)
ROSTER_WEAPONS = ["Neckless", "Bonesmith", "Pallbearers", "Beak", "Tickwell", "Collector", "Monk", "Femur", "Digby", "Keeper",
                  "Intern", "Solace", "Mort", "Actuary", "Ectoplasm", "Vexa"]
for _rid in ROSTER_WEAPONS:
    WEAPONS.append("W_" + _rid)
    WEAPON_FILES["W_" + _rid] = "W_%s.fbx" % _rid
# Effect kit meshes (ArtSource/FX/SM_FX_<Name>.fbx, built by blender_work/fx/fx_meshes.py); names match ESQFxShape in SQFx.h
FX_DIR = os.path.join(os.path.dirname(CHAR_DIR), "FX")
FX_MESHES = ["Crescent", "Ring", "Flame", "Puff", "Spiral", "Shard", "Bolt", "Star", "Bat", "Crow", "Moth", "Leaf", "Feather", "Rune", "Seal",
             "Cleaver", "Claw", "Bandage", "Seed", "Drop",
             # motion kit (Nov 19 2026, effects pass): tapered swing arcs, needles, wave / toothed rings, starburst, motes and thin-line ground marks
             "Swoosh", "Arc", "Streak", "Wave", "Spikes", "Ember", "Petal", "Chevron", "Wisp", "Teeth", "Sigil", "Cog", "Web", "Casket",
             # pickups (Nov 10 2026): XP crystal ladder + gold ladder (ASQHordeManager::PickupTierISMs)
             "Crystal1", "Crystal2", "Crystal3", "Crystal4", "Coin", "CoinStack", "Purse", "GoldBar"]
# Bump when the R_<Id> bodies / palettes / W_<Id> weapons are rebuilt: the old assets are deleted once and re-imported.
ROSTER_VERSION = "roster4_monsters"

KEEP_ANIMS = {
    "Idle", "Idle_B", "Idle_Combat", "2H_Melee_Idle", "Unarmed_Idle",
    "Running_A", "Running_B", "Running_C",
    "Walking_A", "Walking_B", "Walking_C", "Walking_D_Skeletons",
    "Death_A", "Death_A_Pose", "Death_B", "Death_B_Pose",
    "Hit_A", "Hit_B", "Dodge_Forward", "Cheer", "Taunt",
    "Spawn_Ground", "Spawn_Ground_Skeletons", "Skeletons_Awaken_Floor",
    "Lie_Down", "Lie_Idle", "Lie_StandUp",
    "1H_Melee_Attack_Chop", "1H_Melee_Attack_Slice_Horizontal", "2H_Melee_Attack_Chop",
    "2H_Melee_Attack_Slice", "2H_Melee_Attack_Spin", "Unarmed_Melee_Attack_Punch_A",
    "Spellcast_Shoot", "Spellcast_Summon", "Spellcast_Raise", "Spellcast_Long",
    "Throw", "PickUp", "Interact",
}

tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary
reg = unreal.AssetRegistryHelpers.get_asset_registry()


def log(msg):
    unreal.log("[SoulQuota art] " + msg)


def exists(path):
    return eal.does_asset_exist(path)


CONTENT_DIR = os.path.join(os.path.dirname(ROOT), "Content")


def drop_if_stale(path, src):
    """Remove <path> when its source file (FBX/PNG) is newer than the saved .uasset, so the importer below brings the
    new version in. The importers skip assets that exist, and delete_directory on Nov 10 2026 dropped the roster bodies
    from memory but left their .uasset files on disk, so new art never arrived. Returns True if it removed something."""
    f = os.path.join(CONTENT_DIR, *path[len("/Game/"):].split("/")) + ".uasset"
    if not (os.path.isfile(f) and os.path.isfile(src)) or os.path.getmtime(src) <= os.path.getmtime(f) + 1:
        return False
    log("%s is older than %s - re-importing" % (path, os.path.basename(src)))
    if exists(path):
        safe(eal.delete_asset, path)
    if os.path.isfile(f):
        try:
            os.remove(f)
        except OSError as e:
            log("WARNING: could not remove %s (%s) - close anything holding it and run setup again" % (f, e))
            return False
    safe(unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous, [path.rsplit("/", 1)[0]], True)
    return True


def run_task(filename, dest_path, dest_name, options=None):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", filename)
    task.set_editor_property("destination_path", dest_path)
    if dest_name:
        task.set_editor_property("destination_name", dest_name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    if options is not None:
        task.set_editor_property("options", options)
    tools.import_asset_tasks([task])
    return list(task.get_editor_property("imported_object_paths") or [])


def assets_in(folder):
    return [a for a in reg.get_assets_by_path(folder, recursive=False)]


def class_name(asset_data):
    try:
        return str(asset_data.asset_class_path.asset_name)
    except Exception:
        return str(asset_data.asset_class)


def obj_path(asset_data):
    return str(asset_data.package_name) + "." + str(asset_data.asset_name)


def rename(src_obj_path, folder, new_name):
    dst = folder + "/" + new_name
    if exists(dst):
        return dst
    if eal.rename_asset(src_obj_path, dst):
        return dst
    log("could not rename %s -> %s" % (src_obj_path, dst))
    return None


# ---------------------------------------------------------------- legacy FBX importer (predictable options)
def use_legacy_fbx(enable):
    # UE 5.5+ routes FBX through Interchange by default; the legacy path honours FbxImportUI options.
    value = "false" if enable else "true"
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX " + value)


def setp(obj, name, value):
    try:
        obj.set_editor_property(name, value)
    except Exception as e:  # property renamed in this engine version: keep going
        log("skip option %s: %s" % (name, e))


def safe(fn, *args):
    try:
        return fn(*args)
    except Exception as e:
        unreal.log_error("[SoulQuota art] %s%s failed: %s" % (fn.__name__, args, e))
        return None


def fbx_options(skeletal, animations):
    ui = unreal.FbxImportUI()
    setp(ui, "import_mesh", True)
    setp(ui, "import_as_skeletal", skeletal)
    setp(ui, "import_animations", animations)
    setp(ui, "import_materials", False)  # the game uses M_ArtTint at runtime
    setp(ui, "import_textures", False)
    setp(ui, "create_physics_asset", False)
    setp(ui, "automated_import_should_detect_type", False)
    setp(ui, "mesh_type_to_import",
                           unreal.FBXImportType.FBXIT_SKELETAL_MESH if skeletal else unreal.FBXImportType.FBXIT_STATIC_MESH)
    if skeletal:
        sk = ui.get_editor_property("skeletal_mesh_import_data")
        setp(sk, "import_morph_targets", False)
        an = ui.get_editor_property("anim_sequence_import_data")
        setp(an, "animation_length", unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
        setp(an, "remove_redundant_keys", False)
    else:
        sm = ui.get_editor_property("static_mesh_import_data")
        setp(sm, "combine_meshes", True)
        setp(sm, "auto_generate_collision", True)
        setp(sm, "generate_lightmap_u_vs", False)
    return ui


# ---------------------------------------------------------------- textures + material
def import_texture(png, name, refresh=False):
    path = "/Game/Art/Textures/" + name
    if refresh:
        drop_if_stale(path, png)
    if not exists(path):
        run_task(png, "/Game/Art/Textures", name)
    tex = eal.load_asset(path)
    if tex:
        # Palette atlases: crisp, no mip bleeding between colour cells
        setp(tex, "filter", unreal.TextureFilter.TF_NEAREST)
        setp(tex, "mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        eal.save_loaded_asset(tex)
    else:
        log("texture import FAILED: " + png)
    return tex


def make_tint_material(default_tex):
    path = "/Game/Art/Materials/M_ArtTint"
    if exists(path):
        return
    mel = unreal.MaterialEditingLibrary
    mat = tools.create_asset("M_ArtTint", "/Game/Art/Materials", unreal.Material, unreal.MaterialFactoryNew())
    tex = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -700, 0)
    tex.set_editor_property("parameter_name", "Tex")
    if default_tex:
        tex.set_editor_property("texture", default_tex)
    col = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -700, 300)
    col.set_editor_property("parameter_name", "Color")
    col.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 1))
    mul = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -400, 100)
    mel.connect_material_expressions(tex, "RGB", mul, "A")
    mel.connect_material_expressions(col, "", mul, "B")
    mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_BASE_COLOR)

    glow = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -400, 350)
    glow.set_editor_property("parameter_name", "Glow")
    glow.set_editor_property("default_value", 0.0)
    emis = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -200, 250)
    mel.connect_material_expressions(mul, "", emis, "A")
    mel.connect_material_expressions(glow, "", emis, "B")
    mel.connect_material_property(emis, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    rough = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -200, 450)
    rough.set_editor_property("r", 0.85)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    for flag in ("used_with_skeletal_mesh", "used_with_instanced_static_meshes"):
        try:
            mat.set_editor_property(flag, True)
        except Exception as e:
            log("material flag %s: %s" % (flag, e))
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    log("created M_ArtTint")


# ---------------------------------------------------------------- characters
ANIM_RE = re.compile(r"^(?:.*_Anim_)?(?:.*\|)?(.+)$")


def clean_anim_name(asset_name, model=None):
    """Take name only. Anim-only imports name assets after the FBX ("Skeleton_Minion_Walking_A"), mesh imports
    after the mesh ("SK_Minion_Anim_Walking_A"); v1.0 shipped A_Skeleton_Minion_* by mistake - strip every form."""
    n = asset_name
    if n.startswith("A_"):
        n = n[2:]
    n = ANIM_RE.match(n).group(1)
    if model:
        stem = os.path.splitext(MODEL_FILES.get(model, "Skeleton_%s.fbx" % model))[0]
        for prefix in (stem + "_", "SK_" + model + "_"):
            if n.startswith(prefix):
                n = n[len(prefix):]
                break
    return re.sub(r"[^A-Za-z0-9_]", "_", n)


def fbx_path_for(model):
    return os.path.join(CHAR_DIR, MODEL_FILES.get(model, "Skeleton_%s.fbx" % model))


def _fbx_has_take(path, take):
    try:
        with open(path, "rb") as f:
            return take.encode("ascii") in f.read()
    except Exception:
        return False


def _anim_assets(folder):
    return [a for a in assets_in(folder) if class_name(a) == "AnimSequence"]


def _skeleton_asset(folder):
    for a in assets_in(folder):
        if class_name(a) == "Skeleton":
            return unreal.load_asset(obj_path(a))
    return None


def _tidy_anims(model, folder, imported_paths):
    """Rename every imported take to A_<take>, replacing older copies, and save each one explicitly.
    v0.9: the old flow deleted the folder and re-imported in one session; the meshes saved but the
    animations did not, so Minion/Warrior/Rogue/Mage lost every animation (no walk cycle in game)."""
    seen = set()
    candidates = []
    for p in imported_paths:
        try:
            a = eal.find_asset_data(str(p).split(".")[0])
            if a is not None and a.is_valid():
                candidates.append(a)
        except Exception:
            pass
    candidates += assets_in(folder)
    kept = 0
    for a in candidates:
        if class_name(a) != "AnimSequence":
            continue
        src = obj_path(a)
        if src in seen:
            continue
        seen.add(src)
        name = str(a.asset_name)
        clean = clean_anim_name(name, model)
        if not (KEEP_ALL_ANIMS or clean in KEEP_ANIMS):
            eal.delete_asset(str(a.package_name))
            continue
        target = "A_" + clean
        if name != target:
            if exists(folder + "/" + target):
                eal.delete_asset(folder + "/" + target)  # older copy of the same take
            rename(src, folder, target)
        kept += 1
    for a in assets_in(folder):
        eal.save_asset(str(a.package_name), only_if_is_dirty=False)
    return kept


def import_character(model):
    folder = "/Game/Art/Characters/" + model
    sk_path = folder + "/SK_" + model
    fbx = fbx_path_for(model)
    if not os.path.isfile(fbx):
        log("missing " + fbx)
        return
    if exists(folder + "/SKEL_" + model):
        # v0.4 first run renamed the skeleton and broke its references: wipe and re-import
        log(model + ": repairing broken skeleton links, re-importing")
        eal.delete_directory(folder)

    skeleton = _skeleton_asset(folder) if exists(sk_path) else None
    if skeleton is not None:
        # v1.0 fix: rename any wrongly prefixed animations in place (no re-import needed)
        badly_named = [a for a in _anim_assets(folder) if str(a.asset_name) != "A_" + clean_anim_name(str(a.asset_name), model)]
        if badly_named:
            log("%s: renaming %d animations to A_<take>..." % (model, len(badly_named)))
            _tidy_anims(model, folder, [])
        anim_count = len(_anim_assets(folder))
        min_anims = MIN_ANIMS.get(model, 20)
        # per model: only an FBX that contains the marker take can ask for a re-import
        wants_new = _fbx_has_take(fbx, REIMPORT_MARKER) and not exists(folder + "/A_" + REIMPORT_MARKER)
        if anim_count >= min_anims and not wants_new:
            log("%s already imported (%d animations)" % (model, anim_count))
            return
        # Mesh + skeleton are fine: import the animations onto the existing skeleton, delete nothing
        log("%s: importing animations onto the existing skeleton (%d there now)..." % (model, anim_count))
        ui = fbx_options(True, True)
        setp(ui, "import_mesh", False)
        setp(ui, "skeleton", skeleton)
        setp(ui, "mesh_type_to_import", unreal.FBXImportType.FBXIT_ANIMATION)
        paths = run_task(fbx, folder, None, ui)
        kept = _tidy_anims(model, folder, paths)
        log("%s: kept %d animations" % (model, kept))
        if kept < min_anims:
            log("WARNING: %s only has %d animations - delete Content/Art/Characters/%s and run setup again" % (model, kept, model))
        return

    if exists(sk_path):
        log(model + ": mesh has no skeleton (broken import), re-importing")
        safe(eal.delete_directory, folder)
    log("importing %s (this one takes a minute)..." % model)
    paths = run_task(fbx, folder, "SK_" + model, fbx_options(True, True))
    for a in assets_in(folder):
        if class_name(a) == "SkeletalMesh" and str(a.asset_name) != "SK_" + model:
            rename(obj_path(a), folder, "SK_" + model)
        # (the Skeleton keeps its imported name: renaming it broke the mesh/animation links)
    kept = _tidy_anims(model, folder, paths)
    if not exists(sk_path):
        log("WARNING: no skeletal mesh found for " + model)
    log("%s: kept %d animations" % (model, kept))


def import_mesh_only(model):
    """Original mesh skinned to a KayKit skeleton: import the mesh only, onto the owner's existing Skeleton asset.
    Result: /Game/Art/Characters/<Model>/SK_<Model>. No animations are copied: SQArt::Anim falls back to the owner's folder."""
    file_name, owner = MESH_ONLY_MODELS[model]
    folder = "/Game/Art/Characters/" + model
    sk_path = folder + "/SK_" + model
    fbx = os.path.join(CHAR_DIR, file_name)
    drop_if_stale(sk_path, fbx)
    if exists(sk_path):
        log("%s already imported" % model)
        return
    if not os.path.isfile(fbx):
        log("missing " + fbx)
        return
    owner_folder = "/Game/Art/Characters/" + owner
    skeleton = _skeleton_asset(owner_folder)
    if skeleton is None:
        log("WARNING: %s skipped - no Skeleton asset in %s (import %s first)" % (model, owner_folder, owner))
        return
    log("importing %s (mesh only, on the %s skeleton)..." % (model, owner))
    ui = fbx_options(True, False)
    setp(ui, "import_mesh", True)
    setp(ui, "import_animations", False)
    setp(ui, "skeleton", skeleton)
    paths = run_task(fbx, folder, "SK_" + model, ui)
    for a in assets_in(folder):
        cls = class_name(a)
        if cls == "SkeletalMesh" and str(a.asset_name) != "SK_" + model:
            rename(obj_path(a), folder, "SK_" + model)
        elif cls == "Skeleton":
            # the mesh should use the owner's skeleton; a new one means the hierarchy did not match
            log("WARNING: %s got its own skeleton %s - check %s against the %s skeleton" % (model, a.asset_name, file_name, owner))
        elif cls == "AnimSequence":
            safe(eal.delete_asset, str(a.package_name))  # mesh-only: animations come from the owner's folder
    if exists(sk_path):
        safe(eal.save_asset, sk_path, False)
        log("%s: imported %s" % (model, sk_path))
    else:
        log("WARNING: no skeletal mesh found for %s (paths: %s)" % (model, paths))


def refresh_roster():
    """Once per ROSTER_VERSION: delete the previous R_<Id> bodies, their T_R_<Id> palettes and the W_<Id> weapons so the
    rebuilt monster roster re-imports (the importers skip assets that already exist)."""
    stamp_dir = os.path.join(os.path.dirname(ROOT), "Saved", "SQImport")
    stamp = os.path.join(stamp_dir, ROSTER_VERSION + ".done")
    if os.path.isfile(stamp):
        return
    gone = 0
    for model in [m for m in MESH_ONLY_MODELS if m.startswith("R_")]:
        for path in ("/Game/Art/Characters/" + model, "/Game/Art/Textures/T_" + model):
            if exists(path) or eal.does_directory_exist(path):
                if eal.does_directory_exist(path):
                    safe(eal.delete_directory, path)
                else:
                    safe(eal.delete_asset, path)
                gone += 1
    for w in WEAPONS:
        if w.startswith("W_") and exists("/Game/Art/Weapons/SM_" + w):
            safe(eal.delete_asset, "/Game/Art/Weapons/SM_" + w)
            gone += 1
    os.makedirs(stamp_dir, exist_ok=True)
    with open(stamp, "w") as f:
        f.write("refreshed %d roster assets\n" % gone)
    log("roster refresh (%s): removed %d old assets" % (ROSTER_VERSION, gone))


def import_static(fbx, folder, name, refresh=False):
    path = folder + "/" + name
    if refresh:
        drop_if_stale(path, fbx)
    if exists(path):
        return
    run_task(fbx, folder, name, fbx_options(False, False))
    if not exists(path):
        # Interchange may ignore destination_name: find the newest static mesh named after the file
        base = os.path.splitext(os.path.basename(fbx))[0]
        for a in assets_in(folder):
            if class_name(a) == "StaticMesh" and str(a.asset_name) in (base, "SM_" + base):
                rename(obj_path(a), folder, name)
                break
    if not exists(path):
        log("static import FAILED: " + fbx)


# ---------------------------------------------------------------- far impostors (flipbook static poses)
# ArtSource/Impostors/<Model>_Walk_<0-3>.fbx -> /Game/Art/Impostors/SM_<Model>_Walk_<i>. Same space as SK_<Model>
# (cm, same axes, rig root/feet at the origin): use the skeletal mesh's transform and scale. No collision.
IMPOSTOR_DIR = os.path.join(ROOT, "Impostors")
IMPOSTOR_FOLDER = "/Game/Art/Impostors"


def import_impostors():
    if not os.path.isdir(IMPOSTOR_DIR):
        log("no Impostors folder - skipping impostors")
        return
    done = 0
    for f in sorted(os.listdir(IMPOSTOR_DIR)):
        if not f.lower().endswith(".fbx"):
            continue
        name = "SM_" + os.path.splitext(f)[0]
        path = IMPOSTOR_FOLDER + "/" + name
        if exists(path):
            continue
        ui = fbx_options(False, False)
        setp(ui.get_editor_property("static_mesh_import_data"), "auto_generate_collision", False)
        run_task(os.path.join(IMPOSTOR_DIR, f), IMPOSTOR_FOLDER, name, ui)
        mesh = eal.load_asset(path) if exists(path) else None
        if mesh is None:
            log("impostor import FAILED: " + f)
            continue
        try:
            unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).remove_collisions(mesh)
        except Exception as e:
            log("impostor %s: could not strip collision: %s" % (name, e))
        eal.save_loaded_asset(mesh)
        done += 1
    log("impostors: imported %d new" % done)


# ---------------------------------------------------------------- skatepark pieces (Park_*.fbx, UCX collision)
# ArtSource/Props/Park_<Name>.fbx -> /Game/Art/Props/SM_Park_<Name> with the authored UCX_ hulls as simple collision
# (one convex hull per UCX, nothing auto-generated). Palette -> /Game/Art/Textures/T_Park,
# material instance MI_Park (M_ArtTint, Tex = T_Park) set on slot 0 so the meshes look right in the editor too.
def make_park_material(tex, mi_name="MI_Park"):
    path = "/Game/Art/Materials/" + mi_name
    if exists(path):
        return eal.load_asset(path)
    parent = eal.load_asset("/Game/Art/Materials/M_ArtTint")
    if parent is None or tex is None:
        log("%s skipped (M_ArtTint or its texture missing)" % mi_name)
        return None
    mi = tools.create_asset(mi_name, "/Game/Art/Materials", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel = unreal.MaterialEditingLibrary
    mel.set_material_instance_parent(mi, parent)
    mel.set_material_instance_texture_parameter_value(mi, "Tex", tex)
    mel.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(1, 1, 1, 1))
    eal.save_loaded_asset(mi)
    log("created " + mi_name)
    return mi


def import_park(prefix="Park_", palette="park_palette.png", tex_name="T_Park", mi_name="MI_Park", auto_collision=False):
    """Park_*.fbx (skate pieces) and Set_*.fbx (Nov 12 2026 landmark set pieces: chapel, lighthouse, clock tower...)
    share this importer: authored UCX hulls, a palette texture and a material instance on slot 0."""
    if not os.path.isdir(PROP_DIR) or not os.path.isfile(os.path.join(PROP_DIR, palette)):
        return
    tex = safe(import_texture, os.path.join(PROP_DIR, palette), tex_name)
    mi = safe(make_park_material, tex, mi_name)
    for f in sorted(os.listdir(PROP_DIR)):
        if not (f.startswith(prefix) and f.lower().endswith(".fbx")):
            continue
        name = "SM_" + os.path.splitext(f)[0]
        path = "/Game/Art/Props/" + name
        drop_if_stale(path, os.path.join(PROP_DIR, f))
        if exists(path):
            continue
        ui = fbx_options(False, False)
        sm = ui.get_editor_property("static_mesh_import_data")
        # Park_ / Set_ carry authored UCX_ hulls; the KayKit packs (KitD_ / KitM_) have none, so UE makes a simple hull
        setp(sm, "auto_generate_collision", bool(auto_collision))
        setp(sm, "one_convex_hull_per_ucx", True)     # each UCX_ is already convex: don't decompose
        setp(sm, "combine_meshes", True)
        run_task(os.path.join(PROP_DIR, f), "/Game/Art/Props", name, ui)
        mesh = eal.load_asset(path) if exists(path) else None
        if mesh is None:
            log("park import FAILED: " + f)
            continue
        try:
            hulls = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).get_convex_collision_count(mesh)
            log("%s: %d UCX hulls" % (name, hulls))
            if hulls == 0 and not auto_collision:
                log("WARNING: %s has no simple collision - check the UCX_%s_## names" % (name, f[:-4]))
        except Exception as e:
            log("%s: collision count unavailable: %s" % (name, e))
        if mi is not None:
            try:
                # Slot 0 = the palette; a second slot named "...Glow" (lantern heads, lit windows: Set_ / Fair_) keeps the
                # same instance here and is swapped for a glowing one at runtime (ASQArena::ApplySetGlow)
                slots = max(1, len(mesh.get_editor_property("static_materials")))
                for slot in range(slots):
                    mesh.set_material(slot, mi)
            except Exception as e:
                log("%s: could not set %s: %s" % (name, mi_name, e))
        eal.save_loaded_asset(mesh)


def main():
    if not os.path.isdir(ROOT):
        log("ArtSource folder not found at " + ROOT + " - skipping art import")
        return
    use_legacy_fbx(True)
    try:
        t_skel = safe(import_texture, os.path.join(CHAR_DIR, "skeleton_texture.png"), "T_Skeleton")
        safe(import_texture, os.path.join(PROP_DIR, "halloweenbits_texture.png"), "T_HalloweenBits")
        safe(refresh_roster)
        for model, png in MODEL_TEXTURES.items():
            if os.path.isfile(os.path.join(CHAR_DIR, png)):
                safe(import_texture, os.path.join(CHAR_DIR, png), "T_" + model, True)
        # Nov 25 2026 skins (Scripts/make_skins.py): ArtSource/Characters/skins/R_<Id>_<Theme>.png -> T_R_<Id>_<Theme> (SQArt::SkinTexture)
        skin_dir = os.path.join(CHAR_DIR, "skins")
        if os.path.isdir(skin_dir):
            for f in sorted(os.listdir(skin_dir)):
                if f.lower().endswith(".png"):
                    safe(import_texture, os.path.join(skin_dir, f), "T_" + os.path.splitext(f)[0], True)
        safe(make_tint_material, t_skel)

        for m in MODELS:
            safe(import_character, m)
        for m in MESH_ONLY_MODELS:  # after the owner models, so their Skeleton assets exist
            safe(import_mesh_only, m)
        for w in WEAPONS:
            safe(import_static, os.path.join(CHAR_DIR, WEAPON_FILES.get(w, "Skeleton_%s.fbx" % w)), "/Game/Art/Weapons", "SM_" + w, True)
        # Effect kit (Nov 10 2026): stylised shapes for SQFx particles (ESQFxShape), ArtSource/FX -> /Game/Art/FX
        for fx in FX_MESHES:
            safe(import_static, os.path.join(FX_DIR, "SM_FX_%s.fbx" % fx), "/Game/Art/FX", "SM_FX_" + fx, True)
        safe(import_impostors)
        safe(import_park)  # before the generic prop loop, so Park_* keep their UCX collision (that loop then skips them)
        safe(import_park, "Set_", "set_palette.png", "T_Set", "MI_Set")  # landmark set pieces (map remaster)
        # Map art pass (Nov 15 2026): more standing scenery in the same style, from two more CC0 KayKit packs
        # (Kay Lousberg; licences in ArtSource/LICENSE_KayKit_*.txt): Dungeon Remastered -> KitD_*, Medieval Hexagon -> KitM_*
        safe(import_park, "KitD_", "kitd_texture.png", "T_KitD", "MI_KitD", True)
        safe(import_park, "KitM_", "kitm_texture.png", "T_KitM", "MI_KitM", True)
        # Map quality pass (Nov 16 2026): original "Midnight Fairground" set pieces (Fair_*: coffin wheel, tent crypt,
        # lantern arch / tree, bunting, podium, wreck, stilt house, boat, clock wall, gear bridge, pendulum, pipes...)
        # and the KayKit atlases recoloured per map (blender_work/mapq/retint.py), used by ASQArena::BuildScenery
        safe(import_park, "Fair_", "fair_palette.png", "T_Fair", "MI_Fair")
        # Nov 25 2026: the bone chest (Chest_Bone body + Chest_BoneLid hinged lid, blender_work/chest/chest_build.py), used by ASQChest
        safe(import_park, "Chest_", "chest_palette.png", "T_Chest", "MI_Chest", True)
        for pack in ("KitM", "KitD"):
            for scheme in ("Graveyard", "Harbour", "Clock"):
                png = os.path.join(PROP_DIR, "%s_%s.png" % (pack.lower(), scheme.lower()))
                if os.path.isfile(png):
                    safe(import_texture, png, "T_%s_%s" % (pack, scheme), True)
        for f in sorted(os.listdir(PROP_DIR)):
            if f.lower().endswith(".fbx"):
                safe(import_static, os.path.join(PROP_DIR, f), "/Game/Art/Props", "SM_" + os.path.splitext(f)[0])
        safe(eal.save_directory, "/Game/Art", True, True)
        log("art import done")
    finally:
        use_legacy_fbx(False)


main()
