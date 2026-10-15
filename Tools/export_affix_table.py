#!/usr/bin/env python3
"""Export Data/Items/Affixes.json to Unreal data table JSON (FCrossguardAffixRow rows).

Usage:
  python3 Tools/export_affix_table.py           # write Data/Items/AffixTable.json
  python3 Tools/export_affix_table.py --check   # exit 1 if AffixTable.json is out of date

Affixes.json is the source of truth. Import AffixTable.json in the editor as a Data Table with
row struct CrossguardAffixRow.
"""

import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SOURCE = REPO / "Data" / "Items" / "Affixes.json"
OUTPUT = REPO / "Data" / "Items" / "AffixTable.json"


def row(affix):
    return {
        "Name": affix["id"],
        "DisplayName": affix["name"],
        "Stat": affix["stat"],
        "Min": affix["min"],
        "Max": affix["max"],
        "Step": affix["step"],
        "Pools": affix["pools"],
    }


def render(affixes):
    return json.dumps([row(a) for a in affixes], indent="\t", ensure_ascii=False) + "\n"


def main(argv):
    check = "--check" in argv[1:]
    affixes = json.loads(SOURCE.read_text(encoding="utf-8"))
    text = render(affixes)
    if check:
        current = OUTPUT.read_text(encoding="utf-8") if OUTPUT.exists() else None
        if current != text:
            print(f"{OUTPUT.relative_to(REPO)} is out of date; run Tools/export_affix_table.py")
            return 1
        print(f"{OUTPUT.relative_to(REPO)} is up to date ({len(affixes)} rows)")
        return 0
    OUTPUT.write_text(text, encoding="utf-8")
    print(f"Wrote {OUTPUT.relative_to(REPO)} ({len(affixes)} rows)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
