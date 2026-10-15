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

XP sources:

- Killing monsters
- Killing players
- Completing quests
- Gathering resources (herbs, ore)
- Crafting equipment pieces
- Enchanting equipment pieces
- Extracting

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
- Pending: armor affix pools.

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

