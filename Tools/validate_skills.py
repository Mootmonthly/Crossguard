#!/usr/bin/env python3
"""Validate Data/Skills/Skills.json against docs/skill-data-schema.md."""
import json
import os
import re
import sys

CATEGORIES = {"Active", "Spell", "Art", "Passive"}
WEAPON_TAGS = {"Melee", "Ranged"}
TAGS = {"Minion", "Buff", "Debuff", "Summon", "Heal", "Raid"}
MELEE = {"Sword", "Axe", "Dagger", "Spear", "Mace", "Staff", "Hammer", "Shield", "AnyMelee", "TwoHandedMelee"}
RANGED = {"Bow", "Crossbow", "Sling", "Throwing"}
WEAPONS = MELEE | RANGED | {"Wand"}
ARMOR = {"Plate", "PlateAllSlots"}
STATS = {"STR", "AGI", "INT", "AllAttributes", "MHP", "MHPB", "AD", "AAD", "ADB", "AR", "AP",
         "Dexterity", "ActionSpeed", "MoveSpeed", "PH", "APH", "PHB", "MD", "AMD", "MDB", "MR", "MH",
         "AMH", "MHB", "CDR", "CastTime"}
OPS = {"Flat", "Percent", "Multiply"}
TARGETS = {"Self", "Party", "AlliesInRadius", "Target"}
FILTERS = {"Melee", "Ranged", "Minion", "Active", "Spell", "Art"}
COND_KEYS = {"wielding": list, "armor": str, "perEmptyArmorSlot": bool, "onActivation": bool,
             "nextAttack": bool, "other": str}
DMG_TYPES = {"Attack", "Magic"}
DMG_KINDS = {"Impact", "Splash", "PerSecond", "OverTime", "Additional", "WeaponPercent"}
HEAL_TYPES = {"Physical", "Magical"}
SOURCES = {"SkillsDoc", "StatsDoc"}
ID_RE = re.compile(r"^[a-z][a-z0-9]*(_[a-z0-9]+)*$")


