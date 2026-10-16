# Crossguard Design Decisions

Rulings that settle conflicts between the Google Drive design docs
("Crossguard Game Design Document", "Crossguard Progression", "Crossguard Skills",
"Crossguard Stats"). Where this file and a Drive doc disagree, this file wins.
Code and data must follow it.

## Engine

- Unreal Engine 5, starting from scratch. Skills, attributes and effects are built on the
  Gameplay Ability System (GAS).
- Pure game rules (formulas, progression, loadout validation) live in the engine-free
  `CrossguardRules` module so they can be unit-tested outside the editor.

## Naming

- Skills are obtained as **Skill Gems** (not "skillbooks").
- Stat names and abbreviations come from the "Crossguard Stats" doc only. Older names in
  other docs map as follows:

| Old name in other docs | Canonical |
| --- | --- |
| ADR | AR (Attack Resistance) |
| MDR | MR (Magic Resistance) |
| Attack Bonus | ADB (Attack Damage Bonus) |
| Magic Bonus | MDB (Magic Damage Bonus) |
| Weapon Damage | AAD (Additional Attack Damage) |
| Max HP Bonus (MHB) | MHPB |

- **MHB** means Magical Healing Bonus only. Max HP Bonus is **MHPB**.
- **MR** is a % modifier to incoming **Magic** Damage (the Stats doc said "Attack Damage"; typo).

## Experience

| Source | XP |
| --- | --- |
| Basic monster kill (skeleton, goblin, ...) | 5 |
| Boss monster kill (dragon, ...) | 50 |
| Player kill | 25 |
| Quest | 10–100, set per quest |
| Gathering a node (herb, ore) | 10 |
| Crafting a piece | 10 × (rarity level + 1): Common 10 … Mystic 60 |
| Enchanting a piece | 10 × (rarity level + 1): Common 10 … Mystic 60 |
| Successful extraction | 35 |

- Level 1 → 2 takes 50 XP; each later level takes 10% more than the one before
  (`50 × 1.1^(level − 1)`, rounded to the nearest whole XP).
- XP earned in a raid is held until the raid ends. Extracting banks it; failing to extract loses
  it. Dying does not lose it by itself: a teammate can revive you and you can still extract.
- Respec: reassign all attribute points. Price (owner): **1000 banked gold or 1 respec token**; the
  player chooses which to pay with (`FEconomyConfig::RespecGoldPrice`). Respec tokens are bought
  with **real money** (revenue beside cosmetics; each purchase grants one) or come with the
  subscription. Purchases must be verified server-side through the platform store before a token
  is granted (the store is not built: it needs the backend).
- Assumed until ruled otherwise:
  - Attribute points are assigned, and respecs done, only between raids (levels only change then).
  - Killing yourself or an ally gives no XP.
  - XP keeps accumulating at max level, so raising the cap mid-season levels characters up.
  - Quest XP outside 10–100 is clamped into that range.

## Levels, attributes and Skill Capacity

- Attributes: STR, AGI, INT. There is no fixed baseline per attribute.
- A level 1 character has **30 attribute points** to assign freely. No minimum or maximum
  per attribute. (30 spread evenly gives the 10/10/10 reference point the stat formulas use.)
- Each level grants **+3 attribute points** and **+1 Skill Capacity**.
- Level 1 has **6 Skill Capacity**.
- Max level at season start is **15**: 72 attribute points and 20 Skill Capacity.
  Max level (and so max Capacity) rises during a season.
- Each Skill has a Capacity cost (the number in parentheses in the Skills doc). Equipped
  skills reserve their cost; the loadout total cannot exceed the character's Capacity.

## Skills

- Skill definitions come from the "Crossguard Skills" doc, except where the "Crossguard Stats"
  doc has a newer version of the same skill. The Stats doc version wins, including its cost:
  - Dagger Mastery (2), Splinter of Winter (2), Triple Thrust, Spear of Liscor,
    The Mithril Knight ("Plate Armor in every slot").
  - **Shard of Winter is removed**; Splinter of Winter replaces it.
- **Bundle skills** (Splinter of Winter, Spear of Liscor, The Mithril Knight, ...) include the
  full effects of the skills they name plus their own effects, for their own Capacity cost.
- **The loadout blocks** equipping a bundle together with any skill it contains.
- Two bundles that share a component (Spear of Liscor and The Mithril Knight both include
  Antimagic Garb) **can** be equipped together. The shared component applies once.
