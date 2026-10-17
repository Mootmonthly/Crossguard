# Next Build Checklist

**Unattended PC session:** [pc-session.md](pc-session.md) is the command-line runbook (build with
compact errors, `Tools\editor_setup.bat` for sections 2–3 without clicking, editor tests, triage).

Rows marked automated (**auto**) in [editor-tests.md](editor-tests.md#checklist-coverage) run via
`Tools\run_editor_tests.bat`; do those manually only if a test fails or is skipped.

Everything written since the last editor build, in the order to set up and test it. Tick items off
as you go; anything that fails, paste the error or describe what happened.

**Automated check first:** with the editor closed, run `Tools\run_editor_tests.bat` in the project
folder. It builds, runs the `Crossguard.*` editor tests headlessly and writes
`Saved\EditorTests\summary.txt` (paste that if anything fails). See [editor-tests.md](editor-tests.md).

## 1. Pull and build

1. Close the editor. In the project folder: `git pull origin foundation/core-rules`.
2. Right-click `Crossguard.uproject` → **Generate Visual Studio project files** (new source files
   were added).
3. Open `Crossguard.sln`, build **Development Editor / Win64**. Paste any errors.

## 2. Data tables

In `Content/Data/` (right-click → **Reimport** for existing ones):

| Table | Source file | Row struct |
| --- | --- | --- |
| `DT_Weapons` (reimport) | `Data/Items/WeaponTable.json` | `CrossguardWeaponRow` |
| `DT_Armor` (reimport) | `Data/Items/ArmorTable.json` | `CrossguardArmorRow` |
| `DT_Items` (new: drag in) | `Data/Items/ItemTable.json` | `CrossguardItemRow` |

On `BP_CrossguardCharacter`, select **InventoryComponent** and set **Item Table** to `DT_Items`.

Reimport `DT_Items` and `DT_Loot` for the gathering materials (`herb_*`, `ore_*`) and the `gather_herb` /
`gather_ore` tables.

Loot: import `Data/Loot/LootTable.json` as `DT_Loot` (row struct `CrossguardLootTableRow`). On
`BP_CrossguardGameMode` → **Crossguard | Loot**, set Loot Tables `DT_Loot`, Weapon / Armor / Affix /
Item tables (`DT_Weapons`, `DT_Armor`, `DT_Affixes`, `DT_Items`) and Skill Catalog `DA_SkillCatalog`.

Mobs: import `Data/Mobs/MobTable.json` as `DT_Mobs` (row struct `CrossguardMobRow`) and set the game
mode's **Crossguard | Mobs → Mob Table** to it, and **Mob Skill Catalog** to `DA_SkillCatalog`. A
placed `ACrossguardMonster` with **Mob Id** `example_brute` takes that mob's stats. Reimport
`DT_Mobs` (new `Skills` column) and `DT_Weapons` (new spear) if they already exist.

## 3. Character settings

If `BP_CrossguardCharacter` has its own **Default Mapping Context** set, clear it (or add the new
keys yourself): the built-in bindings (1–7, Q, E, F, X, G, Left Ctrl) are only created when it is empty.

On `BP_CrossguardCharacter` → **Crossguard | Debug**:

- If you set `Level` on the Blueprint for testing, tick **bUseDebugProgression**; otherwise the
  character starts at level 1 from XP.
- **DebugInventory**: add a few entries to test with, e.g.
  - Category `Ammo`, Item Id `arrow`, Count 45 (expect stacks of 20 + 20 + 5)
  - Category `Consumable`, Item Id `healing_potion`, Count 3
  - Category `Consumable`, Item Id `bandage`, Count 2
  - Category `Currency`, Item Id `gold_coin`, Count 250
  - Category `Trinket`, Item Id `ruby`, Rarity `Rare`
  - Category `SkillGem`, Item Id `heroic_might`
- **DebugMainHand** `bow` to test arrows.
- **DebugReviveDelay** 5 to test revive; **bDebugLootTakesAll** on to test looting without UI.

## 4. Things to test in a raid (game mode `bIsRaid` on, the default)

| Test | Expected |
| --- | --- |
| Add `throwing_knife` and `explosive_bottle` to `DebugInventory`, press their hotbar key, left click | The knife arcs and hits for 15; the bottle explodes for 10 within 3 m, then 3 per second for 5 s (you too, if you stand in it). |
| Press 1 and 2 with weapons in both sets (equip with `RequestEquipItem(..., WeaponSet)`) | After about 1 s of drawing the new set attacks; nothing attacks while drawing. |
| Throw a knife, then left click again | The next knife throws; after the last one the hands are empty until 1 or 2. |
| Shoot the bow 45 times | Arrows run out and the bow stops firing. |
| Press 3 (debug consumables fill the hotbar in order), then left click | A potion heals 15 HP over 10 s; a bandage heals 10 HP while left click is held for 2 s. |
| Kill the dummy | It ragdolls and leaves a body; you get 5 XP (raid XP, shown by `GetRaidXp`). |
| Put gear on the dummy, kill it, press F at the body | With `bDebugLootTakesAll`, its items move into your inventory. |
| Die with `DebugReviveDelay` set | You ragdoll, a body appears, then you stand up with your gear back. |
| Have someone loot your body before the revive | The looted items stay gone. |
| Place an `ACrossguardExtractionZone`, stand in it 5 s | Output Log: extracted, XP banked, gold banked (250). |
| Set the game mode's `PortalUnlockSeconds` to 0, place an `ACrossguardPointOfInterest` around you, press X | A portal appears (give `BeamMesh` a mesh to see it); 60 s later it opens; step in to extract. Walk 10 m+ away while it channels and it collapses. |
| Press X outside a point of interest, or a second time | Refused (Output Log says why). |
| Place a Blueprint of `CrossguardLootContainer` with Loot Table Id `chest_common`, press F (`bDebugLootTakesAll`) | It holds 0–3 rolled items (1–3 rolls, each can roll nothing) (gold, potions, gear with affixes, maybe a Skill Gem) and they move to you. |
| Place an `ACrossguardMonster` (needs a NavMesh Bounds Volume to path), walk within 15 m in its sight | It chases and hits you (12 per 1.5 s); run 30 m from where it stood and it walks back (full HP at home). Shoot it from 40 m: it comes for you. Kill it: 5 XP and a body with a `basic_monster` roll. |
| Set the dummy's Loot Table Id to `boss_monster`, kill it, loot the body | 3–5 extra items on top of what it carried. |
| Place an `ACrossguardDoor` (give it a mesh), press F | It snaps open and closed. |
| Tick the door's Lock → bLocked, press F | Nothing without a lockpick; with the `Ability.Lockpicking` tag it opens after 5 s. |

Skills: put them in `DebugSkills` (Skill Capacity permitting; raise `Level` with
`bUseDebugProgression`), select with Q, cast with E. A second PIE client or a dummy is the target.

| Test | Expected |
| --- | --- |
| `fireball` at the dummy | 50+ damage on hit; standing within 2 m of the blast hurts you for 15. |
| `heal` at a damaged dummy, then at nothing | The dummy heals 20; then you heal 20. |
| `mute` at the second client, who then tries a skill and left click | Skills fail, left click still attacks; 5 damage per second for 10 s. `mute` at nothing: no cooldown. |
| `eldritch_shield`, then take a sword hit | -10 HP on cast; the hit is absorbed (`AttackShield` in `showdebug abilitysystem`). |
| `chop_down` with an axe, then hit the dummy | 110% damage, the dummy slowed for 2 s. A flying target falls. |
| `shooting_star` with a sword, facing the second client | You dash 10 m through them; they take 50 and get `State.Blinded` for 2 s. |
| `dragonification` | Max HP +50%, 1 / 2 and other skills refused; Flare Breath and Flight appear on Q; all gone after 45 s. |
| `flight`: Space / Left Ctrl | You rise and descend; you fall after 20 s. |
| Next-attack: `slash`, `mortal_strike`, `shade_blow` (sword in hand), then left click the dummy | States shows each ready tag; one hit uses them all: +7, +20 Attack, +20 Magic. Kill with Mortal Strike: it is castable again at once. |
| `gouge` (dagger) then hit | The dummy takes 5 per second for 5 s. |
| `shade_blow` hit, then `heal` the dummy | No healing for 5 s (no "healed" log line). |
| `purge_shot` (bow) then shoot the second client | They are Muted 3 s and +10 damage. |
| `cleave` with a sword next to the dummy | About 85% of a normal hit. |
| `parting_the_land` with `sword_two_handed` | The dummy takes 65 and pops up; two walls (boxes) appear for 15 s. |
| `hunters_leap` | You leap forward (5 m at 10 AGI). |
| `wall`, `light` | A box wall 3 m ahead for 15 s; a light over your head. |
| `stealth` with a second client watching | You vanish on their screen for 12 s or until you attack; then +10% speed (`showdebug abilitysystem`). |
| `immutable` equipped, then hit by `chop_down` | No slow. |
| `unleash` | STR / AGI / INT double for 15 s, Max HP follows. |
| `benediction` aimed at a dead body (DebugReviveDelay 0) | It stands up at full HP with its gear. |
| `multiscale` with a party member (second client, same `?Party=`) | Both have +0.15 Attack and Magic Resistance in `showdebug abilitysystem`; gone when unequipped. |
| `hunters_tracking`, second client (enemy) walks around | Small grey marks follow their path on your screen, fading after 30 s. |
| Press 3 with a potion readied, then G; walk to the box and press F | The stack drops as a small box and comes back on pickup. |
| `summon_skeletons` (needs a NavMesh Bounds Volume) | 6 cylinders appear, attack the dummy, follow you otherwise, vanish after 60 s; casting again replaces them. |
| `summon_tiger` (wand in hand), dummy 8 m away | One cylinder; it leaps at the dummy, readies Slash and Neckbite and hits for 20 (+7, +10 Magic). Log shows the hits. Vanishes after 180 s. |
| `summon_greater_demon` near a second client | A bigger cylinder: raises Eldritch Shield, hits for 40 with Mortal Strike / Shade Blow riders. Gone after 60 s. |
| Tiger's Neckbite lands on the second client | For 2 s they cannot cast skills (basic attacks still work); a channel in progress stops. |
| Place a **CrossguardMobSpawner** (Place Actors, search "Mob Spawner") on a NavMesh: Mobs `skeleton_soldier` (weight 3) and `skeleton_archer` (1), Count 3–5, Radius 600 | On Play, 3–5 skeletons appear around it (cylinders; archers keep away and shoot). Kill one: its body stays. With Respawn Seconds 10, a new one appears 10 s later. |
| Place a spawner with `mithril_dragon` | A huge cylinder (scale 6) standing on the ground, not sunk into it; it summons skeletons and a demon that fight you, not other monsters. |
| Block: client 1 `shield_one_handed` off hand + `sword_one_handed`, hold right click facing client 2; client 2 hits with `dagger_one_handed` | Log: "blocked ... 0% through", and client 2 is stunned 1 s (Impact Power 4 vs 1). Turn away and get hit: full damage. |
| Parry: client 1 `axe_two_handed` held right click vs client 2's `sword_one_handed` | Client 1 takes nothing; client 2 takes 100% of an axe hit and a 1 s stun; client 1's next hit gets +50% ADB (log). |
| Monsters guard: hit a `skeleton_soldier` (sword_two_handed, 5) with a dagger | About half your swings: "parried", and you are stunned 1 s and take its sword's damage. |
| Trading (2 clients): both stand close; client 1 console `TradeItems`, `TradeNearest`; client 2 `TradeRespond 1`; each `TradeOffer "<ids>" 0`, `TradeLock 1`, `TradeAccept` | The log shows each step; after both accept, the items swap inventories. Walk 6 m apart mid-trade: it ends (TooFar). |
| Guilds (2 clients, game mode `bIsRaid` off for the bank): client 1 `GuildCreate "Night Watch"`, `GuildInviteNearest`; client 2 `GuildAccept`; client 1 `GuildInfo`, `TradeItems`, `GuildDeposit <id>`; client 2 `GuildInfo`, `GuildWithdraw <slot>` | The log shows the guild, both members and the stack moving; client 2 (Member) is refused the withdrawal (NotPermitted) until client 1 runs `GuildRank <their id> Officer`. |
| `summon_tiger` without a `pet_gem_tiger` in the inventory | Refused; with one (DebugInventory), it works and the gem stays. |
| `spear_two_handed` as `DebugMainHand` | Hits for 33 once every 1.2 s; `triple_thrust` works. |
| Place an `ACrossguardGatherNode` (Place Actors, search "Gather Node") with Loot Table Id `gather_herb` (another with `gather_ore`), press F at it and stand still | After 2 s: 1–3 herbs (Silverleaf or Bloodroot) in your inventory, +10 raid XP, the node (a sphere) disappears. F at the spot again: refused ("depleted" in the log). Reimport `DT_Items` and `DT_Loot` first (new materials and `gather_*` tables). |
| Press F at a gather node, then walk, attack or get hit before 2 s | The channel breaks (log: "interrupted: moved" / "attacked" / "took damage"): no materials, no XP, the node stays. |

Passive skills: put `heroic_might` (or a mastery) in `DebugSkills` and check
`showdebug abilitysystem`: Strength +10 (masteries only while their weapon is held).

## 5. Things to test between raids (set the game mode's `bIsRaid` off)

| Test | Expected |
| --- | --- |
| Right-click a Skill Gem (`RequestUseItem`) | The skill joins the Skill Library; the gem is used up. |
| Stash: `RequestStoreAnywhere`, `RequestRetrieve` | Items move between inventory and stash. |
| Vendor: `RequestSell(Inventory, ruby id)` | +150 gold for a Rare ruby. |
| `RequestBuyStashTab` with 3000+ gold | A second tab appears; a fourth purchase is refused. |
| `RequestAttributeAllocation` lowering a stat | Refused without a respec token. |
| Saving ([persistence.md](persistence.md)): stash something, stop PIE, start again | `Saved/Crossguard/Characters/pie-0.json` exists (the PIE window's development id; `-CrossguardPlayerId=<name>` on the editor command line gives `dev-<name>.json`); the stash, gold, XP and carried gear come back ("Loaded the save" in the log). Automated: `Crossguard.Scenario.Save.*`. |

These are called from Blueprint (e.g. a debug widget or the Level Blueprint) until the UI exists.

## 5b. Main menu and hub ([hub-and-menu.md](hub-and-menu.md))

| Test | Expected |
| --- | --- |
| `Tools\editor_setup.bat -SkipBuild -CreateHub -CreateMenu` | PASS lines for `Hub map /Game/Maps/L_Hub`, `Menu map /Game/Maps/L_MainMenu`, `GameDefaultMap`; `Config\DefaultEngine.ini` has `GameDefaultMap=/Game/Maps/L_MainMenu.L_MainMenu` (commit it). Open `L_Hub` once, Build → Build Paths, save. |
| Play `L_MainMenu` (Standalone Game) | Title, Play / Join / Settings / Quit, cursor on, no pawn. Settings: change all three, Save settings, Play: the view's FOV and mouse speed match; restart: the values are kept. |
| Menu → Play | `L_Hub` loads as a listen server with `?Hub`; stash chest, vendor and party board ("F: Party board" opens the party window) next to the start; the raid portal across the plaza ("F: Enter the raid"). |
| Open `L_Hub` directly (no `?Hub`) | Still a hub: the stash opens (it is refused in a raid). |
| Raid portal alone | Travels to `L_DevSandbox`, a raid (monsters, XP held). As a party member (second PC joined with Join): refused, HUD "Only your party's leader can start the raid."; the leader's F takes both. |
| Die for the raid as a client with no teammate left | The client lands on `L_MainMenu` with "Your raid is over.". |
| Back to the hub: Menu → Play, raid portal alone, extract (an extraction zone, or a portal after the unlock) | HUD "Returning to the hub in 10 s" counting down; then the listen server travels to `L_Hub` (log "server travel back to the hub"); you spawn in the hub with the gear you carried out, the banked XP (extraction included) and gold; the stash opens (a hub). Paste the log lines. |
| Back to the hub with two PCs (PC 2 joined, one party): PC 1 extracts, PC 2 still raids | No countdown while PC 2 raids. PC 2 extracts (or dies for the raid and stays spectating): 10 s, then both are in `L_Hub`, still one party (`PartyInfo`), PC 1 can launch again. Variant: kill PC 2's process mid-raid after PC 1 extracted: "Returning to the hub in 120 s"; relaunching PC 2 within the grace calls it off ("called off"); otherwise PC 1 goes back alone and PC 2's raid failed ("was still away when the raid ended"). |
| A team goes back early (two PCs, three game processes, Development build; [hub-and-menu.md](hub-and-menu.md) "A team goes back early"). PC 1 (team B, the host): `Crossguard.exe /Game/Maps/Dev/L_DevSandbox?listen -game -log -AllowDevPlayerIds`. PC 2, two processes (team A): `Crossguard.exe <PC 1 address>?Party=teamA?DevPlayerId=alice -game -log -AllowDevPlayerIds` and the same with `bob` (alice joins first: she leads). Allow the game through PC 2's firewall. Both on PC 2 extract (or one dies for the raid, the other extracts) while PC 1 still raids | The dead one is not sent to the menu: keeps watching (the body once nobody is left). Only PC 2's two windows show "Returning to the hub in 10 s"; PC 1 sees nothing and keeps raiding. Then alice's window loads `L_Hub` as a listen server; bob's shows "Joining your team's hub in 5 s" and connects to PC 2's address (PC 1's log: "Team N back to the hub: ... hosts the hub ... / joins the leader's hub <PC 2 ip>:7777?Party=teamA"). On alice's hub: both there, `PartyInfo` shows one party with alice leading (log "is back in party teamA"), allies. PC 1's log has no "held for a reconnect" for them. PC 1 then extracts: "Returning to the hub in 10 s", PC 1 alone travels to its own `L_Hub`. Note: PC 2's characters on alice's hub come from PC 2's save files (per-machine saves). Variant with `-HubServer=<address of a running hub>` on PC 1: both PC 2 windows connect straight there. Paste the log lines. Known limitation: across the internet the leader needs port 7777 forwarded. |

## 6. Two players (PIE, 2 clients, Listen Server)

- Both see each other's bodies ragdoll.
- `showdebug abilitysystem` on a client shows the other player's `Equipment.Wielding.*` tags.
- Looting the same body at the same time never duplicates an item.
- Stealth relevancy (`crossguard.StealthHidesFromEnemies` 1, the default). Run with **Play As
  Client** (dedicated server) and three clients, A and B in one party (`?Party=`), C alone; A casts
  `stealth`. C: A disappears completely (no ghost, no footprints, A gone from the World Outliner /
  the client world's actor list after a few seconds; note how many seconds A's position still updated). B: A
  stays in its world (hidden for now; the purple outline comes later). When A attacks or the 12 s
  end, A reappears cleanly on C's screen at the right spot, with gear and tags right (`showdebug abilitysystem`),
  and can be hit; repeat a few times. With `crossguard.StealthHidesFromEnemies 0` on the server, A
  stays in C's world while hidden (the old behavior).
- Parties and the raid launch (two PCs, or two standalone game processes; [parties.md](parties.md)).
  PC 1: start the game with `-RaidMap=/Game/L_Test` (or set the game mode's **Raid Map Name**) and
  `open /Game/L_Test?listen?Hub` (`?Hub` plays the map as the hub, `bIsRaid` off). PC 2
  `open <PC 1 address>`. PC 1 `PartyInfo` (lists names), `PartyInvite <PC 2's name>`; PC 2
  `PartyInfo` shows the invite, `PartyAccept`. Expected: both `PartyInfo` show two members, PC 1
  leading; `multiscale` on one shows the bonus on the other (allies). PC 2 `PartyStartRaid`: refused
  (NotLeader, Output Log). PC 1 `PartyStartRaid`: both load the raid map together, still allies
  (`multiscale`), raid started (XP held until extraction). Note whether the travel was seamless (both
  keep their stash and XP) and paste the Output Log lines with "starts the raid". Optional, with a
  separate raid server: set **Raid Server Address** instead of the map and add a third player not in
  the party: only the party's clients leave.
- Reconnecting mid-raid ([persistence.md](persistence.md), "Reconnecting"). Two separate game
  processes (not PIE: a PIE client cannot be killed on its own), Development build. Server:
  `Crossguard.exe /Game/L_Test?listen -game -log -AllowDevPlayerIds` (raid map, `bIsRaid` on). Client:
  `Crossguard.exe 127.0.0.1?DevPlayerId=alice -game -log`. On the client pick up an item, take some
  damage and kill a dummy (raid XP). Kill the client process (Task Manager → End task). Expected on
  the server: the client's character stays standing where it was ("disconnected mid-raid: character
  held for a reconnect" in the log); you can hit it. After the server notices the drop (up to about a
  minute; "already in this match" until then), relaunch the client with the same command: it gets the
  same character back (same spot, item, Health, raid XP on the HUD; "reconnected to the raid").
  Then: kill the client again, kill its waiting character from the server and wait out the 120 s
  revive window ("died for the raid while away"); relaunching is refused ("Your raid is over.").
  Paste the log lines. Automated: `Crossguard.Scenario.Reconnect.*`.

## 7. Backend step 3 (Nakama: leases, parties, matchmaking; [backend-setup.md](backend-setup.md))

Needs Docker Desktop, `Tools\run_local_backend.bat`, then `python Tools\backend_smoke.py` (72 checks,
"All backend checks passed."). For one party alone set `CROSSGUARD_MATCH_MAX_WAIT_SEC=5` in
`Backend\.env` and run `run_local_backend.bat` again. Then `Tools\run_local_cluster.bat -Backend`.

| Do | Expect |
| --- | --- |
| Look at both server logs | `Nakama: ... backend on`, `registered as hub server local-hub` / `match server local-match`, `Character saves: Nakama at ... save leases as local-hub`, `Parties: in Nakama`, `Matchmaking: Nakama`. |
| Each client: Log in, Play | "Finding a hub server...", then both on the hub (`L_Hub`); no "Log in first" in the hub log. |
| alice invites bob (P window), bob accepts | Both see the party; no "Party ... Nakama did not take it" in the hub log. Console (`http://127.0.0.1:7351`, Storage): `parties`, `party_members` objects. |
| alice: Start raid (or the portal) | "Looking for a raid..." on both HUDs; within the max wait both travel to `127.0.0.1:7778?MatchTicket=...`. Match log: no "refused: no match ticket" / "not valid"; both on one team. Match log may show `save lease of ... not free yet` once, then the load. |
| Launch again while searching | "Your party is already looking for a raid." |
| bob leaves the party while alice's search runs | "Raid search called off: your party changed." |
| Both extract | Back on `L_Hub` with the party (P window: same members, alice leading). No `version conflict` / `lease lost` in any log. |
| After logging in, menu Join `127.0.0.1:7778` (the match server directly) | Refused: "No match ticket: start the raid from the hub." |
| End the hub process (Task Manager) with players on it, start it again, Play | The players load after at most ~60 s ("not free yet" until the dead hub's lease expires). |

## 8. Guilds on Nakama, Steam login, session refresh ([guild-rules.md](guild-rules.md) "On Nakama", [steam-setup.md](steam-setup.md))

Build first (triage "Batch 5", GN1-GN14). Headless: `Crossguard.Nakama.GuildState`, `GuildBankWrite`,
`Session` (and the in-memory guild scenarios as before). Then `Tools\run_local_backend.bat` (rebuilds the
module), `python Tools\backend_smoke.py` ("All backend checks passed."), `Tools\run_local_cluster.bat -Backend`,
both clients Log in, Play.

| Do | Expect |
| --- | --- |
| Hub log at start | `Guilds: in Nakama (refreshed every 10 s)`. Nakama log: `Crossguard module loaded (... Steam login off ...)` (or on, with keys). |
| alice: guild window (U), create "Iron Wolves" | The window shows the guild at once after the answer; Nakama console → Groups: "Iron Wolves" with alice; Storage: `guilds`, `guild_names`. |
| alice invites bob, bob accepts | bob sees the roster; console → Groups → members: alice superadmin, bob member. |
| alice promotes bob (officer) | Console: bob admin. |
| bob deposits 100 gold and a stack (between raids) | Banked gold and inventory change at once; bank shows them; no `Guild ... not written` in the hub log. Console → Storage `guild_bank_log`: two entries. |
| alice withdraws the stack, bob withdraws 50 gold | Each gets it; the bank shows the rest. |
| Close the hub server (Task Manager) and start it again, both Play | The guild, ranks and bank are back (from Nakama). |
| Raid and back (Start raid, extract) | Guild still there on the hub; the bank was refused during the raid (`OnlyBetweenRaids`). |
| A member (not an officer) tries to withdraw | "Guild: refused (NotPermitted)"; nothing moves. |
| Wait 2 h logged in (or set `session.token_expiry_sec: 360` in `Backend/nakama/local.yml` and rerun `run_local_backend.bat`) | Client log: `Nakama: session refreshed` about 5 min before expiry; a raid launched afterwards still joins (no "Log in first"). |
| **OWNER**, Steam (after [steam-setup.md](steam-setup.md) steps 1-3): Standalone with Steam running, Log in | "Logging in with Steam...", "Logged in as ..."; client log `logged in as ... with Steam`; Nakama console → Accounts: the Steam id. If it fails with 401 after 3 tries: triage GN2. |
