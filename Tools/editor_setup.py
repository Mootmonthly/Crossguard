"""Crossguard editor setup: does the manual editor steps of docs/next-build-checklist.md (sections 2-3).

Unreal Python (needs the `unreal` module): run it inside the editor, never with plain python3.

Headless (what Tools/editor_setup.bat does; build the editor first, the script needs the compiled
Crossguard classes):

    UnrealEditor-Cmd.exe <Project>/Crossguard.uproject -run=pythonscript -script="<Project>/Tools/editor_setup.py"
        -unattended -nullrhi -nosplash -nosound -NoP4 -stdout -FullStdOutLogOutput

Alternative (full editor process, no commandlet; set CG_SETUP_QUIT=1 so it closes when done):

    UnrealEditor-Cmd.exe <Project>/Crossguard.uproject -ExecutePythonScript="<Project>/Tools/editor_setup.py"
        -unattended -nullrhi -nosplash -nosound -NoP4 -stdout

Or from an open editor: Output Log, "Python" console: `exec(open(r"<Project>/Tools/editor_setup.py").read())`
(or File -> Execute Python Script).

Idempotent: every run re-imports the tables from the repository JSON and (re)sets the references;
nothing else is touched.

Steps (one PASS / FAIL / SKIP line each, printed and written to Saved/EditorSetup/summary.txt, last
line "RESULT: PASS ..." or "RESULT: FAIL ..."; the commandlet's exit code is not reliable, read the
summary):

1. Data tables in /Game/Data (created with the right row struct if missing, else refilled from the
   JSON; then saved): DT_Skills, DT_Weapons, DT_Affixes, DT_Armor, DT_Items, DT_Loot, DT_Mobs.
   (Paths and structs as Source/Crossguard/Private/Tests/CrossguardDataTests.cpp checks them.)
2. DA_SkillCatalog (UCrossguardSkillCatalog; found in /Game/Data, /Game, /Game/Data/Skills or
   /Game/Skills, else created in /Game/Data): SkillTable = DT_Skills. AbilityClasses untouched.
3. BP_CrossguardGameMode (found by name anywhere under /Game) CDO:
   Loot.{LootTables, WeaponTable, ArmorTable, AffixTable, ItemTable, SkillCatalog}, MobTable,
   MobSkillCatalog.
4. The character Blueprint (the game mode's Default Pawn Class if it is a Blueprint of
   CrossguardCharacter, else BP_CrossguardCharacter): EquipmentComponent.{WeaponTable, ArmorTable,
   AffixTable}, InventoryComponent.ItemTable, LoadoutComponent.SkillCatalog; DefaultMappingContext
   cleared (checklist section 3) unless CG_SETUP_KEEP_MAPPING_CONTEXT=1.
   Missing Blueprints are skipped (SKIP line), not created, unless CG_SETUP_CREATE_BLUEPRINTS=1.
5. Optional (CG_SETUP_CREATE_MAP=1 or --create-map): /Game/Maps/Dev/L_DevSandbox with a floor,
   light, player start, nav mesh bounds, two gather nodes, a chest_common loot container, a mob
   spawner and a dummy. Skipped if the map exists.

Options (environment variables, or arguments after the script path when the runner passes them):
    CG_SETUP_CREATE_MAP=1            / --create-map
    CG_SETUP_CREATE_BLUEPRINTS=1     / --create-blueprints   (BP_CrossguardGameMode + BP_CrossguardCharacter
                                                              in /Game/Blueprints, native parents)
    CG_SETUP_KEEP_MAPPING_CONTEXT=1  / --keep-mapping-context
    CG_SETUP_QUIT=1                  / --quit                (ask the editor to quit at the end; for
                                                              -ExecutePythonScript runs)

ENGINE-API UNCERTAINTY (written without an engine to run against; check the log if a step fails):
- DataTableFunctionLibrary.fill_data_table_from_json_file(data_table, json_path, import_row_struct)
  argument order and the import_row_struct parameter (added in 5.x). Falls back to an
  AssetImportTask with CSVImportFactory.
- DataTableFactory's "struct" property name, DataAssetFactory's "data_asset_class",
  BlueprintFactory's "parent_class".
- AssetImportData.scripted_add_filename (records the JSON as the reimport source; warning only).
- Editing a native default subobject (EquipmentComponent, ...) through the Blueprint CDO: values are
  set after compiling, then saved and read back to verify.
- Map creation: LevelEditorSubsystem.new_level / save_current_level inside the pythonscript
  commandlet; NavMeshBoundsVolume spawned by EditorActorSubsystem.spawn_actor_from_class getting a
  box brush (UActorFactoryBoxVolume). The nav mesh itself is built when the map is opened in the
  editor (or Build -> Build Paths), not here.
"""

import json
import os
import sys
import traceback

import unreal

# ------------------------------------------------------------------------------------------------
# Configuration
# ------------------------------------------------------------------------------------------------

DATA_FOLDER = "/Game/Data"

# (asset name, JSON relative to the project, row struct name in /Script/Crossguard)
TABLES = [
    ("DT_Skills", "Data/Skills/SkillTable.json", "CrossguardSkillRow"),
    ("DT_Weapons", "Data/Items/WeaponTable.json", "CrossguardWeaponRow"),
    ("DT_Affixes", "Data/Items/AffixTable.json", "CrossguardAffixRow"),
    ("DT_Armor", "Data/Items/ArmorTable.json", "CrossguardArmorRow"),
    ("DT_Items", "Data/Items/ItemTable.json", "CrossguardItemRow"),
    ("DT_Loot", "Data/Loot/LootTable.json", "CrossguardLootTableRow"),
    ("DT_Mobs", "Data/Mobs/MobTable.json", "CrossguardMobRow"),
]

