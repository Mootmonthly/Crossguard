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
| `Tools/validate_skills.py` | Validates `Skills.json`. |
| `docs/design-decisions.md` | Design rulings; overrides the Drive design docs. |

## Setup

1. Install Unreal Engine 5.7 (or right-click `Crossguard.uproject` → *Switch Unreal Engine version*).
2. Install Git LFS and run `git lfs install` once.
3. Right-click `Crossguard.uproject` → *Generate Visual Studio project files*, then build the
   `CrossguardEditor` target.

The `CrossguardServer` target needs a source build of the engine.

## Checks

```sh
cmake -S Tests/CrossguardRules -B build/rules-tests
cmake --build build/rules-tests
ctest --test-dir build/rules-tests --output-on-failure

python3 Tools/validate_skills.py
```
