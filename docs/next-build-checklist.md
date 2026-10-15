# Next Build Checklist

Everything written since the last editor build, in the order to set up and test it. Tick items off
as you go; anything that fails, paste the error or describe what happened.

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
| Place a Blueprint of `CrossguardLootContainer` with Loot Table Id `chest_common`, press F (`bDebugLootTakesAll`) | It holds 1–3 rolled items (gold, potions, gear with affixes, maybe a Skill Gem) and they move to you. |
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
| Trading (2 clients): both stand close; client 1 console `TradeItems`, `TradeNearest`; client 2 `TradeRespond 1`; each `TradeOffer "<ids>" 0`, `TradeLock 1`, `TradeAccept` | The log shows each step; after both accept, the items swap inventories. Walk 6 m apart mid-trade: it ends (TooFar). |
| Guilds (2 clients, game mode `bIsRaid` off for the bank): client 1 `GuildCreate "Night Watch"`, `GuildInviteNearest`; client 2 `GuildAccept`; client 1 `GuildInfo`, `TradeItems`, `GuildDeposit <id>`; client 2 `GuildInfo`, `GuildWithdraw <slot>` | The log shows the guild, both members and the stack moving; client 2 (Member) is refused the withdrawal (NotPermitted) until client 1 runs `GuildRank <their id> Officer`. |
| `summon_tiger` without a `pet_gem_tiger` in the inventory | Refused; with one (DebugInventory), it works and the gem stays. |
| `spear_two_handed` as `DebugMainHand` | Hits for 33 once every 1.2 s; `triple_thrust` works. |

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

These are called from Blueprint (e.g. a debug widget or the Level Blueprint) until the UI exists.

## 6. Two players (PIE, 2 clients, Listen Server)

- Both see each other's bodies ragdoll.
- `showdebug abilitysystem` on a client shows the other player's `Equipment.Wielding.*` tags.
- Looting the same body at the same time never duplicates an item.
