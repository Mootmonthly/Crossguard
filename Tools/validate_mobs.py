#!/usr/bin/env python3
"""Validate Data/Mobs/Mobs.json against docs/mob-data-schema.md.

Each mob's loot is checked like a loot table (Tools/validate_loot.py): every weapon, armor, item
and skill it names must exist. Its skills must exist and be AI-usable (Tools/ai_skills.py). Its
weapon, if any, must exist in Data/Items/Weapons.json and suit its combat role.
"""
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ai_skills import NOT_AI_SKILLS, is_ai_usable, load_skills, roles_for  # noqa: E402
from validate_loot import LOOT_FIELDS, ids_by_kind, validate_loot_body  # noqa: E402

REPO = Path(__file__).resolve().parent.parent
SOURCE = REPO / "Data" / "Mobs" / "Mobs.json"
WEAPONS = REPO / "Data" / "Items" / "Weapons.json"
ID_RE = re.compile(r"^[a-z][a-z0-9]*(_[a-z0-9]+)*$")
KINDS = {"Monster", "Boss"}
ROLES = {"Warrior", "Mage", "Ranged"}
# Weapon types a Warrior cannot hold (it attacks in melee).
RANGED_TYPES = {"Bow", "Crossbow", "Sling"}


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
    "weapon": (lambda v: v is None or isinstance(v, str), "weapon id or null"),
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


# Unreal object path of an asset (/Game/Folder/Asset.Asset); a class path ends in _C.
ASSET_PATH_RE = re.compile(r"^/[A-Za-z0-9_]+(/[A-Za-z0-9_.-]+)+\.[A-Za-z0-9_]+$")


def triple(v):
    return isinstance(v, list) and len(v) == 3 and all(is_num(x) for x in v)


# Art fields, all optional (docs/mob-data-schema.md, "Art").
ART_SPEC = {
    "mesh": (lambda v: isinstance(v, str) and bool(ASSET_PATH_RE.match(v)), "a skeletal mesh path like /Game/Monsters/SK_Skeleton.SK_Skeleton"),
    "animBlueprint": (lambda v: isinstance(v, str) and bool(ASSET_PATH_RE.match(v)) and v.endswith("_C"),
                      "an Anim Blueprint class path like /Game/Monsters/ABP_Skeleton.ABP_Skeleton_C"),
    "meshScale": (positive, "number > 0"),
    "meshOffset": (triple, "[x, y, z] in meters"),
    "meshYaw": (is_num, "number (degrees)"),
}


def weapon_problem(role, weapon):
    """Why the weapon (a Weapons.json object, or None for natural attacks) does not suit the role, or None."""
    if weapon is None:
        return None
    if weapon["weaponType"] == "Shield":
        return "a shield is not a weapon a mob can hold"
    if role == "Warrior" and weapon["weaponType"] in RANGED_TYPES:
        return f"a Warrior cannot hold a {weapon['weaponType']}"
    # Ranged mobs shoot arrows: wands and staves are casting weapons, not theirs.
    if role == "Ranged" and weapon["weaponType"] not in RANGED_TYPES:
        return "a Ranged mob holds a bow, crossbow or sling, or none (a bow)"
    return None


def validate(mobs):
    errors = []
    known = ids_by_kind()
    skills = load_skills()
    weapons = {w["id"]: w for w in json.loads(WEAPONS.read_text(encoding="utf-8"))}
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
        for name in sorted(set(mob) - set(SPEC)):
            if name in ART_SPEC:
                pred, desc = ART_SPEC[name]
                if not pred(mob[name]):
                    err(f"field '{name}' must be {desc}")
            else:
                err(f"unknown field '{name}'")
        if "mesh" not in mob and any(name in mob for name in ART_SPEC):
            err("art fields (animBlueprint, meshScale, meshOffset, meshYaw) need a mesh")
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
                    err(f"skills: '{skill_id}' is not AI-usable ({NOT_AI_SKILLS[skill_id]})")
                elif roles_for(skill_id) and mob.get("combatRole") not in roles_for(skill_id):
                    err(f"skills: '{skill_id}' is only used by {' or '.join(roles_for(skill_id))} mobs")
        weapon_id = mob.get("weapon")
        if isinstance(weapon_id, str):
            if weapon_id not in weapons:
                err(f"weapon: no weapon '{weapon_id}' in Weapons.json")
            elif mob.get("combatRole") in ROLES and weapon_problem(mob["combatRole"], weapons[weapon_id]):
                err(f"weapon: '{weapon_id}': {weapon_problem(mob['combatRole'], weapons[weapon_id])}")
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
