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
| `weapon` | string or null | A weapon id from `Data/Items/Weapons.json` it holds, or `null` for natural attacks (no weapon). For skills that care (masteries, Shade Blow); damage stays `attackDamage`. Warrior: not a `Bow`, `Crossbow` or `Sling`. Ranged: `null` (it holds a bow) or a `Bow`, `Crossbow` or `Sling`. Mage: any. No shields (`weaponType` `Shield`). Exported as `bWieldsWeapon`, `WeaponType` (`Sword` when none), `bTwoHandedWeapon` and `ImpactPower` (blocking and parrying: the weapon's `impactPower`; 3 when none, for natural attacks and a Ranged mob's bow, placeholder). |
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

### Art (optional)

Fields a mob may leave out (the Mob Forge keeps them but has no editor for them yet; edit
`Mobs.json` by hand). Without `mesh` the monster keeps its placeholder cylinder (or its Blueprint's
look). With one, every machine swaps the cylinder for the mesh (`ACrossguardMonster::ApplyMobData` →
the replicated `ACrossguardAICharacter::BodyVisual`), so one spawner can mix mobs that look different.

| Field | Type | Meaning |
| --- | --- | --- |
| `mesh` | asset path | Skeletal mesh, e.g. `/Game/Art/Monsters/SK_Skeleton.SK_Skeleton`. Exported as `Mesh` (`""` when left out). The ragdoll on death needs its Physics Asset (see `deathMode`). |
| `animBlueprint` | class path | Its Anim Blueprint's class, ending in `_C`: `/Game/Art/Monsters/ABP_Skeleton.ABP_Skeleton_C`. Exported as `AnimClass`. |
| `meshScale` | number > 0 | The mesh's own scale, on top of `scale` (default 1). Fit a model to the capsule. |
| `meshOffset` | `[x, y, z]` meters | From the capsule's bottom centre (default `[0, 0, 0]`). Exported in cm. |
| `meshYaw` | number | Degrees (default -90: meshes facing +Y, like the UE mannequin; 0 for meshes facing +X). |
| `animations` | object | Without `animBlueprint`: animation sequences played by state, each optional: `idle` and `move` loop (move while it walks), `attack` once per attack, `death` once, holding its last pose. Paths like `/Game/ThirdParty/KayKit/Animations/Rig_Medium_SpecialSkeletons_Idle.Rig_Medium_SpecialSkeletons_Idle`. Exported as `IdleAnim`, `MoveAnim`, `AttackAnim`, `DeathAnim`. |
| `deathMode` | `"ragdoll"` \| `"animation"` | On death (default `"ragdoll"`): `ragdoll` simulates the mesh's Physics Asset on every machine, like a player's death; a mesh without a Physics Asset plays the death animation instead. `animation` always plays `animations.death` (or the Anim Blueprint's own) and holds the last pose; it needs `animations.death` or `animBlueprint`. The body (loot) is the same either way. Exported as `DeathMode` (`Ragdoll` / `Animation`). |

The capsule is not resized to the mesh: size a mob with `scale`. Tune `meshScale`, `meshOffset`,
`meshYaw`, `animations` and `deathMode` in Play-In-Editor with the `CgReloadArtData` console command
(no restart; [art-placeholder-plan.md](art-placeholder-plan.md#tuning-art-without-restarting)). Minions take the same settings from
their summon ability (`WarriorVisual` / `MageVisual` on Summon Skeletons, `CompanionVisual` on Summon
Tiger and Summon Greater Demon).

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
