# PC Session Runbook (Claude Code on the Windows dev PC)

For a Claude Code session running on the Windows PC with Unreal installed. It builds, does the
editor set-up of [next-build-checklist.md](next-build-checklist.md) sections 2–3 without clicking,
runs the editor tests and triages them. Everything here is command line; the user only needs to
watch.

## Prerequisites

- Unreal Engine 5.8 (Epic launcher or source build). The scripts find it through `-EngineDir`, then
  `%UE_ENGINE_DIR%`, then the `.uproject`'s `EngineAssociation` (registry, launcher list), then
  `C:\Program Files\Epic Games\UE_5.8`.
- Visual Studio 2022 with the **Desktop development with C++** and **Game development with C++**
  workloads (MSVC, Windows SDK). Build.bat needs no `.sln`.
- Git with Git LFS (`git lfs install` once).
- Python plugins: `Crossguard.uproject` enables `PythonScriptPlugin` and `EditorScriptingUtilities`
  (Editor targets only). `Tools\editor_setup.bat` re-adds them if someone removed them.
- `python3` (or `python`) on PATH only for the repository checks (`Tools/run_checks.sh` needs bash,
  CMake and a C++ compiler; optional on the PC).

**Close the editor before any command-line build or set-up.** An open editor (Live Coding) refuses
the build or locks the DLL, and holds the assets `editor_setup` saves. `Tools\build_editor.bat
-KillEditor` stops running editors (unsaved work is lost; ask the user first).

## Command order

All from the project folder in `cmd` or PowerShell (`.bat` wrappers call the `.ps1` with
`-ExecutionPolicy Bypass`).

1. **Pull**

   ```bat
   git pull --rebase --autostash origin foundation/core-rules
   git lfs pull
   ```

2. **Build** (CrossguardEditor Win64 Development, compact errors)

   ```bat
   Tools\build_editor.bat
   ```

   Exit 0 built, 2 failed (errors printed as `Source/...cpp:12: error C2065: ...`), 4 engine not
   found.

3. **Fix loop** (known risks and fixes: [first-compile-triage.md](first-compile-triage.md)): read `Saved\Build\errors.txt`, fix the first error (later ones are often
   consequences), rebuild with `Tools\build_editor.bat`, repeat until exit 0. Commit fixes with a
   clear message; do not change game rules to make something compile.

