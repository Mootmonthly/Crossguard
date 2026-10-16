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
- `Crossguard.Scenario.Monsters.MobSpawner` and the monster data tests: the game mode's **Mob Table**
  `DT_Mobs` (skipped with a warning while unset) with a `skeleton_soldier` row. Loot tests: the game
  mode's Loot Tables `DT_Loot`. Skill Gem and passive tests: `DA_SkillCatalog`. Armor: `DT_Armor`.

Scenario characters ignore the character Blueprint's **Crossguard | Debug** loadout (Debug items,
skills, inventory, revive delay, loot-takes-all, debug HUD are cleared on them), so the tests do not
depend on what you set there for manual testing. `Level` / `bUseDebugProgression` stay; expected
damage is computed from the characters' actual stats, so they work at any level.

### The scenarios

95 tests in 12 files. Grouped by file; the name is the full test name.

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
| `Crossguard.Scenario.Raid.ExtractionZone` | 5 s in the zone (still in the raid at 4.5 s): extracted, raid XP + reward banked, 250 gold banked, log lines. |
| `Crossguard.Scenario.Raid.ExtractPortal` | `PortalUnlockSeconds` 0, two players in a point of interest press X: both portals channel; the one whose owner walks out of keep-alive range collapses; the other opens after the channel time and extracts its owner. |
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

**`CrossguardScenarioSkillTests.cpp`**

| Test | Checks |
| --- | --- |
| `Crossguard.Scenario.Skills.Fireball` | A player with `wand_one_handed` and `fireball` casts at a dummy 6 m away: Fireball goes on cooldown (`Cooldown.Skill.Fireball`); 1 s later the dummy has lost exactly the formula's damage for 50 Magic Damage at coefficient 0.5 (at least 50 before Magic Resistance); the caster, outside the 2 m splash, is unhurt. |

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
| `Crossguard.Scenario.Skills.Wall` | One wall 3 m ahead, on the floor, across the view; gone after 15 s. |
| `Crossguard.Scenario.Skills.Light` | One orb attached 1.2 m over the caster for 120 s; a recast replaces it. |
| `Crossguard.Scenario.Skills.Stealth` | `State.Stealthed`; the hider's capsule is hidden from the PIE player; attacking ends it with +10% Movement Speed and +25% ADB for 3 s; otherwise it lasts 12 s. |
| `Crossguard.Scenario.Skills.Immutable` | A Chop Down hit still deals 110% but does not slow. |
| `Crossguard.Scenario.Skills.Unleash` | STR / AGI / INT double, Max HP follows; back after 15 s. |
| `Crossguard.Scenario.Skills.Benediction` | Aimed at a dead player's body: alive at full HP with the sword back in hand ("revive" logged); at nothing: caster to full, the other player not healed. |
| `Crossguard.Scenario.Skills.Multiscale` | Equipped: the knight and a same-Party member get AR / MR +0.15 and `State.Multiscale`, an outsider nothing; unequipped: gone from all. |
| `Crossguard.Scenario.Skills.HuntersTracking` | Passive on; footprints appear behind a walking enemy, are still there at 25 s, fade about 30 s after the last step. |
| `Crossguard.Scenario.Skills.SummonSkeletons` | 5 Warriors (40 HP) and 1 Mage (30 HP), 60 s life; they damage the dummy; a recast replaces them; gone after 60 s. |
| `Crossguard.Scenario.Skills.SummonTiger` | Refused without `pet_gem_tiger` (no cooldown); with it: one tiger, 100 HP, 20 AD, Hunter's Leap / Neckbite / Slash, gem kept; it hits the dummy (log); gone after 180 s. |
| `Crossguard.Scenario.Skills.SummonGreaterDemon` | One demon, 220 HP, 1.5x, two-handed sword, 40 AD, Mortal Strike / Eldritch Shield / Shade Blow, gone after 60 s. |
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
| `Crossguard.Scenario.Combat.AIIgnoresAllySwings` | A party member's minion never guards against its ally's 12 swings; every one lands (friendly fire). |
| `Crossguard.Scenario.Combat.StunBlocksActions` | Stunned (by a parry, or directly): no attack, skill, jump, movement or guard, a raised guard drops; all work again after. |