- Skills granted while another skill is active (Dragonification's Flare Breath and Flight)
  are usable during it, and **cannot be equipped** alongside it.
- Cleave is an **Art**.
- **Every Spell requires a casting weapon**: a Wand or a Two-Handed Staff (not a quarterstaff), in
  either hand. Exception: in dragon form, Flare Breath and Flight are cast without one. Casting
  weapons do not make a skill Melee or Ranged for CDR.
- **Skill Gems have a rarity** (Common to Mystic), one per gem, no affixes. It only sets loot drop
  chances and never changes the skill. Set per skill in the Skill Forge. A loot table's "any Skill
  Gem" rolls a rarity, then a skill of that rarity; with none at that rarity it falls back to a
  lower one first, so a missing rarity never makes a drop rarer.
- Fireball, Flare Breath, Greater Heal, Benediction and Summon Tiger are **Spells**.
  Greater Heal and Benediction are Magical Healing.
- Mastery cooldown reductions ("Melee Skills -10% Cooldown") apply **only while wielding**
  the mastery's weapon.
- Mortal Strike requires a melee weapon. Arcane Arrows requires a Bow.
- A Passive's own requirement (Armor Mastery: Plate; The Mithril Knight: Plate in every slot; Spear
  of Liscor: a Spear in hand) works like a mastery's weapon: the skill can be equipped without it, and
  its own bonuses count only while the requirement is met. **A bundle's requirement also switches off
  the skills it includes**: The Mithril Knight without Plate in every slot gives no Multiscale,
  Immutable (neither its immunity nor its activation) or Antimagic Garb; Spear of Liscor without a
  Spear gives no Antimagic Garb. They come back on as soon as the requirement is met again (checked on
  every equipment change). A component shared by two bundles is on while either bundle's requirement is
  met. A skill cannot be equipped on its own next to a bundle that includes it (see above), so that
  case does not arise. AI characters skip requirements. Per-skill status:
  [skill-coverage.md](skill-coverage.md).

### Skill numbers filled in

| Skill | Ruling |
| --- | --- |
| Gouge | Cooldown 7 s |
| Shade Blow | Cooldown 10 s |
| Arcane Arrows | Cooldown 25 s |
| Neckbite | Cooldown 7 s. Next melee attack +10 Magic Damage and a 2 s Silence. For now only Summon Tiger's tiger has it; Beastial forms for players come later. |
| Parting The Land | 65 Attack Damage, coefficient 1.0 |
| Chop Down | Slow: -50% Movement Speed for 2 s |
| Hunter's Leap | 0.5 m per point of AGI (5 m at 10 AGI) |
| Eldritch Shield | Lasts 12 s; absorbs Attack Damage only |
| Stealth | Exit bonus triggers whenever stealth ends, including when the 12 s run out |
| Purge Shot | +10 Magic Damage; its Mute disables passive skills too |
| Hunter's Tracking | Always on (Passive) |
| Armor Mastery | Multiplies AR by 1.10 (30% AR becomes 33%) |

### First playable skills

Rulings for the first skills built in C++ (defaults proposed and accepted, plus the changes noted).

| Skill | Ruling |
| --- | --- |
| Fireball | Flies at **30 m/s** where you aim. 50 (0.5) Magic Damage to what it hits, 15 to everyone else within 2 m, you included. Explodes after 10 s if it hits nothing. |
| Heal | 20 Magical Healing to whoever is under the crosshair within 15 m, **ally or enemy**; yourself if nobody is there. |
| Mute | Target under the crosshair within 20 m. 10 s: no skills, passives off, 5 Magic Damage per second. **Basic attacks still work.** With nobody there it fails and the cooldown does not start. |
| Eldritch Shield | Lose 10 HP (never below 1); a 12 s barrier absorbs 45 Attack Damage. Magic Damage passes through. |
| Chop Down | Readies the next melee axe attack for 10 s. That attack deals 110% weapon damage, slows everyone it hits by 50% for 2 s, and Grounds flying targets (their Flight ends). Used up hit or miss. |
| Shooting Star | Dash 10 m in 0.3 s through characters (not walls); 50 Attack Damage to everyone in the path, once each. Everyone within 30 m looking at you (within 45°, line of sight) is Blinded 2 s, **allies included**. |
| Dragonification | 45 s: +50% Max HP (Health keeps its percentage), +25% Attack and Magic Resistance, Flare Breath and Flight granted. Weapons are put away and cannot be drawn; other skills cannot be cast. Passives keep working. |
| Flare Breath | 3 s, 6 m, 60° cone where you aim. 40 (0.5) Magic Damage the first time someone is caught, then 25 (0.5) per second while inside (every 0.25 s). |
| Flight | Spell (Skill Forge change). 20 s. Space rises, Left Ctrl descends, fly speed = walk speed. You fall when it ends, when Dragonification ends, or when Grounded. |
| Summon Skeletons | 5 Warriors (40 HP, 10 Attack Damage per second in melee) and 1 Mage (30 HP, Fireball every 8 s, stops 8 m from its target) for 60 s. Recasting dismisses the old ones. Targets: nearest enemy player within 30 m, else nearest enemy non-player, else follow you. On your team; their kills give you the XP; killing them gives none. |

