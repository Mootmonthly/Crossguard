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
| Rules library | Engine-free C++ for formulas, progression, XP, loadouts, items, affixes, armor, teams, inventory grid, character saves (JSON, migration, validation); unit tested. |
| Data pipeline | JSON → validators → Unreal data tables, cross-checked against the C++ structs (`Tools/run_checks.sh`). 50 skills, 10 weapons, 16 armor pieces, 23 affixes, 4 general items. |
| Character | First-person camera, Enhanced Input built in code, GAS stats derived from STR/AGI/INT. |
| Combat | Weapon attacks (melee sweep, hitscan wand, projectiles), damage and healing formulas, dual wield, cooldowns with CDR cap. Bows use arrows. |
| Equipment | Main hand / off hand, six armor slots, rolled affixes, equipment stats and tags. |
| Skills | Loadout validation (Capacity, bundles, superseded skills), Skill Library gating. Passive stat bonuses play from the skill data. All 50 skills playable: 35 in C++ (including the next-attack system, Stealth, walls, CC immunity, party auras, tracking, Silence and minions that use skills) and the stat passives from data. Neckbite is the tiger's only, until Beastial forms exist. |
| Progression | XP sources, level curve, raid XP banked on extraction, attribute allocation, paid respec tokens. Saved ([persistence.md](persistence.md)). |
| Stash | Per-character tabs of 10 × 30, between raids only. Saved with the character. |
| Inventory | Server-authoritative 10 × 5 grid, Backpack and Quiver, stacking by type and rarity, equip through the inventory, Skill Gems, hotbar keys 3–7 for potions, bandages and throwables, two weapon sets on 1 and 2. |
| Death | Ragdoll, lootable body for every death, F to loot, revive within 120 s returns what was not looted; spectating teammates after. |
| Economy | Gold coins carried in raids, banked on extraction; banked gold; selling gemstones to vendors; buying stash tabs. |
| World | Extract portals (points of interest, 60 s channel, 30 s open), extraction zones, doors, locks and lockpicking. |
| Teams | Parties up to 8 via `?Party=`, friendly fire always on. In-game parties in the hub (invite, accept, leave, kick, promote; members allied at once) and the leader's raid launch ([parties.md](parties.md)); console commands until a party panel exists. |
| Build and tests | Builds on UE 5.8 with no warnings. 143/143 headless, 8/8 networked and 7/7 visual editor tests pass on the owner's PC; every checklist row is automated (`docs/editor-tests.md`). `Tools\editor_setup.bat` imports every data table and sets the game mode's classes and references. First-build notes: [first-compile-triage.md](first-compile-triage.md). |
| Online play | Reconnect to a raid after a crash or logout (same body, if alive); saving to local JSON behind a backend interface; parties with an invite inbox and the hub-to-raid launch; stealthed players hidden from enemy clients (leak 0.02 s, verified); crowd avoidance for monsters and minions (verified). |

## Milestone 0: Verify what is written

The code builds and the automated tests pass; what is left needs hands-on play. Step by step: [next-build-checklist.md](next-build-checklist.md).

| Item | Needs | Notes |
| --- | --- | --- |
| Playtest feel | Editor | Automated tests cover the rules; play for feel, timing and balance. Debug options on the character: `DebugInventory`, `bDebugLootTakesAll`, `DebugReviveDelay`, `bUseDebugProgression`; turn off the game mode's `bIsRaid` to test the stash and vendors. |

## Milestone 1: One complete raid

Goal: enter a raid, fight, loot, extract or die, and see the result.

