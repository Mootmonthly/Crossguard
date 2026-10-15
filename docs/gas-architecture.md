# Gameplay Ability System Setup

How Crossguard's stats and skills map onto Unreal's Gameplay Ability System (GAS).

## Ownership

- `ACrossguardCharacter` owns the `UCrossguardAbilitySystemComponent` (ASC) and the
  `UCrossguardAttributeSet`. Replication mode is Mixed.
- The server calls `InitializeAttributes(Level, Allocation)` on possession. It validates the
  allocation with `CrossguardRules`, then fills every stat and sets Health to MaxHealth.

## Stats

Every stat in the Stats doc is an attribute on `UCrossguardAttributeSet`. Fractions are stored as
fractions: `0.15` = 15%.

| Kind | Base value comes from | Modified by |
| --- | --- | --- |
| STR, AGI, INT | Player allocation | Effects (Heroic Might `+10`, Unleash `×2`) |
| Attribute-derived stats (MaxHealth, ADB, AR, MoveSpeed, CDR/cast time per category, ...) | `Crossguard::DeriveStats`, rewritten by the ASC whenever STR, AGI, INT or MHPB changes | Effects stack on top (Add, Multiply) |
| Everything else (AAD, AP, AMD, APH, AMH, Melee/Ranged/Minion CDR, MHPB) | 0 | Effects |

MaxHealth base = derived Max HP × (1 + MHPB). When MaxHealth changes, Health keeps its
percentage.

Health and MaxHealth replicate to everyone. Every other stat replicates to the owner only.

## Skills

Every skill is a subclass (C++ or Blueprint) of `UCrossguardGameplayAbility`. Set:

- `SkillId`: the id in `Data/Skills/Skills.json`.
- `SkillCategory`: Active, Spell, Art or Passive.
- Asset tags: `Skill.Weapon.Melee`, `Skill.Weapon.Ranged` and/or `Skill.Minion` when they apply.
- `BaseCooldownSeconds`, `BaseCastTimeSeconds`.
- `CooldownTags`: one unique tag per skill, e.g. `Cooldown.Skill.Fireball`.
- `RequiredWeapons`: any one must be equipped to activate (e.g. Cleave: `AnyMelee`).

### Cooldowns

Every ability uses the native `UCrossguardCooldownEffect` by default (no asset needed). The ability
fills its duration with the cooldown after CDR and adds its `CooldownTags`.
CDR = category CDR + matching Melee/Ranged/Minion CDR, summed and capped at 50%.

Cast time: call `GetReducedCastTime()` for wait tasks and cast bars. Uncapped.

### Damage and healing effects

Native effects `UCrossguardDamageEffect` and `UCrossguardHealingEffect` wrap the executions; use
them directly or make Blueprint effects with the executions.

- Damage: an Instant effect with the execution `UCrossguardDamageExecution`. Set
  `SetByCaller.Damage.Attack` and/or `SetByCaller.Damage.Magic` to the base damage, and
  `SetByCaller.Coefficient` when the skill lists one.
- Healing: an Instant effect with `UCrossguardHealingExecution`, using
  `SetByCaller.Healing.Physical` or `SetByCaller.Healing.Magical`.

The executions call the same formulas as the unit tests in `Tests/CrossguardRules`.

## Skill data in Unreal

`Data/Skills/Skills.json` stays the source of truth.

1. After editing it, run `python3 Tools/export_skill_table.py`. It writes
   `Data/Skills/SkillTable.json` in Unreal's data table format (`FCrossguardSkillRow` rows).
2. In the editor, import `SkillTable.json` as a Data Table with row struct `CrossguardSkillRow`
   (first time: drag it into `Content/Data/`, save as `DT_Skills`). Later: right-click → Reimport.
3. Create one `CrossguardSkillCatalog` data asset (`DA_SkillCatalog`). Set `SkillTable` to
   `DT_Skills` and add each implemented skill to `AbilityClasses` (skill id → ability class).
   Data validation flags ids not in the table and classes whose `SkillId` or category disagree.