CATALOG_NAME = "DA_SkillCatalog"
# Where CrossguardDataTests.cpp looks for it, in order. Created in the first one if none exists.
CATALOG_PACKAGES = [
    "/Game/Data/DA_SkillCatalog",
    "/Game/DA_SkillCatalog",
    "/Game/Data/Skills/DA_SkillCatalog",
    "/Game/Skills/DA_SkillCatalog",
]

GAME_MODE_BP = "BP_CrossguardGameMode"
CHARACTER_BP = "BP_CrossguardCharacter"
CREATED_BP_FOLDER = "/Game/Blueprints"

MAP_PATH = "/Game/Maps/Dev/L_DevSandbox"


def _flag(env_name, arg_name):
    if os.environ.get(env_name, "").strip().lower() in ("1", "true", "yes", "on"):
        return True
    return arg_name in sys.argv


CREATE_MAP = _flag("CG_SETUP_CREATE_MAP", "--create-map")
CREATE_BLUEPRINTS = _flag("CG_SETUP_CREATE_BLUEPRINTS", "--create-blueprints")
KEEP_MAPPING_CONTEXT = _flag("CG_SETUP_KEEP_MAPPING_CONTEXT", "--keep-mapping-context")
QUIT_AT_END = _flag("CG_SETUP_QUIT", "--quit")


def _project_dir():
    # Tools/editor_setup.py -> project folder. __file__ is not always set (exec from the console).
    try:
        here = os.path.dirname(os.path.abspath(__file__))
        candidate = os.path.dirname(here)
        if os.path.isfile(os.path.join(candidate, "Crossguard.uproject")):
            return candidate
    except NameError:
        pass
    return os.path.abspath(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))


PROJECT_DIR = _project_dir()
SUMMARY_DIR = os.path.join(PROJECT_DIR, "Saved", "EditorSetup")
SUMMARY_FILE = os.path.join(SUMMARY_DIR, "summary.txt")

# ------------------------------------------------------------------------------------------------
# Reporting
# ------------------------------------------------------------------------------------------------

RESULTS = []  # (status, step, detail)


def log(message):
    unreal.log("[EditorSetup] " + message)


def warn(message):
    unreal.log_warning("[EditorSetup] " + message)


def record(status, step, detail=""):
    RESULTS.append((status, step, detail))
    line = "{:<5} {}{}".format(status, step, (": " + detail) if detail else "")
    if status == "FAIL":
        unreal.log_error("[EditorSetup] " + line)
    elif status == "SKIP":
        unreal.log_warning("[EditorSetup] " + line)
    else:
        unreal.log("[EditorSetup] " + line)


def write_summary():
    failed = [r for r in RESULTS if r[0] == "FAIL"]
    skipped = [r for r in RESULTS if r[0] == "SKIP"]
    passed = [r for r in RESULTS if r[0] == "PASS"]
    lines = ["Crossguard editor setup (Tools/editor_setup.py)", "Project: " + PROJECT_DIR, ""]
    for status, step, detail in RESULTS:
        lines.append("{:<5} {}{}".format(status, step, (": " + detail) if detail else ""))
    lines.append("")
    verdict = "FAIL" if failed else "PASS"
    lines.append("RESULT: {} ({} passed, {} failed, {} skipped)".format(verdict, len(passed), len(failed), len(skipped)))
    try:
        os.makedirs(SUMMARY_DIR, exist_ok=True)
        with open(SUMMARY_FILE, "w", encoding="utf-8", newline="\n") as handle:
            handle.write("\n".join(lines) + "\n")
    except OSError as error:
        unreal.log_error("[EditorSetup] cannot write {}: {}".format(SUMMARY_FILE, error))
    for line in lines:
        if line.startswith("RESULT: FAIL") or line.startswith("FAIL"):
            unreal.log_error("[EditorSetup] " + line)
        else:
            unreal.log("[EditorSetup] " + line)
    return not failed


# ------------------------------------------------------------------------------------------------
# Helpers
# ------------------------------------------------------------------------------------------------

def asset_tools():
    return unreal.AssetToolsHelpers.get_asset_tools()


def object_path(package_path):
    """/Game/Data/DT_Items -> /Game/Data/DT_Items.DT_Items"""
    name = package_path.rsplit("/", 1)[-1]
    return package_path + "." + name


def asset_exists(package_path):
    return unreal.EditorAssetLibrary.does_asset_exist(package_path)


def load(package_path):
    if not asset_exists(package_path):
        return None
    return unreal.EditorAssetLibrary.load_asset(package_path)


def save(asset):
    # only_if_is_dirty=False: always write, so a run after a failed save still persists.
    return unreal.EditorAssetLibrary.save_loaded_asset(asset, False)


def path_of(obj):
    if obj is None:
        return "None"
    try:
        return obj.get_path_name()
    except Exception:  # noqa: BLE001 - logging only
        return str(obj)


def same_object(a, b):
    if a is None or b is None:
        return a is None and b is None
    return path_of(a) == path_of(b)


