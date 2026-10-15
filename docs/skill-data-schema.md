# Skill Data Schema

`Data/Skills/Skills.json` is the source of truth for skill definitions. It is a JSON array of
skill objects, sorted by `id`. `Tools/validate_skills.py` checks it.

Rulings in `docs/design-decisions.md` override the Drive docs.

## Skill object

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `id` | string | yes | `snake_case`, unique, derived from the name (`dagger_mastery`). |
| `name` | string | yes | Display name exactly as in the docs. |
| `capacityCost` | integer ≥ 0 | yes | Skill Capacity cost (number in parentheses). |
| `category` | enum | yes | `Active`, `Spell`, `Art` or `Passive`. |
| `weaponTag` | enum or null | yes | `Melee`, `Ranged` or `null`. Derived from weapon requirements. |
| `tags` | string[] | yes | Extra tags: `Minion`, `Buff`, `Debuff`, `Summon`, `Heal`, `Raid`. May be empty. |
| `requirements` | object | yes | See below. |
| `cooldownSec` | number or null | yes | `null` if the docs give none (passives). |
| `durationSec` | number or null | yes | Main effect duration, `null` if none. |
| `includes` | string[] | yes | Skill ids whose full effects this skill contains permanently (bundles). Loadout blocks equipping both; two bundles may share a component, which applies once. |
| `grantsWhileActive` | string[] | yes | Skill ids usable only while this skill's effect is active (e.g. Dragonification). Loadout blocks equipping both. |
| `minionSkills` | string[] | yes | Skill ids that a summoned minion uses. Not granted to the player. |
| `statModifiers` | Modifier[] | yes | Stat changes. May be empty. |
| `damage` | Damage[] | yes | Damage instances. May be empty. |
| `healing` | Healing[] | yes | Healing instances. May be empty. |
| `mechanics` | string[] | yes | Behaviours the stat fields cannot express, one short imperative line each (`Become invisible for 12 s`). |
| `description` | string | yes | The skill text from the source doc, verbatim, without the cost suffix. |
| `source` | enum | yes | `SkillsDoc` or `StatsDoc`: which doc's version is used. |
| `notes` | string[] | yes | Ambiguities found while entering the skill. One question per entry. |

### requirements

| Field | Type | Meaning |
| --- | --- | --- |
| `weapons` | WeaponType[] | Any one of these must be wielded. Empty = no weapon requirement. |
| `armor` | enum or null | `Plate` (some plate) or `PlateAllSlots`, or `null`. |
| `items` | string[] | Required items, e.g. `"Pet Gem (Tiger)"`. |
| `forms` | string[] | Required forms, e.g. `"Bestial"`. |

WeaponType: `Sword`, `Axe`, `Dagger`, `Spear`, `Mace`, `Staff`, `Hammer`, `Shield`, `Bow`,
`Crossbow`, `Sling`, `Throwing`, `Wand`, `AnyMelee`, `TwoHandedMelee`.

`weaponTag` is `Melee` if every listed weapon is a melee weapon (`Sword`, `Axe`, `Dagger`,
`Spear`, `Mace`, `Staff`, `Hammer`, `Shield`, `AnyMelee`, `TwoHandedMelee`), `Ranged` if every
listed weapon is ranged (`Bow`, `Crossbow`, `Sling`, `Throwing`), `null` if `weapons` is empty.

### Modifier

| Field | Type | Meaning |
| --- | --- | --- |
| `stat` | Stat | Which stat. |
| `op` | enum | `Flat` (add points), `Percent` (add a fraction, `0.15` = +15%), `Multiply` (`2.0` = doubled). |
| `value` | number | Negative for reductions. CDR is a positive fraction (`-10% Cooldown` → `CDR`, `0.10`). |
| `target` | enum | `Self`, `Party`, `AlliesInRadius`, `Target`. |
| `skillFilter` | enum or null | For `CDR`/`CastTime`: which skills it applies to: `Melee`, `Ranged`, `Minion`, `Active`, `Spell`, `Art`, or `null` (all). |
| `condition` | Condition or null | When it applies. `null` = always while equipped/active. |
| `durationSec` | number or null | Temporary buff/debuff length, `null` if permanent. |

Stat: `STR`, `AGI`, `INT`, `AllAttributes`, `MHP`, `MHPB`, `AD`, `AAD`, `ADB`, `AR`, `AP`,
`Dexterity`, `ActionSpeed`, `MoveSpeed`, `PH`, `APH`, `PHB`, `MD`, `AMD`, `MDB`, `MR`, `MH`,
`AMH`, `MHB`, `CDR`, `CastTime`.

Map old names per `docs/design-decisions.md`: ADR→AR, MDR→MR, Attack Bonus→ADB,
Magic Bonus→MDB, Weapon Damage→AAD, Max HP %→MHPB.

### Condition

Exactly one key:

- `{"wielding": ["Axe"]}`: holding one of these weapon types.
- `{"armor": "PlateAllSlots"}`
- `{"perEmptyArmorSlot": true}`: value applies once per empty armor slot.
- `{"onActivation": true}`: applies when the skill is activated (pair with `durationSec`).
- `{"nextAttack": true}`: applies to the next attack only.
- `{"other": "free text"}`: anything else; also add a `notes` entry.

### Damage

| Field | Type | Meaning |
| --- | --- | --- |
| `type` | enum | `Attack` or `Magic`. |
| `amount` | number | Base amount, or a fraction of weapon AD when `kind` is `WeaponPercent`. |
| `coefficient` | number | Fraction of ADB/MDB that applies. `1.0` if the doc lists none. |
| `kind` | enum | `Impact`, `Splash`, `PerSecond`, `OverTime` (total over `durationSec`), `Additional` (added to an attack), `WeaponPercent` (`0.85` = 85% Attack Damage). |
| `durationSec` | number or null | For `PerSecond`/`OverTime`. |
| `hits` | integer | Number of hits; `1` unless stated. |

### Healing

| Field | Type | Meaning |
| --- | --- | --- |
| `type` | enum | `Physical` or `Magical`. |
| `amount` | number or null | HP healed; `null` with `full: true`. |
| `coefficient` | number | Fraction of PHB/MHB that applies; `1.0` default. |
| `full` | boolean | Fully restores HP. |
| `canRevive` | boolean | Works on dead targets. |
