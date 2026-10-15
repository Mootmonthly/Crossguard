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

- There is no magic penetration stat.
