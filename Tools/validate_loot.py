#!/usr/bin/env python3
"""Validate Data/Loot/LootTables.json against docs/loot-data-schema.md.

Checks every referenced weapon, armor, item and skill exists in the other data files.
"""
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SOURCE = REPO / "Data" / "Loot" / "LootTables.json"
RARITIES = ["Common", "Uncommon", "Rare", "Epic", "Legendary", "Mystic"]
KINDS = {"Weapon", "Armor", "Item", "SkillGem", "Gold"}
# Kinds whose id may be null: "any of that kind".
WILDCARD_KINDS = {"Weapon", "Armor", "SkillGem"}
ID_RE = re.compile(r"^[a-z][a-z0-9]*(_[a-z0-9]+)*$")


ENTRY_FIELDS = {"kind", "id", "weight", "count", "armorType"}
# Armor.json armorType values: an any-armor drop can be limited to one.
ARMOR_TYPES = {"Cloth", "Leather", "Plate"}

def is_int(v):
    return isinstance(v, int) and not isinstance(v, bool)


def is_range(v):
    return isinstance(v, list) and len(v) == 2 and all(is_int(x) and x >= 0 for x in v) and v[0] <= v[1]


def load_ids(path):
    return {entry["id"] for entry in json.loads(path.read_text(encoding="utf-8"))}


def ids_by_kind():
    return {
        "Weapon": load_ids(REPO / "Data" / "Items" / "Weapons.json"),
        "Armor": load_ids(REPO / "Data" / "Items" / "Armor.json"),
        "Item": load_ids(REPO / "Data" / "Items" / "Items.json"),
        "SkillGem": load_ids(REPO / "Data" / "Skills" / "Skills.json"),
    }


LOOT_FIELDS = {"rolls", "nothingWeight", "rarityWeights", "entries"}


def validate_loot_body(table, known, err):
    """Checks rolls, nothingWeight, rarityWeights and entries of one table (no id). Also used for
    the loot of mobs (Tools/validate_mobs.py)."""
    if not is_range(table.get("rolls")):
        err("rolls must be [min, max] with 0 <= min <= max")
    if not (is_int(table.get("nothingWeight")) and table["nothingWeight"] >= 0):
        err("nothingWeight must be an integer >= 0")
    weights = table.get("rarityWeights")
    if not isinstance(weights, dict) or set(weights) != set(RARITIES) or not all(is_int(w) and w >= 0 for w in weights.values()):
        err(f"rarityWeights must give an integer >= 0 for each of {RARITIES}")
    elif sum(weights.values()) <= 0:
        err("rarityWeights must not all be 0")
    entries = table.get("entries")
    if not isinstance(entries, list) or not entries:
        err("entries must be a non-empty array")
        return
    for j, entry in enumerate(entries):
        where = f"entries[{j}]"
        if not isinstance(entry, dict) or not {"kind", "id", "weight", "count"} <= set(entry) <= ENTRY_FIELDS:
            err(f"{where}: needs kind, id, weight, count (and optionally armorType)")
            continue
        kind = entry["kind"]
        if kind not in KINDS:
            err(f"{where}: kind must be one of {sorted(KINDS)}")
            continue
        if not (is_int(entry["weight"]) and entry["weight"] >= 0):
            err(f"{where}: weight must be an integer >= 0")
        if not is_range(entry["count"]) or entry["count"][0] < 1:
            err(f"{where}: count must be [min, max] with 1 <= min <= max")
        if kind == "Gold":
            if entry["id"] is not None:
                err(f"{where}: Gold has id null")
        elif entry["id"] is None:
            if kind not in WILDCARD_KINDS:
                err(f"{where}: {kind} needs an id")
        elif entry["id"] not in known[kind]:
            err(f"{where}: no {kind} with id {entry['id']!r}")
        if "armorType" in entry and entry["armorType"] is not None:
            if kind != "Armor" or entry["id"] is not None:
                err(f"{where}: armorType only narrows an any-armor drop (kind Armor, id null)")
            elif entry["armorType"] not in ARMOR_TYPES:
                err(f"{where}: armorType must be null or one of {sorted(ARMOR_TYPES)}")
        if kind in {"Weapon", "Armor", "SkillGem"} and entry["count"] != [1, 1]:
            err(f"{where}: {kind} drops one at a time; count must be [1, 1]")


def validate(tables):
    errors = []
    known = ids_by_kind()
    if not isinstance(tables, list):
        return ["top level must be an array"]
    seen = set()
    for i, table in enumerate(tables):
        label = table.get("id", f"#{i}") if isinstance(table, dict) else f"#{i}"

        def err(msg, label=label):
            errors.append(f"[{label}] {msg}")

        if not isinstance(table, dict):
            err("expected object")
            continue
        expected = {"id"} | LOOT_FIELDS
        for name in expected - set(table):
            err(f"missing field '{name}'")
        for name in set(table) - expected:
            err(f"unknown field '{name}'")
        if not isinstance(table.get("id"), str) or not ID_RE.match(table["id"]):
            err("id must be snake_case")
        elif table["id"].startswith("mob_"):
            err("ids starting with mob_ are reserved for the loot of mobs (Data/Mobs/Mobs.json)")
        elif table["id"] in seen:
            err("duplicate id")
        else:
            seen.add(table["id"])
        validate_loot_body(table, known, err)
    return errors


def main(argv):
    path = Path(argv[1]) if len(argv) > 1 else SOURCE
    tables = json.loads(path.read_text(encoding="utf-8"))
    errors = validate(tables)
    for e in errors:
        print("ERROR " + e)
    if errors:
        return 1
    print(f"{path.name}: {len(tables)} loot tables OK")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
