#!/usr/bin/env python3
"""Validate Data/Mobs/Mobs.json against docs/mob-data-schema.md.

Each mob's loot is checked like a loot table (Tools/validate_loot.py): every weapon, armor, item
and skill it names must exist. Its skills must exist and be AI-usable (Tools/ai_skills.py).
"""
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ai_skills import is_ai_usable, load_skills, roles_for  # noqa: E402
from validate_loot import LOOT_FIELDS, ids_by_kind, validate_loot_body  # noqa: E402

REPO = Path(__file__).resolve().parent.parent
SOURCE = REPO / "Data" / "Mobs" / "Mobs.json"
ID_RE = re.compile(r"^[a-z][a-z0-9]*(_[a-z0-9]+)*$")
KINDS = {"Monster", "Boss"}
ROLES = {"Warrior", "Mage", "Ranged"}


def is_num(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def positive(v):
    return is_num(v) and v > 0


def non_negative(v):
    return is_num(v) and v >= 0


SPEC = {
    "id": (lambda v: isinstance(v, str) and bool(ID_RE.match(v)), "snake_case string"),
    "name": (lambda v: isinstance(v, str) and v.strip() != "", "non-empty string"),
    "kind": (lambda v: v in KINDS, "one of %s" % sorted(KINDS)),
    "combatRole": (lambda v: v in ROLES, "one of %s" % sorted(ROLES)),
    "health": (positive, "number > 0"),
    "attackDamage": (non_negative, "number >= 0"),
    "attackInterval": (positive, "number > 0 (seconds)"),
    "attackRange": (positive, "number > 0 (meters)"),
    "fireballInterval": (positive, "number > 0 (seconds)"),
    "keepDistance": (non_negative, "number >= 0 (meters)"),
    "aggroRange": (non_negative, "number >= 0 (meters)"),
    "leashRange": (positive, "number > 0 (meters)"),
    "moveSpeed": (non_negative, "number >= 0 (meters per second)"),
    "scale": (positive, "number > 0"),
    "skills": (lambda v: isinstance(v, list) and all(isinstance(x, str) for x in v), "array of skill ids"),
    "loot": (lambda v: isinstance(v, dict), "object"),
    "description": (lambda v: isinstance(v, str), "string"),
    "notes": (lambda v: isinstance(v, list) and all(isinstance(x, str) for x in v), "array of strings"),
}


def validate(mobs):
    errors = []
    known = ids_by_kind()
    skills = load_skills()
    if not isinstance(mobs, list):
        return ["top level must be an array"]
    ids = []
    for i, mob in enumerate(mobs):
        label = mob.get("id", f"#{i}") if isinstance(mob, dict) else f"#{i}"

        def err(msg, label=label):
            errors.append(f"[{label}] {msg}")

        if not isinstance(mob, dict):
            err("expected object")
            continue
        for name, (pred, desc) in SPEC.items():
            if name not in mob:
                err(f"missing field '{name}'")
            elif not pred(mob[name]):
                err(f"field '{name}' must be {desc}")
        for name in set(mob) - set(SPEC):
            err(f"unknown field '{name}'")
        if isinstance(mob.get("id"), str):
            if mob["id"] in ids:
                err("duplicate id")
            ids.append(mob["id"])
        if mob.get("combatRole") == "Ranged" and non_negative(mob.get("keepDistance")) and positive(mob.get("attackRange")) \
                and mob["keepDistance"] > mob["attackRange"]:
            err("keepDistance is larger than attackRange: a Ranged mob would stand out of its own range")
        if positive(mob.get("aggroRange", 0)) and positive(mob.get("leashRange")) and mob["aggroRange"] > mob["leashRange"]:
            err("aggroRange is larger than leashRange: it would aggro on enemies it must not chase")
        mob_skills = mob.get("skills")
        if isinstance(mob_skills, list):
            for j, skill_id in enumerate(mob_skills):
                if skill_id in mob_skills[:j]:
                    err(f"skills: '{skill_id}' listed twice")
                elif skill_id not in skills:
                    err(f"skills: no skill '{skill_id}' in Skills.json")
                elif not is_ai_usable(skills[skill_id]):
                    err(f"skills: '{skill_id}' is not AI-usable (an AI active or a data-only passive)")
                elif roles_for(skill_id) and mob.get("combatRole") not in roles_for(skill_id):
                    err(f"skills: '{skill_id}' is only used by {' or '.join(roles_for(skill_id))} mobs")
        loot = mob.get("loot")
        if isinstance(loot, dict):
            if set(loot) != LOOT_FIELDS:
                err(f"loot needs exactly {sorted(LOOT_FIELDS)}")
            else:
                validate_loot_body(loot, known, lambda msg: err("loot: " + msg))
    if ids != sorted(ids):
        errors.append("mobs must be sorted by id")
    return errors


def main(argv):
    path = Path(argv[1]) if len(argv) > 1 else SOURCE
    mobs = json.loads(path.read_text(encoding="utf-8"))
    errors = validate(mobs)
    for e in errors:
        print("ERROR " + e)
    if errors:
        return 1
    print(f"{path.name}: {len(mobs)} mobs OK")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