**`CrossguardScenarioStatusTests.cpp`**

| Test | Checks |
| --- | --- |
| `Crossguard.Scenario.Status.Root` | Rooted 1 s: walk speed 0, no jump, Hunter's Leap refused, basic attack still works; afterwards walk speed and jumping are back. |

**`CrossguardScenarioMonsterTests.cpp`**

| Test | Checks |
| --- | --- |
| `Crossguard.Scenario.Monsters.MobSpawner` | A `CrossguardMobSpawner` with `skeleton_soldier` (weight 1), Count 3–3, Radius 6 m, placed 40 m out during a raid: 3 monsters owned by it, each with Mob Id `skeleton_soldier`, Max Health and Health = the mob's Health in `DT_Mobs` (120), a controller, within the radius, above the floor. |

**`CrossguardScenarioMonsterAITests.cpp`** (no NavMesh: monsters steer straight at their goal)

| Test | Checks |
| --- | --- |
| `Crossguard.Scenario.Monsters.AggroSightAndAttack` | A built-in monster ignores a player beyond aggro range and one behind a wall; once it sees them it closes in and hits for its Attack Damage (formula) once per Attack Interval, from within Attack Range. |
| `Crossguard.Scenario.Monsters.LeashReturnsHome` | 6 m leash: pulled beyond it with nobody in reach, it walks home, healed to full, and ignores a player beyond the leash. |
| `Crossguard.Scenario.Monsters.LeashRetargets` | Pulled beyond the leash while another player is in range within it: turns on that player, not healed. |
| `Crossguard.Scenario.Monsters.ReactsToDamageFromAnyRange` | A player 25 m away is ignored until they hurt it; then it turns, closes in and hits. |
| `Crossguard.Scenario.Monsters.MonsterTeam` | Two monsters and one's minion never fight each other; all three go for a player; nothing hurt among them. |
| `Crossguard.Scenario.Monsters.DeathBodyLootXp` | Six monsters of one mob killed: each dead, not revivable, leaves a body; at least one body holds loot; Basic Monster XP per kill. |
| `Crossguard.Scenario.Monsters.BossKillXpAndLoot` | A boss (`mithril_dragon` if present) killed: Boss Monster XP, a body, the dragon's body holds loot. |
| `Crossguard.Scenario.Monsters.MobDataApplied` | Each mob in the Mob Table (up to 6, all 5 today): role, weapon, Impact Power, damage, intervals, ranges, loot table, kill XP, Health, walk speed, scale, feet on the floor, skills with bundles expanded. |
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
| `Crossguard.Scenario.Hub.BuyStashTabs` | No gold: refused; with gold, three tabs; the fourth refused AtCap, no gold taken. |
| `Crossguard.Scenario.Hub.LoweringAttributeNeedsRespecToken` | Lowering Strength refused without a token (nothing changes), done with one (token used). |

### Checklist coverage

Against [next-build-checklist.md](next-build-checklist.md). **auto**: the tests check the row's
expected result; tick it when they pass. **part**: some of it is checked, do the rest by hand.
**manual**: no test. "Looks" (ragdoll, meshes, arcs, effects) always needs eyes; the tests use
server-side players in one standalone PIE, not real clients.

