# Mob Data Schema

`Data/Mobs/Mobs.json` is the source of truth for monster and boss types. Edit it through the
[Mob Forge](../Tools/MobForge/README.md). `Tools/validate_mobs.py` checks it; `Tools/export_mob_table.py`
writes `Data/Mobs/MobTable.json` for Unreal (row struct `CrossguardMobRow`), and
`Tools/export_loot_table.py` exports each mob's drops as the loot table `mob_<id>`. All run in
`Tools/run_checks.sh`.

An `ACrossguardMonster` placed in a map with a **Mob Id** takes that mob's stats when the game
starts (the game mode's **Mob Table** must be set).

| Field | Type | Meaning |
| --- | --- | --- |
| `id` | string | `snake_case`, unique. The monster's Mob Id. |
| `name` | string | Display name. |
| `kind` | enum | `Monster` (killer gets basic monster XP, 5) or `Boss` (boss XP, 50). |
| `combatRole` | enum | `Warrior` (runs up and hits), `Mage` (keeps its distance, casts Fireball: 50 + 15 splash, as the skill) or `Ranged` (keeps its distance, shoots arrows with a bow: skeleton archers). |
| `weapon` | string or null | A weapon id from `Data/Items/Weapons.json` it holds, or `null` for natural attacks (no weapon). For skills that care (masteries, Shade Blow); damage stays `attackDamage`. Warrior: not a `Bow`, `Crossbow` or `Sling`. Ranged: `null` (it holds a bow) or a `Bow`, `Crossbow` or `Sling`. Mage: any. No shields (`weaponType` `Shield`). Exported as `bWieldsWeapon`, `WeaponType` (`Sword` when none) and `bTwoHandedWeapon`. |
| `health` | number > 0 | Max HP. |
| `attackDamage` | number ≥ 0 | Warrior and Ranged: Attack Damage per hit or arrow, before resistance. |
| `attackInterval` | number > 0 | Warrior and Ranged: seconds between attacks. |
| `attackRange` | number > 0 | Warrior: reach in meters. Ranged: shooting range in meters (it shoots targets it can see this close). |
| `fireballInterval` | number > 0 | Mage: seconds between fireballs. |
| `keepDistance` | number ≥ 0 | Mage and Ranged: meters it stays from its target. Ranged: at most `attackRange`. |
| `aggroRange` | number ≥ 0 | Meters within which it notices enemies it can see. At most `leashRange`. |
| `leashRange` | number > 0 | Meters from home where it gives up: retargets someone in aggro range, or walks home and fully resets. |
| `moveSpeed` | number ≥ 0 | Meters per second (a player walks 4.5). |
| `scale` | number > 0 | Size; 1 = player size. |
| `skills` | string[] | Skill ids from `Data/Skills/Skills.json`, no duplicates, each [AI-usable](#ai-usable-skills). Actives it uses on its target; passives are always on. `[]` for none. Exported as `Skills`. |
| `loot` | object | Its drops: `rolls`, `nothingWeight`, `rarityWeights`, `entries`, exactly as a table in [loot-data-schema.md](loot-data-schema.md) (no `id`). |
| `description` | string | Free text. |
| `notes` | string[] | Open questions. |

## AI-usable skills

A mob can only have skills the monster AI can use (`Tools/ai_skills.py`, shared by the validator
and the Mob Forge sync). That is nearly every skill: in C++ every active skill overrides
`UCrossguardSkillAbility::ShouldAIUse`, passives are always on, and bundles grant their included
skills. The exceptions (`NOT_AI_SKILLS`):

| Skill | Why not |
| --- | --- |
| `flight` | AI cannot fly. |
| `hunters_tracking` | A player's screen effect. |

Some skills ride on one kind of attack, so only one combat role may have them (`SKILL_ROLES`):

| Roles | Skills |
| --- | --- |
| Warrior only | `slash`, `mortal_strike`, `shade_blow`, `gouge`, `neckbite`, `chop_down` |
| Ranged only | `purge_shot`, `arcane_arrows` |
| Any role | Every other AI-usable skill. |

When a skill stops (or starts) working for the AI, or gains a role restriction in its
`ShouldAIUse`, update `Tools/ai_skills.py`.
