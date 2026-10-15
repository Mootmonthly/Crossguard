#!/usr/bin/env python3
"""Export Data/Items/Weapons.json to Unreal data table JSON (FCrossguardWeaponRow rows).

Usage:
  python3 Tools/export_weapon_table.py           # write Data/Items/WeaponTable.json
  python3 Tools/export_weapon_table.py --check   # exit 1 if WeaponTable.json is out of date

Weapons.json is the source of truth. Import WeaponTable.json in the editor as a Data Table with
row struct CrossguardWeaponRow.
"""

import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SOURCE = REPO / "Data" / "Items" / "Weapons.json"
OUTPUT = REPO / "Data" / "Items" / "WeaponTable.json"


def row(weapon):
    speed = weapon["attackSpeed"] or {}
    return {
        "Name": weapon["id"],
        "DisplayName": weapon["name"],
        "WeaponType": weapon["weaponType"],
        "Handedness": weapon["handedness"],
        "Range": weapon["range"],
        "GridWidth": weapon["gridWidth"],
        "GridHeight": weapon["gridHeight"],
        "Stat": weapon["stat"],
        "BaseValue": weapon["baseValue"],
        "PerRarity": weapon["perRarity"],
        "Attacks": speed.get("attacks", 0),
        "PerSeconds": speed.get("perSeconds", 0),
        "Delivery": weapon["delivery"],
        "AmmoId": weapon["ammo"] or "None",  # Unreal imports "None" as an empty FName
        "bCanParry": weapon["canParry"],
    }


def render(weapons):
    return json.dumps([row(w) for w in weapons], indent="\t", ensure_ascii=False) + "\n"


def main(argv):
    check = "--check" in argv[1:]
    weapons = json.loads(SOURCE.read_text(encoding="utf-8"))
    text = render(weapons)
    if check:
        current = OUTPUT.read_text(encoding="utf-8") if OUTPUT.exists() else None
        if current != text:
            print(f"{OUTPUT.relative_to(REPO)} is out of date; run Tools/export_weapon_table.py")
            return 1
        print(f"{OUTPUT.relative_to(REPO)} is up to date ({len(weapons)} rows)")
        return 0
    OUTPUT.write_text(text, encoding="utf-8")
    print(f"Wrote {OUTPUT.relative_to(REPO)} ({len(weapons)} rows)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