| Item | Needs | Notes |
| --- | --- | --- |
| Extract portals | Editor | Built in code: summon with X inside a point of interest after 10 min, 60 s channel, open 30 s for anyone. Needs points of interest placed and beam/portal visuals. |
| Extraction points | Editor | Built in code (`ACrossguardExtractionZone`, 5 s placeholder). Needs placing in maps. The trip back to the hub is built (whole server, once nobody is left in the raid: [hub-and-menu.md](hub-and-menu.md)). |
| Spectating and menu | Editor | Built in code: 120 s revive window, then spectate teammates or return to the menu (`ACrossguardGameMode::ReturnToMenu` → `MenuMapName`, "Your raid is over."). Placeholder UI in code ([ui.md](ui.md)): the HUD shows the downed timer and whom you spectate; needs art. |
| Stash | Editor | Built in code: per character, 10 × 30 tabs (up to 3 more at 3000 gold), Skill Gems learnable from it. Placeholder stash screen in code ([ui.md](ui.md)): tabs, drag-and-drop both ways, buy tab, learn gems; F on a stash chest (`ACrossguardStashChest`, a box) in the hub, or `OpenStash`. Needs a local build + `Crossguard.Scenario.UI.StashAndVendor`, the chest placed in the hub map (`editor_setup -AddHubActors` for `L_DevSandbox`) and art. |
| Shared stash tabs | Code, Decision | Subscription benefit: tabs shared by all of an account's characters. Needs accounts and the backend. |
| Hub / lobby map | Editor | Built in code and script ([hub-and-menu.md](hub-and-menu.md)): `editor_setup -CreateHub` makes `L_Hub` (plaza, player starts, stash chest, vendor NPC, party board, raid portal, `ACrossguardHubMarker` so it is a hub without `?Hub`). The raid portal launches the party (leader or solo) like Start raid; `RaidMapName` defaults to `L_DevSandbox` (placeholder). Back to the hub: a finished team leaves the match server for its hub destination (hub server; menu without one) after a 10 s countdown (120 s grace when only disconnected raiders are left; placeholders), saves written first; in development mode the whole server travels back to `L_Hub` instead, parties kept. Needs a local build, `-CreateHub`, `Crossguard.Scenario.Hub.*`, `Crossguard.Scenario.HubReturn.*`, `Net.SeamlessTravelHubToRaid` (there and back), art. |
| Main menu | Editor | Built in code ([hub-and-menu.md](hub-and-menu.md)): `L_MainMenu` (`editor_setup -CreateMenu`, Game Default Map) with Play (host the hub, listen server), Join (address), Settings (sensitivity, FOV, volume; local save slot), Quit. Placeholder UI. Character select waits for per-account saves. Needs a local build, `-CreateMenu`, `Crossguard.Menu.Urls`, `Crossguard.Scenario.Hub.MainMenuBinds`, art. |
| Inventory and loot UI | Editor | Placeholder UI in code ([ui.md](ui.md)), I or Tab: grids for the inventory, Backpack and Quiver, equipment slots, bodies and chests side by side; click to move, equip, take, take all. Needs art (Widget Blueprints) and drag-and-drop. |
| Hotbar, weapon sets and skill wheel UI | Editor | Built in code (keys 1–7, hold Q, E). Placeholder UI in code ([ui.md](ui.md)): hotbar with counts, weapon set line, cooldowns, a ring of skills while Q is held. Needs art: the radial wheel and icons. |
| Drop and pick up single items | Editor | Built in code (G drops the stack in hand, F picks up). Needs drag-to-drop in the inventory UI and item meshes. |
| Revive spell | Decision, Code | Calls the existing `Revive`. Comes from the skills you add in the Skill Forge. |
| Saving progression | Decision, Editor | Built in code ([persistence.md](persistence.md)): XP, attributes, Skill Library, loadout, respec tokens, gold, stash and gear carried between raids, saved on raid start / end, logout and changes between raids; versioned JSON, validated on load, corrupt saves kept aside. Local JSON files on the server for now (`Saved/Crossguard/Characters`). Needs the backend choice (one class: `ICrossguardSaveBackend`) and a local build + `Crossguard.Scenario.Save.*` run. Player ids are only stable with Steam. |
| Reconnect to a raid | Editor | Built in code (ruled): a raider who crashes or logs out mid-raid leaves their character waiting (killable, lootable) and can log in again to take it back, unless it died meanwhile ([persistence.md](persistence.md), "Reconnecting"). Needs a local build + `Crossguard.Scenario.Reconnect.*` and the manual check (kill the client mid-raid, relaunch, rejoin). Needs stable ids: Steam, or the development ids. No "disconnected" marker yet. |
| Steam accounts | Editor | Lasting player ids for guilds, saves and reconnecting: plugin + config steps in [steam-setup.md](steam-setup.md). Development builds have stand-in ids (`pie-<n>`, `dev-<name>`). |
| Loadout timing | Decision | Can the loadout change during a raid, or only between raids? |