| Slash, Mortal Strike, Shade Blow, Gouge, Purge Shot | "Next attack" skills: ready for 10 s, used up by the next qualifying attack hit or miss: a melee attack with the skill's own weapon (Slash: sword or dagger, Shade Blow: sword or axe, Gouge: dagger, a thrown throwing knife included, Mortal Strike: any melee); Purge Shot: a bow or crossbow shot. Several readied at once all apply to that attack. Mortal Strike's cooldown comes back on a kill. Shade Blow blocks all healing (potions too) for 5 s. Gouge: 5 Attack Damage per second for 5 s and -5% speed. Purge Shot: +10 Magic Damage and a 3 s Mute. |
| Greater Heal | As Heal, 55. |
| Benediction | Target under the crosshair within 15 m is healed to full, ally or enemy. Aimed at a body still in its revive window: a full revive (unlooted gear back); if both a character and a body are in line, the nearer one. Nobody: yourself. Shade Blow's heal block stops the heal but not a revive. |
| Quickshot, Arcane Arrows, Unleash | Self buffs. Unleash doubles STR / AGI / INT (gear and buffs included) for 15 s. Arcane Arrows' dropoff part waits until arrows have damage dropoff. |
| Cleave | 85% of the main-hand weapon's damage to everyone in a 3 m, 120° arc in front, allies too. |
| Triple Thrust | Three thrusts 0.15 s apart, each spear damage +15, 2.5 m reach. Piercing: a hit a guard would stop deals 50% instead. |
| Parting the Land | 8 m × 2 m strip ahead: 65 Attack Damage and a knock-up to everyone in it; a 15 s Wall along each side. |
| Hunter's Leap | Art (Skill Forge change). 0.5 m per AGI point forward, 1.5 m high, 0.6 s. |
| Like A Lion, He Leapt | (Skill Forge.) Leaps at the nearest enemy within 30 m: players first, then monsters and minions; stealthed enemies are skipped; needs line of sight (walls and other world geometry block it, characters do not). Lands 1.5 m short. STR ×1.2 for 6 s. No enemy in range: fails, no cooldown. |
| Light | A light over your head for 120 s; recasting replaces it. |
| Wall | 4 m wide, 3 m tall, 0.5 m thick, 3 m in front, across your view. Indestructible, 15 s. |
| Stealth | Invisible to everyone outside your party (and to minions) for 12 s. Attacking, casting or taking damage ends it early. The exit bonus (+10% speed, +25% Attack Bonus for 3 s) applies however it ends, except death or Mute. Later (with visual assets): party members see a purple outline around you while you are stealthed. Anti-cheat: enemies' clients do not receive a stealthed player's position at all (the character despawns there); no reveal radius. Enemies lose it at once when stealth starts (the server closes their channel instead of waiting out the engine's 5 s relevancy timeout; owner's fix A). Placeholder switch `crossguard.StealthHidesFromEnemies`, default on, until the two-client play test. Assumed: classic net driver (no Iris or Replication Graph); the test's 0.5 s limit is a placeholder. |
| Immutable | Crowd control = Slow, Blind, knock-up, Grounding, Silence, Stun, Root. **Mute is not crowd control.** Equipped (and not Muted): permanently immune. Activated: you and party members within 10 m immune for 5 s. Existing crowd control is not removed. |
| Splinter of Winter | Dagger attacks: +5 Magic Damage and -10% speed for 3 s. A thrown throwing knife is a dagger attack (not a throwing axe). |
| Multiscale | Requires Plate armor (Skill Forge). +15% Attack and Magic Resistance to you, every party member and party minions, at any distance. Two Multiscales do not stack. Off while you are Muted or dead. Also comes with The Mithril Knight. |
| Silence | (Crowd control.) A Silenced character cannot activate skills; a skill already running is interrupted. Basic attacks and passives keep working (unlike Mute, which also switches passives off). Affects players, minions and monsters alike. |
| Root | (Crowd control.) A Rooted character cannot walk, jump or use movement skills (Hunter's Leap, Like A Lion, Flight); attacks, guarding and other skills still work. Movement Speed × 0 for the duration. **Assumed** (no skill applies it yet): flying when rooted is not cancelled; rooting a moving leap does not stop it. |
| Summon Tiger | A tiger for 180 s: 100 HP, 20 Attack Damage claw attacks, 1.3 a second (one every 0.77 s). Skills Hunter's Leap, Neckbite, Slash. Its claws count as any melee weapon for Slash. Needs a Pet Gem (Tiger) (`pet_gem_tiger`, an item) in the inventory; it is not used up. |
| Summon Greater Demon | A demon **3× the player's size** for 60 s (Assumed: 3 m reach, centre to centre, kept from the old 1.5× size; it spawns standing on the summoner's floor): 220 HP, a two-handed sword dealing 40 Attack Damage once every 1.4 s (the sword's speed). Skills Mortal Strike, Sword Mastery, Eldritch Shield, Shade Blow. |
| Minion and monster skills | Mobs can have any skill but Flight (AI cannot fly) and Hunter's Tracking (a player's screen effect); bundles grant their skills. Next-attack skills only suit the role whose attack carries them (melee ones Warriors, Purge Shot and Arcane Arrows Ranged). An AI uses an active skill as soon as it is ready and its target is in that skill's range, aimed straight at the target; its skills come with it: weapon, armor, form and casting-weapon requirements do not apply (Multiscale needs no Plate). Recasting a summon replaces the previous one. When each skill fires (assumptions): next-attack skills within reach + 2 m; Hunter's Leap and Like A Lion at a visible target beyond 1.5 reaches but within the leap; Fireball, Mute (not on the Muted), Parting the Land, Flare Breath, Shooting Star (3–9 m) at a visible target in range; Cleave and Triple Thrust within their reach; Eldritch Shield, Unleash, Immutable within 10 m, Quickshot 15 m, Dragonification 15 m, summons 20 m, Light 30 m; Stealth to sneak in from 15 m; Wall (Mage and Ranged only) when a target is within 6 m; Heal and Greater Heal on itself at 60% Health, Benediction at 35% (it revives nobody). An AI's Like A Lion leaps at its own target. Next-attack skills are only readied when its weapon suits them (natural attacks suit all). A mob's attacks break its Stealth, and Action Speed (Quickshot) shortens its attack interval. |
| Monster team | Monsters, and minions they summon, are one team: allies of each other (party skills such as Immutable and Multiscale reach other monsters; a monster's Multiscale only within 30 m), never targets for each other. |
| Crowd avoidance | (placeholder, Assumed) Monsters and minions steer around their own side's monsters and minions instead of walking into their backs: RVO avoidance on the movement component, consideration radius 3 m, weight 0.5. Chosen over Detour crowd because it also works on the straight-line walk used without a NavMesh and needs no controller change. Enemies are not avoided (an AI has to close in to fight). Players are not avoidance agents: their movement is untouched and AIs do not steer around them. Sides share 31 avoidance groups (team id, or a teamless summoner's own minions); two sides landing in one group also avoid each other. Dead ones are not avoided. |
| Mob weapons | A mob may hold a weapon (Mob Forge): masteries and weapon-type skills work with it; its damage stays its Attack Damage. Warriors: no bows, crossbows or slings; Ranged: a bow, crossbow or sling (none = a bow); no shields. |
| Armor drops by type | A loot table's "any armor" drop can be limited to Cloth, Leather or Plate. Skeleton soldiers drop any plate, archers any leather, mages any cloth piece (weight 10, at each mob's rarity odds). |
| Hunter's Tracking | Footprints of enemy players from the last 30 s, on your screen only, for enemies your client knows about (about 150 m). Stealthed enemies leave none. Off while Muted or dead. The +20 m hearing waits for the sound system. |

