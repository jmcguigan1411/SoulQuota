"""
Soul Quota - imports the main menu fonts (SIL Open Font License) from ArtSource/Fonts as font faces in /Game/UI/Fonts:
  FF_Creepster  (Creepster-Regular.ttf  - the SOUL QUOTA title only)
  FF_LilitaOne  (LilitaOne-Regular.ttf  - buttons and headings)
  FF_Rubik500   (Rubik-Medium.ttf       - labels)
  FF_Rubik700   (Rubik-Bold.ttf         - bold labels)
The menu (SQMenu.cpp) builds runtime fonts from these and falls back to the engine font when one is missing.
setup_project.py runs it unattended, so it only logs (no dialogs). Safe to re-run: existing fonts are skipped.
"""
import os
import unreal

SRC = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "ArtSource", "Fonts")
DEST = "/Game/UI/Fonts"
FONTS = (
    ("Creepster-Regular.ttf", "FF_Creepster"),
    ("LilitaOne-Regular.ttf", "FF_LilitaOne"),
    ("Rubik-Medium.ttf", "FF_Rubik500"),
    ("Rubik-Bold.ttf", "FF_Rubik700"),
)

EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()


def log(msg):
    unreal.log("[SoulQuota fonts] " + msg)


def import_font(file_name, asset_name):
    path = DEST + "/" + asset_name
    if EAL.does_asset_exist(path):
        log("%s already imported - skipped" % asset_name)
        return
    src = os.path.join(SRC, file_name)
    if not os.path.isfile(src):
        unreal.log_warning("[SoulQuota fonts] missing %s - skipped" % src)
        return
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", src)
    task.set_editor_property("destination_path", DEST)
    task.set_editor_property("destination_name", asset_name)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    AT.import_asset_tasks([task])
    asset = EAL.load_asset(path) if EAL.does_asset_exist(path) else None
    if asset:
        log("%s: %s" % (asset_name, asset.get_class().get_name()))
    else:
        unreal.log_error("[SoulQuota fonts] %s did not import from %s" % (asset_name, src))


for font_file, font_name in FONTS:
    try:
        import_font(font_file, font_name)
    except Exception as err:  # keep going: the menu falls back to the engine font
        unreal.log_error("[SoulQuota fonts] %s failed: %s" % (font_name, err))

log("fonts ready in " + DEST)
