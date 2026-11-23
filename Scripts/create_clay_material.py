"""
Soul Quota - creates /Game/Materials/M_Clay: a matte clay surface with a soft rim light,
driven by a "Color" parameter (the C++ code tints it per reaper / enemy at runtime).
Run once in the editor: Tools > Execute Python Script... > this file. Safe to re-run.
"""
import unreal

PATH = "/Game/Materials"
NAME = "M_Clay"
FULL = PATH + "/" + NAME

mel = unreal.MaterialEditingLibrary
assets = unreal.AssetToolsHelpers.get_asset_tools()

if unreal.EditorAssetLibrary.does_asset_exist(FULL):
    unreal.log_warning(FULL + " already exists - leaving it alone.")
else:
    mat = assets.create_asset(NAME, PATH, unreal.Material, unreal.MaterialFactoryNew())

    def node(cls, x, y):
        return mel.create_material_expression(mat, cls, x, y)

    color = node(unreal.MaterialExpressionVectorParameter, -900, -100)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(0.9, 0.9, 0.9, 1.0))

    roughness = node(unreal.MaterialExpressionConstant, -500, 100)
    roughness.set_editor_property("r", 0.72)

    specular = node(unreal.MaterialExpressionConstant, -500, 200)
    specular.set_editor_property("r", 0.25)

    # Rim light: fresnel * colour * strength -> emissive (gives the clay that soft glowing edge)
    fresnel = node(unreal.MaterialExpressionFresnel, -900, 300)
    fresnel.set_editor_property("exponent", 3.5)
    rim_colour = node(unreal.MaterialExpressionMultiply, -600, 300)
    strength = node(unreal.MaterialExpressionConstant, -600, 450)
    strength.set_editor_property("r", 0.35)
    rim = node(unreal.MaterialExpressionMultiply, -350, 350)

    mel.connect_material_expressions(fresnel, "", rim_colour, "A")
    mel.connect_material_expressions(color, "", rim_colour, "B")
    mel.connect_material_expressions(rim_colour, "", rim, "A")
    mel.connect_material_expressions(strength, "", rim, "B")

    mel.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(specular, "", unreal.MaterialProperty.MP_SPECULAR)
    mel.connect_material_property(rim, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    mat.set_editor_property("used_with_instanced_static_meshes", True)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(FULL)
    unreal.log("Created " + FULL)
