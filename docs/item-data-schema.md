# Item Data Schema

## Rarity

Every item has a rarity. The rarity **level** is its index:

| Rarity | Level | Color | Affix slots |
| --- | --- | --- | --- |
| Common | 0 | Gray `#9D9D9D` | 0 |
| Uncommon | 1 | Green `#1EFF00` | 1 |
| Rare | 2 | Blue `#0070DD` | 2 |
| Epic | 3 | Purple `#A335EE` | 3 |
| Legendary | 4 | Orange `#FF8000` | 4 |
| Mystic | 5 | Gold `#FFD700` | 5 |


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
| `gridWidth`, `gridHeight` | integer ≥ 1 | Squares the weapon takes in the inventory grid, unrotated (see [Grid size](#grid-size)). |
| `stat` | enum | What the weapon provides: `AttackDamage` or `MagicDamage` (base damage per attack), or `AttackResistance` (shields: a fraction added to AR while equipped). |
| `baseValue` | number > 0 | Value at Common. Damage in points; AR as a fraction (`0.08` = 8%). |
| `perRarity` | number ≥ 0 | Added per rarity level. |
| `attackSpeed` | object or null | `{"attacks": A, "perSeconds": S}`: A attacks every S seconds, so the interval is S / A. `null` for items that do not attack (shields). |
| `delivery` | enum | How an attack lands: `MeleeSwing`, `Hitscan`, `Projectile`, or `None`. |
| `canParry` | boolean | Right click parries when the off hand is empty. All two-handed weapons except bows; selected one-handed weapons (none yet). Never shields or bows. |

Value at a rarity = `baseValue + perRarity × level`.

Attack interval = `(perSeconds / attacks) / (1 + ActionSpeed)`. ActionSpeed is the
character's stat (0 at 10 AGI). The divisor is floored at 0.1.

## Grid size

Items occupy squares in the inventory grid (10 wide × 5 high for the main grid). `gridWidth` ×
`gridHeight` is the size unrotated. Every item must fit the main grid in some orientation:
`max(w, h) ≤ 10` and `min(w, h) ≤ 5`. The validators check this.

Weapons (width × height):

| Weapon | Size |
| --- | --- |
| Dagger | 1 × 2 |
| One-handed sword, one-handed axe | 1 × 3 |
| One-handed wand | 1 × 2 |
| Shield | 2 × 2 |
| Two-handed weapons (sword, axe, staff, bow) | 2 × 4 |

Armor by slot:

| Slot | Size |
| --- | --- |
| Head | 2 × 2 |
| Chest | 2 × 3 |
| Legs | 2 × 3 |
| Hands | 2 × 2 |
| Feet | 2 × 2 |
| Back | 2 × 2 |

## General items

`Data/Items/Items.json` is a JSON array of non-gear items (ammo, consumables, throwables,
materials), sorted by `id`. `Tools/validate_items.py` checks it; `Tools/export_item_table.py`
writes `ItemTable.json` (row struct `CrossguardItemRow`).

| Field | Type | Meaning |
| --- | --- | --- |
| `id` | string | `snake_case`, unique. |
| `name` | string | Display name. |
| `category` | enum | `Ammo`, `Consumable`, `Throwable`, `Material`. Mirrors the same-named entries of `ECrossguardItemCategory`. |
| `gridWidth`, `gridHeight` | integer ≥ 1 | Squares one item takes in the inventory grid, unrotated; must fit the main grid (see [Grid size](#grid-size)). |
| `maxStack` | integer ≥ 1 | Most items one inventory stack holds. |

Prototype items: Arrow (1 × 3, stack 20) and Bolt (1 × 2, stack 20).

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
| `stat` | enum | The attribute it adds to, by its `UCrossguardAttributeSet` name: `AttackResistance`, `MagicResistance`, `MaxHealth`, `MaxHealthBonus`, `AdditionalAttackDamage`, `AdditionalMagicDamage`, `ActionSpeed`, `AttackPenetration`, `MagicPenetration`, `TrueDamage`, `AttackDamageBonus`, `MagicDamageBonus`, `ActiveCooldownReduction`, `SpellCooldownReduction`, `ArtCooldownReduction`, `ActiveCastTimeReduction`, `SpellCastTimeReduction`, `ArtCastTimeReduction`, `Strength`, `Agility`, `Intelligence`, `Dexterity`, `MoveSpeed`. Unique across affixes. |
| `min`, `max` | number | Roll range, inclusive. Percent stats are fractions (`0.02` = 2%). |
| `step` | number > 0 | Values land on `min + k × step`. |
| `pools` | string[] | Which items can roll it: `AttackWeapon` (weapons dealing AD), `MagicWeapon` (weapons dealing MD), `Shield`, `PlateArmor`, `LeatherArmor`, `ClothArmor`. |

Rolling: an item gets as many distinct affixes from its pool as its rarity has slots
(`Crossguard::RollAffixes`, deterministic per seed).

Flat Max HP from affixes is added after Max HP Bonus: `Max HP = derived × (1 + MHPB) + flat`.

## Armor

`Data/Items/Armor.json` is a JSON array of armor pieces, sorted by `id`. For the prototype there is
one set per armor type. `Tools/validate_armor.py` checks it.

| Field | Type | Meaning |
| --- | --- | --- |
| `id` | string | `snake_case`, unique. |
| `name` | string | Display name. |
| `slot` | enum | `Head`, `Chest`, `Hands`, `Legs`, `Feet`, `Back`. |
| `armorType` | enum | `Cloth`, `Leather`, `Plate`. |
| `gridWidth`, `gridHeight` | integer ≥ 1 | Squares the piece takes in the inventory grid, unrotated (see [Grid size](#grid-size)). |
| `stats` | object[] | Base stats while worn: `{"stat", "baseValue", "perRarity"}`. `stat` is an attribute name the equipment effect applies (`AttackResistance`, `MagicResistance`, `MoveSpeed`, ...), each at most once per piece. Fractions: `0.04` = 4%. |
| `affixPool` | enum | `PlateArmor`, `LeatherArmor`, `ClothArmor`: the pool its affixes roll from. Must match `armorType`. |

Value at a rarity = `baseValue + perRarity × level`, as for weapons.

Prototype values per piece at Common (Chest and Legs double them; rarity adds +1% per level to AR
and MR, not to Plate's Move Speed penalty):

| Type | AR | MR | Move Speed |
| --- | --- | --- | --- |
| Plate | 4% | 0% | −1% |
| Leather | 2% | 2% | 0% |
| Cloth | 0% | 4% | 0% |

The Back slot holds capes for now (Cloth Cape). Backpacks and quivers come with inventory.

## Armor slots and requirements

- Six slots: Head, Chest, Hands, Legs, Feet, Back. Jewelry is a separate system, later.
- `Plate` requirement ([Armor Mastery]): at least one plate piece worn (pending confirmation).
- `PlateAllSlots` ([The Mithril Knight]): plate in Head, Chest, Hands, Legs and Feet. Back is
  excluded.
- Empty armor slots ([Cor Leonis]): all six slots count.