## Milestone 2: Combat depth

| Item | Needs | Notes |
| --- | --- | --- |
| Remaining skill parts | Code, Decision | Neckbite for players (needs Beastial forms), Hunter's Tracking hearing range (needs sound), Arcane Arrows dropoff (needs arrow dropoff). |
| Stealth teammate indicator | Editor | Party members see a purple outline around a stealthed ally (with the visual assets). |
| Skill visuals | Editor | Fireball, breath, shield, Blind screen effect, dragon form, skeleton meshes. |
| Passives on others and free-text mechanics | Decision | Audited all 50 skills ([skill-coverage.md](skill-coverage.md)): 47 done, the rest are the parts in the row above. Party auras, on-hit slows, CC immunity and tracking are in C++; data passives now honour the skill's own weapon and armor requirement; a bundle's requirement switches off what it includes; a thrown throwing knife is a dagger attack (Splinter of Winter, Gouge). |
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
| Trading | Editor | Built in code ([trading.md](trading.md)): ask, offer, lock, accept, all-or-nothing swap. Placeholder trade window in code (T, [ui.md](ui.md)); needs art. |
| Auction house | Code, Decision | Needs the backend and a currency. |
| Guilds and guild bank | Code, Editor, Decision | Built in code ([guild-rules.md](guild-rules.md)): ranks, invites, kicks, bank (gold and items) with permissions and a log; rules unit tested. In server memory only: needs accounts (Steam ids) and a backend to save guilds. Placeholder guild window in code (U, [ui.md](ui.md)); needs art. |
| Respec store purchase | Code | Price set: a respec costs 1000 banked gold or 1 respec token (gold payment built: `RequestAttributeAllocation(…, bPayWithGold)`). The store purchase of tokens (real money) still needs the backend: grant tokens after a verified purchase. |
| Subscription | Decision, Code | About $10 per season: five respec tokens, a shared stash tab, common healing items, throwables, lockpicks. |
| NPC vendors in the hub | Editor | Selling is built (`UCrossguardVendorComponent`). Placeholder vendor NPC (`ACrossguardVendorNPC`, a cylinder; F) and sell screen in code ([ui.md](ui.md)): prices, drag to sell, Sell, sell all gemstones; selling only (buying is not ruled). Needs a local build + `Crossguard.Scenario.UI.StashAndVendor`, the NPC placed in the hub map (`editor_setup -AddHubActors`) and a character model. |
| Cosmetics | Decision, Editor | |

## Milestone 5: Online

