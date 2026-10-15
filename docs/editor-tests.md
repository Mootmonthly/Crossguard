# Editor Tests

Automation tests that run inside the Unreal editor, without opening it: no level, no window, no GPU
(`-nullrhi`). One command builds the editor, runs them and writes a short report. They complement
`Tools/run_checks.sh` (data validators and the engine-free rules unit tests), which runs without
Unreal.

## Run them

From the project folder, in a Windows terminal (cmd or PowerShell):

```
Tools\run_editor_tests.bat
```

That builds **CrossguardEditor Win64 Development**, runs every test named `Crossguard.*` and prints:

```
RESULT: 6 passed, 1 failed (7 tests, 41 s, editor exit code 0)
PASS    Crossguard.Data.EditorAssets  (0.12 s)
        Warning: /Game/Data/DT_Mobs not found: skipped (import Data/Mobs/MobTable.json ...)
FAIL    Crossguard.World.AICharacters  (0.30 s)
        Error: A hit lowers the monster's Health (CrossguardWorldTests.cpp:212)
...
```

The same text goes to `Saved\EditorTests\summary.txt`. Next to it: `index.json` / `index.html` (the
engine's full report), `editor.log` (the whole editor log) and `build.log`.

Options (PowerShell style, after the `.bat` or with
`powershell -ExecutionPolicy Bypass -File Tools\run_editor_tests.ps1`):

| Option | Meaning |
| --- | --- |
| `-SkipBuild` | Use the binaries already built (e.g. right after building in Visual Studio). |
| `-Filter Crossguard.Loot` | Only tests whose name starts with this. Several: `Crossguard.Loot+Crossguard.Teams`. |
| `-EngineDir "D:\UE_5.8"` | Engine folder. Default: the `UE_ENGINE_DIR` environment variable, else the engine `Crossguard.uproject` is associated with (registry / Epic launcher), else `C:\Program Files\Epic Games\UE_5.8`. |
| `-TimeoutMinutes 30` | Stop the editor if the run hangs. |

Exit code: 0 all passed, 1 a test failed (or none matched), 2 the build failed (the last build
errors are printed), 3 the editor wrote no report (crash, timeout: the end of `editor.log` is
printed), 4 engine or project not found.

Notes:

- The build fails while the editor is open with Live Coding on: close the editor first (the script
  says so). With the editor already built, `-SkipBuild` runs the tests even while the editor is open.
- New test files only appear after a build that includes them (re-generate the Visual Studio files
  if you build from the IDE).
- In the editor, the same tests are under **Tools → Session Frontend → Automation**, filter
  `Crossguard`.

## Running them from Claude Code on your PC

A Claude Code session running on the Windows machine can build, run and read the result itself:

1. Open the project folder in Claude Code: the Claude desktop app (Code tab, choose the folder), or a
   terminal in the folder running `claude` (or `claude remote-control` to drive that session from
   claude.ai or the phone app).
2. Ask it to run the editor tests. It runs
   `powershell -ExecutionPolicy Bypass -File Tools\run_editor_tests.ps1` (add `-SkipBuild` if the
   editor is already built), then reads `Saved\EditorTests\summary.txt`, and `build.log` or
   `editor.log` when it needs more.
3. A full build plus run takes minutes; the script waits for the build mutex (`-WaitMutex`) and gives
   the editor 30 minutes before stopping it.

If the engine is not in the default place, set `UE_ENGINE_DIR` once (System Properties →
Environment Variables) so neither you nor Claude has to pass `-EngineDir`.

## What the tests cover

All live in `Source/Crossguard/Private/Tests/` and compile only in builds with automation tests
(`WITH_DEV_AUTOMATION_TESTS`; the data tests also need `WITH_EDITOR`). Flags: `EditorContext |
EngineFilter`.

| Test | Needs | Checks |
| --- | --- | --- |
| `Crossguard.Skills.NativeRegistry` | nothing | Every id in `CrossguardNativeSkills::All()` maps to a concrete class whose default `SkillId` is that id; `Find` and a catalog without data fall back to it; every native ability class with a `SkillId` is the registered class for its id (catches a C++ skill nobody registered, or two classes sharing an id). |
| `Crossguard.Loot.RollLootDeterminism` | nothing | `UCrossguardLootLibrary::RollLoot` with small data tables built in the test (weapons, armor, affixes, items, a mixed loot table): the same seed gives exactly the same items, affixes and stacks; different seeds differ; every drop has a size and a count within its stack size; gold splits into stacks of at most 100. |
| `Crossguard.Loot.AnyArmorOfOneType` | nothing | An "any armor" entry limited to Plate only ever drops plate pieces, over 64 seeds. |
| `Crossguard.Teams.MonstersAndPlayerless` | nothing | On class defaults: a monster is on `MonsterTeamId` and its own ally; a character without a player state and a minion without a summoner have no team and are not a monster's allies. |
| `Crossguard.World.AICharacters` | a transient world | Spawns an `ACrossguardMonster` (Fixed Health 50) and an `ACrossguardMinion` (a native skill in `SkillIds`): both are possessed by their AI controller; Max Health is the Fixed Health; the skill is granted on possession and `GrantSkills` replaces it; a 10-damage hit through `UCrossguardDamageEffect` lowers Health; lethal damage leaves 0 Health, `State.Dead` and `IsAlive() == false`, and further hits keep it at 0; two monsters are allies, a monster's minion is on the monster team, a player-less character is nobody's ally; a minion dies the same way. |
| `Crossguard.Data.RepoTables` | the repository's `Data/` | Imports every generated `…Table.json` with its real row struct (as the editor import does) and fails on import problems; native skills' category and cooldown match their rows; loot entries point at existing weapons, armor, items and skills; every loot table rolls 16 times with the real data without dropping anything unresolvable; every mob's skills and loot table exist. |
| `Crossguard.Data.EditorAssets` | `Content/Data/DT_*`, `DA_SkillCatalog` | For each imported asset the checklist asks for (`DT_Skills`, `DT_Weapons`, `DT_Affixes`, `DT_Armor`, `DT_Items`, `DT_Loot`, `DT_Mobs` in `/Game/Data/`): right row struct and the same rows as the JSON (else: reimport). `DA_SkillCatalog` (looked for in `/Game/Data`, `/Game`, `/Game/Data/Skills`, `/Game/Skills`): `SkillTable` set to `DT_Skills`, every `AbilityClasses` entry's `SkillId` matches and is a row. Missing assets are skipped with a warning, so the test passes on a fresh checkout. |

The world test runs in a world with no level and no game mode, and nothing ticks: actors spawned there
do not get `BeginPlay` (so `MobId` data is not applied), no loot is rolled and no XP is given. Engine
code may log warnings there (no navigation, no physics asset on the placeholder body); they show up
as warnings of that test. If that test ever crashes or reports that a pawn was not possessed, the
world set-up in `CrossguardTestUtils.h` (`FTestWorld`) is the place to look; the other tests do not
use it.

## Adding a test

Create a `.cpp` in `Source/Crossguard/Private/Tests/`, include `Tests/CrossguardTestUtils.h`, wrap
it in `#if WITH_DEV_AUTOMATION_TESTS`, and name the test `Crossguard.<Area>.<What>` with
`CROSSGUARD_TEST_FLAGS`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardMyTest, "Crossguard.Area.What", CROSSGUARD_TEST_FLAGS)

bool FCrossguardMyTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Two plus two"), 2 + 2, 4);
	return true;
}
```

Helpers: `CrossguardTests::MakeTable<FRowStruct>()` (a transient data table to `AddRow` into) and
`CrossguardTests::FTestWorld` (a transient game world, destroyed at the end of the scope). Keep
file-local helpers in a uniquely named namespace: unity builds compile several test files together.