Dropped items land about 1.5 m ahead (never through a wall) and lie there until picked up (interact key) or the raid ends; a pickup takes what
fits and leaves the rest.

All skills end on death.

### Skill categories

Each skill has exactly one category. Cast time and CDR stats are split by category.

| Category | Meaning | Cast time / CDR attribute |
| --- | --- | --- |
| Active | Activated skill that is neither a Spell nor an Art | STR |
| Spell | Tagged "Spell" | INT |
| Art | Tagged "Art" | AGI |
| Passive | Never activated | n/a |

### Weapon tags

Independent of category, a skill is:

- **Melee** if it requires a melee weapon (swords, spears, daggers, shields, axes, maces, ...).
- **Ranged** if it requires a ranged weapon (bows, crossbows, throwing weapons, ...).
- Neither, if it has no weapon requirement (e.g. Dragonification).

"Melee Skills -10% Cooldown" style effects apply to skills with the matching weapon tag.

## Inventory, death and Skill Gems

- Inventory is a grid; items take up space (Dark and Darker style). The raid inventory has 50
  squares (10 × 5, placeholder shape).
- Item sizes (width × height, rotatable): dagger and wand 1 × 2; one-handed sword and axe 1 × 3;
  shield 2 × 2; spear 1 × 5; other two-handed weapons and bows 2 × 4; helm, gloves, boots, cape 2 × 2; chest and legs
  2 × 3; Skill Gems, potions and bandages 1 × 1; throwables (explosive bottles, throwing knives and
  axes, javelins) 1 × 3.
- Arrows are 1 × 3 and Bolts 1 × 2, each stacking up to 20 in the same squares. Throwables stack
  up to 3, consumables (potions, bandages) up to 5.
- Healing Potion: restores 15 HP over 10 s, +5 HP per rarity level. Bandage: restores 10 HP over a
  2 s channel (hold left click). Both are readied in hand first, then used with left click; after a
  use the hands go back to the weapon.
- Assumed until ruled otherwise: drinking a potion is instant (the healing runs on its own);
  releasing a bandage early stops its healing and the bandage is still used up; healing never
  revives a dead player; a bow shoots whichever arrows it finds first (Quiver first), any rarity.
- Stacks only combine items of the same type **and** rarity: a Common health potion does not stack
  with a Rare one.
- Placeholder: Backpack and Quiver items take 2 × 2 while not equipped.
- Backpacks and Quivers are extra containers that add 5–30 squares by rarity, in steps of 5
  (Common 5 … Mystic 30). Quivers only hold Arrows and Bolts.
- On death, all equipped gear and the raid inventory drop. No insurance for now.
- The body is a container: walk up, press the interact key (F) and take items from it, like opening
  a treasure chest. Anyone can loot it, teammates included. Items can also be put into it.
