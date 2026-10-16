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
| `-Visual` | The [visual run](#visual-run): rendering on (no `-nullrhi`), PIE windows on the desktop, screenshots from `Crossguard.Visual.*` into `Saved\EditorTests\Screenshots\`. Default filter `Crossguard.Visual`. |

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
| `Crossguard.UI.RarityContrast` | nothing | Every rarity's item text colour on its item background (also hovered, +0.05) and on a window's panel is at least 4.5:1 (WCAG AA), and white on the default grey button (also hovered); logs each ratio ([ui.md](ui.md), "Contrast"). |
| `Crossguard.Menu.Urls` | nothing | The main menu's URLs (`ACrossguardMenuGameMode`): Play = `<hub>?listen?Hub` (parsed back: map, `listen`, `Hub`), Join trims an address and refuses empty / spaces / options / map paths, the trip back = `<menu>?MenuReason=RaidOver` (key sanitised), reason messages. Flags `EditorContext \| ProductFilter`. |
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
Tools\run_editor_tests.bat -Filter Crossguard.Net -SkipBuild
```

`Crossguard.Net.*` are the [networked scenarios](#networked-scenarios): a server and real PIE
clients in one process. Each takes a few seconds (PIE start and end included). In the open editor they are under Session
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
- `Crossguard.Scenario.Monsters.MobSpawner` and the monster data tests: the game mode's **Mob Table**
  `DT_Mobs` (skipped with a warning while unset) with a `skeleton_soldier` row. Loot tests: the game
  mode's Loot Tables `DT_Loot`. Skill Gem and passive tests: `DA_SkillCatalog`. Armor: `DT_Armor`.

Scenario characters ignore the character Blueprint's **Crossguard | Debug** loadout (Debug items,
skills, inventory, revive delay, loot-takes-all, debug HUD are cleared on them), so the tests do not
depend on what you set there for manual testing. `Level` / `bUseDebugProgression` stay; expected
damage is computed from the characters' actual stats, so they work at any level.

### The scenarios

151 tests in 24 files (133 `Crossguard.Scenario.*`, 10 `Crossguard.Net.*`, 8 `Crossguard.Visual.*`;
2026-10-16; `Net.SeamlessTravelHubToRaid`, `Scenario.UI.StashAndVendor`, `Visual.HubUI`,
`Scenario.UI.PanelsBind`, `Net.ReconnectShowsDisconnected` and `Scenario.Hub.MainMenuBinds` /
`HubMarker` / `RaidPortal` not yet built). Grouped by file; the name is the full test name.

**`CrossguardScenarioRaidTests.cpp`**

| Test | Checks |
| --- | --- |
| `Crossguard.Scenario.Raid.KillDummyLeavesBody` | A player with `sword_one_handed` hits a dummy 1.5 m away: the first hit takes exactly the damage formula's amount (weapon 30 at Common, the player's ADB / AAD, the dummy's AR); it keeps attacking (at the weapon's interval) until the dummy dies: 0 Health, `State.Dead`, a loot container (its body) where it fell, and the player's raid XP +5 (`GetRaidXp`). |

**`CrossguardScenarioRaidWorldTests.cpp`**

| Test | Checks |
| --- | --- |
| `Crossguard.Scenario.Raid.ThrowingKnife` | Key 3 readies 3 knives (sword put away, counts as dagger); each left click throws one for the item's throw damage; after the last the hands are empty and left click does nothing until key 1 draws the sword. |
| `Crossguard.Scenario.Raid.ExplosiveBottle` | The bottle's blast hits the target and a bystander in the radius once, not a dummy outside it; the damage area pulses 5 times on the target and on the thrower standing in it, then is gone. |
| `Crossguard.Scenario.Raid.WeaponSetSwitch` | Dagger equipped into set 2 with `RequestEquipItem`; switching 1 → 2 → 1 takes the draw time, nothing attacks while drawing, then each set's weapon hits. |
| `Crossguard.Scenario.Raid.BowRunsOutOfArrows` | 45 arrows land as 20 + 20 + 5; 45 shots use one each; then the drawn bow does not fire. |
| `Crossguard.Scenario.Raid.PotionAndBandage` | Potion used at once, heals about half by 5 s and all by 10.5 s; a bandage held for the channel heals in full; one released early heals only up to the release. |
| `Crossguard.Scenario.Raid.LootBody` | A dummy with a sword and 2 bandages dies; its body holds them, its hands are empty; F (loot-takes-all) moves everything to the player. |
| `Crossguard.Scenario.Raid.DeathAndRevive` | A player dies with `DebugReviveDelay` 5: capsule stops colliding, gear on a body, still dead 0.5 s before; revives at half Health with sword, helm (and its resistance) and potions back, body gone, "revived" logged, can draw and attack. |
| `Crossguard.Scenario.Raid.LootedBeforeRevive` | A looter takes the sword and ruby off the victim's body; after the revive the victim has only the potions, the looter keeps the rest. |
| `Crossguard.Scenario.Raid.ExtractionZone` | 5 s in the zone (still in the raid at 4.5 s): extracted, raid XP + reward banked, 250 gold banked, log lines; the character leaves the raid and the player has no pawn. |
| `Crossguard.Scenario.Raid.ExtractPortal` | `PortalUnlockSeconds` 0, two players in a point of interest press X: both portals channel; the one whose owner walks out of keep-alive range collapses; the other opens after the channel time and extracts its owner, whose character leaves the world. |
| `Crossguard.Scenario.Raid.PortalRefusals` | X refused too early (when `PortalUnlockSeconds` > 0), outside a point of interest, and a second time; reasons in the log; one portal at most. |
| `Crossguard.Scenario.Raid.ChestCommonLoot` | Three `chest_common` chests each hold one valid roll; F empties the first non-empty one into the player. Skipped without `DT_Loot`. |
| `Crossguard.Scenario.Raid.BossLootOnDummy` | A dummy with Loot Table Id `boss_monster` dies: body holds what it carried plus one `boss_monster` roll (at least the row's minimum); F takes all. Skipped without `DT_Loot`. |
| `Crossguard.Scenario.Raid.DoorOpenClose` | F snaps a closed door open, F again closes it. |
| `Crossguard.Scenario.Raid.LockedDoor` | Locked: F does nothing without a lockpick ("no lockpick" logged); with `Ability.Lockpicking` it opens after the lockpicking time; a lockpick item works too and is used up only when the lock opens. |
| `Crossguard.Scenario.Raid.DropAndPickUp` | G drops the readied potion stack as an `ACrossguardWorldItem` about 1.5 m ahead on the floor; F picks all 3 back up and the box goes. |

**`CrossguardScenarioGatheringTests.cpp`**

| Test | Checks |
| --- | --- |
| `Crossguard.Scenario.Raid.GatherNode` | A native `ACrossguardGatherNode` (`gather_herb`, default 2 s, once per raid) 1.5 m ahead: F starts the channel; standing still, after 2 s at least one herb is in the inventory, raid XP +10 (`GatherNode`), the node is depleted and "gathered" logged; F again is refused ("depleted"), and no more herbs or XP follow. Skipped without `gather_herb` in `DT_Loot` or `herb_silverleaf` in `DT_Items`. |
| `Crossguard.Scenario.Raid.GatherNodeMoveInterrupts` | F at a `gather_ore` node, then the player is moved 1 m aside 0.5 s in: "interrupted: moved", nobody gathering; 2.5 s later no ore, no raid XP, the node not depleted. Skipped without the data like `GatherNode`. |
| `Crossguard.Scenario.Raid.GatherNodeAttackInterrupts` | As MoveInterrupts, but the player (with a sword) swings 0.5 s in: "interrupted: attacked"; no ore, no XP, the node stays. |
| `Crossguard.Scenario.Raid.GatherNodeDamageInterrupts` | As MoveInterrupts, but the player takes 5 damage 0.5 s in: "interrupted: took damage"; no ore, no XP, the node stays. |

**`CrossguardScenarioSkillTests.cpp`**

| Test | Checks |
| --- | --- |
| `Crossguard.Scenario.Skills.Fireball` | A player with `wand_one_handed` and `fireball` casts at a dummy 6 m away: Fireball goes on cooldown (`Cooldown.Skill.Fireball`); 1 s later the dummy has lost exactly the formula's damage for 50 Magic Damage at coefficient 0.5 (at least 50 before Magic Resistance); the caster, outside the 2 m splash, is unhurt. |
| `Crossguard.Scenario.Skills.FireballSelfSplash` | A dummy 1.5 m away: the caster takes the 15 Magic splash (coefficient 1, own Magic Resistance), the dummy the 50 hit and no splash. |

**`CrossguardScenarioSupportSkillTests.cpp`**

| Test | Checks |
| --- | --- |
| `Crossguard.Scenario.Skills.Heal` | Heals the dummy under the crosshair (formula, "healed" logged), cooldown; at nothing it heals the caster. |
| `Crossguard.Scenario.Skills.Mute` | At nothing: fails, no cooldown. On the target: `State.Muted`, skills fail, basic attack lands; 5 per second; lasts 10 s (9–10 ticks); skills work after. |
| `Crossguard.Scenario.Skills.EldritchShield` | Cast costs 10 HP, `AttackShield` 45; a sword hit within 45 is absorbed entirely; the rest lasts 12 s. |
| `Crossguard.Scenario.Skills.ChopDown` | Readied axe hit deals 110%, `State.Slowed` with Movement Speed −50% for 2 s; on a flier it ends Flight. |
| `Crossguard.Scenario.Skills.ShootingStar` | Blinds the watcher (2 s), dash about 10 m through them, 50 Attack Damage to them, caster not blinded. |
| `Crossguard.Scenario.Skills.Dragonification` | Max HP +50% (Health keeps its %), AR / MR +0.25, Flare Breath and Flight granted, no weapons / basic attack / other skills / 1–2 drawing; Flight works; all back after 45 s. |
| `Crossguard.Scenario.Skills.Flight` | `State.Flying`, flying movement; upward input rises; hovers without input; ends after 20 s and the flier lands. |
| `Crossguard.Scenario.Skills.FlightDescend` | After rising 1.5 s, 1 s of downward input (what Left Ctrl does) brings the flier down more than 50 cm, still flying and in the air. |
| `Crossguard.Scenario.Skills.Wall` | One wall 3 m ahead, on the floor, across the view; gone after 15 s. |
| `Crossguard.Scenario.Skills.Light` | One orb attached 1.2 m over the caster for 120 s; a recast replaces it. |
| `Crossguard.Scenario.Skills.Stealth` | `State.Stealthed`; the hider's capsule is hidden from the PIE player; attacking ends it with +10% Movement Speed and +25% ADB for 3 s; otherwise it lasts 12 s. |
| `Crossguard.Scenario.Skills.Immutable` | A Chop Down hit still deals 110% but does not slow. |
| `Crossguard.Scenario.Skills.Unleash` | STR / AGI / INT double, Max HP follows; back after 15 s. |
| `Crossguard.Scenario.Skills.Benediction` | Aimed at a dead player's body: alive at full HP with the sword back in hand ("revive" logged); at nothing: caster to full, the other player not healed. |
| `Crossguard.Scenario.Skills.Multiscale` | Equipped: the knight and a same-Party member get AR / MR +0.15 and `State.Multiscale`, an outsider nothing; unequipped: gone from all. |
| `Crossguard.Scenario.Skills.HuntersTracking` | Passive on; footprints appear behind a walking enemy, are still there at 25 s, fade about 30 s after the last step. |
| `Crossguard.Scenario.Skills.SummonSkeletons` | 5 Warriors (40 HP) and 1 Mage (30 HP), 60 s life; they damage the dummy; a recast replaces them; gone after 60 s. |
| `Crossguard.Scenario.Skills.SummonSkeletonsFollow` | No enemy around: the necromancer walks 20 m away and all six skeletons come to within 4.5 m (follow distance 3 m). |
| `Crossguard.Scenario.Skills.SummonTiger` | Refused without `pet_gem_tiger` (no cooldown); with it: one tiger, 100 HP, 20 AD, 1.3 attacks a second, 2 m reach, Hunter's Leap / Neckbite / Slash, gem kept; it hits the dummy (log); gone after 180 s. |
| `Crossguard.Scenario.Skills.SummonTigerLeapAndRiders` | A dummy 8 m off (kept alive): the tiger uses Hunter's Leap, Neckbite (the dummy is Silenced) and Slash (cooldowns); its hits show the claw 20, the +10 Magic (Neckbite) and the +7 (Slash) after the dummy's Armor. |
| `Crossguard.Scenario.Skills.SummonGreaterDemon` | One demon, 220 HP, 3x the warlock's size standing on its floor, two-handed sword, 40 AD, 6 m reach (3x the tiger's 2 m), Mortal Strike / Eldritch Shield / Shade Blow, gone after 60 s. |
| `Crossguard.Scenario.Skills.SummonGreaterDemonFights` | Against a dummy 4 m off (kept alive): the demon raises Eldritch Shield, uses Mortal Strike and Shade Blow (cooldowns), a hit does at least the sword's 40 after Armor, and Shade Blow's heal block lands on the dummy. |
| `Crossguard.Scenario.Skills.NeckbiteSilence` | The tiger's Neckbite silences a flying victim: Flight stops, Heal fails, basic attack works; about 2 s. |
| `Crossguard.Scenario.Skills.Silence` | `State.Silenced` 2 s: Heal refused (no cooldown), basic attack works; Heal works after. |

**`CrossguardScenarioWeaponSkillTests.cpp`**

| Test | Checks |
| --- | --- |
| `Crossguard.Scenario.Skills.NextAttackStack` | Slash, Mortal Strike, Shade Blow readied (tags on, cooldowns); one sword hit carries all three (+7, +20 Attack, +20 Magic), uses them up, heal-blocks the dummy; the next hit is plain. |
| `Crossguard.Scenario.Skills.MortalStrikeKillRefresh` | Killing with Mortal Strike clears its cooldown; it can be readied again at once. |
| `Crossguard.Scenario.Skills.Gouge` | Dagger hit: −5% move speed and 5 per second for 5 s (25 in all), then nothing. |
| `Crossguard.Scenario.Skills.ShadeBlowHealBlock` | After a Shade Blow hit, a Heal on the dummy does nothing (no "healed" line); 5.5 s later it heals 20. |
| `Crossguard.Scenario.Skills.PurgeShot` | Bow +10 Magic, target Muted 3 s (Slash refused, basic attack works), Slash after. |
| `Crossguard.Scenario.Skills.Cleave` | 85% of the sword to the dummy ahead and one 45° off, nothing behind; compared with a plain hit. |
| `Crossguard.Scenario.Skills.PartingTheLand` | 65 to the dummy, `State.KnockedUp` (30+ cm up), two walls for 15 s. |
| `Crossguard.Scenario.Skills.HuntersLeap` | Lands straight ahead about 0.5 m per AGI point, on the floor. |
| `Crossguard.Scenario.Skills.LikeALion` | No enemy in 30 m: fails, no cooldown. With a dummy 10 m ahead: leaps to 1.5 m short, STR x1.2 for 6 s. Not a checklist row. |
| `Crossguard.Scenario.Skills.SpearAttackRate` | Common spear 33 per thrust, second thrust exactly one interval (1.2 s at 0 Action Speed) later. |
| `Crossguard.Scenario.Skills.TripleThrust` | Three thrusts of spear +15. |
| `Crossguard.Scenario.Skills.Quickshot` | +0.25 Action Speed, +0.05 Move Speed, shorter bow interval; back after 8 s. Not a checklist row. |
| `Crossguard.Scenario.Skills.ArcaneArrows` | Bow +5 Magic while `State.ArcaneArrows` (not used up by a shot); plain after 8 s. Not a checklist row. |
| `Crossguard.Scenario.Skills.HeroicMight` | Strength +10; Muted: off; back after the Mute. Skipped without `DA_SkillCatalog`. |
| `Crossguard.Scenario.Skills.SwordMastery` | +5 AAD, +10% Action Speed, melee CDR only while a sword is held, not with a dagger. Skipped without `DA_SkillCatalog`. |
| `Crossguard.Scenario.Skills.RiposteRider` | A readied riposte (`State.NextAttack.Riposte`) gives the next hit +50% ADB, once. |

**`CrossguardScenarioPassiveSkillTests.cpp`** (per-skill status: [skill-coverage.md](skill-coverage.md))

| Test | Checks |
| --- | --- |
| `Crossguard.Scenario.Skills.ArmorMasteryNeedsPlate` | Without plate the knight's AR and Movement Speed equal a squire's; both in a `plate_cuirass`: the knight's AR is the squire's x1.10 and its speed +0.05. Skipped without `DA_SkillCatalog` or `DT_Armor`. |
| `Crossguard.Scenario.Skills.MithrilKnightNeedsFullPlate` | Level 15 (Capacity 20). Knight and squire (no party) in the same plate. Four pieces: no +5 STR, and the included skills off too: MR equal to the squire's, no `State.Multiscale`, no `Passive.Immutable`, Immutable cannot activate. The fifth (head, chest, hands, legs, feet): +5 STR, MR +0.30 over the squire (Antimagic Garb + Multiscale), both tags on, Immutable activates; helm off: all gone again. |
| `Crossguard.Scenario.Skills.SpearOfLiscorNeedsSpear` | Sword in hand: no +7 STR / +3 AGI and no +0.15 MR (included Antimagic Garb); `spear_two_handed`: all three; sword again: gone. |
| `Crossguard.Scenario.Skills.CorLeonisEmptySlots` | ADB and MDB +0.05 per empty armor slot (count read from the equipment); a `plate_helm` takes one slot's worth off. |
| `Crossguard.Scenario.Skills.SplinterOfWinter` | Dagger hit on a dummy: +5 Magic Damage on top of the dagger (formula), `State.Slowed` and Movement Speed −0.1; both gone after 3 s. |
| `Crossguard.Scenario.Skills.ThrownKnifeSplinterOfWinter` | A `throwing_knife` thrown (key 3, left click) at a dummy 4 m away with Splinter of Winter: knife damage +5 Magic Damage (formula), `State.Slowed`, Movement Speed −0.1. Needs `DT_Items`. |
| `Crossguard.Scenario.Skills.ThrownKnifeGouge` | Gouge activated with knives in hand (they count as a dagger), then a throw: `State.NextAttack.Gouge` used up at once; the hit is plain knife damage, Movement Speed −0.05, one 5 AD tick 1.5 s later. Needs `DT_Items`. |
| `Crossguard.Scenario.Skills.ThrownKnifeSlash` | Slash activated with knives in hand and a riposte readied (`State.NextAttack.Riposte`, as a parry gives), then a throw: Slash used up at once, the riposte still on; the hit is knife damage +7 (formula, no riposte bonus); the riposte is still on after the hit. Needs `DT_Items`. |
| `Crossguard.Scenario.Skills.ThrownAxeChopDown` | Chop Down activated with `throwing_axe` in hand (counts as an axe), then a throw: `State.ChopDownReady` used up at once; the hit is 110% of the axe's damage (formula), Movement Speed −0.5. Needs `DT_Items`. |

**`CrossguardScenarioCombatTests.cpp`**

| Test | Checks |
| --- | --- |
| `Crossguard.Scenario.Combat.ShieldBlock` | A (`sword_one_handed` + `shield_one_handed`) raises the guard facing B (`dagger_one_handed`); B stabs: A loses nothing, the log says "blocked" and "0% through", B has `State.Stunned` (still at 0.5 s, gone at 1.5 s), A still guards. A turns its back; B stabs again: A takes the full dagger hit (formula, A's AR includes the shield's 0.08), no new "blocked" line. Releasing the guard removes `State.Guarding`. |

**`CrossguardScenarioGuardTests.cpp`**

| Test | Checks |
| --- | --- |
| `Crossguard.Scenario.Combat.ParryTwoHanded` | Two-handed sword parries a dagger: defender unhurt, attacker takes the defender's full weapon damage and a 1 s stun, "parried … 0% through"; riposte ready (+50% ADB on the next hit, +Action Speed for 3 s). |
| `Crossguard.Scenario.Combat.ImpactDifferences` | Six parry duels, one per Impact Power line (attacker by 1 / 2, defender by 1 / 2 / 3, tie): damage each way, log share, stuns and their length, guard kept, riposte only when the defender did not lose. |
| `Crossguard.Scenario.Combat.ArrowBlockedNoStun` | A Ranged monster's arrow on a raised shield: blocked, 0% through, nobody hurt, archer never stunned. |
| `Crossguard.Scenario.Combat.SwingClash` | Swings within 0.25 s clash (tie stops both, lighter swinger takes the heavy weapon's damage and a stun, no riposte); the clank pushes the pair apart; an answer 0.4 s late lands in full. |
| `Crossguard.Scenario.Combat.AIGuardsAgainstSwings` | 20 dagger swings at a harmless built-in monster: it raises its guard, some swings are parried and some land. |
| `Crossguard.Scenario.Combat.SkeletonSoldierParryMirror` | Dagger swings at a `skeleton_soldier` (sword, Impact Power 5) until one is parried: the soldier's sword damage comes back on the attacker (logged right after the parry) and the attacker is stunned about 1 s. Skipped without the Mob Table row. |
| `Crossguard.Scenario.Combat.AIIgnoresAllySwings` | A party member's minion never guards against its ally's 12 swings; every one lands (friendly fire). |
| `Crossguard.Scenario.Combat.StunBlocksActions` | Stunned (by a parry, or directly): no attack, skill, jump, movement or guard, a raised guard drops; all work again after. |

**`CrossguardScenarioStatusTests.cpp`**

| Test | Checks |
| --- | --- |
| `Crossguard.Scenario.Status.Root` | Rooted 1 s: walk speed 0, no jump, Hunter's Leap refused, basic attack still works; afterwards walk speed and jumping are back. |

**`CrossguardScenarioStealthRelevancyTests.cpp`**

| Test | Checks |
| --- | --- |
| `Crossguard.Scenario.Status.StealthNetRelevancy` | Calls `IsNetRelevantFor` with the viewers' PlayerControllers: a stealthed player is not relevant to an enemy's, still to an ally's (same party) and its own; relevant to everyone when not stealthed, with `crossguard.StealthHidesFromEnemies` 0, and again once `State.Stealthed` is removed. Skips if the console holds the switch. The real despawn on a client is the two-client check. |

**`CrossguardScenarioMonsterTests.cpp`**

| Test | Checks |
| --- | --- |
| `Crossguard.Scenario.Monsters.MobSpawner` | A `CrossguardMobSpawner` with `skeleton_soldier` (weight 1), Count 3–3, Radius 6 m, placed 40 m out during a raid: 3 monsters owned by it, each with Mob Id `skeleton_soldier`, Max Health and Health = the mob's Health in `DT_Mobs` (120), a controller, within the radius, above the floor. |
| `Crossguard.Scenario.Monsters.SpawnerMixAndCount` | Seeded (`FMath::RandInit`): 24 spawners of `skeleton_soldier` (weight 3) and `skeleton_archer` (1), Count 3–5: each spawned 3 to 5 with all three counts seen, every monster one of the two, archers 12–40% of the ~96 (expected 25%). Skipped without both rows. |

**`CrossguardScenarioMonsterAITests.cpp`** (no NavMesh: monsters steer straight at their goal)

| Test | Checks |
| --- | --- |
| `Crossguard.Scenario.Monsters.AggroSightAndAttack` | A built-in monster ignores a player beyond aggro range and one behind a wall; once it sees them it closes in and hits for its Attack Damage (formula) once per Attack Interval, from within Attack Range. |
| `Crossguard.Scenario.Monsters.LeashReturnsHome` | The real 30 m leash: the player retreats ahead of the chase until the monster is past 30 m from home; it walks back, healed to full, and ignores the player standing beyond the leash. |
| `Crossguard.Scenario.Monsters.LeashRetargets` | Pulled beyond the leash while another player is in range within it: turns on that player, not healed. |
| `Crossguard.Scenario.Monsters.ReactsToDamageFromAnyRange` | A player 40 m away (beyond aggro and the leash) is ignored until they hurt it; then it turns and comes at least 5 m; the player meets it 15 m from its home and it hits. |
| `Crossguard.Scenario.Monsters.MonsterTeam` | Two monsters and one's minion never fight each other; all three go for a player; nothing hurt among them. |
| `Crossguard.Scenario.Monsters.MinionAvoidsSummoner` | A minion's summoner (a monster with no aggro range) stands exactly between it and a player, no NavMesh: the minion steers around it (RVO crowd avoidance, same side) and hits the player within 10 s; the summoner stays within 0.5 m. Logs the time to the first hit. |
| `Crossguard.Scenario.Monsters.DeathBodyLootXp` | Six monsters of one mob killed: each dead, not revivable, leaves a body; at least one body holds loot; Basic Monster XP per kill. |
| `Crossguard.Scenario.Monsters.BossKillXpAndLoot` | A boss (`mithril_dragon` if present) killed: Boss Monster XP, a body, the dragon's body holds loot. |
| `Crossguard.Scenario.Monsters.MobDataApplied` | Each mob in the Mob Table (up to 6, all 5 today): role, weapon, Impact Power, damage, intervals, ranges, loot table, kill XP, Health, walk speed, scale, feet on the floor, skills with bundles expanded. |
| `Crossguard.Scenario.Monsters.MithrilDragonSummons` | A `mithril_dragon` with a player 10 m off casts one of its summons within 20 s: minions with the dragon as summoner, Summon Skeletons or Summon Greater Demon on cooldown. Skipped without the Mob Table row or the Mob Skill Catalog. |
| `Crossguard.Scenario.Monsters.RangedKeepsDistanceAndShoots` | `skeleton_archer` walks in to its Keep Distance (never closer) and hits the player with arrows more than once. |
| `Crossguard.Scenario.Monsters.MageKeepsDistanceAndCasts` | A Mage mob keeps its distance and a fireball hits the player. |
| `Crossguard.Scenario.Monsters.SkeletonMageUsesSkills` | `skeleton_mage` casts Fireball at the player; no Heal while healthy; below 60% it heals itself ("healed" logged). |
| `Crossguard.Scenario.Monsters.SpawnerRespawn` | Respawn Seconds 4: a replacement of the same mob at full Health no sooner than 4 s, within 4 s + the 2 s check; the first body stays. |

The monster data tests (all but the first five above) and `MobSpawner` skip with a warning while the
game mode's Mob Table is unset.

**`CrossguardScenarioSocialTests.cpp`** (trading, guilds, hub; through the components' server requests, the path the console commands and UI use)

| Test | Checks |
| --- | --- |
| `Crossguard.Scenario.Social.TradeSwap` | Ask, accept, offer, both lock, both accept: potions and Rare ruby swap; "Completed" logged; no accept before both lock. |
| `Crossguard.Scenario.Social.TradeChangingAnOfferUnlocksBoth` | A locked offer cannot change; unlocking takes back acceptances; changing an offer unlocks both; the new offer swaps. |
| `Crossguard.Scenario.Social.TradeOfferedStackUsedMidTrade` | Using an offered potion unlocks both; the offer follows the stack; 2 potions swap. |
| `Crossguard.Scenario.Social.TradeEndsTooFar` | Open at 4 m, ends TooFar at 6 m, nothing moved. |
| `Crossguard.Scenario.Social.TradeBankedGoldBetweenRaidsOnly` | Banked gold refused in a raid; between raids more than banked is refused, 200 of 500 swaps. |
| `Crossguard.Scenario.Social.TradeNoRoomChangesNothing` | Receiver's inventory full: ends NoRoom, nothing changes. |
| `Crossguard.Scenario.Social.TradeEndsWhenPartnerDies` | Partner dies: ends PartnerGone, items kept; a dead player cannot be asked. |
| `Crossguard.Scenario.Social.GuildCreateInviteAccept` | Create "Night Watch"; name taken (any case) and bad names refused; invite, accept as Member, both rosters; a Member cannot invite. |
| `Crossguard.Scenario.Social.GuildBankRankPermissions` | Member deposits gold and potions; Member withdraw refused NotPermitted; made Officer, withdraws. |
| `Crossguard.Scenario.Social.GuildBankBetweenRaidsAndDisband` | Membership works in raids, the bank does not; leader cannot leave while others are in; last member leaving disbands and takes the bank. |
| `Crossguard.Scenario.Social.GuildGoldGuards` | Withdrawals, gifts and a last-member take that would overflow int32 gold are refused, nothing changes. |
| `Crossguard.Scenario.Social.ConcurrentLootNeverDuplicates` | Two players open one body and take every stack in the same frame: totals across body and both inventories unchanged. |
| `Crossguard.Scenario.Hub.SkillGemLearnsSkill` | Between raids a `heroic_might` gem joins the Skill Library and is used up; in a raid refused, gem stays. Skipped without `DA_SkillCatalog`. |
| `Crossguard.Scenario.Hub.StashStoreAndRetrieve` | Store anywhere and retrieve 3 potions; refused in a raid. |
| `Crossguard.Scenario.Hub.VendorSellsRareRuby` | Rare ruby sells for its price (150) into banked gold; a potion is refused. |
| `Crossguard.Scenario.Hub.GemstonesStackToThree` | Four Common rubies make stacks of 3 and 1 (max stack 3); a Rare ruby and a sapphire keep their own stacks; the stack of 3 sells whole for 150. |
| `Crossguard.Scenario.Hub.BuyStashTabs` | No gold: refused; with gold, three tabs; the fourth refused AtCap, no gold taken. |
| `Crossguard.Scenario.Hub.LoweringAttributeNeedsRespecToken` | Lowering Strength refused without a token (nothing changes), done with one (token used). |
| `Crossguard.Scenario.Hub.RespecPaidWithGold` | Respec paid with gold (price 1000): refused at 999 gold and in a raid (nothing taken, token not used instead); at 1000 Strength drops and all 1000 go, the token stays; then a token respec takes no gold. |

**`CrossguardScenarioUITests.cpp`** (the placeholder UI, [ui.md](ui.md); nothing is drawn under `-nullrhi`)

| Test | Checks |
| --- | --- |
| `Crossguard.Scenario.UI.WidgetsBind` | Every widget class built for the PIE player's controller (C++ tree, no assets) shows the game state: the HUD's Health and hotbar lines; one inventory cell per stack, click selects and deselects; a dead dummy's body shows its ruby, Take all moves it; one wheel entry per castable skill and picking one selects it; the trade window says no trade; the guild window shows the founder after `RequestCreate`. The player's HUD is `ACrossguardHUD` (a warning if a game mode Blueprint overrides it). |

**`CrossguardScenarioHubUITests.cpp`** (the stash and vendor screens, [ui.md](ui.md); hub, no raid)

| Test | Checks |
| --- | --- |
| `Crossguard.Scenario.UI.StashAndVendor` | The PIE player in the hub with potions and three Common rubies; a native stash chest and vendor NPC. F (their `Interact`) opens each screen ("Stash opened" / "Vendor opened"; with a game viewport the HUD shows the vendor screen in place of the stash). Stash screen: one tab button per tab, a 10 × 30 grid, one cell per stack in the tab and in the inventory; dropping the potions on a free stash square stores them there, dropping them on the inventory takes them back; "Buy tab" greyed at 2999 gold and naming 3000, active at 3000, buying adds a tab (and its button) and costs 3000, the new tab shows empty. Vendor screen: one sell row per stack, the rubies listed at 150 gold; dropping them on the sell box pays 150 and removes them; "Sell all gemstones" is worth 100 for a sapphire and an emerald and sells both, not the potions. In a raid both refuse (logged) and `OpenStash` opens nothing. |

**`CrossguardScenarioUIPanelTests.cpp`** (the attributes and party windows and the team list, [ui.md](ui.md); hub, no raid)

| Test | Checks |
| --- | --- |
| `Crossguard.Scenario.UI.PanelsBind` | Attributes window for the PIE player at level 2: the points line shows unspent points; +1 Strength is not a respec and Apply raises it (the plan follows the new allocation); -1 is a respec: Apply greyed, token and gold respecs greyed with none; with a token the token respec lowers it and spends the token; with 1000 gold the gold respec lowers Agility and takes the 1000. Party window: no rows at first; an invite from PanelHost shows one inbox row with about 180 s left, declining removes it; Invite by name finds "panelfriend" (any case) and founds the party, an unknown name is refused; after PanelFriend accepts: two member rows, "(you) [leader]", PanelFriend listed, and the team list shows PanelFriend; with PanelFriend marked disconnected both show "PanelFriend (disconnected)", and the marker goes when cleared. |

**`CrossguardScenarioPartyTests.cpp`** (parties in the hub, [parties.md](parties.md); the travel itself is never started here: `Net.SeamlessTravelHubToRaid` does it)

| Test | Checks |
| --- | --- |
| `Crossguard.Net.PartyInviteAcceptMakesAllies` | Listen server + 2 clients, hub. The host invites client 1 (founding a party it leads), client 1 sees the invite and accepts on its own client (RPC): on the server both are allies on the party's team (`PartyKey` = party id), the outsider is not; client 1's own component shows two members and the host leading, and its copy of the host has its team; the outsider's client sees the two on one team but no party data on any component, and client 1's copy of the host's component is empty (owner only). |
| `Crossguard.Net.PartyKickPromoteLeave` | Listen server + 2 clients, all in one party. Promote hands the lead to client 1 (its client sees it); the old leader's kick is refused NotLeader; the new leader kicks client 2 (no longer an ally; its client sees no party, another team); the leader leaving hands the lead back to the host; in a raid invite, leave and the launch are refused OnlyInHub; the last member leaving disbands it. |
| `Crossguard.Scenario.Hub.PartyRaidLaunchPlan` | A leads B, Solo alone (standalone hub). A member cannot launch (NotLeader); no destination: NoRaidDestination; with a raid server: client travel to `127.0.0.1:7777?Party=<id>` (Party option read back with `FURL`), Solo without the option; others on the server: no server travel even with a raid map; everyone in the party and a raid map: server travel to `/Game/L_Test?Party=<id>`; in a raid: OnlyInHub. Never travels. |

**`CrossguardScenarioTravelTests.cpp`** (the hub-to-raid launch played for real, [parties.md](parties.md#raid-launch); unbuilt, see [first-compile-triage.md](first-compile-triage.md) Batch 3)

| Test | Checks |
| --- | --- |
| `Crossguard.Net.SeamlessTravelHubToRaid` | Listen server + 1 client in the hub (`Raid(false)`), saving on into a temporary folder (`Saved\Automation\TravelTests`, deleted after). The host invites, client 1 accepts; each banks quest XP and gold, holds a weapon (Rare sword / Uncommon axe) and carries potions or bandages and a ruby. `RaidMapName` = `/Game/Maps/Dev/L_DevSandbox`; the plan is a ServerTravel with `?Party=<id>`; the leader's `StartRaid` travels **seamlessly** for real (`net.AllowPIESeamlessTravel` set to 1 for the run, restored after, also on failure). On the raid map, server side: exactly two player states, each found by its **engine PlayerId** (copied only by seamless travel), same player id and `DevPlayerId`, party key and party view = the party id, allies on one team, same banked XP, level and gold, same weapon in hand and same inventory (kinds and counts; the debug loadout included), raid begun with 0 raid XP, the game mode a raid (`bIsRaid`); the save entry followed the new player state (state Ready, a record with the same player id marked in-raid, the raid start written); no `is online with that id`, `You are already in this match`, `could not rejoin a team after travel` or `NOT supported in single process PIE` in the log. Each client's world is the raid map with its own pawn and its party view. Skipped (warning) without `L_DevSandbox` (`Tools\editor_setup.bat -CreateMap`), without the cvar, or with `-RaidMap=` on the command line. |

**`CrossguardScenarioSaveTests.cpp`** (character saves, [persistence.md](persistence.md); saving is off in tests, so these turn it on with a temporary folder under `Saved\Automation\SaveTests` and delete it at the end)

| Test | Checks |
| --- | --- |
| `Crossguard.Scenario.Save.RoundTrip` | A new character is saved at once. Between raids: 300 XP, 15/15/12 attributes, 2 respec tokens, `fireball` and `heal` learned (`heal` equipped), a stash tab bought (4321 gold left), a Rare sword with two affixes stored rotated at (3, 4) in tab 2, potions stored, an axe in hand, potions and a ruby carried. Saved, then everything wiped in memory; loading gives back the identical record (compared as the save JSON) and the same XP, gold, tabs, sword (square, rotation, affix 0.03 exactly), axe in hand and carried items. Starting a raid saves again: that save lists no carried gear, keeps the stash and gold. |
| `Crossguard.Scenario.Save.CorruptFileRefused` | A save cut in half is refused ("refused (InvalidJson"), nothing of the character changes, the file is kept aside as `.corrupt-*`, the next save writes the live state and loads. A hand-edited save with gold -50 and an unknown item loads repaired: gold 0 (`GoldOutOfRange`), the item quarantined (`UnknownItem`) and kept in the file. A save whose `playerId` is someone else's is refused (`WrongPlayer`). |
| `Crossguard.Scenario.Save.ExtractedPlayerLeavesRaid` | A ruby carried in, a sapphire picked up in the raid (F), a potion in the stash. Extracting saves both gems as carried out; the character then leaves the raid: F on a second sapphire is refused ("interact refused: extracted"), taking the potion from the stash is refused (no character), the record does not change, the character is gone a tick later, and a later save still lists the gems. |
| `Crossguard.Scenario.Save.RefusedLoginKeepsOnlinePlayersSave` | A saving player; a spawned "intruder" whose development id is set to the saver's goes through `PlayerJoined` and `PlayerLeaving` (what a refused duplicate login looks like to the subsystem). Logged "joined while ... is online with that id" and "never finished logging in left"; the saver's entry survives: 100 XP more is written on the next flush. |
| `Crossguard.Scenario.Save.Smoke1StashAndStop` | The by-hand save smoke, scripted (part 1). The PIE player's own state (development id `pie-0`) saves to the fixed folder `Saved/Automation/SaveSmoke/`, stashes a Rare ruby, saves: `pie-0.json` is written and holds the ruby. The folder stays for part 2. |
| `Crossguard.Scenario.Save.Smoke2PlayAgain` | Part 2, a new PIE session (new game instance and save subsystem): the stash is empty at first; login's hooks with saving on load `pie-0.json` from disk: "Loaded the save for pie-0" and the ruby is back in the stash. Deletes the folder. Runs after part 1 (name order); alone it fails with "Smoke1StashAndStop runs first". |

**`CrossguardScenarioReconnectTests.cpp`** (reconnecting mid-raid, [persistence.md](persistence.md) "Reconnecting". A disconnect destroys the raider's PlayerController, as the net driver does when a connection closes; a reconnect is `InitNewPlayer` + `HandleStartingNewPlayer` with a `?DevPlayerId=` development id, honoured during automation tests. Skipped if the game mode's Player Controller Class is not `ACrossguardPlayerController`)

| Test | Checks |
| --- | --- |
| `Crossguard.Scenario.Reconnect.RetakesCharacter` | A raider with a potion, raid XP and 60 % Health. A second login with the same id while connected is refused ("You are already in this match."). The connection closes: the character stays, unpossessed, with its player state (in the raid, same raid XP, potion, Health), held under its id. Another id logs in: a player state and pawn of its own, the raider still held. The same id logs in: the held player state is back on the new controller, the same character is possessed (no new pawn), same raid XP, team, potion and Health. |
| `Crossguard.Net.ReconnectShowsDisconnected` | Dedicated server + 1 client (the teammate). A raider on the client's team: its player state reaches the client not disconnected. The raider's connection closes: held, marked disconnected on the server, and the client's copy says `IsDisconnected` (what the party window and team list show). The same id logs in again: the held player state is back, the mark clears on the server and on the client. |
| `Crossguard.Scenario.Reconnect.DiedWhileAway` | A raider disconnects; its waiting character is killed and its revive window ends (`HandlePermanentDeath` called directly): no longer held, the raid is over for that id, the body is not the player's any more and a body container lies there; logging in again with the id is refused ("Your raid is over."). |

**`CrossguardScenarioHubMenuTests.cpp`** (main menu, hub marker, raid portal and party board,
[hub-and-menu.md](hub-and-menu.md); nothing travels. Also holds `Crossguard.Menu.Urls`, above)

| Test | Checks |
| --- | --- |
| `Crossguard.Scenario.Hub.MainMenuBinds` | `UCrossguardMainMenuWidget` builds for the PIE player in C++; main buttons Play, Join, Settings, Quit in that order; Play's URL is `/Game/Maps/L_Hub?listen?Hub` (no menu game mode in the test world: the default); Join's URL is the trimmed field, none when empty, and clicking Join then refuses with a status line; Settings opens and closes, a field of view step changes it, volume clamps at 100 % (not saved). |
| `Crossguard.Scenario.Hub.HubMarker` | On a raid map with no marker `IsHubMap` is false (`?Hub` alone true); a spawned `ACrossguardHubMarker` makes `IsHubWorld` and `IsHubMap` true and, applied as `InitGame` does, `bIsRaid` off; destroyed, false again. Skipped if the test map already holds a marker. `InitGame` itself is not re-run. |
| `Crossguard.Scenario.Hub.RaidPortal` | Hub (`bIsRaid` off): A leads a party with B, Solo alone, a raid server address set; a plan-only portal (`bPlanOnly`): B refused `NotLeader` (no URL), A plans a client travel to `127.0.0.1:7777?Party=<id>`, Solo to `127.0.0.1:7777`; in a raid A is refused `OnlyInHub`. The party board: prompt, and with a game viewport the PIE player's party window opens. |

**`CrossguardScenarioNetworkTests.cpp`** (networked: a server and real PIE clients, see
[Networked scenarios](#networked-scenarios))

| Test | Checks |
| --- | --- |
| `Crossguard.Net.WieldingTagsReplicate` | Listen server + 1 client. The host equips a sword, the client an axe: the client's copy of the host has `Equipment.Wielding.Sword` and its own pawn `Equipment.Wielding.Axe`; the host unequips and the client's copy loses the tag. |
| `Crossguard.Net.DeathRagdollsOnEveryClient` | Listen server + 2 clients. Client 1 kills client 2: on client 1's copy and on client 2's own body `State.Dead`, the mesh simulating physics, the capsule without collision; the host's view too. |
| `Crossguard.Net.StealthHidesFromEnemyClient` | **Dedicated server** + 3 clients (ally, sneak, enemy). The sneak activates Stealth on its own client (predicted): the enemy client loses the actor while the ally client keeps it; the time from `State.Stealthed` to the drop is logged as `STEALTH LEAK: ... s` (was 4.3–4.5 s on 5.8, the net driver's `RelevantTimeout`; the server now closes the enemy channel when stealth starts, see [anti-cheat.md](anti-cheat.md)) and must be under 0.5 s (placeholder); after 12 s the sneak is back on the enemy client, not stealthed, Health and sword replicated, within 1 m of the server's position. Skipped if `crossguard.StealthHidesFromEnemies` is 0. |
| `Crossguard.Net.ConcurrentLootFromTwoClients` | Listen server + 2 clients. A dummy with 3 potions, 2 bandages and a Rare ruby dies between them; both open the body (server) and both clients request every stack by RPC in the same frame: the body empties, totals across the body and both inventories unchanged, between 1 and 3 stacks gained across both inventories (the client pawns spawn with the character Blueprint's debug loadout, which a taken stack may merge into; whoever's packets the server reads first takes them; the late requests are refused UnknownItem), and client 1's replicated inventory matches the server's. |
| `Crossguard.Net.TrackingMarksOnTrackerOnly` | Listen server + 1 client (enemies). Client 1 has `hunters_tracking`; the host moves 1.2 m a step: footprints (instances of the client pawn's `UCrossguardTrackingComponent`) appear on the tracker's client, none on the host's own component, the server's copy of the tracker, or client 1's copy of the host. |

**`CrossguardScenarioVisualTests.cpp`** (visual: screenshots, only in the [visual run](#visual-run);
each asserts just the state behind the picture)

| Test | Frames (`Saved\EditorTests\Screenshots\<Test>\`) |
| --- | --- |
| `Crossguard.Visual.RagdollBody` | `before.png` a dummy standing; `ragdoll.png` its body 1.5 s after a lethal hit (physics on). |
| `Crossguard.Visual.GatherNode` | `before.png` a `gather_herb` node with a player at it; `depleted.png` after the player gathered it (the node should be gone). Skipped without the gather loot table. |
| `Crossguard.Visual.Chest` | `chest.png` a native `chest_common` container (no mesh of its own yet: a Blueprint adds it). |
| `Crossguard.Visual.MonsterAttack` | `approach.png` a built-in melee monster and a player 3 m apart; `attack.png` the frame of its first hit. |
| `Crossguard.Visual.Dragonification` | `before.png` a player; `dragon.png` the same player in `State.Dragonified` (no form art yet, so alike). |
| `Crossguard.Visual.StealthViews` | Listen server + 2 clients: from the enemy client's and the ally host's screen, `*_before.png`, `*_stealth.png` 1 s into Stealth (gone from the enemy's, still on the ally's), `*_after.png` when it ends. |
| `Crossguard.Visual.UI` | The PIE player's own screen with the placeholder UI ([ui.md](ui.md)): `hud.png` (Health, level, gold, weapon sets, hotbar, skills, "F: Loot body"), `inventory_body.png` (the inventory window with a Rare Backpack and a Common Quiver worn, so their grids show under the main one, arrows in the Quiver, and a dead dummy's body open beside it), `trade.png` (after asking a partner 2 m away), `guild.png`, then in the hub `attributes.png` (K: level 2, 3 unspent points) and `party.png` (P: a party with the partner, marked "(disconnected)", also in the team list top right). Asserts each window is open. |
| `Crossguard.Visual.HubUI` | The same, in the hub ([ui.md](ui.md)): `stash.png` (the stash screen, `OpenStash`: potions stored, 3500 banked gold so "Buy tab" is active, the inventory with rubies and a Rare sapphire), `vendor.png` (the vendor screen, `OpenVendor`: the sell list with prices, "Sell all gemstones"). Asserts each screen is open. |

### Checklist coverage

Against [next-build-checklist.md](next-build-checklist.md). **auto**: the tests check the row's
expected result; tick it when they pass. **part**: some of it is checked, do the rest by hand.
**manual**: no test. "Looks" (ragdoll, meshes, arcs, effects) always needs eyes. `Crossguard.Scenario.*`
use server-side players in one standalone PIE; `Crossguard.Net.*` use real PIE clients.

| Section / row | Status | Tests | By hand |
| --- | --- | --- | --- |
| 1 Pull and build | script | `Tools\build_editor.bat` (also `run_editor_tests.bat`) | git pull; errors listed in `Saved\Build\errors.txt`. |
| 2 Data tables imported | auto | `Crossguard.Data.EditorAssets` | — |
| 2 Tables set on Blueprints / game mode | script | `Tools\editor_setup.bat` | Imports every DT_ table and sets the references; read-back checked, report in `Saved\EditorSetup\summary.txt`. |
| 3 Character settings | script | `Tools\editor_setup.bat` | Clears the default mapping context; Debug loadout is yours to set. |
| 4 Knife hits 15; bottle 10 in 3 m + 3/s for 5 s | auto | `Raid.ThrowingKnife`, `Raid.ExplosiveBottle` | The arc. |
| 4 Weapon sets 1 / 2 | auto | `Raid.WeaponSetSwitch` | — |
| 4 Next knife; empty hands after the last | auto | `Raid.ThrowingKnife` | — |
| 4 Bow 45 shots, runs out | auto | `Raid.BowRunsOutOfArrows` | — |
| 4 Potion 15 over 10 s; bandage 10 over 2 s held | auto | `Raid.PotionAndBandage` | — |
| 4 Kill the dummy: body, 5 XP | auto | `Raid.KillDummyLeavesBody` | The ragdoll's look: `Visual.RagdollBody`. |
| 4 Loot a geared dummy's body | auto | `Raid.LootBody` | — |
| 4 Die and revive with gear | auto | `Raid.DeathAndRevive` | The ragdoll's look: `Visual.RagdollBody`. |
| 4 Looted before revive | auto | `Raid.LootedBeforeRevive` | — |
| 4 Extraction zone 5 s, XP and 250 gold banked | auto | `Raid.ExtractionZone` | — |
| 4 Portal: 60 s, extract; walk away collapses | auto | `Raid.ExtractPortal` | `BeamMesh` look. |
| 4 X refused outside / twice | auto | `Raid.PortalRefusals` | — |
| 4 `chest_common` chest | auto | `Raid.ChestCommonLoot` | Its look: `Visual.Chest` (no mesh yet). |
| 4 Monster: aggro 15 m, 12 per 1.5 s, 30 m leash, shot from 40 m, kill XP and body | auto | `Monsters.AggroSightAndAttack`, `LeashReturnsHome`, `LeashRetargets`, `ReactsToDamageFromAnyRange`, `DeathBodyLootXp` | NavMesh pathing round obstacles (the floor is open). |
| 4 `boss_monster` dummy: 3–5 extra items | auto | `Raid.BossLootOnDummy` | — |
| 4 Door snaps open / closed | auto | `Raid.DoorOpenClose` | The mesh moving. |
| 4 Locked door, lockpicking 5 s | auto | `Raid.LockedDoor` | — |
| 4 `fireball` 50+; 15 to you within 2 m | auto | `Skills.Fireball`, `Skills.FireballSelfSplash` | — |
| 4 `heal` dummy, then self | auto | `Skills.Heal` | — |
| 4 `mute` | auto | `Skills.Mute` | — |
| 4 `eldritch_shield` | auto | `Skills.EldritchShield` | — |
| 4 `chop_down`, flier falls | auto | `Skills.ChopDown` | — |
| 4 `shooting_star` | auto | `Skills.ShootingStar` | — |
| 4 `dragonification` | auto | `Skills.Dragonification` | Q selector UI; the form's look: `Visual.Dragonification` (no art yet). |
| 4 `flight` Space / Left Ctrl | auto | `Skills.Flight`, `Skills.FlightDescend` | The key bindings themselves. |
| 4 Next-attack stack; Mortal Strike kill refresh | auto | `Skills.NextAttackStack`, `Skills.MortalStrikeKillRefresh` | States display. |
| 4 `gouge` | auto | `Skills.Gouge` | — |
| 4 `shade_blow` heal block | auto | `Skills.ShadeBlowHealBlock` | — |
| 4 `purge_shot` | auto | `Skills.PurgeShot` | — |
| 4 `cleave` 85% | auto | `Skills.Cleave` | — |
| 4 `parting_the_land` | auto | `Skills.PartingTheLand` | The pop-up and walls' look. |
| 4 `hunters_leap` | auto | `Skills.HuntersLeap` | — |
| 4 `wall`, `light` | auto | `Skills.Wall`, `Skills.Light` | The light itself. |
| 4 `stealth` vanish on the other screen | auto | `Skills.Stealth`, `Status.StealthNetRelevancy`, `Net.StealthHidesFromEnemyClient`, `Visual.StealthViews` | The drop took ~4.4 s (`RelevantTimeout`); owner's fix A closes the enemy channel at stealth start, test limit 0.5 s (pending local run, [anti-cheat.md](anti-cheat.md)). |
| 4 `immutable` | auto | `Skills.Immutable` | — |
| 4 `unleash` | auto | `Skills.Unleash` | — |
| 4 `benediction` on a body | auto | `Skills.Benediction` | — |
| 4 `multiscale` with a party member | auto | `Skills.Multiscale` | — |
| 4 `hunters_tracking` grey marks | auto | `Skills.HuntersTracking`, `Net.TrackingMarksOnTrackerOnly` | Their look. |
| 4 Drop with G, pick up with F | auto | `Raid.DropAndPickUp` | The box's look. |
| 4 Gather node: 2 s, herbs, +10 XP, depleted, refused again | auto | `Raid.GatherNode`, `Visual.GatherNode` | — |
| 4 Gather node: walking, attacking or damage breaks the channel | auto | `Raid.GatherNodeMoveInterrupts`, `GatherNodeAttackInterrupts`, `GatherNodeDamageInterrupts` | — |
| 4 `summon_skeletons` | auto | `Skills.SummonSkeletons`, `Skills.SummonSkeletonsFollow` | — |
| 4 `summon_tiger` leap, riders, hits for 20 | auto | `Skills.SummonTiger`, `Skills.SummonTigerLeapAndRiders` | — |
| 4 `summon_greater_demon` | auto | `Skills.SummonGreaterDemon`, `Skills.SummonGreaterDemonFights` | — |
| 4 Neckbite silences 2 s, stops a channel | auto | `Skills.NeckbiteSilence`, `Skills.Silence` | — |
| 4 Mob spawner: soldiers and archers, 3–5, archers keep away, body, respawn | auto | `Monsters.MobSpawner`, `SpawnerRespawn`, `RangedKeepsDistanceAndShoots`, `Monsters.SpawnerMixAndCount` | — |
| 4 `mithril_dragon` spawner: scale 6 on the floor, summons fight you | auto | `Monsters.MobDataApplied`, `MonsterTeam`, `Monsters.MithrilDragonSummons` | — |
| 4 Block | auto | `Combat.ShieldBlock` | Two real clients. |
| 4 Parry | auto | `Combat.ParryTwoHanded`, `ImpactDifferences` (`axe_two_handed` vs `sword_one_handed`), `Skills.RiposteRider` | Two real clients. |
| 4 Monsters guard ~half, stun and sword damage back | auto | `Combat.AIGuardsAgainstSwings`, `Combat.SkeletonSoldierParryMirror` | The ~50% rate is checked loosely (some parried, some landed). |
| 4 Trading | auto | `Social.Trade*` (7) | Console commands, two real clients. |
| 4 Guilds | auto | `Social.Guild*` (4) | Console commands, two real clients. |
| 4 `summon_tiger` needs `pet_gem_tiger` | auto | `Skills.SummonTiger` | — |
| 4 Spear 33 per 1.2 s; `triple_thrust` | auto | `Skills.SpearAttackRate`, `Skills.TripleThrust` | — |
| 4 Passives: `heroic_might`, masteries | auto | `Skills.HeroicMight`, `Skills.SwordMastery` | — |
| 5 Skill Gem | auto | `Hub.SkillGemLearnsSkill` | — |
| 5 Stash | auto | `Hub.StashStoreAndRetrieve` | — |
| 5 Vendor ruby +150 | auto | `Hub.VendorSellsRareRuby` | — |
| 5 Stash tabs | auto | `Hub.BuyStashTabs` | — |
| 5 Respec token | auto | `Hub.LoweringAttributeNeedsRespecToken` | — |
| 6 Both see bodies ragdoll | auto | `Net.DeathRagdollsOnEveryClient`, `Visual.RagdollBody` | — |
| 6 `showdebug` shows the other's `Equipment.Wielding.*` | auto | `Net.WieldingTagsReplicate` | The debug HUD itself. |
| 6 Concurrent loot never duplicates | auto | `Social.ConcurrentLootNeverDuplicates`, `Net.ConcurrentLootFromTwoClients` | — |
| 6 Parties and the raid launch | part | `Net.PartyInviteAcceptMakesAllies`, `Net.PartyKickPromoteLeave`, `Hub.PartyRaidLaunchPlan`, `Net.SeamlessTravelHubToRaid` (unbuilt) | Two PCs (a real network, not one process); the ClientTravel path to a raid server. |
| 6 Reconnecting mid-raid | part | `Reconnect.RetakesCharacter`, `Reconnect.DiedWhileAway`, `Net.ReconnectShowsDisconnected` (the teammate's marker) | A real crash and relaunch (two processes): the connection timeout, the client getting its pawn and HUD back. |

Totals: 69 rows: 65 auto, 3 script, 1 part (the party raid launch's travel), none manual (2026-10-16). What the "By hand" column
still leaves to a person is feel, art and real internet play: looks (frames from the visual run),
key bindings, UI, NavMesh pathing round obstacles, and play over a real network. Not checklist
rows: `Combat.ArrowBlockedNoStun`, `SwingClash`, `AIIgnoresAllySwings`, `StunBlocksActions`,
`Skills.LikeALion`, `Quickshot`, `ArcaneArrows`, the extra trade and guild cases, and the
`Crossguard.Net.*` / `Crossguard.Visual.*` scenarios.

### Known limits

- `Crossguard.Scenario.*`: one standalone PIE, server side: players are server-spawned controllers
  with no input, HUD or client. Replication and prediction are the networked tests' job
  (`Crossguard.Net.*`); keys are untested, and the UI only to the point that its widgets build and
  show the right state (`Scenario.UI.WidgetsBind`): nothing is drawn or clicked by a mouse.
- Map travel: only `Net.SeamlessTravelHubToRaid` travels, in one process (single-process PIE with
  `net.AllowPIESeamlessTravel 1`, the engine's own caveat), to a saved map (`L_DevSandbox`). A non-seamless
  travel, the ClientTravel path to a separate raid server, and travel between two machines are untested.
- Networked tests drive clients from C++ (`ActivateSkill` on the client pawn, inventory requests on
  the client component): the real path from a key press to the request is not covered.
- Clanks: there is no native event to bind to, so the clank (`MulticastWeaponClank` →
  `OnWeaponClank` Blueprint event) is counted from the Impact Power log line before it; the
  Blueprint event, sound and effect are not checked.
- AI guard chance (50%) is checked loosely: over 20 swings, at least one parried and one landed.
- No NavMesh in the test map: monsters and minions steer straight at their goal on the open floor, which the distance and follow checks need nothing more for; pathing round obstacles is untested.
- Visuals: the [visual run](#visual-run) only takes pictures; nothing judges them. Audio, arcs and
  effects are never checked.
- Data-dependent tests skip (pass with a warning) when `DT_Mobs`, `DT_Loot`, `DA_SkillCatalog` or
  `DT_Armor` are not set: a skip is not a pass for the checklist row.

### How a scenario runs

1. If a PIE session is running it is ended first.
2. The game mode and character Blueprints are found (Asset Registry) and loaded.
3. A new blank map (`FAutomationEditorCommonUtils::CreateNewMap`) gets: World Settings → Game Mode
   Override = the game mode Blueprint; a 200 m × 200 m floor (engine cube, collision BlockAll, top at
   Z = 0); a Player Start at (−30 m, −30 m) where the PIE player appears, out of the way. Its world
   is flagged `RF_WasLoaded` so the net GUID cache can address the (never saved) level.
4. PIE starts as **one standalone instance** (`RequestPlaySession`, net mode Standalone, 1 client),
   whatever your editor's Play settings are. The PIE world is the server. With `Clients(N)`: a listen
   server (instance 0, the host) and N − 1 clients, or a dedicated server and N clients, all in the
   editor process; the start waits until every client is logged in with a pawn.
5. The game mode's `bIsRaid` is set (on by default, as in checklist section 4) and the optional
   time dilation applied; a log listener starts.
6. The steps run, one per frame, with their waits.
7. PIE ends: always, also after a failure or a timeout (180 real seconds for the steps by default;
   60 s for PIE to start, 30 s to end).

## Writing a scenario

Add a test to the `CrossguardScenario<Area>Tests.cpp` file of its area (`Raid`, `Skills`, `Combat`,
`Monsters`, `Social`; see the file list above) or a new one in `Source/Crossguard/Private/Tests/`, wrapped in
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

### Networked scenarios

`Scenario.Clients(N)` runs PIE as a listen server plus N − 1 clients in the editor process;
`Scenario.Clients(N, true)` as a dedicated server plus N clients. Every client is a real PIE client:
its own world, `ULocalPlayer`, PlayerController and a pawn the game mode spawns at the Player Start
when it logs in over the normal connection (`127.0.0.1:17777`). PIE is ready once every client has
its pawn and the server knows them all. Steps still run against the server world (`C.World()`),
where `Place`, `Equip`, `Kill`, `GrantSkills` and the rest act as before; the clients' worlds are
reached through:

| Call | Does |
| --- | --- |
| `C.ClientCount()`, `C.IsNetworked()` | N; 0 / false in a standalone scenario. |
| `C.ServerWorld()`, `C.ClientWorld(i)` | The server world (= `World()`); client i's world. Listen server: PIE instances 0..N−1, client 0 the host, whose world **is** the server world. Dedicated: server instance 0, clients 1..N. |
| `C.ClientController(i)`, `C.ClientPawn(i)` | Client i's own PlayerController (the one with a `ULocalPlayer`) and character, in its world. |
| `C.ServerPawnOf(i)`, `C.ServerControllerOf(i)` | The same player on the server (matched by the PlayerState's `PlayerId`). Also remembered as `"Client<i>"` and `"Client<i>.Controller"`. |
| `C.OnClient(ServerActor, i)`, `C.OnClient<T>(ServerActor, i)` | Client i's copy of a server actor, by net GUID: null until it has replicated, while it is not relevant to that client (stealth), or once destroyed there. On the listen host it is the server actor itself. |
| `C.MakeAllies(Player, Ally)` | Server: `Player` joins `Ally`'s team (`ACrossguardPlayerState::SetTeamId`). PIE clients log in without `?Party=`, each on a team of its own. |
| `C.HasSkill(Character, SkillId)` | A non-passive skill is granted; on a client pawn, once its spec has replicated. |
| `Scenario.WaitUntilOnClient(i, Name, Timeout, [](FScenarioContext& C, UWorld& ClientWorld) { ... }, Poll = 0)` | `WaitUntil` for a client-side condition (named `[client i] ...` in the log); fails at once if that world is gone. |
| `C.ExpectServerTravel(MapPackage, TimeoutRealSeconds = 90)` | Call in the step that starts a server travel (`UWorld::ServerTravel`, `StartRaid`). The items after it wait until PIE instance 0's world is `MapPackage` (long package name, e.g. `/Game/Maps/Dev/L_DevSandbox`, matched without the `UEDPIE_<n>_` prefix), has begun play and runs an `ACrossguardGameMode`; then `World()` / `GameMode()` are the new ones, remembered names and `LookFrom` cameras are forgotten, game time is the new world's. Fails past the timeout. The destination must be a saved map. Clients travel on their own: wait for them with `WaitUntilOnClient`. Single-process PIE travels seamlessly only with `net.AllowPIESeamlessTravel 1` (`Net.SeamlessTravelHubToRaid` sets it). |

Rules of thumb:

- Replication is asynchronous: never read a client right after a server-side change; poll with
  `WaitUntilOnClient` (in one process it usually takes a frame or two; allow 5 s).
- The listen host sees everything (`IsNetRelevantFor` filters connections only). Relevancy tests
  use a dedicated server, every player a remote client.
- Activate a `LocalPredicted` skill on the **client** pawn (`C.ActivateSkill(C.ClientPawn(i), ...)`),
  as the player would; the server refuses it for a pawn that is not local there. Wait for
  `C.HasSkill` on the client pawn first (the spec replicates).
- Client-side requests go through the client pawn's components (`C.ClientPawn(i)->GetInventoryComponent()->RequestTakeFromContainer(...)`
  sends the server RPC); server-side helpers (`OpenLootContainer`, `Equip`) go through `ServerPawnOf`.
- `GEditor->PlayWorld` is not the server once there are several instances (the editor leaves it on
  the last one it ticked); the framework uses PIE instance 0.
- Each instance gets a 1280 × 720 window (nothing is drawn under `-nullrhi`).

### Visual run

```
Tools\run_editor_tests.bat -Visual -SkipBuild
```

Runs without `-nullrhi`, so the PIE windows (1280 × 720 each, on the desktop: leave them alone)
draw, and the visual scenarios `Crossguard.Visual.*` (`CrossguardScenarioVisualTests.cpp`) take
screenshots into `Saved\EditorTests\Screenshots\<Test>\<Name>.png` (the folder is cleared first; the
summary lists the files). They carry `CROSSGUARD_VISUAL_FLAGS` (`NonNullRHI`), so a headless run does
not list them at all: they never run by default. Each asserts only what it can read back; a person
looks at the frames.

Rendering needs light: the scenario map gets a movable sun, a sky atmosphere and a sky light (only
when the editor can render) and its floor casts no shadow (a 200× scaled cube shadows its own top).

| Call | Does |
| --- | --- |
| `C.LookFrom(Eye, LookAt, Client = -1)` | A camera actor at `Eye` looking at `LookAt` becomes the view target of the PIE player (standalone) or of client `Client`'s local player (spawned in that world). Kept as the view target every frame (the game mode's `SetViewTargetWithBlend` and possession would take it back). Pawns stay put. Wait a frame before shooting. |
| `C.Screenshot(Name, Client = -1)` | Writes `Screenshots\<Test>\<Name>.png`: Slate's screenshot of the PIE window (`FSlateApplication::TakeScreenshot`), so the scene and the UMG widgets (HUD, open windows) are in it, the window's title bar included; falls back to the viewport's `ReadPixels` (scene only). Adds an error under `-nullrhi`. |

Scenes must stay on the floor (within ±100 m of the origin): characters off it fall forever, and a
falling actor drops out of relevancy.

What the frames show today (2026-10-16): characters are the engine mannequin in its rest pose (no
animations), monsters a dark checkered cylinder and gather nodes a checkered sphere (placeholder
meshes), the native chest nothing, the dragon form the plain mannequin; equipped weapons have no
mesh. The placeholder UI lays out within a 1280 × 720 window; its one readability problem is the
inventory grid's item labels, tiny and truncated in one-square cells ("Healing Po… x2").

### API reference (`CrossguardScenario.h`)

`FScenario` (built in `RunTest`):

| Call | Does |
| --- | --- |
| `Step(Name, [](FScenarioContext& C) { ... })` | One action, in its own frame. |
| `Wait(Seconds)` | Waits game seconds. |
| `WaitUntil(Name, TimeoutSeconds, [](FScenarioContext& C) { return Done; }, PollSeconds = 0)` | Polls every frame (or every `PollSeconds`) until true; fails after the timeout. The condition may act (attack until dead). |
| `WaitWatching(Name, Seconds, [](FScenarioContext& C) { ... })` | Waits `Seconds` game seconds, calling the lambda every frame (count Health drops, top up, act). Never fails on its own: unlike `WaitUntil` it just ends after `Seconds`. |
| `WaitUntilOnClient(i, Name, TimeoutSeconds, [](FScenarioContext& C, UWorld& ClientWorld) { return Done; }, PollSeconds = 0)` | `WaitUntil` on a client's world ([Networked scenarios](#networked-scenarios)). |
| `TimeDilation(X)`, `Timeout(RealSeconds)`, `Raid(bool)`, `KeepDebugLoadout()` | Options: global time dilation from the start (1); real-time limit for the steps (180); game mode `bIsRaid` (true); keep the character Blueprint's Debug loadout on spawned characters. |
| `Clients(N, bDedicatedServer = false)` | Option: networked PIE, a listen server (host = client 0) and N − 1 clients, or a dedicated server and N clients. |
| `Run()` | Queues it. Last call in `RunTest`, once. |

`FScenarioContext` (`C` in steps):

| Group | Call | Does |
| --- | --- | --- |
| World | `C.Test`, `World()`, `GameMode()`, `CharacterClass()`, `LocalPlayer()`, `Now()` | The test; the PIE world; the `ACrossguardGameMode`; the character Blueprint class; the PIE player's character (player 1, also remembered as `"LocalPlayer"`); game seconds. |
| Flow | `Fail(Why)`, `Require(Cond, What)`, `Skip(Why)`, `IsStopped()` | See Rules. |
| Names | `Remember(Name, Actor)`, `FindActor(Name)`, `Get<T>(Name)`, `SetNumber(Name, V)`, `Number(Name, Default)` | Spawn calls remember their actor under its name. |
| Spawning | `SpawnPlayer(Name, FloorPoint, Yaw = 0, Party = "")` | A real player: a PlayerController of the game mode's class with its PlayerState and team (`InitNewPlayer` with `?Name=` and `?Party=`), possessing a new character Blueprint through `HandleStartingNewPlayer` → `RestartPlayer` (begins the raid on a raid map). `IsPlayerControlled()` is true (trading, player-kill XP); same Party = same team. No local player, input or HUD. Its controller is remembered as `"<Name>.Controller"`. |
| | `SpawnDummy(Name, FloorPoint, Yaw = 180)` | The character Blueprint possessed by a plain `AAIController` (like the checklist's placed dummy with Auto Possess AI): ability system set up, not player-controlled, no player state or team, BasicMonster kill XP. |
| | `SpawnMonster(Name, MobId, FloorPoint, Yaw = 0, [](ACrossguardMonster& M) { ... } = nullptr)` | An `ACrossguardMonster` with that Mob Id (stats from the Mob Table at BeginPlay), with its AI: it fights players and minions within its aggro range (15 m). The lambda runs on the deferred spawn, after `MobId` is set and before BeginPlay and possession (`CombatRole`, `FixedHealth`, `AggroRange`, ...); a Mob Id's row still applies over it at BeginPlay. |
| | `SpawnMinion(Name, Summoner, FloorPoint, MinionClass = nullptr, [](ACrossguardMinion& M) { ... } = nullptr)` | A minion as summon skills spawn it (`UCrossguardSummonAbility::SpawnMinion`): owned and instigated by `Summoner`, `SetSummoner` before BeginPlay (its team), facing the summoner's yaw, run by its minion controller. Class: an `ACrossguardMinion` subclass (default `ACrossguardMinion`); the lambda runs before BeginPlay. Unlike a summon: no life span, always spawned, not dismissed by a recast. |
| | `SpawnBlockingBox(Name, Center, Size)` | An engine cube scaled to `Size` cm, centred at `Center`, `BlockAll` (pawns, swings, line of sight). Returns the `AStaticMeshActor`; `Destroy()` it to open the way. |
| | `SpawnActor<T>(Name, Location, Yaw = 0, [](T& A) { ... }, Class = nullptr)` | Any actor; the lambda runs before BeginPlay. Location as is (not put on the floor). |
| | `Place(Character, FloorPoint, Yaw)`, `bKeepDebugLoadout` | Teleport; keep the Blueprint's Debug loadout for characters spawned after setting it. |
| Gear | `Equip(Character, WeaponId, Slot = MainHand, Rarity = Common)`, `Unequip(Character, Slot)` | Server equip of a fresh weapon in the active set (a two-handed main hand empties the off hand first). Fails the scenario on an error (unknown id, Weapon Table unset). |
| | `GiveItem(Character, ItemId, Count = 1, Rarity)`, `GiveItemOfCategory(Character, Category, ItemId, Count, Rarity)` | Into the inventory (category from `DT_Items`; `SkillGem` with the skill id). Returns what did not fit. |
| | `GrantSkills(Character, {TEXT("fireball"), ...})` | The loadout, as `DebugSkills` does (learned first, checked against Skill Capacity; when over capacity the level is raised, with debug progression, up to 15 until it fits); native classes on the ability system when the LoadoutComponent has no Skill Catalog. |
| Acting | `Face(Character, Yaw)`, `Aim(Character, Target)`, `AimAt(Character, Point)` | Level turn; aim from the eyes at the target's centre (pitch included) for aimed skills and swings. AI-controlled characters keep only the yaw. The camera lags: wait 0.2 s after `Face`/`Aim` before an aimed skill, swing or guard. |
| | `BasicAttack(Character)`, `RaiseGuard(Character)`, `LowerGuard(Character)` | Left click (false while the last attack recovers); right click held / released. |
| | `ActivateSkill(Character, SkillId)`, `IsOnCooldown(Character, SkillId)`, `HasSkill(Character, SkillId)` | Activates a granted active skill (as E); its `CooldownTags` are on; it is granted. |
| Net | `ClientCount()`, `ClientWorld(i)`, `ClientPawn(i)`, `ClientController(i)`, `ServerPawnOf(i)`, `ServerControllerOf(i)`, `OnClient<T>(ServerActor, i)`, `MakeAllies(Player, Ally)` | Networked scenarios only: see [Networked scenarios](#networked-scenarios). |
| Visual | `LookFrom(Eye, LookAt, Client = -1)`, `Screenshot(Name, Client = -1)` | Visual scenarios only: see [Visual run](#visual-run). |
| | `SetHealth(A, Value)`, `ApplyStatus(A, CrossguardTags::State_Silenced, Seconds)` | Health base value; a status through `UCrossguardSkillAbility::ApplyStatus` (self-applied, so CC immunity still counts). |
| | `Kill(Victim, Killer = nullptr)` | Lethal damage through the damage effect: death, body and kill XP as in play. |
| | `Damage(Source, Victim, Attack, Magic = 0)` | One hit of these base amounts through the damage effect (as `Kill`): stats and the victim's damage reactions apply (a monster turns on `Source`), guards do not. No Source: the victim's own. Returns the Health it took off. |
| Reading | `Health(A)`, `MaxHealth(A)`, `Stat(A, UCrossguardAttributeSet::GetXAttribute())` | Attributes (−1 / 0 without an ability system). |
| | `HasTag(A, CrossguardTags::State_Stunned)`, `HasTag(A, TEXT("State.Stunned"))`, `IsDead(A)` | Owned gameplay tags. |
| | `PlayerState(A)`, `WeaponValue(Character, Slot)`, `ExpectedDamage(Source, Target, AttackBase, MagicBase = 0, Coefficient = 1, AttackShare = 1)` | `ACrossguardPlayerState`; the weapon's AD / MD at its rarity; one hit by the damage formula with current stats. |
| | `TargetOf(Pawn)` | Whom an AI pawn is fighting: a monster's or minion's controller target (`GetCurrentTarget`), else its AI controller's focus actor; null without one. |
| | `ClankCount(A)` | Weapon clanks (block, parry, clash: `MulticastWeaponClank`) the character has played since it spawned; the defender plays it. 0 for non-characters. Native: `ACrossguardCharacter::GetWeaponClankCount()` and the `OnWeaponClankNative` delegate. |
| | `FindAll<T>()`, `FindNearest<T>(Point, Radius)` | Actors in the PIE world. |
| Game | `SetRaid(bool)`, `SetTimeDilation(X)` | `bIsRaid` (and begins / ends every player's raid); global time dilation. |
| | `ExpectServerTravel(MapPackage, TimeoutRealSeconds = 90)`, `IsTravelling()` | Follow a server travel: see [Networked scenarios](#networked-scenarios). |
| Log | `LogContains(Text)`, `LogCount(Text)`, `LogLines()`, `ClearLog()` | The captured log. |

### Scenario framework: engine calls (UE 5.8, verified)

The framework was written without an engine; it first built and ran green on UE 5.8 on 2026-10-15
([first-compile-triage.md](first-compile-triage.md) has the fixes). What it leans on, and what 5.8
needed:

1. PIE start (`CrossguardScenario.cpp`, `BeginScenario`): `FRequestPlaySessionParams.EditorPlaySettings`;
   `ULevelEditorPlaySettings::SetPlayNetMode` (`PIE_Standalone`, `PIE_ListenServer`, `PIE_Client` =
   dedicated server when `bLaunchSeparateServer` is false), `SetPlayNumberOfClients`,
   `SetRunUnderOneProcess`, `NewWindowWidth/Height`, `SetClientWindowSize`; `GEditor->RequestPlaySession`.
   Works headless, several instances included (`PlayLevel.cpp`: instance 0 is the standalone / listen
   host / dedicated server, the rest clients).
2. Worlds: `GEditor->GetPIEWorldContext(Instance)` and `FWorldContext::RunAsDedicated`.
   `GEditor->PlayWorld` is only used to see whether any PIE session exists: after the editor's PIE
   tick loop it points at the last instance ticked.
3. Client copies: `UNetDriver::GetNetGuidCache()`, `FNetGUIDCache::GetNetGUID` / `GetObjectFromNetGUID`.
   An unsaved world is not "name stable", so its level cannot be addressed (every spawn warns
   `PersistentLevel NOT Supported`); the test map's world gets `RF_WasLoaded` (assumed harmless for a
   throwaway map; `UWorld::bIsNameStableForNetworking` is private).
4. Extra players (`SpawnPlayer`): a PlayerController spawned with `SpawnActor` (no `UPlayer`), then
   `SetAsLocalPlayerController` (5.8: without a net driver a controller is local only with a
   `ULocalPlayer`, and GAS refuses `LocalPredicted` abilities for a non-local one),
   `ACrossguardGameMode::InitNewPlayer`, `SetPawn`, `HandleStartingNewPlayer_Implementation` →
   `RestartPlayer`. PIE clients' players match server and client by `APlayerState::GetPlayerId`.
5. The log listener: `FOutputDevice::CanBeUsedOnMultipleThreads` must be true, or GLog buffers the
   lines for its own thread and the test reads nothing.
6. `USceneComponent::IsVisible()` is false while `bHiddenInGame` (always, for a capsule): read
   `GetVisibleFlag()`.
7. `FAutomationEditorCommonUtils::CreateNewMap()` and `IAssetRegistry::GetDerivedClassNames`
   (`FTopLevelAssetPath`) work as written.