def is_num(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def is_int(v):
    return isinstance(v, int) and not isinstance(v, bool)


def is_strlist(v):
    return isinstance(v, list) and all(isinstance(x, str) for x in v)


def check_fields(obj, spec, where, err):
    """spec: name -> predicate. Reports missing/extra/mistyped fields."""
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
    return ok


def enum(values, nullable=False):
    return (lambda v: (v is None and nullable) or (isinstance(v, str) and v in values),
            ("one of %s" % sorted(values)) + (" or null" if nullable else ""))


NUM_OR_NULL = (lambda v: v is None or is_num(v), "number or null")
STRLIST = (is_strlist, "array of strings")

SKILL_SPEC = {
    "id": (lambda v: isinstance(v, str), "string"),
    "name": (lambda v: isinstance(v, str) and v != "", "non-empty string"),
    "capacityCost": (lambda v: is_int(v) and v >= 0, "integer >= 0"),
    "category": enum(CATEGORIES),
    "weaponTag": enum(WEAPON_TAGS, True),
    "tags": (lambda v: is_strlist(v) and all(t in TAGS for t in v), "array of tags from %s" % sorted(TAGS)),
    "requirements": (lambda v: isinstance(v, dict), "object"),
    "cooldownSec": NUM_OR_NULL,
    "durationSec": NUM_OR_NULL,
    "includes": STRLIST,
    "grantsWhileActive": STRLIST,
    "minionSkills": STRLIST,
    "statModifiers": (lambda v: isinstance(v, list), "array"),
    "damage": (lambda v: isinstance(v, list), "array"),
    "healing": (lambda v: isinstance(v, list), "array"),
    "mechanics": STRLIST,
    "description": (lambda v: isinstance(v, str) and v != "", "non-empty string"),
    "source": enum(SOURCES),
    "notes": STRLIST,
}
REQ_SPEC = {
    "weapons": (lambda v: is_strlist(v) and all(w in WEAPONS for w in v), "array of WeaponType %s" % sorted(WEAPONS)),
    "armor": enum(ARMOR, True),
    "items": STRLIST,
    "forms": STRLIST,
}
MOD_SPEC = {
    "stat": enum(STATS),
    "op": enum(OPS),
    "value": (is_num, "number"),
    "target": enum(TARGETS),
    "skillFilter": enum(FILTERS, True),
    "condition": (lambda v: v is None or isinstance(v, dict), "object or null"),
    "durationSec": NUM_OR_NULL,
}
DMG_SPEC = {
    "type": enum(DMG_TYPES),
    "amount": (is_num, "number"),
    "coefficient": (is_num, "number"),
    "kind": enum(DMG_KINDS),
    "durationSec": NUM_OR_NULL,
    "hits": (lambda v: is_int(v) and v >= 1, "integer >= 1"),
}
HEAL_SPEC = {
    "type": enum(HEAL_TYPES),
    "amount": NUM_OR_NULL,
    "coefficient": (is_num, "number"),
    "full": (lambda v: isinstance(v, bool), "boolean"),
    "canRevive": (lambda v: isinstance(v, bool), "boolean"),
}


def validate(skills):
    errors = []
    if not isinstance(skills, list):
        return ["top level must be a JSON array"]

    ids = []
    seen = set()
    for i, s in enumerate(skills):
        sid = s.get("id") if isinstance(s, dict) else None
        label = sid if isinstance(sid, str) else f"<index {i}>"

        def err(msg, label=label):
            errors.append(f"[{label}] {msg}")

        if isinstance(sid, str):
            if not ID_RE.match(sid):
                err("id is not snake_case")
            if sid in seen:
                err("duplicate id")
            seen.add(sid)
            ids.append(sid)
        if not check_fields(s, SKILL_SPEC, "skill", err):
            pass
        if not isinstance(s, dict):
            continue

        # requirements
        req = s.get("requirements")
        req_ok = isinstance(req, dict) and check_fields(req, REQ_SPEC, "requirements", err)

        # weaponTag consistency
        if req_ok and isinstance(s.get("weaponTag"), (str, type(None))):
            w = req["weapons"]
            if not w:
                expect = None
            elif all(x in MELEE for x in w):
                expect = "Melee"
            elif all(x in RANGED for x in w):
                expect = "Ranged"
            else:
                expect = "invalid"
                err("requirements.weapons mixes melee and ranged weapons (or includes Wand); no valid weaponTag")
            if expect != "invalid" and s.get("weaponTag") != expect:
                err(f"weaponTag is {s.get('weaponTag')!r} but requirements.weapons implies {expect!r}")

        # modifiers
        for j, m in enumerate(s.get("statModifiers") or []):
            where = f"statModifiers[{j}]"
            if not check_fields(m, MOD_SPEC, where, err):
                continue
            c = m["condition"]
            if c is not None:
                if len(c) != 1:
                    err(f"{where}.condition must have exactly one key")
                else:
                    k, v = next(iter(c.items()))
                    if k not in COND_KEYS:
                        err(f"{where}.condition has invalid key '{k}'")
                    elif not isinstance(v, COND_KEYS[k]):
                        err(f"{where}.condition.{k} has wrong type")
                    elif k == "wielding" and not (v and all(x in WEAPONS for x in v)):
                        err(f"{where}.condition.wielding must be a non-empty list of WeaponType")
                    elif k == "armor" and v not in ARMOR:
                        err(f"{where}.condition.armor invalid '{v}'")
            if m["skillFilter"] is not None and m["stat"] not in ("CDR", "CastTime"):
                err(f"{where}: skillFilter only applies to CDR/CastTime")

        for j, d in enumerate(s.get("damage") or []):
            where = f"damage[{j}]"
            if check_fields(d, DMG_SPEC, where, err):
                if d["kind"] in ("PerSecond", "OverTime") and d["durationSec"] is None:
                    err(f"{where}: {d['kind']} damage needs durationSec")

        for j, h in enumerate(s.get("healing") or []):
            where = f"healing[{j}]"
            if check_fields(h, HEAL_SPEC, where, err):
                if h["full"] and h["amount"] is not None:
                    err(f"{where}: full healing must have amount null")
                if not h["full"] and h["amount"] is None:
                    err(f"{where}: amount is null but full is false")

        if s.get("category") == "Passive" and s.get("cooldownSec") is not None:
            err("Passive skill must have cooldownSec null")

    # sorted
    if ids != sorted(ids):
        for a, b in zip(ids, ids[1:]):
            if a > b:
                errors.append(f"[{b}] array not sorted by id (comes after '{a}')")
                break

    # references
    idset = set(ids)
    graph = {}
    for s in skills:
        if not isinstance(s, dict) or not isinstance(s.get("id"), str):
            continue
        sid = s["id"]
        for field in ("includes", "grantsWhileActive", "minionSkills"):
            refs = s.get(field)
            if not is_strlist(refs):
                continue
            for r in refs:
                if r == sid:
                    errors.append(f"[{sid}] {field} references itself")
                elif r not in idset:
                    errors.append(f"[{sid}] {field} references unknown id '{r}'")
        if is_strlist(s.get("includes")):
            graph[sid] = [r for r in s["includes"] if r in idset]

    # cycles in includes
    state = {}

    def dfs(n, path):
        state[n] = 1
        for m in graph.get(n, []):
            if state.get(m) == 1:
                cyc = path[path.index(m):] + [m] if m in path else [n, m]
                errors.append(f"[{n}] includes cycle: {' -> '.join(cyc)}")
            elif m not in state:
                dfs(m, path + [m])
        state[n] = 2

    for n in graph:
        if n not in state:
            dfs(n, [n])
    return errors


def main(argv):
    if len(argv) > 2:
        print("usage: validate_skills.py [path]", file=sys.stderr)
        return 2
    if len(argv) == 2:
        path = argv[1]
    else:
        root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        path = os.path.join(root, "Data", "Skills", "Skills.json")
    try:
        with open(path, encoding="utf-8") as f:
            skills = json.load(f)
    except (OSError, ValueError) as e:
        print(f"ERROR: cannot load {path}: {e}")
        return 1
    errors = validate(skills)
    if errors:
        for e in errors:
            print("ERROR:", e)
        print(f"{len(errors)} error(s)")
        return 1
    counts = {}
    for s in skills:
        counts[s["category"]] = counts.get(s["category"], 0) + 1
    print(f"OK: {len(skills)} skills")
    for c in sorted(CATEGORIES):
        print(f"  {c}: {counts.get(c, 0)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
