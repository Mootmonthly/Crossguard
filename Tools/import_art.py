"""Crossguard placeholder art import (docs/art-placeholder-plan.md, "Direct-download list").

Unreal Python (needs the `unreal` module): run it inside the editor, never with plain python3.

    UnrealEditor-Cmd.exe <Project>/Crossguard.uproject -run=pythonscript -script="<Project>/Tools/import_art.py"
        -unattended -nullrhi -nosplash -nosound -NoP4 -stdout -FullStdOutLogOutput

CG_ART_DIR: the folder holding the extracted downloads, outside the repository (default
%TEMP%/CrossguardArt): kenney/<pack>/..., polyhaven/<id>/... (scripted downloads), and drop/<zip>/...
for the owner's manual downloads (itch.io KayKit, Google Drive Quaternius), each zip extracted into a
folder named after it (" by @Quaternius" and the Drive date suffix dropped, other characters "_").
CG_ART_ONLY: import only the assets whose destination contains this text (iterating).

Imports the files of ASSETS (only what the game uses) into /Game/ThirdParty/<Pack>/ as static meshes,
skeletal meshes (with their animations; "skeletal" with a skeleton shares that skeleton asset) and
animation-only files onto a skeleton ("anims"), and sound waves. Idempotent: an asset that exists is
re-imported over. One PASS / FAIL / SKIP line each, written to Saved/ArtImport/summary.txt, last line
"RESULT: ...". The size of each imported mesh is printed (fitting the data's scales).

Hooking the assets up is data and Blueprint settings only: the data fields are in the repository
(Data/Items/Weapons.json mesh, Items.json projectileMesh / impactSound, Data/Mobs/Mobs.json mesh;
re-export the tables, then Tools/editor_setup.bat reimports them). The last step here sets
BP_CrossguardCharacter's Crossguard | Art sounds and hit / clank effects (CHARACTER_SOUNDS) and saves it.
"""

import os
import shutil
import tempfile

import unreal

PROJECT_DIR = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
ART_DIR = os.environ.get("CG_ART_DIR") or os.path.join(tempfile.gettempdir(), "CrossguardArt")
SUMMARY = os.path.join(PROJECT_DIR, "Saved", "ArtImport", "summary.txt")

KENNEY_FBX = "kenney/{pack}/Models/FBX format/{name}.fbx"
KENNEY_OGG = "kenney/{pack}/Audio/{name}.ogg"
POLYHAVEN_FBX = "polyhaven/{id}/{id}_1k.fbx"
KAYKIT_SKELETONS = "drop/KayKit_Skeletons_1.1_FREE/KayKit_Skeletons_1.1_FREE/{folder}/fbx/{name}.fbx"
KAYKIT_ANIMS = "drop/KayKit_Character_Animations_1.1/KayKit_Character_Animations_1.1/Animations/fbx/Rig_Medium/{name}.fbx"
QUATERNIUS_WEAPONS = "drop/Medieval_Weapons_Pack/Medieval Weapons Pack by @Quaternius/FBX/{name}.fbx"
QUATERNIUS_MONSTERS = "drop/Ultimate_Monsters/Ultimate Monsters/{group}/FBX/{name}.fbx"

# KayKit's skeletons share Rig_Medium: the Warrior's skeleton asset, for the others and the animations.
# Their proportions differ (the Rogue is shorter): the shared skeleton takes bone lengths from each
# mesh, the root and hips from the animation (KAYKIT_RIG_ANIMATION_BONES; set_rig_proportions).
KAYKIT_RIG = "/Game/ThirdParty/KayKit/Skeletons/SK_Skeleton_Warrior_Skeleton"
KAYKIT_RIG_ANIMATION_BONES = ["Rig_Medium", "root", "hips"]
KAYKIT_RIG_MESHES = ["SK_Skeleton_Warrior", "SK_Skeleton_Rogue", "SK_Skeleton_Mage", "SK_Skeleton_Minion"]
# Import scale per destination folder (default 1): Quaternius' weapons come in about 5x too big.
IMPORT_SCALE = {"/Game/ThirdParty/Quaternius/MedievalWeapons": 0.2}

