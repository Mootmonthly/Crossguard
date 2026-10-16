"""Crossguard placeholder art import (docs/art-placeholder-plan.md, "Direct-download list").

Unreal Python (needs the `unreal` module): run it inside the editor, never with plain python3.

    UnrealEditor-Cmd.exe <Project>/Crossguard.uproject -run=pythonscript -script="<Project>/Tools/import_art.py"
        -unattended -nullrhi -nosplash -nosound -NoP4 -stdout -FullStdOutLogOutput

CG_ART_DIR: the folder holding the extracted downloads (kenney/<pack>/..., polyhaven/<id>/...),
outside the repository. Default %TEMP%/CrossguardArt.

Imports the files of ASSETS (only what the game uses) into /Game/ThirdParty/<Pack>/ as static meshes,
skeletal meshes (with their animations) and sound waves. Idempotent: an asset that exists is
re-imported over. One PASS / FAIL / SKIP line each, written to Saved/ArtImport/summary.txt, last line
"RESULT: ...". The size of each imported mesh is printed (fitting the data's scales).

Hooking the assets up is data and Blueprint settings only: the data fields are in the repository
(Data/Items/Weapons.json mesh, Items.json projectileMesh / impactSound, Data/Mobs/Mobs.json mesh;
re-export the tables, then Tools/editor_setup.bat reimports them). The last step here sets
BP_CrossguardCharacter's Crossguard | Art sounds (CHARACTER_SOUNDS) and saves it.
"""

import os
import tempfile

import unreal

PROJECT_DIR = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
ART_DIR = os.environ.get("CG_ART_DIR") or os.path.join(tempfile.gettempdir(), "CrossguardArt")
SUMMARY = os.path.join(PROJECT_DIR, "Saved", "ArtImport", "summary.txt")

KENNEY_FBX = "kenney/{pack}/Models/FBX format/{name}.fbx"
KENNEY_OGG = "kenney/{pack}/Audio/{name}.ogg"
POLYHAVEN_FBX = "polyhaven/{id}/{id}_1k.fbx"

