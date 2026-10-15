#!/usr/bin/env python3
"""Export Data/Loot/LootTables.json to Unreal data table JSON (FCrossguardLootTableRow rows).

Usage:
  python3 Tools/export_loot_table.py           # write Data/Loot/LootTable.json
  python3 Tools/export_loot_table.py --check   # exit 1 if LootTable.json is out of date

LootTables.json is the source of truth, plus each mob's loot in Data/Mobs/Mobs.json (exported as the
table "mob_<mob id>"). Import LootTable.json in the editor as a Data Table with row struct
CrossguardLootTableRow.
"""

import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SOURCE = REPO / "Data" / "Loot" / "LootTables.json"
OUTPUT = REPO / "Data" / "Loot" / "LootTable.json"
MOBS = REPO / "Data" / "Mobs" / "Mobs.json"
RARITIES = ["Common", "Uncommon", "Rare", "Epic", "Legendary", "Mystic"]


def row(table):
    return {
        "Name": table["id"],
        "RollsMin": table["rolls"][0],
        "RollsMax": table["rolls"][1],
        "NothingWeight": table["nothingWeight"],
        "RarityWeights": [table["rarityWeights"][r] for r in RARITIES],
        "Entries": [
            {
                "Kind": e["kind"],
                "Id": e["id"] or "None",
                "bHasArmorType": e.get("armorType") is not None,
                "ArmorType": e.get("armorType") or "Cloth",
                "Weight": e["weight"],
                "CountMin": e["count"][0],
                "CountMax": e["count"][1],
            }
            for e in table["entries"]
        ],
    }


def render(tables):
    return json.dumps([row(t) for t in tables], indent="\t", ensure_ascii=False) + "\n"


def main(argv):
    check = "--check" in argv[1:]
    tables = json.loads(SOURCE.read_text(encoding="utf-8"))
    if MOBS.exists():
        tables += [{"id": "mob_" + m["id"], **m["loot"]} for m in json.loads(MOBS.read_text(encoding="utf-8"))]
    text = render(tables)
    if check:
        current = OUTPUT.read_text(encoding="utf-8") if OUTPUT.exists() else None
        if current != text:
            print(f"{OUTPUT.relative_to(REPO)} is out of date; run Tools/export_loot_table.py")
            return 1
        print(f"{OUTPUT.relative_to(REPO)} is up to date ({len(tables)} rows)")
        return 0
    OUTPUT.write_text(text, encoding="utf-8")
    print(f"Wrote {OUTPUT.relative_to(REPO)} ({len(tables)} rows)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