def row_struct(struct_name):
    """The UScriptStruct /Script/Crossguard.<struct_name>, or None."""
    # Python exposes BlueprintType USTRUCTs as unreal.<Name> (without the F prefix).
    py_type = getattr(unreal, struct_name, None)
    if py_type is not None:
        try:
            return py_type.static_struct()
        except Exception:  # noqa: BLE001
            pass
    try:
        # UNCERTAIN: find_object(outer, name) with a full path and outer None.
        return unreal.find_object(None, "/Script/Crossguard." + struct_name)
    except Exception:  # noqa: BLE001
        return None


def native_class(name):
    """unreal.<name> (a UClass wrapper type) or None if the module is not loaded / class missing."""
    return getattr(unreal, name, None)


def find_assets_named(asset_name, root="/Game"):
    """Package paths of every asset called asset_name under root."""
    found = []
    try:
        paths = unreal.EditorAssetLibrary.list_assets(root, recursive=True, include_folder=False)
    except Exception as error:  # noqa: BLE001
        warn("list_assets({}) failed: {}".format(root, error))
        return found
    for path in paths:
        # list_assets returns object paths, "/Game/X/BP_Foo.BP_Foo" (sometimes without the suffix).
        package = str(path).split(".", 1)[0]
        if package.rsplit("/", 1)[-1] == asset_name:
            found.append(package)
    return sorted(set(found))


def load_blueprint_class(package_path, bp=None):
    cls = None
    try:
        cls = unreal.EditorAssetLibrary.load_blueprint_class(package_path)
    except Exception:  # noqa: BLE001
        cls = None
    if cls is None and bp is not None:
        try:
            cls = bp.generated_class()  # UNCERTAIN: Python-only helper on unreal.Blueprint.
        except Exception:  # noqa: BLE001
            cls = None
    return cls


def safe_modify(obj):
    """UObject::Modify (marks the package dirty, records undo). UNCERTAIN: exposed as Object.modify()."""
    try:
        obj.modify()
    except Exception:  # noqa: BLE001
        pass


def compile_blueprint(bp):
    try:
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        return True
    except Exception as error:  # noqa: BLE001
        warn("compile_blueprint({}) failed: {}".format(path_of(bp), error))
        return False


def is_child_of(cls, parent_cls):
    if cls is None or parent_cls is None:
        return False
    try:
        parent_static = parent_cls.static_class()
    except Exception:  # noqa: BLE001
        parent_static = parent_cls
    try:
        # UNCERTAIN: Class.is_child_of is not always exposed; SystemLibrary/MathLibrary fallbacks below.
        return bool(unreal.MathLibrary.class_is_child_of(cls, parent_static))
    except Exception:  # noqa: BLE001
        pass
    try:
        cdo = unreal.get_default_object(cls)
        return isinstance(cdo, parent_cls)
    except Exception:  # noqa: BLE001
        return False


# ------------------------------------------------------------------------------------------------
# Step 1: data tables
# ------------------------------------------------------------------------------------------------

def json_row_names(json_file):
    with open(json_file, "r", encoding="utf-8") as handle:
        rows = json.load(handle)
    return [str(row.get("Name", "")) for row in rows]


def table_row_names(table):
    try:
        names = unreal.DataTableFunctionLibrary.get_data_table_row_names(table)
        return [str(n) for n in names]
    except Exception as error:  # noqa: BLE001
        warn("get_data_table_row_names failed: {}".format(error))
        return None


def table_row_struct(table):
    for getter in (
        lambda: unreal.DataTableFunctionLibrary.get_data_table_row_struct(table),
        lambda: table.get_editor_property("row_struct"),
    ):
        try:
            value = getter()
            if value is not None:
                return value
        except Exception:  # noqa: BLE001
            continue
    return None


def create_data_table(asset_name, struct):
    factory = unreal.DataTableFactory()
    try:
        factory.set_editor_property("struct", struct)  # UNCERTAIN: UDataTableFactory::Struct.
    except Exception as error:  # noqa: BLE001
        warn("DataTableFactory.struct not settable ({}); the JSON fill sets the row struct instead".format(error))
    return asset_tools().create_asset(asset_name, DATA_FOLDER, unreal.DataTable, factory)


def fill_from_json(table, json_file, struct):
    """True if the table now holds the JSON's rows."""
    library = unreal.DataTableFunctionLibrary
    try:
        # UE 5.x signature: (data_table, json_file_path, import_row_struct=None) -> bool.
        return bool(library.fill_data_table_from_json_file(table, json_file, struct))
    except TypeError:
        # Older signature without import_row_struct.
        return bool(library.fill_data_table_from_json_file(table, json_file))


def import_with_task(asset_name, json_file, struct):
    """Fallback: the regular importer (as drag-and-drop does). Returns the table or None."""
    settings = unreal.CSVImportSettings()
    settings.set_editor_property("import_row_struct", struct)
    settings.set_editor_property("import_type", unreal.CSVImportType.ECSV_DATA_TABLE)
    factory = unreal.CSVImportFactory()
    factory.set_editor_property("automated_import_settings", settings)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", json_file)
    task.set_editor_property("destination_path", DATA_FOLDER)
    task.set_editor_property("destination_name", asset_name)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    task.set_editor_property("factory", factory)
    asset_tools().import_asset_tasks([task])
    return load(DATA_FOLDER + "/" + asset_name)


def record_import_source(table, json_file):
    """So right-click -> Reimport in the editor uses the repository JSON. Best effort."""
    try:
        import_data = table.get_editor_property("asset_import_data")
        if import_data is None:
            return
        current = ""
        try:
            current = import_data.get_first_filename()
        except Exception:  # noqa: BLE001
            pass
        if current and os.path.normcase(os.path.abspath(current)) == os.path.normcase(os.path.abspath(json_file)):
            return
        import_data.scripted_add_filename(json_file, 0, "")  # UNCERTAIN: UAssetImportData::ScriptedAddFilename.
    except Exception as error:  # noqa: BLE001
        warn("could not record {} as the reimport source of {}: {}".format(json_file, path_of(table), error))


