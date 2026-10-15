#!/usr/bin/env python3
"""Check the generated data tables against their C++ row structs.

Data/Skills/SkillTable.json -> FCrossguardSkillRow (CrossguardSkillTypes.h)
Data/Items/WeaponTable.json -> FCrossguardWeaponRow (CrossguardItemTypes.h)

Every JSON field must be a UPROPERTY of the matching struct, every struct field must be present,
and every enum value must be an entry of the C++ enum. Catches drift between the exporter and the
header without needing Unreal.

Usage: python3 Tools/check_table_types.py
"""

import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PUBLIC = REPO / "Source" / "Crossguard" / "Public"
HEADERS = [PUBLIC / "Skills" / "CrossguardSkillTypes.h", PUBLIC / "Items" / "CrossguardItemTypes.h"]
TABLES = [
    (REPO / "Data" / "Skills" / "SkillTable.json", "FCrossguardSkillRow"),
    (REPO / "Data" / "Items" / "WeaponTable.json", "FCrossguardWeaponRow"),
]


def parse_header(text):
    enums = {
        m.group(1): re.findall(r"^\s*(\w+),", m.group(2), re.M)
        for m in re.finditer(r"enum class (\w+) : uint8\s*\{(.*?)\};", text, re.S)
    }
    structs = {}
    for m in re.finditer(r"struct CROSSGUARD_API (\w+)[^{]*\{(.*?)\n\};", text, re.S):
        fields = re.findall(r"UPROPERTY\((?:[^()]|\([^()]*\))*\)\s*\n\s*([\w<>]+) (\w+)", m.group(2))
        structs[m.group(1)] = {name: cpp_type for cpp_type, name in fields}
    return enums, structs


def check(obj, struct, path, enums, structs, errors, is_row=False):
    fields = structs[struct]
    for key, value in obj.items():
        if is_row and key == "Name":
            continue
        if key not in fields:
            errors.append(f"{path}.{key}: not a field of {struct}")
            continue
        array = re.fullmatch(r"TArray<(\w+)>", fields[key])
        base = array.group(1) if array else fields[key]
        for item in (value if array else [value]):
            if base in enums and item not in enums[base]:
                errors.append(f"{path}.{key}: {item!r} is not an entry of {base}")
            if base in structs:
                check(item, base, f"{path}.{key}", enums, structs, errors)
    for field in fields:
        if field not in obj:
            errors.append(f"{path}: missing field {field}")


def main():
    enums, structs = {}, {}
    for header in HEADERS:
        header_enums, header_structs = parse_header(header.read_text(encoding="utf-8"))
        enums.update(header_enums)
        structs.update(header_structs)

    failed = False
    for table, row_struct in TABLES:
        if row_struct not in structs:
            print(f"ERROR: {row_struct} not found in the headers")
            failed = True
            continue
        rows = json.loads(table.read_text(encoding="utf-8"))
        errors = []
        for row in rows:
            check(row, row_struct, row.get("Name", "?"), enums, structs, errors, is_row=True)
        for error in errors:
            print(f"ERROR: {table.name}: {error}")
        if errors:
            failed = True
        else:
            print(f"OK: {table.name}: {len(rows)} rows match {row_struct}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
