# Item Data Schema

## Rarity

Every item has a rarity. The rarity **level** is its index:

| Rarity | Level | Color | Affix slots (placeholder) |
| --- | --- | --- | --- |
| Common | 0 | Gray `#9D9D9D` | 0 |
| Uncommon | 1 | Green `#1EFF00` | 1 |
| Rare | 2 | Blue `#0070DD` | 2 |
| Epic | 3 | Purple `#A335EE` | 3 |
| Legendary | 4 | Orange `#FF8000` | 4 |
| Mystic | 5 | Gold `#FFD700` | 5 |

Affix slot counts follow Dark and Darker's pattern and are placeholders until confirmed.

## Weapons

`Data/Items/Weapons.json` is a JSON array of weapon classes, sorted by `id`. For the prototype
there is one entry per weapon class (not per individual weapon). `Tools/validate_weapons.py`
checks it.

| Field | Type | Meaning |
| --- | --- | --- |
| `id` | string | `snake_case`, unique. |
| `name` | string | Display name. |
| `weaponType` | enum | A `WeaponType` from `docs/skill-data-schema.md`: `Sword`, `Axe`, `Dagger`, `Shield`, `Bow`, `Wand`, `MagicStaff`, ... Skill requirements match on this. |
| `handedness` | enum | `OneHanded` or `TwoHanded`. |
| `range` | enum | `Melee` or `Ranged`. |
| `stat` | enum | What the weapon provides: `AttackDamage` or `MagicDamage` (base damage per attack), or `AttackResistance` (shields: a fraction added to AR while equipped). |
| `baseValue` | number > 0 | Value at Common. Damage in points; AR as a fraction (`0.08` = 8%). |
| `perRarity` | number ≥ 0 | Added per rarity level. |
| `attackSpeed` | object or null | `{"attacks": A, "perSeconds": S}`: A attacks every S seconds, so the interval is S / A. `null` for items that do not attack (shields). |
| `delivery` | enum | How an attack lands: `MeleeSwing`, `Hitscan`, `Projectile`, or `None`. |
| `canParry` | boolean | Right click parries when the off hand is empty. All two-handed weapons except bows; selected one-handed weapons (none yet). Never shields or bows. |

Value at a rarity = `baseValue + perRarity × level`.

Attack interval = `(perSeconds / attacks) / (1 + ActionSpeed)`. ActionSpeed is the
character's stat (0 at 10 AGI). The divisor is floored at 0.1.

## Equipment slots

- **Main hand**: any weapon except a shield. Left click: its basic attack.
- **Off hand**: any one-handed weapon, including a shield.
- A two-handed weapon goes in the main hand and leaves the off hand empty (no shield either;
  two-handed pavise shields come later).
- **Right click**: off-hand basic attack when dual wielding; block with a shield; parry when the
  off hand is empty and the main-hand weapon can parry; otherwise nothing.

## Affixes

`Data/Items/Affixes.json` is a JSON array of affix definitions, sorted by `id`.
`Tools/validate_affixes.py` checks it.

| Field | Type | Meaning |
| --- | --- | --- |
| `id` | string | `snake_case`, unique. |
| `name` | string | Display name. |
| `stat` | enum | The attribute it adds to, by its `UCrossguardAttributeSet` name: `AttackResistance`, `MagicResistance`, `MaxHealth`, `MaxHealthBonus`, `AdditionalAttackDamage`, `AdditionalMagicDamage`, `ActionSpeed`, `AttackPenetration`, `MagicPenetration`, `TrueDamage`, `AttackDamageBonus`, `MagicDamageBonus`, `ActiveCooldownReduction`, `SpellCooldownReduction`, `ArtCooldownReduction`, `ActiveCastTimeReduction`, `SpellCastTimeReduction`, `ArtCastTimeReduction`, `Strength`, `Agility`, `Intelligence`, `Dexterity`. Unique across affixes. |
| `min`, `max` | number | Roll range, inclusive. Percent stats are fractions (`0.02` = 2%). |
| `step` | number > 0 | Values land on `min + k × step`. |
| `pools` | string[] | Which items can roll it: `AttackWeapon` (weapons dealing AD), `MagicWeapon` (weapons dealing MD), `Shield`. Armor pools come later. |

Rolling: an item gets as many distinct affixes from its pool as its rarity has slots
(`Crossguard::RollAffixes`, deterministic per seed).

Flat Max HP from affixes is added after Max HP Bonus: `Max HP = derived × (1 + MHPB) + flat`.