def setup_table(asset_name, json_rel, struct_name):
    step = "Data table {}/{}".format(DATA_FOLDER, asset_name)
    package = DATA_FOLDER + "/" + asset_name
    json_file = os.path.join(PROJECT_DIR, json_rel.replace("/", os.sep))
    if not os.path.isfile(json_file):
        record("FAIL", step, "{} not found".format(json_file))
        return None
    struct = row_struct(struct_name)
    if struct is None:
        record("FAIL", step, "row struct /Script/Crossguard.{} not found (is the editor built with the latest C++?)".format(struct_name))
        return None

    created = False
    table = load(package)
    if table is not None and not isinstance(table, unreal.DataTable):
        record("FAIL", step, "{} exists but is a {}, not a DataTable".format(package, table.get_class().get_name()))
        return None
    if table is None:
        table = create_data_table(asset_name, struct)
        created = table is not None

    ok = False
    method = "fill_data_table_from_json_file"
    if table is not None:
        try:
            ok = fill_from_json(table, json_file, struct)
        except Exception as error:  # noqa: BLE001
            warn("{}: fill_data_table_from_json_file raised {}".format(asset_name, error))
            ok = False
    if not ok:
        method = "AssetImportTask"
        try:
            table = import_with_task(asset_name, json_file, struct)
            ok = table is not None
        except Exception as error:  # noqa: BLE001
            warn("{}: AssetImportTask raised {}".format(asset_name, error))
            ok = False
    if not ok or table is None:
        record("FAIL", step, "import of {} failed (see the log above)".format(json_rel))
        return None

    record_import_source(table, json_file)
    if not save(table):
        record("FAIL", step, "save failed")
        return table

    # Verify: struct and row names as in the JSON (what Crossguard.Data.EditorAssets checks).
    problems = []
    actual_struct = table_row_struct(table)
    if actual_struct is not None and not same_object(actual_struct, struct):
        problems.append("row struct is {} (expected {})".format(path_of(actual_struct), path_of(struct)))
    expected_rows = json_row_names(json_file)
    actual_rows = table_row_names(table)
    if actual_rows is not None and sorted(actual_rows) != sorted(expected_rows):
        missing = sorted(set(expected_rows) - set(actual_rows))
        extra = sorted(set(actual_rows) - set(expected_rows))
        problems.append("rows differ (missing {}, extra {})".format(missing[:5], extra[:5]))
    if problems:
        record("FAIL", step, "; ".join(problems))
        return table
    record("PASS", step, "{} {} rows via {} ({})".format("created," if created else "reimported,", len(expected_rows), method, struct_name))
    return table


# ------------------------------------------------------------------------------------------------
# Step 2: skill catalog
# ------------------------------------------------------------------------------------------------

def setup_catalog(skill_table):
    step = "Skill catalog " + CATALOG_NAME
    catalog_class = native_class("CrossguardSkillCatalog")
    if catalog_class is None:
        record("FAIL", step, "class CrossguardSkillCatalog not found (Crossguard module not loaded: build the editor)")
        return None
    catalog = None
    package = None
    for candidate in CATALOG_PACKAGES:
        catalog = load(candidate)
        if catalog is not None:
            package = candidate
            break
    created = False
    if catalog is None:
        factory = unreal.DataAssetFactory()
        try:
            factory.set_editor_property("data_asset_class", catalog_class.static_class())  # UNCERTAIN property name.
        except Exception as error:  # noqa: BLE001
            record("FAIL", step, "DataAssetFactory.data_asset_class: {}".format(error))
            return None
        folder, name = CATALOG_PACKAGES[0].rsplit("/", 1)
        catalog = asset_tools().create_asset(name, folder, catalog_class, factory)
        package = CATALOG_PACKAGES[0]
        created = catalog is not None
    if catalog is None:
        record("FAIL", step, "could not create {}".format(CATALOG_PACKAGES[0]))
        return None
    if not isinstance(catalog, catalog_class):
        record("FAIL", step, "{} is a {}, not a CrossguardSkillCatalog".format(package, catalog.get_class().get_name()))
        return None
    if skill_table is None:
        record("FAIL", step, "DT_Skills is missing, SkillTable not set")
        return catalog
    if not same_object(catalog.get_editor_property("skill_table"), skill_table):
        catalog.set_editor_property("skill_table", skill_table)
    if not save(catalog):
        record("FAIL", step, "save failed")
        return catalog
    if not same_object(catalog.get_editor_property("skill_table"), skill_table):
        record("FAIL", step, "SkillTable did not stick")
        return catalog
    record("PASS", step, "{} {}, SkillTable = DT_Skills".format("created" if created else "found", package))
    return catalog


# ------------------------------------------------------------------------------------------------
# Steps 3-4: Blueprints
# ------------------------------------------------------------------------------------------------

def find_blueprint(asset_name):
    """(package, Blueprint) of the asset called asset_name, or (None, None). Warns on duplicates."""
    packages = find_assets_named(asset_name)
    if not packages:
        return None, None
    if len(packages) > 1:
        warn("several {} found: {}; using {}".format(asset_name, packages, packages[0]))
    bp = load(packages[0])
    if bp is None or not isinstance(bp, unreal.Blueprint):
        return None, None
    return packages[0], bp


