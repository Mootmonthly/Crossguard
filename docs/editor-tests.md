# Editor Tests

Automation tests that run inside the Unreal editor, without opening it: no window, no GPU
(`-nullrhi`). Most need no level; the [scenario tests](#scenario-tests) play the real game in
Play-In-Editor on a blank map they create, so the build checklist's play-tests run without clicking. One command builds the editor, runs them and writes a short report. They complement
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
(`WITH_DEV_AUTOMATION_TESTS`; the data and scenario tests also need `WITH_EDITOR`). Flags:
`EditorContext | EngineFilter` (scenario tests: `EditorContext | ProductFilter`). The scenario tests
are described in their own section below.

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

## Scenario tests

`Crossguard.Scenario.*` turn rows of [next-build-checklist.md](next-build-checklist.md) into tests
that play the game: each creates a blank map, starts Play-In-Editor, spawns players and targets,
equips them, attacks, casts, waits, and checks Health, tags, XP and the Output Log. They run with
the others (`Tools\run_editor_tests.bat`), or alone:

```
Tools\run_editor_tests.bat -Filter Crossguard.Scenario
Tools\run_editor_tests.bat -Filter Crossguard.Scenario.Combat -SkipBuild
```

Each takes a few seconds (PIE start and end included). In the open editor they are under Session
Frontend → Automation, filter `Crossguard.Scenario` (the **Product** filter): running one there
**replaces the open level with an unsaved blank map**, so save first.

### What they need

The same Blueprint and data set-up as the checklist (sections 2 and 3):

- A game mode Blueprint: a Blueprint Class with parent `CrossguardGameMode` (found through the Asset
  Registry; the one named `BP_CrossguardGameMode` wins if there are several). Not found: every
  scenario fails with "no Blueprint of CrossguardGameMode found".
- A character Blueprint: the game mode's **Default Pawn Class** if it is a Blueprint of
  `CrossguardCharacter`, else the Blueprint named `BP_CrossguardCharacter` (a warning says so). On it:
  `EquipmentComponent` → Weapon Table `DT_Weapons` (Armor `DT_Armor`, Affix `DT_Affixes`);
  `InventoryComponent` → Item Table `DT_Items`; `LoadoutComponent` → Skill Catalog
  `DA_SkillCatalog` (without it skills are granted from the native C++ classes and a note says so).
- `Crossguard.Scenario.Monsters.MobSpawner`: the game mode's **Mob Table** `DT_Mobs` (skipped with a
  warning while unset) with a `skeleton_soldier` row.

Scenario characters ignore the character Blueprint's **Crossguard | Debug** loadout (Debug items,
skills, inventory, revive delay, loot-takes-all, debug HUD are cleared on them), so the tests do not
depend on what you set there for manual testing. `Level` / `bUseDebugProgression` stay; expected
damage is computed from the characters' actual stats, so they work at any level.

### The scenarios

| Test | Checks |
| --- | --- |
| `Crossguard.Scenario.Raid.KillDummyLeavesBody` | A player with `sword_one_handed` hits a dummy 1.5 m away: the first hit takes exactly the damage formula's amount (weapon 30 at Common, the player's ADB / AAD, the dummy's AR); it keeps attacking (at the weapon's interval) until the dummy dies: 0 Health, `State.Dead`, a loot container (its body) where it fell, and the player's raid XP +5 (`GetRaidXp`). |
| `Crossguard.Scenario.Skills.Fireball` | A player with `wand_one_handed` and `fireball` in the loadout casts at a dummy 6 m away: Fireball goes on cooldown (`Cooldown.Skill.Fireball`); 1 s later the dummy has lost exactly the formula's damage for 50 Magic Damage at coefficient 0.5 (at least 50 before Magic Resistance); the caster, outside the 2 m splash, is unhurt. |
| `Crossguard.Scenario.Combat.ShieldBlock` | A (`sword_one_handed` + `shield_one_handed`) raises the guard facing B (`dagger_one_handed`); B stabs: A loses nothing, the log says "blocked" and "0% through", B has `State.Stunned` (still at 0.5 s, gone at 1.5 s), A still guards. A turns its back; B stabs again: A takes the full dagger hit (formula, A's AR includes the shield's 0.08), no new "blocked" line. Releasing the guard removes `State.Guarding`. |
| `Crossguard.Scenario.Monsters.MobSpawner` | A `CrossguardMobSpawner` with `skeleton_soldier` (weight 1), Count 3–3, Radius 6 m, placed 40 m out during a raid: 3 monsters owned by it, each with Mob Id `skeleton_soldier`, Max Health and Health = the mob's Health in `DT_Mobs` (120), a controller, within the radius, above the floor. |