- Every death leaves a visible body, even with nothing on it: seeing bodies is information.
- A downed player can be revived for 120 s; after that they are dead for the raid (it counts as a
  failed extraction). Dead players spectate their remaining teammates; a solo player, or one with no
  teammates left in the raid, goes back to the menu.
- Revived players get back whatever is still on their body (worn gear goes back on). Anything
  looted while they were down is gone unless they get it back from whoever took it.
- Skill Gems are items. Only between raids, right-clicking a gem in the inventory or stash learns
  its skill permanently into the character's Skill Library and consumes the gem. A skill already
  in the library cannot be learned again (the gem is kept). Gems can also be sold on the auction
  house, deposited in a guild bank, or traded.
- The loadout can only equip skills in the Skill Library.
- Stash: between-raid storage, per character. One tab is 10 × 30 squares. Up to 3 more tabs per
  character, 3000 gold each.
- Gold is the in-game currency. It comes from selling gear and trinkets found in raids to NPC
  vendors, and from loot on monster corpses and in chests.
- Gold found in a raid is carried as coins (1 × 1, 100 per stack) and lost on death like any item.
  Extracting with coins moves them into the character's banked gold automatically. Selling to a
  vendor pays banked gold.
- Gemstones (Ruby, Sapphire, Emerald) are 1 × 1 trinkets selling for 50 gold at Common, +50 per
  rarity level (300 at Mystic). They stack **up to 3 per kind per rarity** (three Common rubies in
  one stack; a Rare ruby or a sapphire starts its own).
- Vendor prices for gear (weapons and armor): 5 gold per grid square at Common, +5 per rarity
  level: `5 × squares × (rarity level + 1)`. A 2 × 4 Mystic weapon sells for 240. Placeholder for a
  richer formula later.
- Assumed until ruled otherwise: vendors are used between raids only; a whole stack sells at once
  (a stack of 3 Common rubies: 150 gold).
- Lockpicking: the best chests and doors need it, through a skill or a lockpick item. It takes 5 s,
  faster with Dexterity, and cannot fail. The lockpicking skill comes from the Skill Forge.
- Assumed until ruled otherwise: time = 5 s ÷ (1 + Dexterity) (30% Dexterity: 3.8 s); a lockpick
  item is used up per lock picked, the skill needs none; lockpicking continues while you stay in
  range, and pressing the interact key again stops it.
- The lockpick item is 1 × 1 and stacks to 3.
- Guild banks: a shared stash per guild; every member deposits, only officers withdraw, manage or
  redistribute.

## Subscription

- Planned price: about $10 per season (not final).
- Each season: five respec tokens, a shared stash tab (usable by all of the account's characters),
  several stacks of common healing items, some throwables and lockpicks.

## Extraction

From the design doc: after 10 minutes, players can summon an extract portal anywhere inside a
point of interest instead of only using fixed extraction points. Ruled details:

- Unlocks 10 minutes after the raid starts, the same for every player.
- Only inside a point of interest. One portal per player per raid.
- The portal channels for 60 s where it was summoned, with a beam of light into the sky that nearby
  players can see. The summoner can move around and defend it; damage does not interrupt it. The
  channel needs the summoner's side within 10 m.
- Once open it lasts 30 s. Anyone can step in and extract, enemies included.
- Fixed extraction points also exist.
- "Within 10 m" is satisfied by any living member of the summoner's team (the summoner alone when
  solo); the channel collapses only when none are left.