| Section / row | Status | Tests | By hand |
| --- | --- | --- | --- |
| 1 Pull and build | manual | (`run_editor_tests.bat` builds) | Pull, project files. |
| 2 Data tables imported | auto | `Crossguard.Data.EditorAssets` | — |
| 2 Tables set on Blueprints / game mode | manual | (scenarios skip or fail when unset) | Editor property set-up. |
| 3 Character settings | manual | — | Input mapping context, Debug loadout (scenarios ignore it). |
| 4 Knife hits 15; bottle 10 in 3 m + 3/s for 5 s | auto | `Raid.ThrowingKnife`, `Raid.ExplosiveBottle` | The arc. |
| 4 Weapon sets 1 / 2 | auto | `Raid.WeaponSetSwitch` | — |
| 4 Next knife; empty hands after the last | auto | `Raid.ThrowingKnife` | — |
| 4 Bow 45 shots, runs out | auto | `Raid.BowRunsOutOfArrows` | — |
| 4 Potion 15 over 10 s; bandage 10 over 2 s held | auto | `Raid.PotionAndBandage` | — |
| 4 Kill the dummy: body, 5 XP | auto | `Raid.KillDummyLeavesBody` | The ragdoll. |
| 4 Loot a geared dummy's body | auto | `Raid.LootBody` | — |
| 4 Die and revive with gear | auto | `Raid.DeathAndRevive` | The ragdoll. |
| 4 Looted before revive | auto | `Raid.LootedBeforeRevive` | — |
| 4 Extraction zone 5 s, XP and 250 gold banked | auto | `Raid.ExtractionZone` | — |
| 4 Portal: 60 s, extract; walk away collapses | auto | `Raid.ExtractPortal` | `BeamMesh` look. |
| 4 X refused outside / twice | auto | `Raid.PortalRefusals` | — |
| 4 `chest_common` chest | auto | `Raid.ChestCommonLoot` | — |
| 4 Monster: aggro 15 m, 12 per 1.5 s, 30 m leash, shot from 40 m, kill XP and body | part | `Monsters.AggroSightAndAttack`, `LeashReturnsHome`, `LeashRetargets`, `ReactsToDamageFromAnyRange`, `DeathBodyLootXp` | NavMesh pathing; the 30 m leash (tests use 6 m) and 40 m (tests 25 m). |
| 4 `boss_monster` dummy: 3–5 extra items | auto | `Raid.BossLootOnDummy` | — |
| 4 Door snaps open / closed | auto | `Raid.DoorOpenClose` | The mesh moving. |
| 4 Locked door, lockpicking 5 s | auto | `Raid.LockedDoor` | — |
| 4 `fireball` 50+; 15 to you within 2 m | part | `Skills.Fireball` | The self-splash within 2 m. |
| 4 `heal` dummy, then self | auto | `Skills.Heal` | — |
| 4 `mute` | auto | `Skills.Mute` | — |
| 4 `eldritch_shield` | auto | `Skills.EldritchShield` | — |
| 4 `chop_down`, flier falls | auto | `Skills.ChopDown` | — |
| 4 `shooting_star` | auto | `Skills.ShootingStar` | — |
| 4 `dragonification` | auto | `Skills.Dragonification` | Q selector UI. |
| 4 `flight` Space / Left Ctrl | part | `Skills.Flight` | Descending (Left Ctrl), the key bindings. |
| 4 Next-attack stack; Mortal Strike kill refresh | auto | `Skills.NextAttackStack`, `Skills.MortalStrikeKillRefresh` | States display. |
| 4 `gouge` | auto | `Skills.Gouge` | — |
| 4 `shade_blow` heal block | auto | `Skills.ShadeBlowHealBlock` | — |
| 4 `purge_shot` | auto | `Skills.PurgeShot` | — |
| 4 `cleave` 85% | auto | `Skills.Cleave` | — |
| 4 `parting_the_land` | auto | `Skills.PartingTheLand` | The pop-up and walls' look. |
| 4 `hunters_leap` | auto | `Skills.HuntersLeap` | — |
| 4 `wall`, `light` | auto | `Skills.Wall`, `Skills.Light` | The light itself. |
| 4 `stealth` vanish on the other screen | part | `Skills.Stealth` | Vanishing on a real second client (replication). |
| 4 `immutable` | auto | `Skills.Immutable` | — |
| 4 `unleash` | auto | `Skills.Unleash` | — |
| 4 `benediction` on a body | auto | `Skills.Benediction` | — |
| 4 `multiscale` with a party member | auto | `Skills.Multiscale` | — |
| 4 `hunters_tracking` grey marks | part | `Skills.HuntersTracking` | The marks on a real client's screen. |
| 4 Drop with G, pick up with F | auto | `Raid.DropAndPickUp` | The box's look. |
| 4 Gather node: 2 s, herbs, +10 XP, depleted, refused again | auto | `Raid.GatherNode` | The node's mesh hiding. |
| 4 Gather node: walking, attacking or damage breaks the channel | part | `Raid.GatherNodeMoveInterrupts` | Attacking and taking damage (rules unit tested: `CheckGatherInterrupt`). |
| 4 `summon_skeletons` | part | `Skills.SummonSkeletons` | Following you; NavMesh pathing. |
| 4 `summon_tiger` leap, riders, hits for 20 | part | `Skills.SummonTiger` | Leap, the +7 / +10 Magic riders (only "it hits" is checked). |
| 4 `summon_greater_demon` | part | `Skills.SummonGreaterDemon` | Raising Eldritch Shield, its 40 hits with riders in a fight. |
| 4 Neckbite silences 2 s, stops a channel | auto | `Skills.NeckbiteSilence`, `Skills.Silence` | — |
| 4 Mob spawner: soldiers and archers, 3–5, archers keep away, body, respawn | part | `Monsters.MobSpawner`, `SpawnerRespawn`, `RangedKeepsDistanceAndShoots` | Weighted mix and 3–5 count (tests use one mob, fixed count); NavMesh. |
| 4 `mithril_dragon` spawner: scale 6 on the floor, summons fight you | part | `Monsters.MobDataApplied`, `MonsterTeam` | The dragon casting its summons (team rule is tested with a generic minion). |
| 4 Block | auto | `Combat.ShieldBlock` | Two real clients. |
| 4 Parry | auto | `Combat.ParryTwoHanded`, `ImpactDifferences` (`axe_two_handed` vs `sword_one_handed`), `Skills.RiposteRider` | Two real clients. |
| 4 Monsters guard ~half, stun and sword damage back | part | `Combat.AIGuardsAgainstSwings` | The ~50% rate and `skeleton_soldier`'s mirror damage / 1 s stun (built-in monster, damage 0). |
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
| 6 Both see bodies ragdoll | manual | — | Two real clients, replication, visuals. |
| 6 `showdebug` shows the other's `Equipment.Wielding.*` | manual | — | Replication to a client, debug HUD. |
| 6 Concurrent loot never duplicates | part | `Social.ConcurrentLootNeverDuplicates` | Two real clients' RPC timing (the test is server-side, one frame). |

