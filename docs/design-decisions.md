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
- Respec: reassign all attribute points. Bought with **real money** (revenue beside cosmetics);
  each purchase grants one respec token. Price not set. Purchases must be verified server-side
  through the platform store before a token is granted.
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

### Skill numbers filled in

| Skill | Ruling |
| --- | --- |
| Gouge | Cooldown 7 s |
| Shade Blow | Cooldown 10 s |
| Arcane Arrows | Cooldown 25 s |
| Neckbite | Cooldown 7 s. Requires a Beastial transformation Art (not designed yet). |
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

| Slash, Mortal Strike, Shade Blow, Gouge, Purge Shot | "Next attack" skills: ready for 10 s, used up by the next qualifying attack hit or miss: a melee attack with the skill's own weapon (Slash: sword or dagger, Shade Blow: sword or axe, Gouge: dagger, Mortal Strike: any melee); Purge Shot: a bow or crossbow shot. Several readied at once all apply to that attack. Mortal Strike's cooldown comes back on a kill. Shade Blow blocks all healing (potions too) for 5 s. Gouge: 5 Attack Damage per second for 5 s and -5% speed. Purge Shot: +10 Magic Damage and a 3 s Mute. |
| Greater Heal | As Heal, 55. |
| Benediction | Target under the crosshair within 15 m is healed to full, ally or enemy. Aimed at a body still in its revive window: a full revive (unlooted gear back); if both a character and a body are in line, the nearer one. Nobody: yourself. Shade Blow's heal block stops the heal but not a revive. |
| Quickshot, Arcane Arrows, Unleash | Self buffs. Unleash doubles STR / AGI / INT (gear and buffs included) for 15 s. Arcane Arrows' dropoff part waits until arrows have damage dropoff. |
| Cleave | 85% of the main-hand weapon's damage to everyone in a 3 m, 120° arc in front, allies too. |
| Triple Thrust | Three thrusts 0.15 s apart, each spear damage +15, 2.5 m reach. Piercing waits for blocking. |
| Parting the Land | 8 m × 2 m strip ahead: 65 Attack Damage and a knock-up to everyone in it; a 15 s Wall along each side. |
| Hunter's Leap | Art (Skill Forge change). 0.5 m per AGI point forward, 1.5 m high, 0.6 s. |
| Like A Lion, He Leapt | (Skill Forge.) Leaps at the nearest enemy within 30 m: players first, then monsters and minions; stealthed enemies are skipped; needs line of sight (walls and other world geometry block it, characters do not). Lands 1.5 m short. STR ×1.2 for 6 s. No enemy in range: fails, no cooldown. |
| Light | A light over your head for 120 s; recasting replaces it. |
| Wall | 4 m wide, 3 m tall, 0.5 m thick, 3 m in front, across your view. Indestructible, 15 s. |
| Stealth | Invisible to everyone outside your party (and to minions) for 12 s. Attacking, casting or taking damage ends it early. The exit bonus (+10% speed, +25% Attack Bonus for 3 s) applies however it ends, except death or Mute. Later: an indicator so teammates can tell you are stealthed. |
| Immutable | Crowd control = Slow, Blind, knock-up, Grounding (later Silence, Stun, Root). **Mute is not crowd control.** Equipped (and not Muted): permanently immune. Activated: you and party members within 10 m immune for 5 s. Existing crowd control is not removed. |
| Splinter of Winter | Dagger attacks: +5 Magic Damage and -10% speed for 3 s. |
| Multiscale | Requires Plate armor (Skill Forge). +15% Attack and Magic Resistance to you, every party member and party minions, at any distance. Two Multiscales do not stack. Off while you are Muted or dead. Also comes with The Mithril Knight. |
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
  shield 2 × 2; two-handed weapons and bows 2 × 4; helm, gloves, boots, cape 2 × 2; chest and legs
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
  rarity level (300 at Mystic).
- Vendor prices for gear (weapons and armor): 5 gold per grid square at Common, +5 per rarity
  level: `5 × squares × (rarity level + 1)`. A 2 × 4 Mystic weapon sells for 240. Placeholder for a
  richer formula later.
- Assumed until ruled otherwise: vendors are used between raids only; gemstones do not stack; a
  whole stack sells at once.
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

- Drawing a weapon set (1 / 2) takes 1 s, faster with Dexterity (`1 s ÷ (1 + Dexterity)`); an
  animation will play here. Nothing attacks while drawing.
- A hotbar key takes its item in hand (weapons are put away); left click uses or throws one. The
  hands stay on that item while any of it (same type and rarity) is left, so players can throw again
  after a miss or bandage repeatedly; when it runs out the hands are empty until a weapon set is
  drawn. Pressing the item's key again empties the hands.
- Hotbar slots remember the item type and rarity, so a new stack of the same item fills the slot.

## Throwables

| Item | Effect |
| --- | --- |
| Throwing Knife | 15 Attack Damage, +2 per rarity level. Counts as a Dagger. |
| Throwing Axe | 25 Attack Damage, +2 per rarity level. Counts as an Axe. |
| Explosive Bottle | Shatters where it lands: 10 Magic Damage to everyone within 3 m, then 3 Magic Damage per second for 5 s in that area. |

- All are 1 × 3 and stack to 3. Friendly fire applies, the thrower included.
- "Counts as a Dagger / Axe" while in hand: mastery bonuses apply to the throw, on-hit skill
  effects of that weapon type trigger on its hits, and skills requiring that weapon can be used.
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
| Bow | 30 AD | +2 | 1 per second; projectile |
| One-Handed Wand | 15 MD | +2 | 1.2 per second; hitscan |
| Two-Handed Staff | 30 MD | +3 | once every 1.3 s; slower projectile |

- Action Speed divides the attack interval: interval ÷ (1 + Action Speed).
- **Dual wielding**: any one-handed weapon fits the off hand. Left click attacks with the main
  hand, right click with the off hand. With a shield in the off hand, right click blocks. With the
  off hand empty, right click parries if the main-hand weapon can parry: every two-handed weapon
  except bows, and selected one-handed weapons (none chosen yet). Block and parry are not built.
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

## Open questions