4. **Editor set-up** (data tables, `DA_SkillCatalog`, Blueprint references)

   ```bat
   Tools\editor_setup.bat -SkipBuild
   ```

   Runs `Tools\editor_setup.py` in `UnrealEditor-Cmd.exe Crossguard.uproject -run=pythonscript
   -script=...`. One `PASS` / `FAIL` / `SKIP` line per step and a final `RESULT:` line; exit 0 all
   passed, 1 a step failed, 2 build failed (without `-SkipBuild`), 3 no summary (crash or Python
   error before the summary: the script prints the `LogPython: Error` lines), 4 set-up.

   What it does (idempotent; safe to rerun):

   | Step | Assets |
   | --- | --- |
   | Data tables, created with the row struct if missing, else refilled from the JSON, saved | `/Game/Data/DT_Skills` (`CrossguardSkillRow`, `Data/Skills/SkillTable.json`), `DT_Weapons` (`CrossguardWeaponRow`, `Data/Items/WeaponTable.json`), `DT_Affixes` (`CrossguardAffixRow`, `AffixTable.json`), `DT_Armor` (`CrossguardArmorRow`, `ArmorTable.json`), `DT_Items` (`CrossguardItemRow`, `ItemTable.json`), `DT_Loot` (`CrossguardLootTableRow`, `Data/Loot/LootTable.json`), `DT_Mobs` (`CrossguardMobRow`, `Data/Mobs/MobTable.json`) |
   | Skill catalog (found in `/Game/Data`, `/Game`, `/Game/Data/Skills`, `/Game/Skills`, else created in `/Game/Data`) | `DA_SkillCatalog.SkillTable` = `DT_Skills` (`AbilityClasses` untouched) |
   | `BP_CrossguardGameMode` (by name, anywhere under `/Game`) | **Crossguard \| Loot**: Loot Tables `DT_Loot`, Weapon / Armor / Affix / Item tables, Skill Catalog; **Crossguard \| Mobs**: Mob Table `DT_Mobs`, Mob Skill Catalog |
   | Character Blueprint (game mode's Default Pawn Class if a Blueprint of `CrossguardCharacter`, else `BP_CrossguardCharacter`) | `EquipmentComponent` Weapon / Armor / Affix Table, `InventoryComponent` Item Table, `LoadoutComponent` Skill Catalog; Default Mapping Context cleared (checklist 3; `-KeepMappingContext` to keep it) |

   Options: `-CreateMap` also creates `/Game/Maps/Dev/L_DevSandbox` (floor, light, player start,
   nav mesh bounds, `gather_herb` / `gather_ore` nodes, a `chest_common` container, a skeleton mob
   spawner, a dummy, game mode override) if it does not exist; open it once in the editor (or
   Build → Build Paths) for the nav mesh. `-CreateBlueprints` creates missing
   `BP_CrossguardGameMode` / `BP_CrossguardCharacter` in `/Game/Blueprints` (native parents, Default
   Pawn Class set) instead of skipping them; only with the user's OK. **The owner's PC already has both
   Blueprints: do not pass `-CreateBlueprints`.** If a step reports them missing, look for a different
   name or path before creating anything. `-Mode Editor` runs the script
   through `-ExecutePythonScript=` in a full null-RHI editor process (it quits at the end) instead
   of the commandlet; try it when a step fails only under the commandlet (e.g. map creation).

   A `SKIP` for a missing Blueprint means the scenario tests will fail or skip: report it to the
   user rather than inventing assets.

5. **Editor tests**

   ```bat
   Tools\run_editor_tests.bat -SkipBuild
   ```

   Narrow with `-Filter Crossguard.Data` (then `Crossguard.Scenario.Raid`, …) while iterating. See
   [editor-tests.md](editor-tests.md).

6. **Triage** each `FAIL` in `Saved\EditorTests\summary.txt`: data not set up (rerun step 4),
   test bug, or game bug ([editor-tests.md](editor-tests.md) "Known limits": until a first green
   run a failure may be the test). Fix, rebuild (step 2), rerun the narrowed filter, then the whole
   set. A skip ("Skipped without `DT_Mobs`", …) is not a pass: fix the set-up.

7. Commit only the files you changed (never `git add -A`; `Saved\`, `Intermediate\`, `Binaries\` are
   ignored, but `.uasset` changes under `Content\` are real and go through LFS). Push to
   `foundation/core-rules` after `git pull --rebase --autostash`.

## Where logs land

| What | File |
| --- | --- |
| Build (build_editor) | `Saved\Build\build.log`, errors only: `Saved\Build\errors.txt` |
| Editor set-up | `Saved\EditorSetup\summary.txt`, `editor.log`, `build.log` |
| Editor tests | `Saved\EditorTests\summary.txt`, `index.json`, `index.html`, `editor.log`, `build.log` |
| Editor's own log | `Saved\Logs\Crossguard.log` |
| Crashes | `Saved\Crashes\` |

## Still human-only

The tools cover checklist sections 1–3 and every **auto** row. These need the user (see the **By
hand** column of [editor-tests.md](editor-tests.md#checklist-coverage)):

- Feel and visuals: ragdolls, meshes, arcs, effects, the portal beam, the light, walls, the gather
  node hiding, the Q selector UI, sounds and animations.
- Two real clients (PIE, 2 clients, Listen Server): bodies ragdolling on both, `showdebug` of the
  other's `Equipment.Wielding.*`, stealth vanishing, tracking marks, trading and guilds through the
  console, block / parry between clients, concurrent looting.
- Real input: key bindings (1–7, Q, E, F, X, G, Space / Left Ctrl), the Debug loadout on
  `BP_CrossguardCharacter` (**Crossguard | Debug**: DebugInventory, DebugMainHand, …: the user's
  choice for manual play, not set by the script).
- NavMesh pathing (summons following, monsters walking round obstacles), the `mithril_dragon` fight.
