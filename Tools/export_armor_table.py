#!/usr/bin/env python3
"""Export Data/Items/Armor.json to Unreal data table JSON (FCrossguardArmorRow rows).

Usage:
  python3 Tools/export_armor_table.py           # write Data/Items/ArmorTable.json
  python3 Tools/export_armor_table.py --check   # exit 1 if ArmorTable.json is out of date

Armor.json is the source of truth. Import ArmorTable.json in the editor as a Data Table with
row struct CrossguardArmorRow.
"""

import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SOURCE = REPO / "Data" / "Items" / "Armor.json"
OUTPUT = REPO / "Data" / "Items" / "ArmorTable.json"


def row(piece):
    return {
        "Name": piece["id"],
        "DisplayName": piece["name"],
        "Slot": piece["slot"],
        "ArmorType": piece["armorType"],
        "GridWidth": piece["gridWidth"],
        "GridHeight": piece["gridHeight"],
        "Stats": [
            {"Stat": s["stat"], "BaseValue": s["baseValue"], "PerRarity": s["perRarity"]}
            for s in piece["stats"]
        ],
        "AffixPool": piece["affixPool"],
    }


def render(pieces):
    return json.dumps([row(p) for p in pieces], indent="\t", ensure_ascii=False) + "\n"


def main(argv):
    check = "--check" in argv[1:]
    pieces = json.loads(SOURCE.read_text(encoding="utf-8"))
    text = render(pieces)
    if check:
        current = OUTPUT.read_text(encoding="utf-8") if OUTPUT.exists() else None
        if current != text:
            print(f"{OUTPUT.relative_to(REPO)} is out of date; run Tools/export_armor_table.py")
            return 1
        print(f"{OUTPUT.relative_to(REPO)} is up to date ({len(pieces)} rows)")
        return 0
    OUTPUT.write_text(text, encoding="utf-8")
    print(f"Wrote {OUTPUT.relative_to(REPO)} ({len(pieces)} rows)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