# (kind, source under ART_DIR, destination folder, asset name)
ASSETS = [
    # Weapons and shields (Weapons.json mesh), thrown items (Items.json projectileMesh).
    ("static", KENNEY_FBX.format(pack="kenney_mini-dungeon", name="weapon-sword"), "/Game/ThirdParty/Kenney/MiniDungeon", "SM_Sword"),
    ("static", KENNEY_FBX.format(pack="kenney_mini-dungeon", name="weapon-spear"), "/Game/ThirdParty/Kenney/MiniDungeon", "SM_Spear"),
    ("static", KENNEY_FBX.format(pack="kenney_mini-dungeon", name="shield-round"), "/Game/ThirdParty/Kenney/MiniDungeon", "SM_ShieldRound"),
    ("static", KENNEY_FBX.format(pack="kenney_survival-kit", name="tool-axe"), "/Game/ThirdParty/Kenney/SurvivalKit", "SM_ToolAxe"),
    ("static", KENNEY_FBX.format(pack="kenney_survival-kit", name="bottle"), "/Game/ThirdParty/Kenney/SurvivalKit", "SM_Bottle"),
    ("static", POLYHAVEN_FBX.format(id="ornate_medieval_dagger"), "/Game/ThirdParty/PolyHaven/OrnateMedievalDagger", "SM_OrnateMedievalDagger"),
    ("static", POLYHAVEN_FBX.format(id="wooden_axe"), "/Game/ThirdParty/PolyHaven/WoodenAxe", "SM_WoodenAxe"),
    ("static", POLYHAVEN_FBX.format(id="kite_shield"), "/Game/ThirdParty/PolyHaven/KiteShield", "SM_KiteShield"),
    # World props for Blueprints later (chest, door, herb and ore nodes).
    ("static", POLYHAVEN_FBX.format(id="treasure_chest"), "/Game/ThirdParty/PolyHaven/TreasureChest", "SM_TreasureChest"),
    ("static", POLYHAVEN_FBX.format(id="large_castle_door"), "/Game/ThirdParty/PolyHaven/LargeCastleDoor", "SM_LargeCastleDoor"),
    ("static", POLYHAVEN_FBX.format(id="nettle_plant"), "/Game/ThirdParty/PolyHaven/NettlePlant", "SM_NettlePlant"),
    ("static", POLYHAVEN_FBX.format(id="rock_moss_set_01"), "/Game/ThirdParty/PolyHaven/RockMossSet01", "SM_RockMossSet01"),
    # Monsters (Mobs.json mesh): rigged, with their animation takes.
    ("skeletal", KENNEY_FBX.format(pack="kenney_graveyard-kit_5.0", name="character-skeleton"), "/Game/ThirdParty/Kenney/GraveyardKit", "SK_Skeleton"),
    ("skeletal", KENNEY_FBX.format(pack="kenney_graveyard-kit_5.0", name="character-zombie"), "/Game/ThirdParty/Kenney/GraveyardKit", "SK_Zombie"),
    # Character sound slots and projectile impacts.
    ("sound", KENNEY_OGG.format(pack="kenney_rpg-audio", name="knifeSlice"), "/Game/ThirdParty/Kenney/RpgAudio", "S_Swing"),
    ("sound", KENNEY_OGG.format(pack="kenney_impact-sounds", name="impactPunch_medium_000"), "/Game/ThirdParty/Kenney/ImpactSounds", "S_Hit"),
    ("sound", KENNEY_OGG.format(pack="kenney_impact-sounds", name="impactMetal_heavy_000"), "/Game/ThirdParty/Kenney/ImpactSounds", "S_Block"),
    ("sound", KENNEY_OGG.format(pack="kenney_impact-sounds", name="impactSoft_heavy_000"), "/Game/ThirdParty/Kenney/ImpactSounds", "S_Death"),
    ("sound", KENNEY_OGG.format(pack="kenney_impact-sounds", name="impactWood_light_000"), "/Game/ThirdParty/Kenney/ImpactSounds", "S_ImpactWood"),
    ("sound", KENNEY_OGG.format(pack="kenney_impact-sounds", name="impactMetal_light_000"), "/Game/ThirdParty/Kenney/ImpactSounds", "S_ImpactMetal"),
    ("sound", KENNEY_OGG.format(pack="kenney_impact-sounds", name="impactGlass_heavy_000"), "/Game/ThirdParty/Kenney/ImpactSounds", "S_ImpactGlass"),
]

# BP_CrossguardCharacter's Crossguard | Art sound slots (property -> sound asset).
CHARACTER_BLUEPRINT = "/Game/BP_CrossguardCharacter"
CHARACTER_SOUNDS = {
    "swing_sound": "/Game/ThirdParty/Kenney/RpgAudio/S_Swing",
    "hit_sound": "/Game/ThirdParty/Kenney/ImpactSounds/S_Hit",
    "block_sound": "/Game/ThirdParty/Kenney/ImpactSounds/S_Block",
    "death_sound": "/Game/ThirdParty/Kenney/ImpactSounds/S_Death",
}

_lines = []
_failed = 0


def record(status, step, detail=""):
    global _failed
    line = "{:<5} {}{}".format(status, step, (": " + detail) if detail else "")
    _lines.append(line)
    if status == "FAIL":
        _failed += 1
    unreal.log(line)


def fbx_options(kind):
    options = unreal.FbxImportUI()
    options.set_editor_property("automated_import_should_detect_type", False)
    options.set_editor_property("import_mesh", True)
    options.set_editor_property("import_materials", True)
    options.set_editor_property("import_textures", True)
    if kind == "skeletal":
        options.set_editor_property("import_as_skeletal", True)
        options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
        options.set_editor_property("import_animations", True)
        options.set_editor_property("create_physics_asset", True)
    else:
        options.set_editor_property("import_as_skeletal", False)
        options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
        options.set_editor_property("import_animations", False)
        options.static_mesh_import_data.set_editor_property("combine_meshes", True)
    return options