KAYKIT_RIG_ANIMS = ["Rig_Medium_General", "Rig_Medium_MovementBasic", "Rig_Medium_CombatMelee", "Rig_Medium_CombatRanged",
                    "Rig_Medium_Special"]

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
    # The owner's downloads. Monsters: KayKit skeletons (one rig) and their animations, Quaternius
    # Orc (example brute) and Dragon_Evolved (mithril dragon) with their own takes.
    ("skeletal", KAYKIT_SKELETONS.format(folder="characters", name="Skeleton_Warrior"), "/Game/ThirdParty/KayKit/Skeletons", "SK_Skeleton_Warrior"),
    ("skeletal", KAYKIT_SKELETONS.format(folder="characters", name="Skeleton_Rogue"), "/Game/ThirdParty/KayKit/Skeletons", "SK_Skeleton_Rogue", KAYKIT_RIG),
    ("skeletal", KAYKIT_SKELETONS.format(folder="characters", name="Skeleton_Mage"), "/Game/ThirdParty/KayKit/Skeletons", "SK_Skeleton_Mage", KAYKIT_RIG),
    ("skeletal", KAYKIT_SKELETONS.format(folder="characters", name="Skeleton_Minion"), "/Game/ThirdParty/KayKit/Skeletons", "SK_Skeleton_Minion", KAYKIT_RIG),
] + [
    ("anims", KAYKIT_ANIMS.format(name=name), "/Game/ThirdParty/KayKit/Animations", name, KAYKIT_RIG) for name in KAYKIT_RIG_ANIMS
] + [
    ("skeletal", QUATERNIUS_MONSTERS.format(group="Big", name="Orc"), "/Game/ThirdParty/Quaternius/UltimateMonsters", "SK_Orc"),
    ("skeletal", QUATERNIUS_MONSTERS.format(group="Flying", name="Dragon_Evolved"), "/Game/ThirdParty/Quaternius/UltimateMonsters", "SK_Dragon_Evolved"),
    # Weapons (one low-poly set for every hand; the bow and the arrow too).
    ("static", QUATERNIUS_WEAPONS.format(name="Sword"), "/Game/ThirdParty/Quaternius/MedievalWeapons", "SM_Sword"),
    ("static", QUATERNIUS_WEAPONS.format(name="Sword_Big"), "/Game/ThirdParty/Quaternius/MedievalWeapons", "SM_Sword_Big"),
    ("static", QUATERNIUS_WEAPONS.format(name="Dagger"), "/Game/ThirdParty/Quaternius/MedievalWeapons", "SM_Dagger"),
    ("static", QUATERNIUS_WEAPONS.format(name="Axe_Small"), "/Game/ThirdParty/Quaternius/MedievalWeapons", "SM_Axe_Small"),
    ("static", QUATERNIUS_WEAPONS.format(name="Axe_Double"), "/Game/ThirdParty/Quaternius/MedievalWeapons", "SM_Axe_Double"),
    ("static", QUATERNIUS_WEAPONS.format(name="Spear"), "/Game/ThirdParty/Quaternius/MedievalWeapons", "SM_Spear"),
    ("static", QUATERNIUS_WEAPONS.format(name="Shield_Heater"), "/Game/ThirdParty/Quaternius/MedievalWeapons", "SM_Shield_Heater"),
    ("static", QUATERNIUS_WEAPONS.format(name="Bow_Wooden"), "/Game/ThirdParty/Quaternius/MedievalWeapons", "SM_Bow_Wooden"),
    ("static", QUATERNIUS_WEAPONS.format(name="Arrow"), "/Game/ThirdParty/Quaternius/MedievalWeapons", "SM_Arrow"),
    ("static", KAYKIT_SKELETONS.format(folder="assets", name="Skeleton_Staff"), "/Game/ThirdParty/KayKit/Skeletons", "SM_Skeleton_Staff"),
]

