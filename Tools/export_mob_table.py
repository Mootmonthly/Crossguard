#!/usr/bin/env python3
"""Export Data/Mobs/Mobs.json to Unreal data table JSON (FCrossguardMobRow rows).

Usage:
  python3 Tools/export_mob_table.py           # write Data/Mobs/MobTable.json
  python3 Tools/export_mob_table.py --check   # exit 1 if MobTable.json is out of date

Mobs.json is the source of truth. Distances go from meters to centimeters. A mob's weapon id becomes
bWieldsWeapon, WeaponType ("Sword" when it has none), bTwoHandedWeapon and ImpactPower (3 when it has none) from
Data/Items/Weapons.json. Each mob's loot is
exported by Tools/export_loot_table.py as the loot table "mob_<id>". Import MobTable.json in the
editor as a Data Table with row struct CrossguardMobRow.
"""

import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SOURCE = REPO / "Data" / "Mobs" / "Mobs.json"
OUTPUT = REPO / "Data" / "Mobs" / "MobTable.json"
WEAPONS = REPO / "Data" / "Items" / "Weapons.json"
# Impact Power without a weapon: natural attacks (Warrior, Mage) and a Ranged mob's bow. Placeholder.
NATURAL_IMPACT_POWER = 3


def row(mob, weapons):
    weapon = weapons[mob["weapon"]] if mob["weapon"] else None
    return {
        "Name": mob["id"],
        "DisplayName": mob["name"],
        "Kind": mob["kind"],
        "CombatRole": mob["combatRole"],
        "bWieldsWeapon": weapon is not None,
        "WeaponType": weapon["weaponType"] if weapon else "Sword",
        "bTwoHandedWeapon": bool(weapon) and weapon["handedness"] == "TwoHanded",
        "ImpactPower": weapon["impactPower"] if weapon else NATURAL_IMPACT_POWER,
        "Health": mob["health"],
        "AttackDamage": mob["attackDamage"],
        "AttackInterval": mob["attackInterval"],
        "AttackRange": round(mob["attackRange"] * 100, 3),
        "FireballInterval": mob["fireballInterval"],
        "KeepDistance": round(mob["keepDistance"] * 100, 3),
        "AggroRange": round(mob["aggroRange"] * 100, 3),
        "LeashRange": round(mob["leashRange"] * 100, 3),
        "MoveSpeed": round(mob["moveSpeed"] * 100, 3),
        "Scale": mob["scale"],
        "Skills": mob["skills"],
        "LootTableId": "mob_" + mob["id"],
        # Art (optional in the source): an empty path keeps the placeholder cylinder.
        "Mesh": mob.get("mesh", ""),
        "AnimClass": mob.get("animBlueprint", ""),
        "MeshScale": mob.get("meshScale", 1),
        "MeshOffset": {k: round(v * 100, 3) for k, v in zip(("X", "Y", "Z"), mob.get("meshOffset", [0, 0, 0]))},
        "MeshYaw": mob.get("meshYaw", -90),
    }


def render(mobs):
    weapons = {w["id"]: w for w in json.loads(WEAPONS.read_text(encoding="utf-8"))}
    return json.dumps([row(m, weapons) for m in mobs], indent="\t", ensure_ascii=False) + "\n"


def main(argv):
    check = "--check" in argv[1:]
    mobs = json.loads(SOURCE.read_text(encoding="utf-8"))
    text = render(mobs)
    if check:
        current = OUTPUT.read_text(encoding="utf-8") if OUTPUT.exists() else None
        if current != text:
            print(f"{OUTPUT.relative_to(REPO)} is out of date; run Tools/export_mob_table.py")
            return 1
        print(f"{OUTPUT.relative_to(REPO)} is up to date ({len(mobs)} rows)")
        return 0
    OUTPUT.write_text(text, encoding="utf-8")
    print(f"Wrote {OUTPUT.relative_to(REPO)} ({len(mobs)} rows)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
