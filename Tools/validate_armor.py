#!/usr/bin/env python3
"""Validate Data/Items/Armor.json against docs/item-data-schema.md."""
import json
import os
import re
import sys

SLOTS = {"Head", "Chest", "Hands", "Legs", "Feet", "Back"}
ARMOR_TYPES = {"Cloth", "Leather", "Plate"}
POOLS = {"PlateArmor", "LeatherArmor", "ClothArmor"}
STATS = {"AttackResistance", "MagicResistance", "MoveSpeed", "MaxHealth", "MaxHealthBonus",
         "Strength", "Agility", "Intelligence", "Dexterity", "ActionSpeed"}
RESISTANCES = {"AttackResistance", "MagicResistance"}
MYSTIC_LEVEL = 5
GRID_WIDTH, GRID_HEIGHT = 10, 5  # main inventory grid
ID_RE = re.compile(r"^[a-z][a-z0-9]*(_[a-z0-9]+)*$")


def is_num(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def check_fields(obj, spec, where, err):
    """spec: name -> (predicate, description). Reports missing/extra/mistyped fields."""
    if not isinstance(obj, dict):
        err(f"{where}: expected object")
        return False
    ok = True
    for name, (pred, desc) in spec.items():
        if name not in obj:
            err(f"{where}: missing field '{name}'")
            ok = False
        elif not pred(obj[name]):
            err(f"{where}: field '{name}' must be {desc}")
            ok = False
    for name in obj:
        if name not in spec:
            err(f"{where}: unknown field '{name}'")
            ok = False
    return ok


def enum(values):
    return (lambda v: isinstance(v, str) and v in values, "one of %s" % sorted(values))


def non_negative(v):
    return is_num(v) and v >= 0


def grid_size(v):
    return isinstance(v, int) and not isinstance(v, bool) and v >= 1


STAT_SPEC = {
    "stat": enum(STATS),
    "baseValue": (is_num, "number"),
    "perRarity": (non_negative, "number >= 0"),
}

ARMOR_SPEC = {
    "id": (lambda v: isinstance(v, str), "string"),
    "name": (lambda v: isinstance(v, str) and v != "", "non-empty string"),
    "slot": enum(SLOTS),
    "armorType": enum(ARMOR_TYPES),
    "gridWidth": (grid_size, "integer >= 1"),
    "gridHeight": (grid_size, "integer >= 1"),
    "stats": (lambda v: isinstance(v, list), "list"),
    "affixPool": enum(POOLS),
}


def validate(pieces):
    errors = []
    if not isinstance(pieces, list):
        return ["top level must be a JSON array"]

    ids = []
    seen = set()
    sets = {}
    for i, a in enumerate(pieces):
        aid = a.get("id") if isinstance(a, dict) else None
        label = aid if isinstance(aid, str) else f"<index {i}>"

        def err(msg, label=label):
            errors.append(f"[{label}] {msg}")

        if isinstance(aid, str):
            if not ID_RE.match(aid):
                err("id is not snake_case")
            if aid in seen:
                err("duplicate id")
            seen.add(aid)
            ids.append(aid)
        check_fields(a, ARMOR_SPEC, "armor", err)
        if not isinstance(a, dict):
            continue

        gw, gh = a.get("gridWidth"), a.get("gridHeight")
        if grid_size(gw) and grid_size(gh) and (max(gw, gh) > GRID_WIDTH or min(gw, gh) > GRID_HEIGHT):
            err(f"size {gw}x{gh} does not fit the {GRID_WIDTH}x{GRID_HEIGHT} inventory grid")

        atype, slot, pool = a.get("armorType"), a.get("slot"), a.get("affixPool")
        if atype in ARMOR_TYPES and pool in POOLS and pool != atype + "Armor":
            err(f"affixPool '{pool}' must be '{atype}Armor' to match armorType")

        if isinstance(atype, str) and isinstance(slot, str) and atype in ARMOR_TYPES and slot in SLOTS:
            key = (atype, slot)
            if key in sets:
                err(f"{atype} {slot} already used by '{sets[key]}'")
            else:
                sets[key] = label

        stats = a.get("stats")
        if isinstance(stats, list):
            used = set()
            for j, s in enumerate(stats):
                where = f"stats[{j}]"
                ok = check_fields(s, STAT_SPEC, where, err)
                if not isinstance(s, dict):
                    continue
                stat = s.get("stat")
                if isinstance(stat, str) and stat in STATS:
                    if stat in used:
                        err(f"{where}: duplicate stat '{stat}'")
                    used.add(stat)
                if ok and stat in RESISTANCES:
                    top = s["baseValue"] + MYSTIC_LEVEL * s["perRarity"]
                    if top >= 1:
                        err(f"{where}: {stat} at Mystic ({top:g}) must be < 1")

    if ids != sorted(ids):
        for a, b in zip(ids, ids[1:]):
            if a > b:
                errors.append(f"[{b}] array not sorted by id (comes after '{a}')")
                break
    return errors


def main(argv):
    if len(argv) > 2:
        print("usage: validate_armor.py [path]", file=sys.stderr)
        return 2
    if len(argv) == 2:
        path = argv[1]
    else:
        root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        path = os.path.join(root, "Data", "Items", "Armor.json")
    try:
        with open(path, encoding="utf-8") as f:
            pieces = json.load(f)
    except (OSError, ValueError) as e:
        print(f"ERROR: cannot load {path}: {e}")
        return 1
    errors = validate(pieces)
    if errors:
        for e in errors:
            print("ERROR:", e)
        print(f"{len(errors)} error(s)")
        return 1
    by_type = {}
    by_slot = {}
    for a in pieces:
        by_type[a["armorType"]] = by_type.get(a["armorType"], 0) + 1
        by_slot[a["slot"]] = by_slot.get(a["slot"], 0) + 1
    print(f"OK: {len(pieces)} armor pieces")
    for t in sorted(by_type):
        print(f"  {t}: {by_type[t]}")
    for s in sorted(by_slot):
        print(f"  {s}: {by_slot[s]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