def create_blueprint(asset_name, parent_type):
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent_type.static_class())  # UNCERTAIN property name.
    bp = asset_tools().create_asset(asset_name, CREATED_BP_FOLDER, unreal.Blueprint, factory)
    if bp is None:
        return None, None
    compile_blueprint(bp)
    save(bp)
    return CREATED_BP_FOLDER + "/" + asset_name, bp


def set_if_different(target, prop, value, changes, label):
    current = target.get_editor_property(prop)
    if not same_object(current, value):
        target.set_editor_property(prop, value)
        changes.append("{} {} -> {}".format(label, path_of(current), path_of(value).rsplit(".", 1)[-1]))


def setup_game_mode(tables, catalog):
    step = "Game mode " + GAME_MODE_BP
    gm_type = native_class("CrossguardGameMode")
    if gm_type is None:
        record("FAIL", step, "class CrossguardGameMode not found (build the editor)")
        return None, None
    package, bp = find_blueprint(GAME_MODE_BP)
    created = False
    if bp is None and CREATE_BLUEPRINTS:
        package, bp = create_blueprint(GAME_MODE_BP, gm_type)
        created = bp is not None
    if bp is None:
        record("SKIP", step, "no Blueprint named {} under /Game. Create one (parent CrossguardGameMode) or rerun with "
                             "-CreateBlueprints / CG_SETUP_CREATE_BLUEPRINTS=1".format(GAME_MODE_BP))
        return None, None

    compile_blueprint(bp)
    cls = load_blueprint_class(package, bp)
    if cls is None:
        record("FAIL", step, "cannot load the generated class of " + package)
        return bp, None
    cdo = unreal.get_default_object(cls)
    if not isinstance(cdo, gm_type):
        record("FAIL", step, "{} does not derive from CrossguardGameMode".format(package))
        return bp, cls

    missing = [n for n in ("DT_Loot", "DT_Weapons", "DT_Armor", "DT_Affixes", "DT_Items", "DT_Mobs") if tables.get(n) is None]
    changes = []
    loot = cdo.get_editor_property("loot")  # FCrossguardLootSources (a copy: set it back)
    loot_changes = []
    for prop, table_name in (("loot_tables", "DT_Loot"), ("weapon_table", "DT_Weapons"), ("armor_table", "DT_Armor"),
                             ("affix_table", "DT_Affixes"), ("item_table", "DT_Items")):
        if tables.get(table_name) is not None:
            set_if_different(loot, prop, tables[table_name], loot_changes, "Loot." + prop)
    if catalog is not None:
        set_if_different(loot, "skill_catalog", catalog, loot_changes, "Loot.skill_catalog")
    if loot_changes:
        cdo.set_editor_property("loot", loot)
        changes.extend(loot_changes)
    if tables.get("DT_Mobs") is not None:
        set_if_different(cdo, "mob_table", tables["DT_Mobs"], changes, "mob_table")
    if catalog is not None:
        set_if_different(cdo, "mob_skill_catalog", catalog, changes, "mob_skill_catalog")
    # The C++ defaults do not reach a Blueprint that saved its own: the reconnect hold needs
    # CrossguardPlayerController (docs/persistence.md), the placeholder UI CrossguardHUD (docs/ui.md).
    # A child class of either is fine.
    class_fixes = []
    for prop, class_name in (("player_controller_class", "CrossguardPlayerController"), ("hud_class", "CrossguardHUD")):
        wanted = native_class(class_name)
        if wanted is None:
            continue
        current = cdo.get_editor_property(prop)
        if current is None or not unreal.MathLibrary.class_is_child_of(current, wanted):
            cdo.set_editor_property(prop, wanted)
            changes.append("{} {} -> {}".format(prop, current.get_name() if current is not None else "None", class_name))
        class_fixes.append((prop, wanted))

    if created:
        char_package, char_bp = find_blueprint(CHARACTER_BP)
        if char_bp is None:
            char_package, char_bp = create_blueprint(CHARACTER_BP, native_class("CrossguardCharacter"))
        char_cls = load_blueprint_class(char_package, char_bp) if char_bp is not None else None
        if char_cls is not None:
            cdo.set_editor_property("default_pawn_class", char_cls)
            changes.append("default_pawn_class -> " + CHARACTER_BP)

    # Set after compiling (a compile re-instances the CDO), then save without recompiling.
    if not save(bp):
        record("FAIL", step, "save failed")
        return bp, cls

    # Read back.
    cdo = unreal.get_default_object(cls)
    loot = cdo.get_editor_property("loot")
    problems = []
    for prop, table_name in (("loot_tables", "DT_Loot"), ("weapon_table", "DT_Weapons"), ("armor_table", "DT_Armor"),
                             ("affix_table", "DT_Affixes"), ("item_table", "DT_Items")):
        if tables.get(table_name) is not None and not same_object(loot.get_editor_property(prop), tables[table_name]):
            problems.append("Loot." + prop)
    if tables.get("DT_Mobs") is not None and not same_object(cdo.get_editor_property("mob_table"), tables["DT_Mobs"]):
        problems.append("mob_table")
    if catalog is not None:
        if not same_object(loot.get_editor_property("skill_catalog"), catalog):
            problems.append("Loot.skill_catalog")
        if not same_object(cdo.get_editor_property("mob_skill_catalog"), catalog):
            problems.append("mob_skill_catalog")
    for prop, wanted in class_fixes:
        current = cdo.get_editor_property(prop)
        if current is None or not unreal.MathLibrary.class_is_child_of(current, wanted):
            problems.append(prop)
    if problems or missing or catalog is None:
        detail = []
        if problems:
            detail.append("did not stick: " + ", ".join(problems))
        if missing:
            detail.append("tables missing: " + ", ".join(missing))
        if catalog is None:
            detail.append("DA_SkillCatalog missing")
        record("FAIL", step, "; ".join(detail))
        return bp, cls
    record("PASS", step, "{}{} ({})".format("created " if created else "", package,
                                             "; ".join(changes) if changes else "already set"))
    return bp, cls