| Item | Needs | Notes |
| --- | --- | --- |
| Dedicated servers | Code, Editor | **Plan: [dedicated-server-plan.md](dedicated-server-plan.md)** (UE 5.8 source build, `CrossguardServer` for Win64 / Linux, `Tools\build_server.bat`, local cluster `Tools\run_local_cluster.bat`, checklist of listen-server code; task [tasks/setup-source-engine.md](tasks/setup-source-engine.md)). Everything is server-authoritative already. **Owner decision (architecture):** raids run on central dedicated match servers that many teams join, the hub on dedicated hub servers, a backend handles accounts, saves, parties and matchmaking; a player's PC never hosts. In code: a finished team leaves the match server for `GetHubDestination` (backend hub stub, `-HubServer=`, else the menu with a message) while other teams play on; the listen-server flow (menu Play hosting, whole-server travel hub → raid → hub) is development mode only (`Crossguard::IsDevListenMode`: `-DevListenHub` or PIE, never Shipping; tagged `// DEV-ONLY (listen server)`). The leader-hosted hub path is removed. Left: the server target and deployment (plan above), match servers checking the backend's ticket. |
| Matchmaking with parties | Code, Decision | Hub parties and the launch are built ([parties.md](parties.md)): the hub asks `ICrossguardMatchmaking` (`UCrossguardMatchmakingSubsystem`) for a match server and each member's client travels there with `?MatchTicket=<ticket>`. Today: a stub ("not available") and, in development builds only, a fake assignment from `-RaidServer=` with `?Party=<id>`; the whole-server launch is development mode only. Left: the backend's matchmaker (Nakama, [design-decisions.md](design-decisions.md) "Online architecture"), asynchronous requests (queue), match servers that accept only the backend's tickets and take the team from them. Pending local verification: two-PC launch ([next-build-checklist.md](next-build-checklist.md) section 6). |
| Backend and accounts | Code (in progress) | Chosen: Nakama ([design-decisions.md](design-decisions.md) "Online architecture"). **Built (steps 1-2, [backend-setup.md](backend-setup.md))**: local backend in Docker (Nakama 3.41.0 + PostgreSQL, `Tools\run_local_backend.bat`), Go module with server-only save RPCs (versioned writes), session verification and the auth policy (Steam; device ids in development only), unit and end-to-end tests; in the game: the Nakama REST client, `UCrossguardNakamaSaveBackend` (`-SaveBackend=Nakama`), Nakama user ids as player ids verified at login (`?NakamaToken=`, `PreLoginAsync`), the menu's Log in (device id). Unbuilt in Unreal (triage: [first-compile-triage.md](first-compile-triage.md) "Batch 4"). Left: Steam ticket login, session refresh, save leases, parties that outlive a hub server (a team arriving on a hub server gets its team from `?Party=` but not its party yet), matchmaking (`ICrossguardMatchmaking`: match assignment and the hub server for players leaving a match, `FindHubServer`), purchases, auction house. |
| Hub servers | Code, Editor | Dedicated, shared hub servers (sharded instances) that players reach from the menu and that match servers send finished teams to (`HubServerAddress` / `-HubServer=` today, the backend's pick later). Not built: the menu's Play connecting to a hub server through the backend (today Play hosts a listen hub in development mode only and is refused elsewhere), hub instance assignment, rebuilding parties on arrival. |
| Platform store | Decision, Code | Steam or other; purchases verified server-side. |
| Anti-cheat review | Code | Reviewed: every server RPC, ability, console command, debug option and replicated property ([anti-cheat.md](anti-cheat.md)). Fixed the grid-position and attribute-allocation overflows, vendor gold overflow, interact spam, oversized trade offers, loadout changes while dead, and debug options in Shipping. Left: gear affixes are visible to everyone (the stealth position leak is fixed, pending local verification). |

## Open questions

Decisions that block items above. Answers go into design-decisions.md.

| Question | Blocks |
| --- | --- |
| Is a lockpick used up per lock? Exact Dexterity effect? | Lockpicking (currently: used up; 5 s ÷ (1 + Dexterity)) |
| Extraction time | Extraction (currently: 5 s) |
| Final subscription price | Subscription |
| Can the loadout change during a raid? | Loadout timing |
| Materials stack size? Gathering channel time, respawn, yields? | Gathering (currently: stack 20; 2 s, once per raid, 1–3 per node) |
| Which arrows does a bow use first when several rarities are carried? | Ammo (currently: first found) |
| Is drinking a potion instant? Does bandaging slow movement? | Consumables (currently: instant, full speed) |
| Jewelry, quarterstaffs, two-handed pavise shields, the Beastial transformation for Neckbite | Later content |
| Real drop rates: what each monster and chest drops, and rarity odds | Loot tables (placeholders now) |
| More monsters: enter them in the Mob Forge (https://claude.ai/artifact/JPDt46urNPnzMsgZuD7RYW) | Monster AI |

## Placeholders to replace

- Attribute scaling formulas (linear around 10 points).
- Backpack and Quiver item size while unequipped (2 × 2), main grid shape (10 × 5).
- Body grid width (10) and interact range (250 cm).
