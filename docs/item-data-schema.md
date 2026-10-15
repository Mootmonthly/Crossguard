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
| `weaponType` | enum | A `WeaponType` from `docs/skill-data-schema.md`: `Sword`, `Axe`, `Dagger`, `Shield`, `Bow`, `Wand`, `Staff`, ... Skill requirements match on this. |
| `handedness` | enum | `OneHanded` or `TwoHanded`. |
| `range` | enum | `Melee` or `Ranged`. |
| `stat` | enum | What the weapon provides: `AttackDamage` or `MagicDamage` (base damage per attack), or `AttackResistance` (shields: a fraction added to AR while equipped). |
| `baseValue` | number > 0 | Value at Common. Damage in points; AR as a fraction (`0.08` = 8%). |
| `perRarity` | number ≥ 0 | Added per rarity level. |
| `attackSpeed` | object or null | `{"attacks": A, "perSeconds": S}`: A attacks every S seconds, so the interval is S / A. `null` for items that do not attack (shields). |
| `delivery` | enum | How an attack lands: `MeleeSwing`, `Hitscan`, `Projectile`, or `None`. |

Value at a rarity = `baseValue + perRarity × level`.

Attack interval = `(perSeconds / attacks) / (1 + ActionSpeed)`. ActionSpeed is the
character's stat (0 at 10 AGI). The divisor is floored at 0.1.

## Equipment slots (prototype)

- **Main hand**: any weapon.
- **Off hand**: a one-handed shield, only when the main hand is one-handed.
- A two-handed weapon takes both hands.