- A collapsed portal still uses up the player's one summon for the raid.
- Summoning is on X for now.
- Assumed until ruled otherwise (placeholder until the trip to the hub exists): an extracted
  character leaves the raid at once. Its gear is saved as carried out, then the character is removed
  from the world (nothing stays behind, unlike a death's body); it can no longer pick up, loot, open
  or trade, and keeps no portal alive. The player spectates a teammate still in the raid; with none
  left they stay where they extracted (not sent to the menu, unlike a solo death).

## Gathering

Herb and ore nodes placed in raid maps (`ACrossguardGatherNode`). Ruled: gathering a node gives
**10 XP** (`FXpConfig::GatherNode`; in a raid it is held until extraction like all raid XP).
Everything else is a placeholder until ruled; the numbers live in `Crossguard::FGatherConfig`
(`CrossguardRules/Gathering.h`) and on each placed node, the yields in `Data/Loot/LootTables.json`:

- F at a node starts a **2 s channel** (placeholder). It breaks if the gatherer moves more than
  30 cm (placeholder slack), takes any damage, attacks or uses a skill or the guard (any non-passive
  ability), dies, or leaves the node's 2.5 m interact range. F at the node again stops it. One
  gatherer at a time (placeholder). Throwing an item is not an ability and does not break it yet.
- On completion the node's **loot table** is rolled into the gatherer's inventory: `gather_herb`
  (Silverleaf ×1–3 or Bloodroot ×1–2) and `gather_ore` (Iron Ore ×1–3 or Mithril Ore ×1), one roll,
  never nothing, always Common (placeholder weights and counts). What does not fit drops on the
  ground at the node, like a dropped stack (placeholder).
- The node is then **depleted** (its mesh hides) for the rest of the raid: `RespawnSeconds` 0 =
  once per raid (placeholder). A node with `RespawnSeconds` > 0 comes back after that long.
- **Materials** (placeholder): Silverleaf, Bloodroot, Iron Ore, Mithril Ore, category Material,
  1 × 1, stack 20 (materials stack size is not ruled), sell 5 / 10 / 5 / 15 gold, nothing extra per
  rarity. They are for crafting, which is not designed yet.

## Controls

| Key | Action |
| --- | --- |
| WASD, mouse, Space | Move, look, jump |
| Left click | Attack with the main hand; use the readied hotbar item |
| Right click | Off-hand attack, block or parry |
| 1, 2 | Switch between the two weapon sets (each a main hand and an off hand) |
| 3–7 | Hotbar: consumables and throwables assigned to these keys |
| Hold Q | Skill wheel: hover an activatable skill (Active, Spell or Art) to select it |
| E | Activate the selected skill |
| F | Interact (bodies, chests, doors) |
| X | Summon an extract portal (placeholder key) |
| Space / Left Ctrl | Rise / descend while flying ([Flight]) |
| G | Drop the stack in hand (placeholder until the inventory UI has drag-to-drop) |
| I or Tab | Inventory screen (placeholder key) |
| T | Trade: ask the nearest player, or open the trade window (placeholder key) |
| U | Guild window (placeholder key) |
| Esc | Close the open windows (placeholder key) |

- Drawing a weapon set (1 / 2) takes 1 s, faster with Dexterity (`1 s ÷ (1 + Dexterity)`); an
  animation will play here. Nothing attacks while drawing.
- A hotbar key takes its item in hand (weapons are put away); left click uses or throws one. The
  hands stay on that item while any of it (same type and rarity) is left, so players can throw again
  after a miss or bandage repeatedly; when it runs out the hands are empty until a weapon set is
  drawn. Pressing the item's key again empties the hands.
- Hotbar slots remember the item type and rarity, so a new stack of the same item fills the slot.
- Assumed until ruled otherwise (placeholder UI, [ui.md](ui.md)): while a window (inventory, trade,
  guild) or the skill wheel is open, the mouse cursor shows and mouse look stops; the keys still work
  (walking, hotbar, casting). Hovering a skill on the wheel selects it at once. Opening a body or
  chest opens the inventory beside it; closing the inventory closes the body or chest. Moving items
  is click an item, then click a square (no drag-and-drop yet).

## Throwables

| Item | Effect |
| --- | --- |
| Throwing Knife | 15 Attack Damage, +2 per rarity level. Counts as a Dagger. |
| Throwing Axe | 25 Attack Damage, +2 per rarity level. Counts as an Axe. |
| Explosive Bottle | Shatters where it lands: 10 Magic Damage to everyone within 3 m, then 3 Magic Damage per second for 5 s in that area. |

- All are 1 × 3 and stack to 3. Friendly fire applies, the thrower included.
- "Counts as a Dagger / Axe" while in hand: mastery bonuses apply to the throw, on-hit skill
  effects of that weapon type trigger on its hits, and skills requiring that weapon can be used.
- **A thrown throwing knife is a dagger attack**: Splinter of Winter's +5 Magic Damage and slow ride on
  it, and it uses up a readied Gouge (hit or miss) like a dagger stab. Assumed (placeholder): other
  next-attack skills still need a melee attack or a bow shot (Slash, Mortal Strike, ...), and a throw
  does not use up a parry's riposte.
- Weapon stats, affixes, masteries and weapon requirements count only for the weapon set that is
  drawn and in hand (from the end of the draw). Put-away and stowed weapons give nothing. Throwables
  are not in weapon sets and count only while in hand. Armor always counts.

## Camera

- First-person camera.

## Teams and friendly fire

- Players enter a match solo or in a premade team of **up to 8**. Matchmaking passes the party
  key as the `?Party=<key>` travel URL option; the game mode puts everyone with the same key on
  one team. Without a key, a player is a team of one.
- **Friendly fire is always on.** This overrides the design doc's "no friendly fire". Teams only
  decide who counts as an ally (party skills like Multiscale, UI).
- Idea, not built: an AGI skill "Disable Friendly Fire".
- In-game parties (details: [parties.md](parties.md)): players form a party in the hub; the party
  id is its `?Party=` key, so members are on one team (allies) at once. The leader launches the
  raid. **(placeholder / Assumed)**: only the leader invites, kicks and promotes; inviting while
  solo founds the party; invites expire after 60 s; a leaving leader hands over to the
  longest-standing member and the last member disbands it; membership changes and the launch only
  in the hub (`bIsRaid` off); logging out of the hub leaves the party; a solo player may launch
  alone. Raid launch **(placeholder until a matchmaker exists)**: when the whole hub server is the
  party, the server travels to the game mode's `RaidMapName` with `?Party=`; otherwise each
  member's client travels to `RaidServerAddress` with `?Party=` (refused if a listen host would
  have to leave). Parties live in hub memory only. Until a hub map exists, `?Hub` on a map URL plays
  that map as the hub (placeholder).

