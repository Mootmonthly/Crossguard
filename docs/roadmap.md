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
| Rules library | Engine-free C++ for formulas, progression, XP, loadouts, items, affixes, armor, teams, inventory grid; 339 unit-test checks. |
| Data pipeline | JSON → validators → Unreal data tables, cross-checked against the C++ structs (`Tools/run_checks.sh`). 49 skills, 9 weapons, 16 armor pieces, 23 affixes, 4 general items. |
| Character | First-person camera, Enhanced Input built in code, GAS stats derived from STR/AGI/INT. |
| Combat | Weapon attacks (melee sweep, hitscan wand, projectiles), damage and healing formulas, dual wield, cooldowns with CDR cap. Bows use arrows. |
| Equipment | Main hand / off hand, six armor slots, rolled affixes, equipment stats and tags. |
| Skills | Loadout validation (Capacity, bundles, superseded skills), Skill Library gating, passive ability base class. **No individual skills implemented yet.** |
| Progression | XP sources, level curve, raid XP banked on extraction, attribute allocation, paid respec tokens. Session only, not saved. |
| Inventory | Server-authoritative 10 × 5 grid, Backpack and Quiver, stacking by type and rarity, equip through the inventory, Skill Gems, potions and bandages (Q to ready, left click to use). |
| Death | Ragdoll, lootable body for every death, E to loot, revive returns what was not looted. |
| Teams | Parties up to 8 via `?Party=`, friendly fire always on. |

## Milestone 0: Verify what is written

Several systems were written without an editor build. Do this before building more on top.

| Item | Needs | Notes |
| --- | --- | --- |
| Build and fix compile errors | Editor | Progression, inventory, bodies, revive, ammo and consumables are uncompiled. |
| Import new tables | Editor | `DT_Items` (row `CrossguardItemRow`); reimport `DT_Weapons`, `DT_Armor`. Set `InventoryComponent.ItemTable`. |
| Playtest each system | Editor | Debug options on the character: `DebugInventory`, `bDebugLootTakesAll`, `DebugReviveDelay`, `bUseDebugProgression`. |
| Two-client PIE check | Editor | `Equipment.Wielding.*` tags reach other clients; bodies ragdoll on every client. |

## Milestone 1: One complete raid

Goal: enter a raid, fight, loot, extract or die, and see the result.

| Item | Needs | Notes |
| --- | --- | --- |
| Extraction points | Code, Editor | A volume that ends the raid for a player (`EndRaid(true)`, +35 XP). Placement in levels is editor work. |
| Raid end on failure | Code, Decision | When does a raid end for a dead player: when they leave, after a timer, when the match ends? |
| Stash | Code | Per character, 10 × 30 per tab; extra tabs bought with in-game currency. Skill Gems can be learned from it. |
| Shared stash tabs | Code, Decision | Subscription benefit: tabs shared by all of an account's characters. Needs accounts and the backend. |
| Hub / lobby map | Editor | Where players manage stash, Skill Library, attributes, loadout; game mode with `bIsRaid` off. |
| Inventory and loot UI | Editor | Grid widgets for inventory, Backpack, Quiver, equipment, bodies. The C++ API is ready. |
| Hotbar for consumables | Code, Decision | Replaces the Q placeholder. How many slots and which keys? |
| Drop and pick up single items | Code | Items lying in the world, separate from bodies. |
| Revive spell | Decision, Code | Calls the existing `Revive`. Comes from the skills you add in the Skill Forge. |
| Saving progression | Code, Decision | XP, level, attributes, Skill Library, stash, respec tokens. Needs a choice of backend. |
| Loadout timing | Decision | Can the loadout change during a raid, or only between raids? |

## Milestone 2: Combat depth

| Item | Needs | Notes |
| --- | --- | --- |
| Implement skills | Code | 49 skills in the data, none playable yet. Start with the ones the first raids need. |
| Generate passive effects from data | Code | Passives are hand-made today. |
| Block (shields) and parry | Code | Right-click actions already chosen by the rules; not built. |
| Status effects | Code | Slow, Mute, Silence, crowd control, damage shields (Eldritch Shield). |
| Throwables | Decision, Code | Sizes and stack (3) ruled; damage and behaviour not. |
| Minions | Code | Summon Tiger and minion CDR. |
| Animations, first-person arms | Editor | Melee hits are sphere sweeps until weapon collision exists. |
| Weapon-on-weapon collision (fencing) | Code, Editor | |

## Milestone 3: World and PvE

| Item | Needs | Notes |
| --- | --- | --- |
| Monster AI | Code, Editor | Basic monsters (5 XP) and bosses (50 XP) via `KillXpSource`. |
| Loot tables | Decision, Code | What monsters, chests and bosses drop. |
| Treasure chests | Code, Editor | Reuse `ACrossguardLootContainer`. |
| Gathering nodes | Code, Editor | Herbs and ore, 10 XP per node; materials stack size not ruled. |
| Raid maps | Editor | PCG plugin is enabled. |
| Quests | Decision, Code | 10–100 XP each. |

## Milestone 4: Economy and social

| Item | Needs | Notes |
| --- | --- | --- |
| Crafting and enchanting | Decision, Code | XP is ruled (10–60 by rarity); recipes are not. |
| Trading | Code | Player to player. |
| Auction house | Code, Decision | Needs the backend and a currency. |
| Guilds and guild bank | Code | Members deposit; officers withdraw, manage, redistribute. |
| Respec store purchase | Code | Grants respec tokens after a verified purchase; price not set. |
| Subscription | Decision, Code | Shared stash tabs; other benefits not set. |
| In-game currency | Decision, Code | Buys stash tabs; the auction house needs it too. |
| Cosmetics | Decision, Editor | |

## Milestone 5: Online

| Item | Needs | Notes |
| --- | --- | --- |
| Dedicated servers | Code, Editor | Everything is server-authoritative already. |
| Matchmaking with parties | Code | Must pass `?Party=<key>` in the travel URL. |
| Backend and accounts | Decision, Code | Persistence, purchases, auction house. |
| Platform store | Decision, Code | Steam or other; purchases verified server-side. |
| Anti-cheat review | Code | e.g. body contents replicate to everyone today; AI ability system init on clients. |

## Open questions

Decisions that block items above. Answers go into design-decisions.md.

| Question | Blocks |
| --- | --- |
| Stash tab price, and the in-game currency itself | Buying stash tabs |
| Subscription price and other benefits | Shared stash tabs |
| When does the raid end for a player who died and was not revived? | Raid end |
| Hotbar: how many slots, which keys? | Hotbar |
| Can the loadout change during a raid? | Loadout timing |
| What do throwables do (damage, area, effects)? | Throwables |
| Materials stack size? | Gathering |
| Which arrows does a bow use first when several rarities are carried? | Ammo (currently: first found) |
| Is drinking a potion instant? Does bandaging slow movement? | Consumables (currently: instant, full speed) |
| Respec price? | Store |
| Jewelry, quarterstaffs, two-handed pavise shields, the Beastial transformation for Neckbite | Later content |

## Placeholders to replace

- Attribute scaling formulas (linear around 10 points).
- Backpack and Quiver item size while unequipped (2 × 2), main grid shape (10 × 5).
- Body grid width (10) and interact range (250 cm).
