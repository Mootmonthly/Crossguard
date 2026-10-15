#!/usr/bin/env python3
"""Validate Data/Items/Weapons.json against docs/item-data-schema.md."""
import json
import os
import re
import sys

WEAPON_TYPES = {"Sword", "Axe", "Dagger", "Spear", "Mace", "Quarterstaff", "Hammer", "Shield", "Bow",
                "Crossbow", "Sling", "Throwing", "Wand", "MagicStaff", "AnyMelee", "TwoHandedMelee"}
HANDEDNESS = {"OneHanded", "TwoHanded"}
RANGES = {"Melee", "Ranged"}
STATS = {"AttackDamage", "MagicDamage", "AttackResistance"}
DELIVERIES = {"MeleeSwing", "Hitscan", "Projectile", "None"}
MAX_RARITY_LEVEL = 5
GRID_WIDTH, GRID_HEIGHT = 10, 5  # main inventory grid
ITEMS_PATH = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Data", "Items", "Items.json")
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


def positive(v):
    return is_num(v) and v > 0


def grid_size(v):
    return isinstance(v, int) and not isinstance(v, bool) and v >= 1


WEAPON_SPEC = {
    "id": (lambda v: isinstance(v, str), "string"),
    "name": (lambda v: isinstance(v, str) and v != "", "non-empty string"),
    "weaponType": enum(WEAPON_TYPES),
    "handedness": enum(HANDEDNESS),
    "range": enum(RANGES),
    "gridWidth": (grid_size, "integer >= 1"),
    "gridHeight": (grid_size, "integer >= 1"),
    "stat": enum(STATS),
    "baseValue": (positive, "number > 0"),
    "perRarity": (lambda v: is_num(v) and v >= 0, "number >= 0"),
    "attackSpeed": (lambda v: v is None or isinstance(v, dict), "object or null"),
    "delivery": enum(DELIVERIES),
    "ammo": (lambda v: v is None or isinstance(v, str), "null or an Ammo item id"),
    "canParry": (lambda v: isinstance(v, bool), "boolean"),
    "impactPower": (lambda v: isinstance(v, int) and not isinstance(v, bool) and v >= 1, "integer >= 1"),
}
SPEED_SPEC = {
    "attacks": (positive, "number > 0"),
    "perSeconds": (positive, "number > 0"),
}


def load_ammo_ids(path=ITEMS_PATH):
    """Ids of the Ammo entries in Items.json."""
    with open(path, encoding="utf-8") as f:
        items = json.load(f)
    return {i["id"] for i in items if isinstance(i, dict) and i.get("category") == "Ammo"
            and isinstance(i.get("id"), str)}


def validate(weapons, ammo_ids=None):
    errors = []
    if ammo_ids is None:
        ammo_ids = load_ammo_ids()
    if not isinstance(weapons, list):
        return ["top level must be a JSON array"]

    ids = []
    seen = set()
    for i, w in enumerate(weapons):
        wid = w.get("id") if isinstance(w, dict) else None
        label = wid if isinstance(wid, str) else f"<index {i}>"

        def err(msg, label=label):
            errors.append(f"[{label}] {msg}")

        if isinstance(wid, str):
            if not ID_RE.match(wid):
                err("id is not snake_case")
            if wid in seen:
                err("duplicate id")
            seen.add(wid)
            ids.append(wid)
        check_fields(w, WEAPON_SPEC, "weapon", err)
        if not isinstance(w, dict):
            continue

        gw, gh = w.get("gridWidth"), w.get("gridHeight")
        if grid_size(gw) and grid_size(gh) and (max(gw, gh) > GRID_WIDTH or min(gw, gh) > GRID_HEIGHT):
            err(f"size {gw}x{gh} does not fit the {GRID_WIDTH}x{GRID_HEIGHT} inventory grid")

        speed = w.get("attackSpeed")
        if isinstance(speed, dict):
            check_fields(speed, SPEED_SPEC, "attackSpeed", err)

        delivery = w.get("delivery")
        rng = w.get("range")
        stat = w.get("stat")
        wtype = w.get("weaponType")

        ammo = w.get("ammo")
        if isinstance(ammo, str) and ammo not in ammo_ids:
            err(f"ammo '{ammo}' is not an Ammo item in Items.json (known: {sorted(ammo_ids)})")

        # attackSpeed null iff delivery None
        if "attackSpeed" in w and (speed is None or isinstance(speed, dict)) and delivery in DELIVERIES:
            if (speed is None) != (delivery == "None"):
                err("attackSpeed must be null if and only if delivery is 'None'")

        if stat == "AttackResistance":
            if wtype != "Shield":
                err("stat AttackResistance is only allowed for weaponType Shield")
            if "attackSpeed" in w and speed is not None:
                err("AttackResistance weapon must have attackSpeed null")
            base, per = w.get("baseValue"), w.get("perRarity")
            if is_num(base) and is_num(per) and base + MAX_RARITY_LEVEL * per >= 1:
                err(f"AttackResistance at Mystic ({base + MAX_RARITY_LEVEL * per:g}) must be < 1")

        if wtype == "Shield" and w.get("handedness") in HANDEDNESS and w["handedness"] != "OneHanded":
            err("Shield must be OneHanded")

        if rng == "Melee" and delivery in DELIVERIES and delivery not in ("MeleeSwing", "None"):
            err(f"Melee range requires delivery MeleeSwing or None, got '{delivery}'")
        if rng == "Ranged" and delivery in DELIVERIES and delivery not in ("Hitscan", "Projectile"):
            err(f"Ranged range requires delivery Hitscan or Projectile, got '{delivery}'")
        # Shields block instead; bows cannot parry.
        if w.get("canParry") is True and w.get("weaponType") in ("Shield", "Bow"):
            err(f"{w.get('weaponType')} cannot parry")

    if ids != sorted(ids):
        for a, b in zip(ids, ids[1:]):
            if a > b:
                errors.append(f"[{b}] array not sorted by id (comes after '{a}')")
                break
    return errors


def main(argv):
    if len(argv) > 2:
        print("usage: validate_weapons.py [path]", file=sys.stderr)
        return 2
    if len(argv) == 2:
        path = argv[1]
    else:
        root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        path = os.path.join(root, "Data", "Items", "Weapons.json")
    try:
        with open(path, encoding="utf-8") as f:
            weapons = json.load(f)
    except (OSError, ValueError) as e:
        print(f"ERROR: cannot load {path}: {e}")
        return 1
    try:
        ammo_ids = load_ammo_ids()
    except (OSError, ValueError) as e:
        print(f"ERROR: cannot load {ITEMS_PATH}: {e}")
        return 1
    errors = validate(weapons, ammo_ids)
    if errors:
        for e in errors:
            print("ERROR:", e)
        print(f"{len(errors)} error(s)")
        return 1
    counts = {}
    for w in weapons:
        counts[w["weaponType"]] = counts.get(w["weaponType"], 0) + 1
    print(f"OK: {len(weapons)} weapons")
    for t in sorted(counts):
        print(f"  {t}: {counts[t]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