# BP_CrossguardCharacter's Crossguard | Art slots (property -> sound or Niagara asset). The Niagara
# systems are Epic's Niagara Examples Pack (Fab), added to Content/NiagaraExamples by the owner.
CHARACTER_BLUEPRINT = "/Game/BP_CrossguardCharacter"
CHARACTER_SOUNDS = {
    "swing_sound": "/Game/ThirdParty/Kenney/RpgAudio/S_Swing",
    "hit_sound": "/Game/ThirdParty/Kenney/ImpactSounds/S_Hit",
    "block_sound": "/Game/ThirdParty/Kenney/ImpactSounds/S_Block",
    "death_sound": "/Game/ThirdParty/Kenney/ImpactSounds/S_Death",
    "hit_vfx": "/Game/NiagaraExamples/FX_Weapons/Impacts/NS_Impact_Concrete",
    "clank_vfx": "/Game/NiagaraExamples/FX_Sparks/NS_Spark_Burst",
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


def fbx_options(kind, skeleton=None, scale=1.0):
    options = unreal.FbxImportUI()
    options.set_editor_property("automated_import_should_detect_type", False)
    options.set_editor_property("import_mesh", kind != "anims")
    options.set_editor_property("import_materials", kind != "anims")
    options.set_editor_property("import_textures", kind != "anims")
    if kind in ("skeletal", "anims"):
        options.set_editor_property("import_as_skeletal", True)
        options.set_editor_property("mesh_type_to_import",
                                    unreal.FBXImportType.FBXIT_ANIMATION if kind == "anims" else unreal.FBXImportType.FBXIT_SKELETAL_MESH)
        options.set_editor_property("import_animations", True)
        options.set_editor_property("create_physics_asset", kind == "skeletal")
        if skeleton is not None:
            options.set_editor_property("skeleton", skeleton)
    else:
        options.set_editor_property("import_as_skeletal", False)
        options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
        options.set_editor_property("import_animations", False)
        options.static_mesh_import_data.set_editor_property("combine_meshes", True)
        options.static_mesh_import_data.set_editor_property("import_uniform_scale", scale)
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


def animations_in(folder):
    """Object paths of the AnimSequences directly in folder."""
    return [p for p in unreal.EditorAssetLibrary.list_assets(folder, recursive=False)
            if isinstance(unreal.EditorAssetLibrary.load_asset(p), unreal.AnimSequence)]


def provide_textures(path):
    """Quaternius' Ultimate Monsters FBX files name Atlas_Monsters.png, which the pack keeps in the
    folders beside FBX/ (glTF/, Blends/), not next to the FBX: copied there (the download folder, not
    the repository) so the import finds the texture."""
    folder = os.path.dirname(path)
    for texture in ("Atlas_Monsters.png",):
        target = os.path.join(folder, texture)
        if os.path.isfile(target):
            continue
        for sibling in ("glTF", "Blends"):
            source = os.path.join(os.path.dirname(folder), sibling, texture)
            if os.path.isfile(source):
                shutil.copyfile(source, target)
                break


def import_one(kind, source, folder, name, skeleton_path=None):
    step = "{}/{}".format(folder, name)
    path = os.path.join(ART_DIR, source)
    if not os.path.isfile(path):
        record("SKIP", step, "not downloaded: " + path)
        return
    provide_textures(path)
    skeleton = None
    if skeleton_path:
        skeleton = unreal.EditorAssetLibrary.load_asset(skeleton_path)
        if not isinstance(skeleton, unreal.Skeleton):
            record("FAIL", step, "skeleton {} missing (import its mesh first)".format(skeleton_path))
            return
    before = set(animations_in(folder)) if kind == "anims" else set()
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", path)
    task.set_editor_property("destination_path", folder)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    if kind in ("static", "skeletal", "anims"):
        task.set_editor_property("options", fbx_options(kind, skeleton, IMPORT_SCALE.get(folder, 1.0)))
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    imported = list(task.get_editor_property("imported_object_paths") or [])
    if kind == "anims":
        anims = [p for p in imported if isinstance(unreal.EditorAssetLibrary.load_asset(p), unreal.AnimSequence)]
        if not anims:
            anims = sorted(set(animations_in(folder)) - before)
        if not anims:
            record("FAIL", step, "no animation imported from {} (imported: {})".format(source, ", ".join(imported) or "none"))
            return
        names = sorted(a.split(".")[-1] for a in anims)
        record("PASS", step, "{} animation(s): {}".format(len(names), ", ".join(names)))
        return
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
        anims = sorted(p.split(".")[-1] for p in imported if isinstance(unreal.EditorAssetLibrary.load_asset(p), unreal.AnimSequence))
        physics = asset.get_editor_property("physics_asset")
        extra += "; skeleton {}; physics asset {}; animation(s): {}".format(
            asset.get_editor_property("skeleton").get_name(), physics.get_name() if physics else "none", ", ".join(anims) or "none")
    record("PASS", step, "{} ({} objects){}".format(kind, len(imported), ("; " + extra) if extra else ""))


def set_rig_proportions():
    step = "Mesh proportions on " + KAYKIT_RIG
    skeleton = unreal.EditorAssetLibrary.load_asset(KAYKIT_RIG)
    if skeleton is None:
        record("SKIP", step, "not imported")
        return
    # Every mesh's bones on the shared skeleton (the Rogue's cape), then the proportions for all of them.
    for mesh_name in KAYKIT_RIG_MESHES:
        mesh = unreal.EditorAssetLibrary.load_asset("/Game/ThirdParty/KayKit/Skeletons/" + mesh_name)
        if mesh is not None:
            unreal.CrossguardArtLibrary.merge_mesh_bones(skeleton, mesh)
    bones = unreal.CrossguardArtLibrary.use_mesh_proportions(skeleton, KAYKIT_RIG_ANIMATION_BONES)
    if not unreal.EditorAssetLibrary.save_loaded_asset(skeleton, False):
        record("FAIL", step, "set but saving failed")
        return
    record("PASS", step, "{} bones from the mesh; {} from the animation".format(bones, ", ".join(KAYKIT_RIG_ANIMATION_BONES)))


def set_character_sounds():
    step = "Art slots on " + CHARACTER_BLUEPRINT
    blueprint = unreal.EditorAssetLibrary.load_asset(CHARACTER_BLUEPRINT)
    if blueprint is None:
        record("SKIP", step, "Blueprint not found")
        return
    defaults = unreal.get_default_object(blueprint.generated_class())
    changed = []
    for prop, sound_path in CHARACTER_SOUNDS.items():
        sound = unreal.EditorAssetLibrary.load_asset(sound_path)
        if sound is None:
            record("SKIP", step, "{} missing (not imported / added)".format(sound_path))
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
    only = os.environ.get("CG_ART_ONLY", "")
    for entry in ASSETS:
        kind, source, folder, name = entry[:4]
        if only and only not in "{}/{}".format(folder, name):
            continue
        try:
            import_one(kind, source, folder, name, entry[4] if len(entry) > 4 else None)
        except Exception as error:  # noqa: BLE001
            record("FAIL", "{}/{}".format(folder, name), str(error))
    try:
        set_rig_proportions()
    except Exception as error:  # noqa: BLE001
        record("FAIL", "Mesh proportions on " + KAYKIT_RIG, str(error))
    try:
        set_character_sounds()
    except Exception as error:  # noqa: BLE001
        record("FAIL", "Art slots on " + CHARACTER_BLUEPRINT, str(error))
    passed = sum(1 for line in _lines if line.startswith("PASS"))
    skipped = sum(1 for line in _lines if line.startswith("SKIP"))
    _lines.append("")
    _lines.append("RESULT: {} ({} passed, {} failed, {} skipped)".format("FAIL" if _failed else "PASS", passed, _failed, skipped))
    os.makedirs(os.path.dirname(SUMMARY), exist_ok=True)
    with open(SUMMARY, "w", encoding="utf-8") as handle:
        handle.write("\n".join(_lines) + "\n")
    unreal.log(_lines[-1])


main()
