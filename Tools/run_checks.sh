#!/usr/bin/env bash
# Runs every check that works without Unreal: skill data, generated table, rules unit tests, and the
# Nakama Go module tests when Go is installed.
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

# Nakama runtime module (Backend/nakama/modules). Needs Go 1.27.1+ (older Go downloads it: GOTOOLCHAIN=auto).
if command -v go >/dev/null 2>&1; then
	(cd Backend/nakama/modules && go vet ./... && go test ./...)
else
	echo "Go not installed: skipping the Nakama module tests (Backend/nakama/modules)."
fi