def pick_character_blueprint(game_mode_cls):
    """(package, Blueprint, note): the game mode's Default Pawn Class if it is a Blueprint of
    CrossguardCharacter (as the editor tests pick it), else BP_CrossguardCharacter."""
    char_type = native_class("CrossguardCharacter")
    note = ""
    if game_mode_cls is not None:
        try:
            pawn_cls = unreal.get_default_object(game_mode_cls).get_editor_property("default_pawn_class")
        except Exception:  # noqa: BLE001
            pawn_cls = None
        if pawn_cls is not None:
            pawn_path = path_of(pawn_cls)  # /Game/X/BP_Foo.BP_Foo_C for Blueprints
            if pawn_path.startswith("/Game/") and is_child_of(pawn_cls, char_type):
                package = pawn_path.split(".", 1)[0]
                bp = load(package)
                if isinstance(bp, unreal.Blueprint):
                    return package, bp, "the game mode's Default Pawn Class"
            elif pawn_path.startswith("/Script/"):
                note = ("the game mode's Default Pawn Class is the native {}; PIE uses it without these tables. "
                        "Set BP_CrossguardGameMode Default Pawn Class to {} (not changed by this script)".format(
                            pawn_path.rsplit(".", 1)[-1], CHARACTER_BP))
    package, bp = find_blueprint(CHARACTER_BP)
    return package, bp, note


def setup_character(tables, catalog, game_mode_cls):
    step = "Character Blueprint"
    char_type = native_class("CrossguardCharacter")
    if char_type is None:
        record("FAIL", step, "class CrossguardCharacter not found (build the editor)")
        return
    package, bp, note = pick_character_blueprint(game_mode_cls)
    if bp is None and CREATE_BLUEPRINTS:
        package, bp = create_blueprint(CHARACTER_BP, char_type)
    if bp is None:
        record("SKIP", step, "no Blueprint named {} under /Game (and the game mode's Default Pawn Class is not a "
                             "character Blueprint). Rerun with -CreateBlueprints to create one".format(CHARACTER_BP))
        return
    step = "Character Blueprint " + package

    compile_blueprint(bp)
    cls = load_blueprint_class(package, bp)
    if cls is None:
        record("FAIL", step, "cannot load the generated class")
        return
    cdo = unreal.get_default_object(cls)
    if not isinstance(cdo, char_type):
        record("FAIL", step, "does not derive from CrossguardCharacter")
        return

    # Native default subobjects; editing them on the Blueprint CDO is what the details panel does.
    equipment = cdo.get_editor_property("equipment_component")
    inventory = cdo.get_editor_property("inventory_component")
    loadout = cdo.get_editor_property("loadout_component")
    if equipment is None or inventory is None or loadout is None:
        record("FAIL", step, "EquipmentComponent / InventoryComponent / LoadoutComponent missing on the CDO")
        return

    changes = []
    wanted = [
        (equipment, "weapon_table", "DT_Weapons", "EquipmentComponent.weapon_table"),
        (equipment, "armor_table", "DT_Armor", "EquipmentComponent.armor_table"),
        (equipment, "affix_table", "DT_Affixes", "EquipmentComponent.affix_table"),
        (inventory, "item_table", "DT_Items", "InventoryComponent.item_table"),
    ]
    for target, prop, table_name, label in wanted:
        if tables.get(table_name) is not None:
            safe_modify(target)
            set_if_different(target, prop, tables[table_name], changes, label)
    if catalog is not None:
        safe_modify(loadout)
        set_if_different(loadout, "skill_catalog", catalog, changes, "LoadoutComponent.skill_catalog")

    if not KEEP_MAPPING_CONTEXT:
        try:
            current = cdo.get_editor_property("default_mapping_context")
            if current is not None:
                cdo.set_editor_property("default_mapping_context", None)
                changes.append("default_mapping_context {} -> None (built-in key bindings)".format(path_of(current)))
        except Exception as error:  # noqa: BLE001
            warn("default_mapping_context: {}".format(error))

    safe_modify(cdo)
    if not save(bp):
        record("FAIL", step, "save failed")
        return

    # Read back from a fresh CDO lookup.
    cdo = unreal.get_default_object(cls)
    equipment = cdo.get_editor_property("equipment_component")
    inventory = cdo.get_editor_property("inventory_component")
    loadout = cdo.get_editor_property("loadout_component")
    problems = []
    for target, prop, table_name, label in (
        (equipment, "weapon_table", "DT_Weapons", "EquipmentComponent.weapon_table"),
        (equipment, "armor_table", "DT_Armor", "EquipmentComponent.armor_table"),
        (equipment, "affix_table", "DT_Affixes", "EquipmentComponent.affix_table"),
        (inventory, "item_table", "DT_Items", "InventoryComponent.item_table"),
    ):
        if tables.get(table_name) is None:
            problems.append(table_name + " missing")
        elif not same_object(target.get_editor_property(prop), tables[table_name]):
            problems.append(label + " did not stick")
    if catalog is None:
        problems.append("DA_SkillCatalog missing")
    elif not same_object(loadout.get_editor_property("skill_catalog"), catalog):
        problems.append("LoadoutComponent.skill_catalog did not stick")
    if problems:
        record("FAIL", step, "; ".join(problems))
        return
    detail = "; ".join(changes) if changes else "already set"
    if note:
        detail += " (" + note + ")"
    record("PASS", step, detail)
    if note and "native" in note:
        record("SKIP", "Game mode Default Pawn Class", note)


