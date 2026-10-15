#!/usr/bin/env python3
"""Validate Data/Items/Items.json against docs/item-data-schema.md."""
import json
import os
import re
import sys

CATEGORIES = {"Ammo", "Consumable", "Throwable", "Material", "Currency", "Trinket", "Tool"}
GRID_WIDTH, GRID_HEIGHT = 10, 5  # main inventory grid
# Stack size is set per category (docs/design-decisions.md). Materials, trinkets and tools are not ruled yet.
STACK_BY_CATEGORY = {"Ammo": 20, "Throwable": 3, "Consumable": 5, "Currency": 100}
ID_RE = re.compile(r"^[a-z][a-z0-9]*(_[a-z0-9]+)*$")


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


def is_num(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def grid_size(v):
    return isinstance(v, int) and not isinstance(v, bool) and v >= 1


def non_negative_int(v):
    return isinstance(v, int) and not isinstance(v, bool) and v >= 0


ITEM_SPEC = {
    "id": (lambda v: isinstance(v, str), "string"),
    "name": (lambda v: isinstance(v, str) and v != "", "non-empty string"),
    "category": enum(CATEGORIES),
    "gridWidth": (grid_size, "integer >= 1"),
    "gridHeight": (grid_size, "integer >= 1"),
    "maxStack": (grid_size, "integer >= 1"),
    "sellValue": (non_negative_int, "integer >= 0"),
    "sellPerRarity": (non_negative_int, "integer >= 0"),
}


CONSUMABLE_SPEC = {
    "healAmount": (lambda v: is_num(v) and v > 0, "number > 0"),
    "healPerRarity": (lambda v: is_num(v) and v >= 0, "number >= 0"),
    "healDuration": (lambda v: is_num(v) and v > 0, "number > 0"),
    "useTime": (lambda v: is_num(v) and v >= 0, "number >= 0"),
}

WEAPON_TYPES = {
    "Sword", "Axe", "Dagger", "Spear", "Mace", "Quarterstaff", "Hammer", "Shield",
    "Bow", "Crossbow", "Sling", "Throwing", "Wand", "MagicStaff",
}

THROWABLE_SPEC = {
    "throwDamageType": enum({"Attack", "Magic"}),
    "throwDamage": (lambda v: is_num(v) and v > 0, "number > 0"),
    "throwDamagePerRarity": (lambda v: is_num(v) and v >= 0, "number >= 0"),
    "throwRadius": (lambda v: is_num(v) and v >= 0, "number >= 0"),
    "throwDotPerSecond": (lambda v: is_num(v) and v >= 0, "number >= 0"),
    "throwDotSeconds": (lambda v: is_num(v) and v >= 0, "number >= 0"),
    "countsAsWeapon": (lambda v: v is None or (isinstance(v, str) and v in WEAPON_TYPES),
                       "null or one of %s" % sorted(WEAPON_TYPES)),
    "impactPower": (non_negative_int, "integer >= 0"),
}


def validate(items):
    errors = []
    if not isinstance(items, list):
        return ["top level must be a JSON array"]

    ids = []
    seen = set()
    for i, it in enumerate(items):
        iid = it.get("id") if isinstance(it, dict) else None
        label = iid if isinstance(iid, str) else f"<index {i}>"

        def err(msg, label=label):
            errors.append(f"[{label}] {msg}")

        if isinstance(iid, str):
            if not ID_RE.match(iid):
                err("id is not snake_case")
            if iid in seen:
                err("duplicate id")
            seen.add(iid)
            ids.append(iid)
        spec = dict(ITEM_SPEC)
        if isinstance(it, dict) and it.get("category") == "Consumable":
            spec.update(CONSUMABLE_SPEC)
        if isinstance(it, dict) and it.get("category") == "Throwable":
            spec.update(THROWABLE_SPEC)
        check_fields(it, spec, "item", err)
        if not isinstance(it, dict):
            continue
        if it.get("category") != "Consumable":
            for name in CONSUMABLE_SPEC:
                if name in it:
                    err(f"field '{name}' is only allowed for Consumable items")
        if it.get("category") != "Throwable":
            for name in THROWABLE_SPEC:
                if name in it:
                    err(f"field '{name}' is only allowed for Throwable items")
        elif is_num(it.get("throwDotPerSecond")) and is_num(it.get("throwDotSeconds")):
            if it["throwDotPerSecond"] > 0 and it["throwDotSeconds"] <= 0:
                err("throwDotSeconds must be > 0 when throwDotPerSecond > 0")
            if it["throwDotPerSecond"] == 0 and it["throwDotSeconds"] != 0:
                err("throwDotSeconds must be 0 when throwDotPerSecond is 0")

        gw, gh = it.get("gridWidth"), it.get("gridHeight")
        if grid_size(gw) and grid_size(gh) and (max(gw, gh) > GRID_WIDTH or min(gw, gh) > GRID_HEIGHT):
            err(f"size {gw}x{gh} does not fit the {GRID_WIDTH}x{GRID_HEIGHT} inventory grid")

        expected = STACK_BY_CATEGORY.get(it.get("category"))
        if expected is not None and it.get("maxStack") != expected:
            err(f"maxStack must be {expected} for {it.get('category')}")

        if it.get("category") == "Trinket" and non_negative_int(it.get("sellValue")) and it["sellValue"] == 0:
            err("sellValue must be > 0 for Trinket")

    if ids != sorted(ids):
        for a, b in zip(ids, ids[1:]):
            if a > b:
                errors.append(f"[{b}] array not sorted by id (comes after '{a}')")
                break
    return errors


def main(argv):
    if len(argv) > 2:
        print("usage: validate_items.py [path]", file=sys.stderr)
        return 2
    if len(argv) == 2:
        path = argv[1]
    else:
        root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        path = os.path.join(root, "Data", "Items", "Items.json")
    try:
        with open(path, encoding="utf-8") as f:
            items = json.load(f)
    except (OSError, ValueError) as e:
        print(f"ERROR: cannot load {path}: {e}")
        return 1
    errors = validate(items)
    if errors:
        for e in errors:
            print("ERROR:", e)
        print(f"{len(errors)} error(s)")
        return 1
    counts = {}
    for it in items:
        counts[it["category"]] = counts.get(it["category"], 0) + 1
    print(f"OK: {len(items)} items")
    for c in sorted(counts):
        print(f"  {c}: {counts[c]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