## Items and weapons

- Rarities: Common, Uncommon, Rare, Epic, Legendary, Mystic (levels 0–5). Rarity raises a
  weapon's AD/MD (or a shield's AR) and its affix slots.
- Prototype weapons are classes, not individual weapons: `Data/Items/Weapons.json`.

| Weapon | Value at Common | Per rarity level | Attack speed |
| --- | --- | --- | --- |
| One-Handed Sword | 30 AD | +2 | 1 per second |
| One-Handed Dagger | 17 AD | +1 | 3 per second |
| One-Handed Axe | 27 AD | +2 | 1.2 per second |
| One-Handed Shield | 8% AR | +2% | does not attack; melee weapon |
| Two-Handed Sword | 37 AD | +3 | once every 1.4 s |
| Two-Handed Axe | 35 AD | +3 | once every 1.2 s |
| Spear | 33 AD | +2 | once every 1.2 s |
| Bow | 30 AD | +2 | 1 per second; projectile |
| One-Handed Wand | 15 MD | +2 | 1.2 per second; hitscan |
| Two-Handed Staff | 30 MD | +3 | once every 1.3 s; slower projectile |

- Action Speed divides the attack interval: interval ÷ (1 + Action Speed).
- **Dual wielding**: any one-handed weapon fits the off hand. Left click attacks with the main
  hand, right click with the off hand. With a shield in the off hand, right click blocks. With the
  off hand empty, right click parries if the main-hand weapon can parry: every two-handed weapon
  except bows, and selected one-handed weapons (none chosen yet). See Block and parry below.
- A two-handed weapon cannot be used with a shield (two-handed pavise shields come later).
- Masteries match weapon type regardless of handedness (Sword Mastery covers both swords).
- Staffs: the Two-Handed Staff is a magic weapon (`MagicStaff`) and does not count for Trauma
  Mastery. Quarterstaffs (`Quarterstaff`, blunt two-handed melee) come later and do.
- Shield Bash will be an Art.

### Affixes

Weapons and armor have different affix pools. Weapon pools:

| Weapon damage | Affixes |
| --- | --- |
| AD weapons | AAD, Action Speed, AP, True Damage, ADB, CDR, STR, AGI, INT, Dexterity |
| MD weapons | AMD, Action Speed, Magic Penetration, True Damage, MDB, Cast Speed, CDR, STR, AGI, INT, Dexterity |

- **True Damage** is flat damage added after resistances, with no bonus applied:
  `damage = (base + flat) × (1 + bonus × coefficient) × (1 − resistance × (1 − penetration)) + TD`.
  It is added once per damaging hit.
- **Magic Penetration** is the magic counterpart of AP: it removes a percentage of the target's MR.
- CDR and Cast Speed affixes are split per skill category: Active, Spell and Art CDR; Active, Spell
  and Art Cast Speed.
- Ranges: percentage stats 2–5%; base attributes, AAD, AMD, True Damage and flat Max HP 1–3.
  Values roll on whole steps (1 point, 1%). Max HP stays small because every armor piece can
  roll it.
- Affix slots by rarity: Common 0, Uncommon 1, Rare 2, Epic 3, Legendary 4, Mystic 5.
- Flat Max HP from affixes is added after Max HP Bonus: `Max HP = derived × (1 + MHPB) + flat`.
- Shields roll: AR, MR, Max HP, Max HP Bonus, AAD, AMD, Active/Spell/Art CDR, Active/Spell/Art
  Cast Speed, Action Speed, Dexterity. AR, MR and Max HP Bonus use the 2–5% range.

## Formulas

### Damage

```
damage = (base + flat) × (1 + bonus × coefficient) × (1 − resistance × (1 − penetration))
```

- `base`: weapon AD, or the skill's listed damage.
- `flat`: AAD for Attack Damage, AMD for Magic Damage.
- `bonus`: ADB or MDB, whichever matches the damage type.
- `coefficient`: the number in parentheses after a skill's damage, e.g. Fireball `50 (0.5)`.
  Only that fraction of the bonus applies. **No coefficient listed means 1.0.**
- `resistance`: the target's AR or MR.
- `penetration`: AP removes that **percentage** of the target's resistance
  (40% AR with 50% AP leaves 20% AR).

### Cooldown reduction

