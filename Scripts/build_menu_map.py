"""
Soul Quota - builds the MainMenu map: a dim sky light, thin purple fog and a fixed-exposure post process volume.
Everything you see in it (the graveyard nook, the reaper, the lights) is spawned by C++ (ASQMenuHUD -> ASQMenuShowroom).
Its World Settings set the game mode to SQMenuGameMode (no pawn, the menu HUD), overriding the project's
GlobalDefaultGameMode (SQGameMode). Needs the C++ built first (so /Script/SoulQuota.SQMenuGameMode exists).
setup_project.py runs it unattended (logs only). Safe to re-run: skips if the map already exists.
"""
import unreal

MAP_PATH = "/Game/Maps/MainMenu"
MENU_GAME_MODE = "/Script/SoulQuota.SQMenuGameMode"

level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def log(msg):
    unreal.log("[SoulQuota menu map] " + msg)


def try_set(obj, prop, value):
    try:
        obj.set_editor_property(prop, value)
    except Exception as err:  # property names can shift between engine versions
        unreal.log_warning("[SoulQuota menu map] could not set {} on {}: {}".format(prop, obj.get_name(), err))


def spawn(actor_class, location=(0, 0, 0), rotation=None):
    rot = rotation if rotation else unreal.Rotator(0.0, 0.0, 0.0)
    return actor_subsystem.spawn_actor_from_class(actor_class, unreal.Vector(*location), rot)


def world_settings_of_current_level():
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    for getter in ("get_world_settings", "k2_get_world_settings"):
        fn = getattr(world, getter, None)
        if fn:
            try:
                ws = fn()
                if ws:
                    return ws
            except Exception:
                pass
    found = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)
    return found[0] if found else None


def build():
    if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        log("MainMenu map already exists - skipped")
        return

    # setup_project.py may have left the Graveyard open: start a fresh level for the menu
    if not level_subsystem.new_level(MAP_PATH):
        unreal.log_error("[SoulQuota menu map] could not create " + MAP_PATH)
        return

    # a dim, cool ambient light (the showroom brings its own moon, lantern and fill lights)
    sky = spawn(unreal.SkyLight, (0, 0, 600))
    sky_light = sky.get_component_by_class(unreal.SkyLightComponent)
    try_set(sky_light, "mobility", unreal.ComponentMobility.MOVABLE)
    try_set(sky_light, "real_time_capture", False)
    try_set(sky_light, "intensity", 0.35)
    try_set(sky_light, "light_color", unreal.Color(r=150, g=160, b=220, a=255))
    cubemap_path = "/Engine/MapTemplates/Sky/SunsetAmbientCubemap"
    if unreal.EditorAssetLibrary.does_asset_exist(cubemap_path):
        cubemap = unreal.load_asset(cubemap_path)
        if cubemap:
            try_set(sky_light, "source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
            try_set(sky_light, "cubemap", cubemap)

    # thin midnight-purple fog: the far graves fade into it and it fills the empty sky
    fog = spawn(unreal.ExponentialHeightFog, (0, 0, -200))
    fog_comp = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    try_set(fog_comp, "fog_density", 0.01)
    try_set(fog_comp, "fog_height_falloff", 0.2)
    try_set(fog_comp, "fog_inscattering_luminance", unreal.LinearColor(0.06, 0.04, 0.12, 1.0))

    # fixed exposure like the Graveyard map (the showroom camera sets the same, this is the backup)
    post = spawn(unreal.PostProcessVolume, (0, 0, 0))
    try_set(post, "unbound", True)
    settings = post.get_editor_property("settings")
    try_set(settings, "override_auto_exposure_method", True)
    try_set(settings, "auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
    try_set(settings, "override_auto_exposure_bias", True)
    try_set(settings, "auto_exposure_bias", 11.0)
    try_set(settings, "override_vignette_intensity", True)
    try_set(settings, "vignette_intensity", 0.6)
    try_set(post, "settings", settings)

    # World Settings > GameMode Override = SQMenuGameMode
    menu_mode = unreal.load_class(None, MENU_GAME_MODE)
    ws = world_settings_of_current_level()
    if menu_mode and ws:
        try_set(ws, "default_game_mode", menu_mode)
        log("game mode override set to SQMenuGameMode")
    else:
        unreal.log_error("[SoulQuota menu map] could not set the game mode override (class found: %s, world settings found: %s). "
                         "Build the C++ first, then set World Settings > GameMode Override = SQMenuGameMode by hand."
                         % (bool(menu_mode), bool(ws)))

    level_subsystem.save_current_level()
    log("MainMenu map created at " + MAP_PATH)


build()
