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
- `CooldownGameplayEffectClass`: the shared cooldown effect (below).

### Shared cooldown effect

Create one Blueprint effect, `GE_Cooldown`:

- Duration Policy: Has Duration.
- Duration Magnitude: Set by Caller, tag `SetByCaller.Cooldown`.

The ability fills the duration with its cooldown after CDR, and adds its `CooldownTags`.
CDR = category CDR + matching Melee/Ranged/Minion CDR, summed and capped at 50%.

Cast time: call `GetReducedCastTime()` for wait tasks and cast bars. Uncapped.

### Damage and healing effects

- Damage: an Instant effect with the execution `UCrossguardDamageExecution`. Set
  `SetByCaller.Damage.Attack` and/or `SetByCaller.Damage.Magic` to the base damage, and
  `SetByCaller.Coefficient` when the skill lists one.
- Healing: an Instant effect with `UCrossguardHealingExecution`, using
  `SetByCaller.Healing.Physical` or `SetByCaller.Healing.Magical`.

The executions call the same formulas as the unit tests in `Tests/CrossguardRules`.

## Not built yet

- Loading `Skills.json` into Unreal (a data table, or an import step that generates skill assets).
- Loadout component using `Crossguard::ValidateLoadout`.
- Shields (Eldritch Shield), CC, Mute, Silence, minions.
- Weapons and AD. Abilities pass weapon AD as the `SetByCaller.Damage.Attack` base for now.
