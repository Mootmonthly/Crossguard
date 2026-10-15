#!/usr/bin/env python3
"""Check Data/Skills/SkillTable.json against the C++ row structs in CrossguardSkillTypes.h.

Every JSON field must be a UPROPERTY of the matching struct, every struct field must be present,
and every enum value must be an entry of the C++ enum. Catches drift between the exporter and the
header without needing Unreal.

Usage: python3 Tools/check_skill_types.py
"""

import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
HEADER = REPO / "Source" / "Crossguard" / "Public" / "Skills" / "CrossguardSkillTypes.h"
TABLE = REPO / "Data" / "Skills" / "SkillTable.json"
ROW_STRUCT = "FCrossguardSkillRow"


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


def check(obj, struct, path, enums, structs, errors):
    fields = structs[struct]
    for key, value in obj.items():
        if struct == ROW_STRUCT and key == "Name":
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
    enums, structs = parse_header(HEADER.read_text(encoding="utf-8"))
    if ROW_STRUCT not in structs:
        print(f"ERROR: {ROW_STRUCT} not found in {HEADER.name}")
        return 1
    rows = json.loads(TABLE.read_text(encoding="utf-8"))
    errors = []
    for row in rows:
        check(row, ROW_STRUCT, row.get("Name", "?"), enums, structs, errors)
    for error in errors:
        print(f"ERROR: {error}")
    if errors:
        return 1
    print(f"OK: {len(rows)} rows match {ROW_STRUCT} ({len(structs)} structs, {len(enums)} enums)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