## Loadout

`UCrossguardLoadoutComponent` (add it to the character, set `SkillCatalog`):

- `RequestLoadout(Skills)`: the server validates with `Crossguard::ValidateLoadout` against
  Skill Capacity at the character's level, then grants one ability per effective skill. Bundle
  components are granted as separate abilities; a component shared by two bundles is granted once.
- Rejections reach the owning client through `OnLoadoutRejected` with the reason.
- `ValidateLoadout(Skills)` checks without applying, for UI.
- Equipped skills replicate to everyone.

## Passive skills

Subclass `UCrossguardPassiveAbility`. It activates on the server when granted and applies its
`PassiveEffects` (Infinite effects holding the stat modifiers) until unequipped. Example: Heroic
Might = one Infinite effect adding 10 to Strength.

## Weapons and equipment

- Import `Data/Items/WeaponTable.json` as a Data Table with row struct `CrossguardWeaponRow`
  (`DT_Weapons`) and set it as the character's `EquipmentComponent.WeaponTable`.
- `UCrossguardEquipmentComponent` holds the main-hand and off-hand `FCrossguardWeaponItem`
  (weapon id, rarity, affixes). The server validates changes with `Crossguard::ValidateEquipment`.
- It applies one native `UCrossguardEquipmentEffect`: shield AR, plus `Equipment.Wielding.<Type>`
  tags (and `AnyMelee`, `TwoHandedMelee`). Mastery effects use these as tag requirements, e.g.
  Sword Mastery's Infinite effect requires `Equipment.Wielding.Sword`, so its bonuses and its
  Melee CDR only apply while a sword is held.
- Right click: `ACrossguardCharacter::TrySecondaryAction()` attacks with the off-hand weapon
  (`UCrossguardOffHandAttackAbility`) when dual wielding. Block and parry are not built yet.
- `ACrossguardCharacter::TryWeaponAttack()` activates `UCrossguardWeaponAttackAbility`: damage is
  the weapon's AD/MD at its rarity; the attack interval (weapon speed ÷ (1 + Action Speed)) limits
  the rate. Melee swings sweep for pawns in front; wands trace on the `Weapon` channel; bows and
  staffs spawn `ACrossguardProjectile` (set per-weapon projectile Blueprints for speed).

## Teams

- `ACrossguardGameMode` (default pawn `ACrossguardCharacter`, player state
  `ACrossguardPlayerState`) assigns teams on login from the `?Party=<key>` URL option through
  `Crossguard::FTeamRegistry`: same key, same team, up to 8. Logins past the cap are refused.
- `ACrossguardPlayerState::TeamId` replicates to everyone; it also implements
  `IGenericTeamAgentInterface` for AI perception.
- `UCrossguardTeamLibrary::GetTeamId` / `AreAllies` work on pawns, controllers and player states.
- Friendly fire is always on: nothing checks teams before applying damage.

## Checks

`Tools/run_checks.sh` runs the skill data validator, the table export check, the C++ struct check
and the rules unit tests.

## Not built yet

- Generating the stat-modifier effects for passives from the table (currently made by hand).
- Skill Gem ownership; restricting when the loadout can change.
- Shields (Eldritch Shield), CC, Mute, Silence, minions.
- Client-side ability system init for pawns without a PlayerState (AI). Players are covered by
  `OnRep_PlayerState`.
- Animations, weapon-on-weapon collision (fencing), input bindings.
- Affix pool and applying affix stats.
- Inventory: the server currently trusts the item a client asks to equip. Once inventory exists,
  equip by inventory slot and look the item up on the server.
- Verify in two-client PIE (`showdebug abilitysystem`) that `Equipment.Wielding.*` tags reach
  simulated proxies; they are added to the equipment effect spec's dynamic granted tags.
