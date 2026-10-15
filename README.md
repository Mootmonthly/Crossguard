# Crossguard
Fantasy extraction game. Unreal Engine 5 with the Gameplay Ability System.

## Layout

| Path | Contents |
| --- | --- |
| `Crossguard.uproject` | Unreal project (GameplayAbilities and PCG plugins enabled). |
| `Source/Crossguard` | Primary game module. |
| `Source/CrossguardRules` | Engine-free game rules: progression, derived stats, damage, cooldowns, loadout validation. |
| `Tests/CrossguardRules` | Standalone unit tests for `CrossguardRules`. |
| `Data/Skills/Skills.json` | Skill definitions (schema in `docs/skill-data-schema.md`). |
| `Data/Skills/SkillTable.json` | Generated Unreal data table import. Do not edit. |
| `Tools/` | Data validation, table export and check scripts. |
| `docs/design-decisions.md` | Design rulings; overrides the Drive design docs. |

## Setup

1. Install Unreal Engine 5.7 (or right-click `Crossguard.uproject` → *Switch Unreal Engine version*).
2. Install Git LFS and run `git lfs install` once.
3. Right-click `Crossguard.uproject` → *Generate Visual Studio project files*, then build the
   `CrossguardEditor` target.

The `CrossguardServer` target needs a source build of the engine.

## Checks

```sh
Tools/run_checks.sh
```

Runs the skill data validator, the data table export check, the C++ struct check and the rules
unit tests. None of them need Unreal.
