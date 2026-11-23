"""
Soul Quota - builds the Graveyard map (lighting + atmosphere only).
The arena itself (floor, walls, gravestones) is spawned by C++ at runtime
from Tuning.csv, so this script only needs to run once.
The values here match the runtime look: ASQArena re-applies moon / sky / fog / grade per biome
from Biomes.csv + Tuning.csv every time the arena builds, so an existing map does not need rebuilding.

Run inside the Unreal Editor:  Tools > Execute Python Script... > pick this file
(or in the Output Log, switch the command box to "Python" and paste the path).
"""
import unreal

MAP_PATH = "/Game/Maps/Graveyard"

level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def try_set(obj, prop, value):
    try:
        obj.set_editor_property(prop, value)
    except Exception as err:  # property names can shift between engine versions
        unreal.log_warning("Could not set {} on {}: {}".format(prop, obj.get_name(), err))


def spawn(actor_class, location=(0, 0, 0), rotation=None):
    rot = rotation if rotation else unreal.Rotator(0.0, 0.0, 0.0)
    return actor_subsystem.spawn_actor_from_class(actor_class, unreal.Vector(*location), rot)


if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
    unreal.log_warning("Graveyard map already exists - opening it instead of rebuilding.")
    level_subsystem.load_level(MAP_PATH)
else:
    level_subsystem.new_level(MAP_PATH)

    # Moonlight: cool, low, dramatic shadows
    moon = spawn(unreal.DirectionalLight, (0, 0, 2000), unreal.Rotator(roll=0.0, pitch=-38.0, yaw=35.0))
    moon_light = moon.get_component_by_class(unreal.DirectionalLightComponent)
    try_set(moon_light, "mobility", unreal.ComponentMobility.MOVABLE)
    try_set(moon_light, "intensity", 3.0)
    try_set(moon_light, "light_color", unreal.Color(r=169, g=196, b=255, a=255))
    try_set(moon_light, "atmosphere_sun_light", True)

    spawn(unreal.SkyAtmosphere, (0, 0, 0))

    sky = spawn(unreal.SkyLight, (0, 0, 500))
    sky_light = sky.get_component_by_class(unreal.SkyLightComponent)
    try_set(sky_light, "mobility", unreal.ComponentMobility.MOVABLE)
    try_set(sky_light, "real_time_capture", True)
    try_set(sky_light, "intensity", 1.6)

    fog = spawn(unreal.ExponentialHeightFog, (0, 0, -100))
    fog_comp = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    # Light fog that starts beyond the playfield (readable fights, moody edges)
    try_set(fog_comp, "fog_density", 0.008)
    try_set(fog_comp, "fog_height_falloff", 0.25)
    try_set(fog_comp, "start_distance", 1800.0)
    try_set(fog_comp, "fog_inscattering_luminance", unreal.LinearColor(0.04, 0.03, 0.1, 1.0))
    try_set(fog_comp, "volumetric_fog", False)

    post = spawn(unreal.PostProcessVolume, (0, 0, 0))
    try_set(post, "unbound", True)
    settings = post.get_editor_property("settings")
    try_set(settings, "override_auto_exposure_method", True)
    try_set(settings, "auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
    try_set(settings, "override_auto_exposure_bias", True)
    try_set(settings, "auto_exposure_bias", 11.0)
    try_set(settings, "override_vignette_intensity", True)
    try_set(settings, "vignette_intensity", 0.4)
    try_set(settings, "override_bloom_intensity", True)
    try_set(settings, "bloom_intensity", 0.8)
    # Grade: a little punch, teal shadows, warm highlights
    try_set(settings, "override_color_saturation", True)
    try_set(settings, "color_saturation", unreal.Vector4(1.12, 1.12, 1.12, 1.0))
    try_set(settings, "override_color_contrast", True)
    try_set(settings, "color_contrast", unreal.Vector4(1.06, 1.06, 1.06, 1.0))
    try_set(settings, "override_color_gain_shadows", True)
    try_set(settings, "color_gain_shadows", unreal.Vector4(0.91, 1.018, 1.06, 1.0))
    try_set(settings, "override_color_gain_highlights", True)
    try_set(settings, "color_gain_highlights", unreal.Vector4(1.05, 1.015, 0.95, 1.0))
    try_set(post, "settings", settings)

    level_subsystem.save_current_level()
    unreal.log("Graveyard map created at " + MAP_PATH + ". Press Play!")