Totals: 66 rows: 49 auto, 12 part, 5 manual. Not checklist rows: `Combat.ArrowBlockedNoStun`,
`SwingClash`, `AIIgnoresAllySwings`, `StunBlocksActions`, `Skills.LikeALion`, `Quickshot`,
`ArcaneArrows`, the extra trade and guild cases.

### Known limits

- Written without an engine to build against: compiled and run on no real build yet. Until a first
  green run, a failure may be the test, not the game (see the framework section below).
- One standalone PIE, server side: players are server-spawned controllers with no input, HUD or
  client. Nothing about replication, client prediction, keys or UI is tested.
- Clanks: there is no native event to bind to, so the clank (`MulticastWeaponClank` →
  `OnWeaponClank` Blueprint event) is counted from the Impact Power log line before it; the
  Blueprint event, sound and effect are not checked.
- AI guard chance (50%) is checked loosely: over 20 swings, at least one parried and one landed.
- No NavMesh in the test map: monsters steer straight; pathing round obstacles is untested.
- Visuals and audio (ragdolls, meshes, arcs, effects, sounds, animations) are never checked.
- Data-dependent tests skip (pass with a warning) when `DT_Mobs`, `DT_Loot`, `DA_SkillCatalog` or
  `DT_Armor` are not set: a skip is not a pass for the checklist row.

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