# ------------------------------------------------------------------------------------------------
# Step 5: dev sandbox map (optional)
# ------------------------------------------------------------------------------------------------

def spawn(actor_subsystem, cls, location, rotation=None, label=None):
    rotation = rotation or unreal.Rotator(0.0, 0.0, 0.0)
    actor = actor_subsystem.spawn_actor_from_class(cls, location, rotation)
    if actor is not None and label:
        try:
            actor.set_actor_label(label)
        except Exception:  # noqa: BLE001
            pass
    return actor


def spawn_mesh(actor_subsystem, mesh_path, location, scale, label, collision=True):
    mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
    actor = spawn(actor_subsystem, unreal.StaticMeshActor, location, label=label)
    if actor is None:
        return None
    component = actor.get_editor_property("static_mesh_component")
    component.set_static_mesh(mesh)
    actor.set_actor_scale3d(scale)
    if not collision:
        component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    return actor


def create_dev_map(game_mode_cls):
    step = "Dev map " + MAP_PATH
    if asset_exists(MAP_PATH):
        record("SKIP", step, "already exists (not modified)")
        return
    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    created = False
    try:
        created = bool(level_subsystem.new_level(MAP_PATH))  # UNCERTAIN inside the commandlet.
    except Exception as error:  # noqa: BLE001
        warn("LevelEditorSubsystem.new_level: {}".format(error))
    if not created:
        try:
            world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
            created = world is not None and bool(unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PATH))
        except Exception as error:  # noqa: BLE001
            warn("EditorLoadingAndSavingUtils.new_blank_map/save_map: {}".format(error))
    if not created:
        record("FAIL", step, "could not create the map (see the log)")
        return

    placed = []
    skipped = []
    v = unreal.Vector
    # 100 m x 100 m floor, top at z = 0.
    floor = spawn_mesh(actor_subsystem, "/Engine/BasicShapes/Cube.Cube", v(0, 0, -50), v(100, 100, 1), "Floor")
    if floor:
        # A 100x scaled cube shadows its own top and goes black; the floor needs no shadow.
        floor.get_editor_property("static_mesh_component").set_cast_shadow(False)
        placed.append("floor")
    if spawn(actor_subsystem, unreal.SkyAtmosphere, v(0, 0, 0), label="SkyAtmosphere"):
        placed.append("sky atmosphere")
    # Movable lights: no Lightmass build, so the map never says "lighting needs to be rebuilt".
    # unreal.Rotator's positional order is roll, pitch, yaw: name them (the sun once pointed up).
    light = spawn(actor_subsystem, unreal.DirectionalLight, v(0, 0, 1000), unreal.Rotator(roll=0.0, pitch=-45.0, yaw=30.0), "Sun")
    if light:
        light.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
        placed.append("light")
    sky = spawn(actor_subsystem, unreal.SkyLight, v(0, 0, 1200), label="SkyLight")
    if sky:
        sky.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
        sky.light_component.set_editor_property("real_time_capture", True)
        placed.append("sky light")
    if spawn(actor_subsystem, unreal.PlayerStart, v(-1500, 0, 100), label="PlayerStart"):
        placed.append("player start")
    nav = spawn(actor_subsystem, unreal.NavMeshBoundsVolume, v(0, 0, 0), label="NavMeshBounds")
    if nav:
        # UNCERTAIN: the spawned volume's brush is a 200 uu box (UActorFactoryBoxVolume); scaled to
        # cover the floor and 20 m of height.
        nav.set_actor_scale3d(v(50, 50, 10))
        placed.append("nav mesh bounds (build paths after opening)")

    gather_type = native_class("CrossguardGatherNode")
    if gather_type is not None:
        for table_id, location, label in (("gather_herb", v(-800, 600, 50), "GatherNode_Herb"),
                                          ("gather_ore", v(-800, -600, 50), "GatherNode_Ore")):
            node = spawn(actor_subsystem, gather_type, location, label=label)
            if node:
                node.set_editor_property("loot_table_id", table_id)
                placed.append(table_id + " node")
    else:
        skipped.append("gather node (class missing)")

    container_type = native_class("CrossguardLootContainer")
    if container_type is not None:
        chest = spawn(actor_subsystem, container_type, v(-600, 0, 50), label="Chest_Common")
        if chest:
            chest.set_editor_property("loot_table_id", "chest_common")
            # The native container has no mesh; a box marks the spot.
            spawn_mesh(actor_subsystem, "/Engine/BasicShapes/Cube.Cube", v(-600, 0, 25), v(0.8, 0.5, 0.5), "Chest_Common_Marker", collision=False)
            placed.append("chest_common container")
    else:
        skipped.append("loot container (class missing)")

    spawner_type = native_class("CrossguardMobSpawner")
    option_type = native_class("CrossguardMobSpawnOption")
    if spawner_type is not None and option_type is not None:
        spawner = spawn(actor_subsystem, spawner_type, v(2500, 0, 50), label="MobSpawner_Skeletons")
        if spawner:
            options = []
            for mob_id, weight in (("skeleton_soldier", 3), ("skeleton_archer", 1)):
                option = option_type()
                option.set_editor_property("mob_id", mob_id)
                option.set_editor_property("weight", weight)
                options.append(option)
            spawner.set_editor_property("mobs", options)
            spawner.set_editor_property("count_min", 3)
            spawner.set_editor_property("count_max", 5)
            spawner.set_editor_property("radius", 600.0)
            placed.append("mob spawner")
    else:
        skipped.append("mob spawner (class missing)")

    # Dummy: the character Blueprint (else the native class) possessed by a plain AI controller.
    dummy_cls = None
    char_package, char_bp = find_blueprint(CHARACTER_BP)
    if char_bp is not None:
        dummy_cls = load_blueprint_class(char_package, char_bp)
    if dummy_cls is None:
        dummy_cls = native_class("CrossguardCharacter")
    if dummy_cls is not None:
        dummy = spawn(actor_subsystem, dummy_cls, v(-1000, 0, 100), unreal.Rotator(roll=0.0, pitch=0.0, yaw=180.0), "Dummy")
        if dummy:
            try:
                dummy.set_editor_property("auto_possess_ai", unreal.AutoPossessAI.PLACED_IN_WORLD_OR_SPAWNED)
            except Exception as error:  # noqa: BLE001
                warn("dummy auto_possess_ai: {}".format(error))
            placed.append("dummy")
    else:
        skipped.append("dummy (class missing)")

    if game_mode_cls is not None:
        try:
            world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
            world.get_world_settings().set_editor_property("default_game_mode", game_mode_cls)
            placed.append("game mode override " + GAME_MODE_BP)
        except Exception as error:  # noqa: BLE001
            warn("world settings game mode: {}".format(error))

    saved = False
    try:
        saved = bool(level_subsystem.save_current_level())
    except Exception as error:  # noqa: BLE001
        warn("save_current_level: {}".format(error))
    if not saved:
        try:
            saved = bool(unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True))
        except Exception as error:  # noqa: BLE001
            warn("save_dirty_packages: {}".format(error))
    if not saved:
        record("FAIL", step, "placed {} but saving failed".format(", ".join(placed)))
        return
    detail = "placed " + ", ".join(placed)
    if skipped:
        detail += "; skipped " + ", ".join(skipped)
    record("PASS", step, detail)


