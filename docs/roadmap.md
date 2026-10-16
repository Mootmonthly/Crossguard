# Crossguard Roadmap

Where the game stands and what comes next, in order. Rulings live in
[design-decisions.md](design-decisions.md); how systems are built lives in
[gas-architecture.md](gas-architecture.md). Update this file when a milestone item lands or the
order changes.

**Needs** says what an item is waiting on:

- **Code**: can be written without the editor (most of the C++ and data work).
- **Editor**: needs someone in Unreal (assets, Blueprints, UI, levels, playtesting).
- **Decision**: needs a design ruling first (see [Open questions](#open-questions)).

## Built so far

| Area | What exists |
| --- | --- |
| Rules library | Engine-free C++ for formulas, progression, XP, loadouts, items, affixes, armor, teams, inventory grid; 386 unit-test checks. |
| Data pipeline | JSON → validators → Unreal data tables, cross-checked against the C++ structs (`Tools/run_checks.sh`). 49 skills, 10 weapons, 16 armor pieces, 23 affixes, 4 general items. |
| Character | First-person camera, Enhanced Input built in code, GAS stats derived from STR/AGI/INT. |
| Combat | Weapon attacks (melee sweep, hitscan wand, projectiles), damage and healing formulas, dual wield, cooldowns with CDR cap. Bows use arrows. |
| Equipment | Main hand / off hand, six armor slots, rolled affixes, equipment stats and tags. |
| Skills | Loadout validation (Capacity, bundles, superseded skills), Skill Library gating. Passive stat bonuses play from the skill data. All 50 skills playable: 35 in C++ (including the next-attack system, Stealth, walls, CC immunity, party auras, tracking, Silence and minions that use skills) and the stat passives from data. Neckbite is the tiger's only, until Beastial forms exist. |
| Progression | XP sources, level curve, raid XP banked on extraction, attribute allocation, paid respec tokens. Session only, not saved. |
| Stash | Per-character tabs of 10 × 30, between raids only. Not saved yet. |
| Inventory | Server-authoritative 10 × 5 grid, Backpack and Quiver, stacking by type and rarity, equip through the inventory, Skill Gems, hotbar keys 3–7 for potions, bandages and throwables, two weapon sets on 1 and 2. |
| Death | Ragdoll, lootable body for every death, F to loot, revive within 120 s returns what was not looted; spectating teammates after. |
| Economy | Gold coins carried in raids, banked on extraction; banked gold; selling gemstones to vendors; buying stash tabs. |
| World | Extract portals (points of interest, 60 s channel, 30 s open), extraction zones, doors, locks and lockpicking. |
| Teams | Parties up to 8 via `?Party=`, friendly fire always on. |

## Milestone 0: Verify what is written

Several systems were written without an editor build. Do this before building more on top. Step by step: [next-build-checklist.md](next-build-checklist.md).

| Item | Needs | Notes |
| --- | --- | --- |
| Build and fix compile errors | Done | Built on UE 5.8 on the owner's PC; 103/103 editor tests pass (all first-run failures were test-side). Notes in [first-compile-triage.md](first-compile-triage.md). |
| Import new tables | Done | `Tools\editor_setup.bat` imports every `DT_` table into `/Game/Data` and sets the references (rerun after data changes). |
| Playtest each system | Editor | Debug options on the character: `DebugInventory`, `bDebugLootTakesAll`, `DebugReviveDelay`, `bUseDebugProgression`; turn off the game mode's `bIsRaid` to test the stash and vendors. |
| Two-client PIE check | Editor | `Equipment.Wielding.*` tags reach other clients; bodies ragdoll on every client; stealth hidden from enemy clients (time the possible ~5 s leak, dedicated server + 3 clients). |
| Crowd avoidance | Editor, Code | Without a nav mesh a minion walks into its summoner's back and stops (seen in tests). Check on a real map; turn on RVO/detour avoidance for monsters and minions if they bunch up. |

## Milestone 1: One complete raid

Goal: enter a raid, fight, loot, extract or die, and see the result.

| Item | Needs | Notes |
| --- | --- | --- |
| Extract portals | Editor | Built in code: summon with X inside a point of interest after 10 min, 60 s channel, open 30 s for anyone. Needs points of interest placed and beam/portal visuals. |
| Extraction points | Editor | Built in code (`ACrossguardExtractionZone`, 5 s placeholder). Needs placing in maps; the trip to the hub needs the hub and saving. |
| Spectating and menu | Editor | Built in code: 120 s revive window, then spectate teammates or return to the menu. Needs a main menu map and a spectator HUD. |
| Stash | Editor | Built in code: per character, 10 × 30 tabs (up to 3 more at 3000 gold), Skill Gems learnable from it. Needs the stash UI in the hub. |
| Shared stash tabs | Code, Decision | Subscription benefit: tabs shared by all of an account's characters. Needs accounts and the backend. |
| Hub / lobby map | Editor | Where players manage stash, Skill Library, attributes, loadout; game mode with `bIsRaid` off. |
| Inventory and loot UI | Editor | Grid widgets for inventory, Backpack, Quiver, equipment, bodies. The C++ API is ready. |
| Hotbar, weapon sets and skill wheel UI | Editor | Built in code (keys 1–7, hold Q, E). Needs the hotbar bar, the radial skill wheel and a weapon set indicator. |
| Drop and pick up single items | Editor | Built in code (G drops the stack in hand, F picks up). Needs drag-to-drop in the inventory UI and item meshes. |
| Revive spell | Decision, Code | Calls the existing `Revive`. Comes from the skills you add in the Skill Forge. |
| Saving progression | Code, Decision | XP, level, attributes, Skill Library, stash, respec tokens. Needs a choice of backend. |
| Steam accounts | Editor | Lasting player ids for guilds and saves: plugin + config steps in [steam-setup.md](steam-setup.md). |
| Loadout timing | Decision | Can the loadout change during a raid, or only between raids? |

## Milestone 2: Combat depth

| Item | Needs | Notes |
| --- | --- | --- |
| Remaining skill parts | Code, Decision | Neckbite for players (needs Beastial forms), Hunter's Tracking hearing range (needs sound), Arcane Arrows dropoff (needs arrow dropoff). |
| Stealth teammate indicator | Editor | Party members see a purple outline around a stealthed ally (with the visual assets). |
| Skill visuals | Editor | Fireball, breath, shield, Blind screen effect, dragon form, skeleton meshes. |
| Passives on others and free-text mechanics | Code | Data-driven passives cover Self stat modifiers (masteries, Heroic Might, Cor Leonis, ...); party auras, on-hit slows and mechanics like Hunter's Tracking still need code. |
| Block (shields) and parry | Editor | Built in code: hold right click; Impact Power decides; stun, riposte, AI guards. Needs animations and a guard visual. |
| Status effects | Code | Slow, Root, Mute, Silence, Stun, Blind and Attack Damage shields exist; none applies Root yet. |
| Minion meshes | Editor | Meshes for skeletons, tiger and demon (cylinders now). All three summons exist, Minion CDR applies to them, and minions and monsters use their skills. |
| Animations, first-person arms | Editor | Melee hits are sphere sweeps until weapon collision exists. |
| Weapon-on-weapon collision (fencing) | Code, Editor | Clashes are approximated (a swing's blade is in the way 0.25 s); real blade traces need swing animations. Clank animation and sound go in `OnWeaponClank`. |

## Milestone 3: World and PvE

| Item | Needs | Notes |
| --- | --- | --- |
| Monster AI | Editor, Decision | Built in code: `ACrossguardMonster` guards its spot, aggroes within 15 m (line of sight), chases, attacks (Warrior, Mage or Ranged), uses its skills, leashes at 30 m from home (retargets anyone in aggro range first, else walks home and fully resets); turns on attackers from any range; drops its loot. Mobs come from the Mob Forge (4 so far); `ACrossguardMobSpawner` spawns weighted picks in raids. Needs meshes and placing spawners in maps. |
| Loot tables | Decision, Code | What monsters, chests and bosses drop. |
| Treasure chests | Editor | Built in code: an `ACrossguardLootContainer` with a `LootTableId` fills itself when the raid starts (it can be locked). Needs a chest Blueprint with a mesh, placed in maps. |
| Loot tables | Decision | Built: `Data/Loot/LootTables.json` (monsters, bosses, chests) with placeholder drop rates and rarity weights. Needs real numbers. |
| Lockpicking skill | Editor | Lockpicking and the lockpick item are built; the skill comes from the Skill Forge (grant `Ability.Lockpicking`). |
| Doors in maps | Editor | `ACrossguardDoor` needs a mesh and placing; it snaps open until it is animated. |
| Gathering nodes | Editor, Decision | Built in code: `ACrossguardGatherNode` (F, 2 s channel broken by moving, damage or attacking; rolls `gather_herb` / `gather_ore` into the inventory; 10 XP; depleted once per raid). Four placeholder materials (herbs, ores). Needs meshes (herb, ore Blueprints) and placing in maps; channel time, respawn, yields and materials stack size are placeholders. |
| Raid maps | Editor | PCG plugin is enabled. |
| Quests | Decision, Code | 10–100 XP each. |

## Milestone 4: Economy and social

| Item | Needs | Notes |
| --- | --- | --- |
| Crafting and enchanting | Decision, Code | XP is ruled (10–60 by rarity); recipes are not. |
| Trading | Editor | Built in code ([trading.md](trading.md)): ask, offer, lock, accept, all-or-nothing swap. Needs a trade window UI (console commands for now). |
| Auction house | Code, Decision | Needs the backend and a currency. |
| Guilds and guild bank | Code, Editor, Decision | Built in code ([guild-rules.md](guild-rules.md)): ranks, invites, kicks, bank (gold and items) with permissions and a log; rules unit tested. In server memory only: needs accounts (Steam ids) and a backend to save guilds. Needs a guild window (console commands for now). |
| Respec store purchase | Code | Grants respec tokens after a verified purchase; price not set. |
| Subscription | Decision, Code | About $10 per season: five respec tokens, a shared stash tab, common healing items, throwables, lockpicks. |
| NPC vendors in the hub | Editor | Selling is built (`UCrossguardVendorComponent`); needs the NPC and a sell screen. |
| Cosmetics | Decision, Editor | |

## Milestone 5: Online

| Item | Needs | Notes |
| --- | --- | --- |
| Dedicated servers | Code, Editor | Everything is server-authoritative already. |
| Matchmaking with parties | Code | Must pass `?Party=<key>` in the travel URL. |
| Backend and accounts | Decision, Code | Persistence, purchases, auction house. |
| Platform store | Decision, Code | Steam or other; purchases verified server-side. |
| Anti-cheat review | Code | Reviewed: every server RPC, ability, console command, debug option and replicated property ([anti-cheat.md](anti-cheat.md)). Fixed the grid-position and attribute-allocation overflows, vendor gold overflow, interact spam, oversized trade offers, loadout changes while dead, and debug options in Shipping. Left: stealthed positions still replicate to enemies, and gear affixes are visible to everyone. |

## Open questions

Decisions that block items above. Answers go into design-decisions.md.

| Question | Blocks |
| --- | --- |
| Do gemstones stack? | Gemstones (currently: no) |
| Is a lockpick used up per lock? Exact Dexterity effect? | Lockpicking (currently: used up; 5 s ÷ (1 + Dexterity)) |
| Extraction time | Extraction (currently: 5 s) |
| Final subscription price | Subscription |
| Can the loadout change during a raid? | Loadout timing |
| Materials stack size? Gathering channel time, respawn, yields? | Gathering (currently: stack 20; 2 s, once per raid, 1–3 per node) |
| Which arrows does a bow use first when several rarities are carried? | Ammo (currently: first found) |
| Is drinking a potion instant? Does bandaging slow movement? | Consumables (currently: instant, full speed) |
| Respec price? | Store |
| Jewelry, quarterstaffs, two-handed pavise shields, the Beastial transformation for Neckbite | Later content |
| Tiger attack speed, Demon size? | Summon Tiger, Summon Greater Demon (currently: 1 s; 1.5×) |
| Real drop rates: what each monster and chest drops, and rarity odds | Loot tables (placeholders now) |
| More monsters: enter them in the Mob Forge (https://claude.ai/artifact/JPDt46urNPnzMsgZuD7RYW) | Monster AI |

## Placeholders to replace

- Attribute scaling formulas (linear around 10 points).
- Backpack and Quiver item size while unequipped (2 × 2), main grid shape (10 × 5).
- Body grid width (10) and interact range (250 cm).