- All CDR sources that apply to a skill **add together**: the attribute CDR for its category
  plus gear and skill sources (e.g. "Melee Skills -10% Cooldown", Elder Summoner's -30%).
- Total CDR is **capped at 50%**.
- Cast time reduction sources also add together, with **no cap**.

## Placeholders (to be replaced by real formulas)

Attribute scaling is linear around the 10-point reference until real formulas exist.
All values live in one config (`FScalingConfig` in `CrossguardRules`):

- Max HP: 100 at 10 STR.
- ADB, AR, Active cast time / CDR, PHB: 0% at 10 STR.
- Movement Speed 100%, Action Speed 0%, Dexterity, Art cast time / CDR: at 10 AGI.
- MDB, MR, MHB, Spell cast time / CDR: 0% at 10 INT.

## Block and parry

Right click held: **block** with a shield in the off hand, **parry** with a weapon that can parry
and an empty off hand (`canParry`). No timing window, no cooldown: hold it as long as you like.
While held: half movement speed, no attacks (skills still work). It faces where you look (the mouse
moves it) and stops only Attack Damage hits that meet it: from within 50° of where it faces
(assumption), arrows included. Magic Damage always goes through (a skill may change that later).

Every weapon has an **Impact Power** (`impactPower`, Weapons.json): dagger 1, one-handed sword 3,
shield 4, one-handed axe 4, spear 5, two-handed sword 5, two-handed axe 6, bow 3; placeholders wand
1, two-handed staff 4, throwing knife 1, throwing axe 3, natural attacks (claws, unarmed mobs) 3.
An explosion has none and is never stopped. When a hit meets a guard, the difference decides:

| Difference | Loser |
| --- | --- |
| 0 | Nothing happens; the hit is stopped. |
| 1 | Takes 25% of the winner's hit. |
| 2 | Takes 50%, stunned 0.5 s. |
| 3 or more | Takes 100%, stunned 1 s. |

- The attacker wins: the defender takes that share of the hit (and the stun).
- The defender wins (mirror rule): the hit is stopped; the attacker takes that share of the
  defender's weapon damage (a shield deals none) and the stun. Assumption: melee only; a stopped
  arrow or throw does not reach back to its thrower.
- Shares apply to the finished hit (bonuses and True Damage included).
- A **parry** the defender does not lose (a tie or a win): the next attack within 3 s gets +50%
  Attack Damage Bonus, and +10% Action Speed for 3 s.
- Skills that deal Attack Damage (Cleave, Triple Thrust, Shooting Star, Parting the Land) are
  checked the same way with the caster's weapon. Triple Thrust pierces: a hit the guard would stop
  deals 50% instead. Parting the Land's knock-up only lands if its damage does.
- A stopped hit carries no on-hit effects (slows, Mute, Silence).
- **Clashes**: two swinging weapons that meet collide too, with the same Impact Power check (no
  parry bonus: nobody parried). Until swings are animated (weapon-on-weapon collision along the
  blade), a melee basic attack's blade counts as in the way for 0.25 s after it starts
  (assumption): a melee hit from the front in that time clashes with it. The first swing has
  already landed by then; a real collision with animations replaces this.
- Every Impact Power check (block, parry, clash) **clanks**: both are pushed back from the point of
  impact (300 cm/s, assumption) and every machine fires `OnWeaponClank(ImpactPoint)` on the
  defender's character Blueprint, where the bounce-back animation and the sound go.
- **Stun** is crowd control (Immutable makes you immune): no moving, jumping, attacking, throwing,
  item use, guarding or skills; running skills and the guard stop. Never on a body.
- Guarding also stops throwing; a parry's speed bonus does not stack (a new parry restarts it).
- A missed guard is its own punishment: the hit lands in full.
- Monsters and summons guard too: when a melee attack is aimed at one (its target, or in reach
  within 60° of the attacker's facing; never an ally's swing), it raises its guard toward the
  attacker half the time, for 1 s (assumption), with its weapon's Impact Power (3 unarmed).

## Trading

Players trade face to face, in raids or between them: ask within 5 m, both offer, both lock, both accept, the server swaps everything or nothing. Banked gold only between raids (in raids, gold coins are items). Details and assumptions: [trading.md](trading.md).

## Saving

Characters are saved by the server behind a backend interface; the backend itself is not chosen
(local JSON files until then). How it works: [persistence.md](persistence.md).

- Saved: banked XP and level, attribute allocation, respec tokens, Skill Library, equipped skills,
  banked gold, bought stash tabs and every stash item (rarity, affixes, stack counts), and the gear
  carried between raids (inventory, Backpack, Quiver, worn weapons and armor).
- Not saved: raid XP (banked only by extracting, as ruled), anything picked up in a raid until the
  player extracts.
- Assumed until ruled otherwise:
  - (placeholder) The gear carried into a raid is at risk from the moment the raid starts: a save
    made during a raid lists none, so a server crash or the player **logging out during a raid
    loses it like a failed extraction**, and the next load has no carried gear.
  - (placeholder) The hotbar and the active weapon set are not saved (the hotbar refills from the
    keys' remembered items).
  - (placeholder) A corrupt save is refused whole and kept aside; the player starts fresh. Values
    out of range are repaired: negative gold, XP or tokens become 0, an allocation over the level's
    points is reset to all points unassigned (a free re-spend), a stack over its stack size is
    clamped. Items and skills the game data no longer has are kept in a quarantine and come back
    when the data does.
  - (placeholder) Player ids without Steam (`session-<n>`) are saved too, for development; they
    are not stable across server restarts (turn `bSaveSessionIds` off once Steam is in).

## Open questions

