# Loot Data Schema

`Data/Loot/LootTables.json` is the source of truth for what monsters drop, chests hold and
gathering nodes yield.
`Tools/validate_loot.py` checks it (every id must exist in the weapon, armor, item or skill data);
`Tools/export_loot_table.py` writes `Data/Loot/LootTable.json` for Unreal (row struct
`CrossguardLootTableRow`). Both run in `Tools/run_checks.sh`.

**All numbers in the current tables are placeholders** until drop rates are designed.

## Table

| Field | Type | Meaning |
| --- | --- | --- |
| `id` | string | `snake_case`, unique. Chests and monsters name it (`LootTableId`). |
| `rolls` | [min, max] | How many drops the table rolls, inclusive. |
| `nothingWeight` | integer ≥ 0 | Weight of "nothing" against the entries, for each drop. |
| `rarityWeights` | object | Integer weight for each of `Common` … `Mystic`. Rolls the rarity of weapons, armor, items and "any Skill Gem" drops. |
| `entries` | Entry[] | What can drop. Non-empty. |

## Entry

| Field | Type | Meaning |
| --- | --- | --- |
| `kind` | enum | `Weapon`, `Armor`, `Item` (a row of Items.json), `SkillGem`, `Gold`. |
| `id` | string or null | The weapon / armor / item / skill id. `null` for Gold, and for "any" Weapon, Armor or Skill Gem. |
| `armorType` | string, optional | Any-armor drops only (`kind` `Armor`, `id` null): `Cloth`, `Leather` or `Plate` limits the pick to that type. Absent or null: every type. Exported as `bHasArmorType` + `ArmorType`. |
| `weight` | integer ≥ 0 | Chance against the other entries and `nothingWeight`. |
| `count` | [min, max] | How many, inclusive, at least 1. Weapons, armor and gems are always `[1, 1]`. |

## Rolling

For each drop: pick an entry (or nothing) by weight, then:

- **Weapon / Armor**: a rarity from `rarityWeights`; "any" picks a random weapon / armor piece
  (of `armorType`, if set); affixes roll for the rarity.
- **Item**: a rarity from `rarityWeights`; large counts split into stacks.
- **Gold**: `count` gold coins.
- **SkillGem** with an id: that gem, at its skill's rarity (Skills.json `rarity`).
- **SkillGem** "any": a rarity from `rarityWeights`, then a random skill of that rarity. If no
  skill has that rarity, the nearest rarity below; only if none below, the nearest above. Skills
  with no rarity never drop this way.

The same seed always gives the same loot (Crossguard::RollLoot, unit tested).

## Gathering tables

Gathering nodes (`ACrossguardGatherNode`, `LootTableId`) roll their table once when a gather
completes, into the gatherer's inventory. `gather_herb` (Silverleaf ×1–3 weight 3, Bloodroot ×1–2
weight 1) and `gather_ore` (Iron Ore ×1–3 weight 3, Mithril Ore ×1 weight 1): `rolls` [1, 1],
`nothingWeight` 0 (a node never yields nothing), all Common. Placeholders like every table.
