#!/usr/bin/env bash
# Runs every check that works without Unreal: skill data, generated table, rules unit tests.
set -euo pipefail
cd "$(dirname "$0")/.."

python3 Tools/validate_skills.py
python3 Tools/export_skill_table.py --check
python3 Tools/validate_weapons.py
python3 Tools/export_weapon_table.py --check
python3 Tools/validate_affixes.py
python3 Tools/export_affix_table.py --check
python3 Tools/validate_armor.py
python3 Tools/export_armor_table.py --check
python3 Tools/validate_items.py
python3 Tools/export_item_table.py --check
python3 Tools/validate_loot.py
python3 Tools/validate_mobs.py
python3 Tools/export_loot_table.py --check
python3 Tools/export_mob_table.py --check
python3 Tools/check_table_types.py
# Unreal-only script: syntax check only (it imports the editor's `unreal` module at run time).
python3 -m py_compile Tools/editor_setup.py

cmake -S Tests/CrossguardRules -B build/rules-tests >/dev/null
cmake --build build/rules-tests
ctest --test-dir build/rules-tests --output-on-failure
