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
| `weaponType` | enum | A `WeaponType` from `docs/skill-data-schema.md`: `Sword`, `Axe`, `Dagger`, `Spear`, `Shield`, `Bow`, `Wand`, `MagicStaff`, ... Skill requirements match on this. |
| `handedness` | enum | `OneHanded` or `TwoHanded`. |
| `range` | enum | `Melee` or `Ranged`. |
| `gridWidth`, `gridHeight` | integer ≥ 1 | Squares the weapon takes in the inventory grid, unrotated (see [Grid size](#grid-size)). |
| `stat` | enum | What the weapon provides: `AttackDamage` or `MagicDamage` (base damage per attack), or `AttackResistance` (shields: a fraction added to AR while equipped). |
| `baseValue` | number > 0 | Value at Common. Damage in points; AR as a fraction (`0.08` = 8%). |
| `perRarity` | number ≥ 0 | Added per rarity level. |
| `attackSpeed` | object or null | `{"attacks": A, "perSeconds": S}`: A attacks every S seconds, so the interval is S / A. `null` for items that do not attack (shields). |
| `delivery` | enum | How an attack lands: `MeleeSwing`, `Hitscan`, `Projectile`, or `None`. |
| `ammo` | string or null | `id` of an `Ammo` entry in `Data/Items/Items.json` that each attack uses up (the bow uses `arrow`). `null`: no ammo. Exported as `AmmoId`, `None` for null. |
| `canParry` | boolean | Right click parries when the off hand is empty. All two-handed weapons except bows; selected one-handed weapons (none yet). Never shields or bows. |
| `impactPower` | integer ≥ 1 | Blocking and parrying: when weapons collide, the higher Impact Power wins. Dagger 1, one-handed sword 3, shield 4, one-handed axe 4, spear 5, two-handed sword 5, two-handed axe 6, bow 3; wand 1 and staff 4 are placeholders. Exported as `ImpactPower`. |

Value at a rarity = `baseValue + perRarity × level`.

### Weapon art (optional)

Fields an entry may leave out (none are set yet: no art is in the project). Empty: nothing is
shown, as before. `UCrossguardWeaponVisualsComponent` shows the drawn set's meshes
([art-placeholder-plan.md](art-placeholder-plan.md#how-the-art-hooks-into-the-code)).

| Field | Type | Meaning |
| --- | --- | --- |
| `mesh` | asset path | A static mesh held while the weapon is drawn, e.g. `/Game/Art/Weapons/SM_Sword.SM_Sword`. Exported as `Mesh` (`""` when left out). |
| `attachSocket` | string | Hand socket on the body (and the first-person arms). Left out: `hand_r` in the main hand, `hand_l` in the off hand. Needs `mesh`. Exported as `AttachSocket` (`None` when left out). |
| `relativeTransform` | object | The mesh's place on the socket: `location` `[x, y, z]` cm, `rotation` `[pitch, yaw, roll]` degrees, `scale` (a number or `[x, y, z]`); each optional. Needs `mesh`. Exported as `MeshOffset`, `MeshRotation`, `MeshScale`. |
| `projectile` | object | A projectile weapon without ammo (the staff): its shot's look, each optional: `mesh`, `trailVfx`, `impactVfx`, `impactSound` (asset paths). An ammo row's fields win. Exported as `ProjectileMesh`, `ProjectileTrailVfx`, `ProjectileImpactVfx`, `ProjectileImpactSound`. |

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
| Spear | 1 × 5 |
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
materials, currency, trinkets, tools), sorted by `id`. `Tools/validate_items.py` checks it;
`Tools/export_item_table.py` writes `ItemTable.json` (row struct `CrossguardItemRow`).

| Field | Type | Meaning |
| --- | --- | --- |
| `id` | string | `snake_case`, unique. |
| `name` | string | Display name. |
| `category` | enum | `Ammo`, `Consumable`, `Throwable`, `Material`, `Currency` (gold coins), `Trinket` (valuables sold to vendors, e.g. gemstones), `Tool` (lockpicks and other tools). Mirrors the same-named entries of `ECrossguardItemCategory`. |
| `gridWidth`, `gridHeight` | integer ≥ 1 | Squares one item takes in the inventory grid, unrotated; must fit the main grid (see [Grid size](#grid-size)). |
| `maxStack` | integer ≥ 1 | Most items one inventory stack holds. Set by category: Ammo 20, Throwable 3, Consumable 5, Currency 100. Gemstones (`emerald`, `ruby`, `sapphire`, Trinkets) 3. Material, other Trinkets and Tool are not ruled yet (any integer ≥ 1). |
| `sellValue` | integer ≥ 0 | Gold a vendor pays at Common. 0: vendors do not buy it. Trinkets must have `sellValue` > 0. |
| `sellPerRarity` | integer ≥ 0 | Extra gold per rarity level (Common 0 ... Mystic 5). |

Sell value at a rarity = `sellValue + sellPerRarity × level`. The exporter writes `SellValue` and
`SellPerRarity` for every row.

### Projectile art (optional, Ammo and Throwable)

What the projectile shows, over the projectile Blueprint's own (`ACrossguardProjectile::Visual`): an
arrow's from its `Ammo` row (the bow's `ammo`), a throw's from the thrown item's row. Each may be
left out (the exporter writes `""` for a path, no turn and scale 1). Paths are Unreal object paths
(`/Game/Folder/Asset.Asset`). Tune in Play-In-Editor with `CgReloadArtData` (see
[art-placeholder-plan.md](art-placeholder-plan.md#tuning-art-without-restarting)).

| Field | Exported as | Meaning |
| --- | --- | --- |
| `projectileMesh` | `ProjectileMesh` | Static mesh flying with it (the projectile's X axis is the flight direction). |
| `projectileRotation` | `ProjectileRotation` | `[pitch, yaw, roll]` degrees: turns the mesh from the flight direction (default `[0, 0, 0]`). A mesh modelled along +Y (Quaternius' arrow) faces its flight with `[0, -90, 0]`. Needs `projectileMesh`. |
| `projectileScale` | `ProjectileScale` | Number > 0 or `[x, y, z]` > 0: the mesh's scale (default 1). Needs `projectileMesh`. |
| `trailVfx` | `TrailVfx` | Niagara system attached for the flight. |
| `impactVfx` | `ImpactVfx` | Niagara system where it hits or explodes. |
| `impactSound` | `ImpactSound` | Sound where it hits or explodes. |

Consumables also have these fields; other categories must not have them. The exporter writes
`HealAmount`, `HealPerRarity`, `HealDuration` and `UseTime` for every row (0 for non-consumables).

| Field | Type | Meaning |
| --- | --- | --- |
| `healAmount` | number > 0 | HP restored at Common. |
| `healPerRarity` | number ≥ 0 | Extra HP per rarity level (Common 0 ... Mystic 5). |
| `healDuration` | number > 0 | Seconds the healing is spread over. |
| `useTime` | number ≥ 0 | Seconds the player channels (holds left click) to use it. 0: instant. |

HP restored at a rarity = `healAmount + healPerRarity × level`.

Throwables must have these fields; other categories must not have them. The exporter writes
`ThrowDamageType`, `ThrowDamage`, `ThrowDamagePerRarity`, `ThrowRadiusMeters`, `ThrowDotPerSecond`,
`ThrowDotSeconds`, `CountsAsWeapon`, `bHasCountsAsWeapon` and `ImpactPower` for every row (defaults for
non-throwables: `Attack`, 0, 0, 0, 0, 0, `Sword`, false, 0).

| Field | Type | Meaning |
| --- | --- | --- |
| `throwDamageType` | enum | `Attack` or `Magic` (`ECrossguardDamageType`). |
| `throwDamage` | number > 0 | Damage on hit at Common. |
| `throwDamagePerRarity` | number ≥ 0 | Extra damage per rarity level (Common 0 ... Mystic 5). |
| `throwRadius` | number ≥ 0 | Meters. 0: hits the single target it strikes. > 0: explodes where it lands and hits everyone within the radius. Exported as `ThrowRadiusMeters`. |
| `throwDotPerSecond` | number ≥ 0 | Damage per second left behind in the radius after an explosion. 0: none. |
| `throwDotSeconds` | number ≥ 0 | How long that lasts. Must be > 0 when `throwDotPerSecond` > 0, and 0 when it is 0. |
| `countsAsWeapon` | enum or null | A `WeaponType` (`Sword`, `Axe`, `Dagger`, `Spear`, `Mace`, `Quarterstaff`, `Hammer`, `Shield`, `Bow`, `Crossbow`, `Sling`, `Throwing`, `Wand`, `MagicStaff`) whose skills apply to the throw, or `null`. Exported as `CountsAsWeapon` plus `bHasCountsAsWeapon` (false for null; `CountsAsWeapon` is then `Sword`, ignore it). |
| `impactPower` | integer ≥ 0 | Blocking and parrying: the throw's Impact Power against a block or parry, like a weapon's `impactPower`. 0: not a weapon hit, cannot be blocked. Throwing Knife 1, Throwing Axe 3, Explosive Bottle 0. Exported as `ImpactPower`. |

Throw damage at a rarity = `throwDamage + throwDamagePerRarity × level`.

Prototype items: Arrow (1 × 3, stack 20), Bolt (1 × 2, stack 20), Bandage (1 × 1, stack 5, heals
10 over 2 s, 2 s use time) and Healing Potion (1 × 1, stack 5, heals 15 + 5 per rarity level over
10 s, instant). Arrow, Bolt, Bandage and Healing Potion are not sold to vendors (`sellValue` 0).
Gold Coin (Currency, 1 × 1, stack 100, `sellValue` 0). Emerald, Ruby and Sapphire (Trinkets,
1 × 1, stack 3 per rarity) sell for 50 at Common, +50 per rarity level, up to 300 at Mystic.
Lockpick (Tool, 1 × 1, stack 3, not sold). Throwing Knife and Throwing Axe (Throwables, 1 × 3,
stack 3, not sold): Attack damage 15 / 25 at Common, +2 per rarity level, single target, count as a
Dagger / Axe. Explosive Bottle (Throwable, 1 × 3, stack 3, not sold): Magic damage 10, explodes in a
3 m radius and leaves 3 damage per second for 5 s, counts as no weapon.
Materials (gathered from nodes, for crafting later; all placeholders): Silverleaf (`herb_silverleaf`)
and Bloodroot (`herb_bloodroot`) herbs, Iron Ore (`ore_iron`) and Mithril Ore (`ore_mithril`); 1 × 1,
stack 20 (materials stack size is not ruled), sell 5 / 10 / 5 / 15 gold, `sellPerRarity` 0.

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