def size_of(asset):
    """The mesh's bounding box size in cm, as text ("" if unknown)."""
    try:
        if isinstance(asset, unreal.StaticMesh):
            box = asset.get_bounding_box()
        elif isinstance(asset, unreal.SkeletalMesh):
            box = asset.get_bounds().box_extent * 2.0
            return "{:.0f} x {:.0f} x {:.0f} cm".format(box.x, box.y, box.z)
        else:
            return ""
        extent = box.max - box.min
        return "{:.0f} x {:.0f} x {:.0f} cm".format(extent.x, extent.y, extent.z)
    except Exception as error:  # noqa: BLE001
        return "size unknown ({})".format(error)


def import_one(kind, source, folder, name):
    step = "{}/{}".format(folder, name)
    path = os.path.join(ART_DIR, source)
    if not os.path.isfile(path):
        record("SKIP", step, "not downloaded: " + path)
        return
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", path)
    task.set_editor_property("destination_path", folder)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    if kind in ("static", "skeletal"):
        task.set_editor_property("options", fbx_options(kind))
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    imported = list(task.get_editor_property("imported_object_paths") or [])
    asset = unreal.EditorAssetLibrary.load_asset("{}/{}.{}".format(folder, name, name))
    if asset is None:
        record("FAIL", step, "nothing imported from {} (imported: {})".format(source, ", ".join(imported) or "none"))
        return
    wanted = {"static": unreal.StaticMesh, "skeletal": unreal.SkeletalMesh, "sound": unreal.SoundWave}[kind]
    if not isinstance(asset, wanted):
        record("FAIL", step, "imported as {} instead of {}".format(type(asset).__name__, wanted.__name__))
        return
    extra = size_of(asset)
    if kind == "skeletal":
        anims = [p for p in unreal.EditorAssetLibrary.list_assets(folder, recursive=False)
                 if isinstance(unreal.EditorAssetLibrary.load_asset(p), unreal.AnimSequence) and name in p]
        extra += "; {} animation(s)".format(len(anims))
    record("PASS", step, "{} ({} objects){}".format(kind, len(imported), ("; " + extra) if extra else ""))


def set_character_sounds():
    step = "Sounds on " + CHARACTER_BLUEPRINT
    blueprint = unreal.EditorAssetLibrary.load_asset(CHARACTER_BLUEPRINT)
    if blueprint is None:
        record("SKIP", step, "Blueprint not found")
        return
    defaults = unreal.get_default_object(blueprint.generated_class())
    changed = []
    for prop, sound_path in CHARACTER_SOUNDS.items():
        sound = unreal.EditorAssetLibrary.load_asset(sound_path)
        if sound is None:
            record("SKIP", step, "{} missing (not imported)".format(sound_path))
            return
        current = defaults.get_editor_property(prop)
        if current is None or current.get_path_name() != sound.get_path_name():
            defaults.set_editor_property(prop, sound)
            changed.append(prop)
    if not changed:
        record("PASS", step, "already set")
        return
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint, False):
        record("FAIL", step, "set {} but saving failed".format(", ".join(changed)))
        return
    record("PASS", step, "set " + ", ".join(changed))


def main():
    record("INFO", "Art folder", ART_DIR)
    for kind, source, folder, name in ASSETS:
        try:
            import_one(kind, source, folder, name)
        except Exception as error:  # noqa: BLE001
            record("FAIL", "{}/{}".format(folder, name), str(error))
    try:
        set_character_sounds()
    except Exception as error:  # noqa: BLE001
        record("FAIL", "Sounds on " + CHARACTER_BLUEPRINT, str(error))
    passed = sum(1 for line in _lines if line.startswith("PASS"))
    skipped = sum(1 for line in _lines if line.startswith("SKIP"))
    _lines.append("")
    _lines.append("RESULT: {} ({} passed, {} failed, {} skipped)".format("FAIL" if _failed else "PASS", passed, _failed, skipped))
    os.makedirs(os.path.dirname(SUMMARY), exist_ok=True)
    with open(SUMMARY, "w", encoding="utf-8") as handle:
        handle.write("\n".join(_lines) + "\n")
    unreal.log(_lines[-1])


main()