Checklist rows covered (tick them when the scenario passes; the rest of each row, such as the
ragdoll's look, still needs eyes):

| Checklist row (section 4) | Scenario test | Not covered |
| --- | --- | --- |
| Kill the dummy → body, 5 XP | `Crossguard.Scenario.Raid.KillDummyLeavesBody` | The ragdoll itself. |
| `fireball` at the dummy → 50+ damage on hit | `Crossguard.Scenario.Skills.Fireball` | Standing within 2 m of the blast hurts you for 15. |
| Block: shield + sword vs dagger → "blocked … 0% through", 1 s stun; turn away → full damage | `Crossguard.Scenario.Combat.ShieldBlock` | Two real clients (the test uses two server-side players in one standalone PIE). |
| CrossguardMobSpawner with skeletons → they appear around it | `Crossguard.Scenario.Monsters.MobSpawner` | Archers keeping away, respawn, NavMesh pathing. |

### How a scenario runs

1. If a PIE session is running it is ended first.
2. The game mode and character Blueprints are found (Asset Registry) and loaded.
3. A new blank map (`FAutomationEditorCommonUtils::CreateNewMap`) gets: World Settings → Game Mode
   Override = the game mode Blueprint; a 200 m × 200 m floor (engine cube, collision BlockAll, top at
   Z = 0); a Player Start at (−30 m, −30 m) where the PIE player appears, out of the way.
4. PIE starts as **one standalone instance** (`RequestPlaySession`, net mode Standalone, 1 client),
   whatever your editor's Play settings are. The PIE world is the server.
5. The game mode's `bIsRaid` is set (on by default, as in checklist section 4) and the optional
   time dilation applied; a log listener starts.
6. The steps run, one per frame, with their waits.
7. PIE ends: always, also after a failure or a timeout (180 real seconds for the steps by default;
   60 s for PIE to start, 30 s to end).

## Writing a scenario

Add a test to the `CrossguardScenario<Area>Tests.cpp` file of its area (`Raid`, `Skills`, `Combat`,
`Monsters`) or a new one in `Source/Crossguard/Private/Tests/`, wrapped in
`#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR`. One scenario per test, named
`Crossguard.Scenario.<Area>.<Name>`, with `CROSSGUARD_SCENARIO_FLAGS`:

```cpp
#include "Tests/CrossguardScenario.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AbilitySystem/CrossguardGameplayTags.h"
#include "Character/CrossguardCharacter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioMyThing, "Crossguard.Scenario.Combat.MyThing", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioMyThing::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	FScenario Scenario(*this);

	Scenario.Step(TEXT("Spawn and equip"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(0, 1000, 0), 0);
		ACrossguardCharacter* Dummy = C.SpawnDummy(TEXT("Dummy"), FVector(150, 1000, 0));
		if (!C.Require(Player && Dummy, TEXT("both spawned")) || !C.Equip(Player, TEXT("axe_one_handed")))
		{
			return;
		}
		C.Aim(Player, Dummy);
	});
	Scenario.Wait(0.5f); // land on the floor

	Scenario.Step(TEXT("Hit"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Player"));
		ACrossguardCharacter* Dummy = C.Get<ACrossguardCharacter>(TEXT("Dummy"));
		if (!Player || !Dummy) { C.Fail(TEXT("gone")); return; }
		const float Before = C.Health(Dummy);
		const float Expected = C.ExpectedDamage(Player, Dummy, C.WeaponValue(Player));
		if (!C.Require(C.BasicAttack(Player), TEXT("attack activates"))) return;
		C.Test.TestEqual(TEXT("Damage"), Before - C.Health(Dummy), Expected, 0.05f);
	});

	Scenario.Run();
	return true;
}

#endif
```

New files need a project-file regenerate (or a build from `run_editor_tests.bat`) to be picked up.
Keep file-local helpers and constants in a uniquely named namespace (unity builds).

### Rules

- **Steps** run in order, one per frame, on the game thread, against the PIE world (server side).
- **Time**: `Wait(Seconds)` and `WaitUntil` timeouts are **game seconds** (`World->GetTimeSeconds()`),
  i.e. real seconds at time dilation 1. `Scenario.TimeDilation(10)` (or `C.SetTimeDilation(10)` in a
  step; the engine caps it at 20) speeds up everything at once: cooldowns, timers, ability waits and
  the scenario's waits, so a 60 s summon test takes 6 real seconds. Steps themselves take no time.
- **Asserting**: `C.Test.TestEqual / TestTrue / TestNotNull / AddInfo` are soft (the scenario goes
  on). `if (!C.Require(Cond, What)) return;` for what later steps depend on: `Require` and
  `Fail(Why)` add an error and skip the remaining steps. `Skip(Why)` stops with a warning and the
  test passes (optional data missing). PIE always ends.
- **Actors** live in the PIE world: refer to them by name in later steps (`Get<T>(Name)`, which
  reports a missing one; `FindActor(Name)` for silent), never by raw pointers captured across steps.
  Numbers between steps: `SetNumber(Name, Value)` / `Number(Name)`.
- **Places**: the floor top is Z = 0; character spawn points are points on the floor. Keep a scene
  within about 50 m of the origin, more than 15 m (monster aggro range) from other monsters, and away
  from the PIE player at (−30 m, −30 m). Give each scene its own Y row (the examples use Y = 10 m,
  20 m, 30 m) so a stray projectile or splash does not reach another.
- **Expected numbers**: prefer `ExpectedDamage(Source, Target, AttackBase, MagicBase, Coefficient)`
  with the base from the data (`Data/Items/*.json`, `Data/Skills/Skills.json`) over hard-coded
  totals: it uses both characters' current stats with the same formula as the damage execution, so
  the test holds at any level and allocation. Health never goes below 0: compare with
  `FMath::Min(Expected, HealthBefore)`.
- **Guards** face the player's view through the camera manager, which updates once a frame:
  `Wait(0.2f)` after turning a guarding character before it is hit.
- **Log**: everything logged while the scenario runs is captured as `"Category: message"`
  (`LogContains`, `LogCount`, `LogLines`, `ClearLog`). Warnings and errors are also reported by the
  automation framework itself; an error logged by the game fails the test.

### API reference (`CrossguardScenario.h`)

`FScenario` (built in `RunTest`):

| Call | Does |
| --- | --- |
| `Step(Name, [](FScenarioContext& C) { ... })` | One action, in its own frame. |
| `Wait(Seconds)` | Waits game seconds. |
| `WaitUntil(Name, TimeoutSeconds, [](FScenarioContext& C) { return Done; }, PollSeconds = 0)` | Polls every frame (or every `PollSeconds`) until true; fails after the timeout. The condition may act (attack until dead). |
| `TimeDilation(X)`, `Timeout(RealSeconds)`, `Raid(bool)`, `KeepDebugLoadout()` | Options: global time dilation from the start (1); real-time limit for the steps (180); game mode `bIsRaid` (true); keep the character Blueprint's Debug loadout on spawned characters. |
| `Run()` | Queues it. Last call in `RunTest`, once. |

`FScenarioContext` (`C` in steps):

| Group | Call | Does |
| --- | --- | --- |
| World | `C.Test`, `World()`, `GameMode()`, `CharacterClass()`, `LocalPlayer()`, `Now()` | The test; the PIE world; the `ACrossguardGameMode`; the character Blueprint class; the PIE player's character (player 1, also remembered as `"LocalPlayer"`); game seconds. |
| Flow | `Fail(Why)`, `Require(Cond, What)`, `Skip(Why)`, `IsStopped()` | See Rules. |
| Names | `Remember(Name, Actor)`, `FindActor(Name)`, `Get<T>(Name)`, `SetNumber(Name, V)`, `Number(Name, Default)` | Spawn calls remember their actor under its name. |
| Spawning | `SpawnPlayer(Name, FloorPoint, Yaw = 0, Party = "")` | A real player: a PlayerController of the game mode's class with its PlayerState and team (`InitNewPlayer` with `?Name=` and `?Party=`), possessing a new character Blueprint through `HandleStartingNewPlayer` → `RestartPlayer` (begins the raid on a raid map). `IsPlayerControlled()` is true (trading, player-kill XP); same Party = same team. No local player, input or HUD. Its controller is remembered as `"<Name>.Controller"`. |
| | `SpawnDummy(Name, FloorPoint, Yaw = 180)` | The character Blueprint possessed by a plain `AAIController` (like the checklist's placed dummy with Auto Possess AI): ability system set up, not player-controlled, no player state or team, BasicMonster kill XP. |
| | `SpawnMonster(Name, MobId, FloorPoint, Yaw = 0)` | An `ACrossguardMonster` with that Mob Id (stats from the Mob Table at BeginPlay), with its AI: it fights players and minions within its aggro range (15 m). |
| | `SpawnActor<T>(Name, Location, Yaw = 0, [](T& A) { ... }, Class = nullptr)` | Any actor; the lambda runs before BeginPlay. Location as is (not put on the floor). |
| | `Place(Character, FloorPoint, Yaw)`, `bKeepDebugLoadout` | Teleport; keep the Blueprint's Debug loadout for characters spawned after setting it. |
| Gear | `Equip(Character, WeaponId, Slot = MainHand, Rarity = Common)`, `Unequip(Character, Slot)` | Server equip of a fresh weapon in the active set (a two-handed main hand empties the off hand first). Fails the scenario on an error (unknown id, Weapon Table unset). |
| | `GiveItem(Character, ItemId, Count = 1, Rarity)`, `GiveItemOfCategory(Character, Category, ItemId, Count, Rarity)` | Into the inventory (category from `DT_Items`; `SkillGem` with the skill id). Returns what did not fit. |
| | `GrantSkills(Character, {TEXT("fireball"), ...})` | The loadout, as `DebugSkills` does (learned first, checked against Skill Capacity; when over capacity the level is raised, with debug progression, up to 15 until it fits); native classes on the ability system when the LoadoutComponent has no Skill Catalog. |
| Acting | `Face(Character, Yaw)`, `Aim(Character, Target)`, `AimAt(Character, Point)` | Level turn; aim from the eyes at the target's centre (pitch included) for aimed skills and swings. AI-controlled characters keep only the yaw. The camera lags: wait 0.2 s after `Face`/`Aim` before an aimed skill, swing or guard. |
| | `BasicAttack(Character)`, `RaiseGuard(Character)`, `LowerGuard(Character)` | Left click (false while the last attack recovers); right click held / released. |
| | `ActivateSkill(Character, SkillId)`, `IsOnCooldown(Character, SkillId)` | Activates a granted active skill (as E); its `CooldownTags` are on. |
| | `SetHealth(A, Value)`, `ApplyStatus(A, CrossguardTags::State_Silenced, Seconds)` | Health base value; a status through `UCrossguardSkillAbility::ApplyStatus` (self-applied, so CC immunity still counts). |
| | `Kill(Victim, Killer = nullptr)` | Lethal damage through the damage effect: death, body and kill XP as in play. |
| Reading | `Health(A)`, `MaxHealth(A)`, `Stat(A, UCrossguardAttributeSet::GetXAttribute())` | Attributes (−1 / 0 without an ability system). |
| | `HasTag(A, CrossguardTags::State_Stunned)`, `HasTag(A, TEXT("State.Stunned"))`, `IsDead(A)` | Owned gameplay tags. |
| | `PlayerState(A)`, `WeaponValue(Character, Slot)`, `ExpectedDamage(Source, Target, AttackBase, MagicBase = 0, Coefficient = 1, AttackShare = 1)` | `ACrossguardPlayerState`; the weapon's AD / MD at its rarity; one hit by the damage formula with current stats. |
| | `FindAll<T>()`, `FindNearest<T>(Point, Radius)` | Actors in the PIE world. |
| Game | `SetRaid(bool)`, `SetTimeDilation(X)` | `bIsRaid` (and begins / ends every player's raid); global time dilation. |
| Log | `LogContains(Text)`, `LogCount(Text)`, `LogLines()`, `ClearLog()` | The captured log. |

### Scenario framework: engine calls to check first

The framework was written without an engine to compile against. If the scenario tests do not build
or misbehave on a real build, these are the calls to check, most likely first:

1. PIE start (`CrossguardScenario.cpp`, `BeginScenario`): `FRequestPlaySessionParams` (header
   `PlayInEditorDataTypes.h`) and its `EditorPlaySettings` field; `ULevelEditorPlaySettings::
   SetPlayNetMode(EPlayNetMode::PIE_Standalone)`, `SetPlayNumberOfClients`, `SetRunUnderOneProcess`;
   `GEditor->RequestPlaySession`. Headless (`-nullrhi`), PIE opens without a viewport: if "PIE did not
   start" appears, this is the place.
2. `FAutomationEditorCommonUtils::CreateNewMap()` (`Tests/AutomationEditorCommon.h`, UnrealEd).
3. `IAssetRegistry::GetDerivedClassNames` with `FTopLevelAssetPath` (UE 5.1+ signature).
4. Extra players: a PlayerController spawned with `SpawnActor` (no `UPlayer`), then
   `ACrossguardGameMode::InitNewPlayer`, `APlayerController::SetPawn`,
   `ACrossguardGameMode::HandleStartingNewPlayer_Implementation` → `RestartPlayer`. In standalone
   `IsLocalController()` is true for it, which GAS needs to activate locally predicted abilities; the
   engine creates its `PlayerInput` on first tick.
5. The log listener (`GLog->AddOutputDevice`, `FOutputDevice::CanBeUsedOnAnyThread`).
