"""Runs every one-time setup script. Safe to re-run (each script skips what already exists)."""
import os, runpy, traceback
import unreal
here = os.path.dirname(os.path.abspath(__file__))
for script in ("create_clay_material.py", "create_audio.py", "import_art.py", "import_fonts.py", "create_ground_material.py",
               "create_style_materials.py",
               "build_graveyard_map.py",
               "build_menu_map.py"):
    try:
        runpy.run_path(os.path.join(here, script))
    except Exception:
        unreal.log_error("[SoulQuota setup] %s failed:\n%s" % (script, traceback.format_exc()))