# ------------------------------------------------------------------------------------------------
# Main
# ------------------------------------------------------------------------------------------------

def main():
    log("project {}; create map: {}, create blueprints: {}".format(PROJECT_DIR, CREATE_MAP, CREATE_BLUEPRINTS))

    if native_class("CrossguardGameMode") is None:
        record("FAIL", "Crossguard C++ classes", "unreal.CrossguardGameMode missing: the Crossguard module is not "
                                                   "loaded. Build CrossguardEditor (Tools\\build_editor.bat) and rerun")
        return

    # The commandlet may start before the asset registry has scanned /Game.
    try:
        unreal.AssetRegistryHelpers.get_asset_registry().search_all_assets(True)
    except Exception as error:  # noqa: BLE001
        warn("asset registry scan: {}".format(error))

    tables = {}
    for asset_name, json_rel, struct_name in TABLES:
        try:
            tables[asset_name] = setup_table(asset_name, json_rel, struct_name)
        except Exception:  # noqa: BLE001
            record("FAIL", "Data table " + asset_name, traceback.format_exc().strip().splitlines()[-1])
            unreal.log_error(traceback.format_exc())
            tables[asset_name] = None

    catalog = None
    try:
        catalog = setup_catalog(tables.get("DT_Skills"))
    except Exception:  # noqa: BLE001
        record("FAIL", "Skill catalog " + CATALOG_NAME, traceback.format_exc().strip().splitlines()[-1])
        unreal.log_error(traceback.format_exc())

    game_mode_cls = None
    try:
        _, game_mode_cls = setup_game_mode(tables, catalog)
    except Exception:  # noqa: BLE001
        record("FAIL", "Game mode " + GAME_MODE_BP, traceback.format_exc().strip().splitlines()[-1])
        unreal.log_error(traceback.format_exc())

    try:
        setup_character(tables, catalog, game_mode_cls)
    except Exception:  # noqa: BLE001
        record("FAIL", "Character Blueprint", traceback.format_exc().strip().splitlines()[-1])
        unreal.log_error(traceback.format_exc())

    if CREATE_MAP:
        try:
            create_dev_map(game_mode_cls)
        except Exception:  # noqa: BLE001
            record("FAIL", "Dev map " + MAP_PATH, traceback.format_exc().strip().splitlines()[-1])
            unreal.log_error(traceback.format_exc())


def run():
    try:
        main()
    except Exception:  # noqa: BLE001
        record("FAIL", "editor_setup.py", traceback.format_exc().strip().splitlines()[-1])
        unreal.log_error(traceback.format_exc())
    ok = write_summary()
    if QUIT_AT_END:
        try:
            unreal.SystemLibrary.quit_editor()
        except Exception as error:  # noqa: BLE001
            warn("quit_editor: {}".format(error))
    # Under -run=pythonscript an uncaught exception / SystemExit does not reliably set the process
    # exit code; the wrappers read summary.txt instead. Raising here at least logs an error.
    if not ok:
        raise RuntimeError("Crossguard editor setup FAILED, see " + SUMMARY_FILE)


run()
