#!/usr/bin/env python3
"""Validate Data/Items/Affixes.json against docs/item-data-schema.md."""
import json
import os
import re
import sys

STATS = {"MoveSpeed", "AttackResistance", "MagicResistance", "MaxHealth", "MaxHealthBonus", "AdditionalAttackDamage", "AdditionalMagicDamage", "ActionSpeed", "AttackPenetration",
         "MagicPenetration", "TrueDamage", "AttackDamageBonus", "MagicDamageBonus",
         "ActiveCooldownReduction", "SpellCooldownReduction", "ArtCooldownReduction",
         "ActiveCastTimeReduction", "SpellCastTimeReduction", "ArtCastTimeReduction",
         "Strength", "Agility", "Intelligence", "Dexterity"}
POOLS = {"AttackWeapon", "MagicWeapon", "Shield", "PlateArmor", "LeatherArmor", "ClothArmor"}
ID_RE = re.compile(r"^[a-z][a-z0-9]*(_[a-z0-9]+)*$")
GRID_TOLERANCE = 1e-9


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


def positive(v):
    return is_num(v) and v > 0


def pool_list(v):
    return (isinstance(v, list) and len(v) > 0
            and all(isinstance(p, str) and p in POOLS for p in v))


AFFIX_SPEC = {
    "id": (lambda v: isinstance(v, str), "string"),
    "name": (lambda v: isinstance(v, str) and v != "", "non-empty string"),
    "stat": enum(STATS),
    "min": (is_num, "number"),
    "max": (is_num, "number"),
    "step": (positive, "number > 0"),
    "pools": (pool_list, "non-empty list of %s" % sorted(POOLS)),
}


def validate(affixes):
    errors = []
    if not isinstance(affixes, list):
        return ["top level must be a JSON array"]

    ids = []
    seen = set()
    stats = {}
    for i, a in enumerate(affixes):
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
        check_fields(a, AFFIX_SPEC, "affix", err)
        if not isinstance(a, dict):
            continue

        stat = a.get("stat")
        if isinstance(stat, str) and stat in STATS:
            if stat in stats:
                err(f"stat '{stat}' already used by '{stats[stat]}'")
            else:
                stats[stat] = label

        lo, hi, step = a.get("min"), a.get("max"), a.get("step")
        if is_num(lo) and is_num(hi):
            if lo > hi:
                err(f"min ({lo:g}) must be <= max ({hi:g})")
            elif positive(step):
                k = (hi - lo) / step
                if abs(k - round(k)) > GRID_TOLERANCE:
                    err(f"max ({hi:g}) is not on the grid min + k*step (step {step:g})")

        pools = a.get("pools")
        if isinstance(pools, list) and len(pools) != len(set(map(str, pools))):
            err("pools contains duplicates")

    if ids != sorted(ids):
        for a, b in zip(ids, ids[1:]):
            if a > b:
                errors.append(f"[{b}] array not sorted by id (comes after '{a}')")
                break
    return errors


def main(argv):
    if len(argv) > 2:
        print("usage: validate_affixes.py [path]", file=sys.stderr)
        return 2
    if len(argv) == 2:
        path = argv[1]
    else:
        root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        path = os.path.join(root, "Data", "Items", "Affixes.json")
    try:
        with open(path, encoding="utf-8") as f:
            affixes = json.load(f)
    except (OSError, ValueError) as e:
        print(f"ERROR: cannot load {path}: {e}")
        return 1
    errors = validate(affixes)
    if errors:
        for e in errors:
            print("ERROR:", e)
        print(f"{len(errors)} error(s)")
        return 1
    counts = {}
    for a in affixes:
        for p in a["pools"]:
            counts[p] = counts.get(p, 0) + 1
    print(f"OK: {len(affixes)} affixes")
    for p in sorted(counts):
        print(f"  {p}: {counts[p]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
