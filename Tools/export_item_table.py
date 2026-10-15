#!/usr/bin/env python3
"""Export Data/Items/Items.json to Unreal data table JSON (FCrossguardItemRow rows).

Usage:
  python3 Tools/export_item_table.py           # write Data/Items/ItemTable.json
  python3 Tools/export_item_table.py --check   # exit 1 if ItemTable.json is out of date

Items.json is the source of truth. Import ItemTable.json in the editor as a Data Table with
row struct CrossguardItemRow.
"""

import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SOURCE = REPO / "Data" / "Items" / "Items.json"
OUTPUT = REPO / "Data" / "Items" / "ItemTable.json"


def row(item):
    return {
        "Name": item["id"],
        "DisplayName": item["name"],
        "Category": item["category"],
        "GridWidth": item["gridWidth"],
        "GridHeight": item["gridHeight"],
        "MaxStack": item["maxStack"],
        "SellValue": item["sellValue"],
        "SellPerRarity": item["sellPerRarity"],
        "HealAmount": item.get("healAmount", 0),
        "HealPerRarity": item.get("healPerRarity", 0),
        "HealDuration": item.get("healDuration", 0),
        "UseTime": item.get("useTime", 0),
        "ThrowDamageType": item.get("throwDamageType", "Attack"),
        "ThrowDamage": item.get("throwDamage", 0),
        "ThrowDamagePerRarity": item.get("throwDamagePerRarity", 0),
        "ThrowRadiusMeters": item.get("throwRadius", 0),
        "ThrowDotPerSecond": item.get("throwDotPerSecond", 0),
        "ThrowDotSeconds": item.get("throwDotSeconds", 0),
        "CountsAsWeapon": item.get("countsAsWeapon") or "Sword",
        "bHasCountsAsWeapon": item.get("countsAsWeapon") is not None,
        "ImpactPower": item.get("impactPower", 0),
    }


def render(items):
    return json.dumps([row(i) for i in items], indent="\t", ensure_ascii=False) + "\n"


def main(argv):
    check = "--check" in argv[1:]
    items = json.loads(SOURCE.read_text(encoding="utf-8"))
    text = render(items)
    if check:
        current = OUTPUT.read_text(encoding="utf-8") if OUTPUT.exists() else None
        if current != text:
            print(f"{OUTPUT.relative_to(REPO)} is out of date; run Tools/export_item_table.py")
            return 1
        print(f"{OUTPUT.relative_to(REPO)} is up to date ({len(items)} rows)")
        return 0
    OUTPUT.write_text(text, encoding="utf-8")
    print(f"Wrote {OUTPUT.relative_to(REPO)} ({len(items)} rows)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
