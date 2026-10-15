#!/usr/bin/env python3
"""Export Data/Skills/Skills.json to Unreal data table JSON (FCrossguardSkillRow rows).

Usage:
  python3 Tools/export_skill_table.py           # write Data/Skills/SkillTable.json
  python3 Tools/export_skill_table.py --check   # exit 1 if SkillTable.json is out of date

Skills.json is the source of truth. Import SkillTable.json in the editor as a Data Table with
row struct CrossguardSkillRow (see docs/gas-architecture.md). Field and enum names here must match
Source/Crossguard/Public/Skills/CrossguardSkillTypes.h.
"""

import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SOURCE = REPO / "Data" / "Skills" / "Skills.json"
OUTPUT = REPO / "Data" / "Skills" / "SkillTable.json"

# Schema stat abbreviation -> ECrossguardStat entry.
STATS = {
    "STR": "Strength",
    "AGI": "Agility",
    "INT": "Intelligence",
    "AllAttributes": "AllAttributes",
    "MHP": "MaxHealth",
    "MHPB": "MaxHealthBonus",
    "AD": "AttackDamage",
    "AAD": "AdditionalAttackDamage",
    "ADB": "AttackDamageBonus",
    "AR": "AttackResistance",
    "AP": "AttackPenetration",
    "Dexterity": "Dexterity",
    "ActionSpeed": "ActionSpeed",
    "MoveSpeed": "MoveSpeed",
    "PH": "PhysicalHealing",
    "APH": "AdditionalPhysicalHealing",
    "PHB": "PhysicalHealingBonus",
    "MD": "MagicDamage",
    "AMD": "AdditionalMagicDamage",
    "MDB": "MagicDamageBonus",
    "MR": "MagicResistance",
    "MH": "MagicalHealing",
    "AMH": "AdditionalMagicalHealing",
    "MHB": "MagicalHealingBonus",
    "CDR": "CooldownReduction",
    "CastTime": "CastTimeReduction",
}


def number(value):
    """Null becomes 0 (the row structs use 0 for "none")."""
    return 0 if value is None else value


def condition(cond):
    row = {"Type": "None", "Weapons": [], "Armor": "None", "Description": ""}
    if cond is None:
        return row
    (key, value), = cond.items()
    if key == "wielding":
        row.update(Type="Wielding", Weapons=list(value))
    elif key == "armor":
        row.update(Type="Armor", Armor=value)
    elif key == "perEmptyArmorSlot":
        row["Type"] = "PerEmptyArmorSlot"
    elif key == "onActivation":
        row["Type"] = "OnActivation"
    elif key == "nextAttack":
        row["Type"] = "NextAttack"
    elif key == "other":
        row.update(Type="Other", Description=value)
    else:
        raise ValueError(f"unknown condition key {key!r}")
    return row


def modifier(mod):
    return {
        "Stat": STATS[mod["stat"]],
        "Op": mod["op"],
        "Value": mod["value"],
        "Target": mod["target"],
        "SkillFilter": mod["skillFilter"] or "None",
        "Condition": condition(mod["condition"]),
        "DurationSec": number(mod["durationSec"]),
    }


def damage(entry):
    return {
        "Type": entry["type"],
        "Amount": entry["amount"],
        "Coefficient": entry["coefficient"],
        "Kind": entry["kind"],
        "DurationSec": number(entry["durationSec"]),
        "Hits": entry["hits"],
    }


def healing(entry):
    return {
        "Type": entry["type"],
        "Amount": number(entry["amount"]),
        "Coefficient": entry["coefficient"],
        "bFull": entry["full"],
        "bCanRevive": entry["canRevive"],
    }


def row(skill):
    req = skill["requirements"]
    return {
        "Name": skill["id"],
        "DisplayName": skill["name"],
        "CapacityCost": skill["capacityCost"],
        "Category": skill["category"],
        "WeaponTag": skill["weaponTag"] or "None",
        "Tags": list(skill["tags"]),
        "Requirements": {
            "Weapons": list(req["weapons"]),
            "Armor": req["armor"] or "None",
            "Items": list(req["items"]),
            "Forms": list(req["forms"]),
        },
        "CooldownSec": number(skill["cooldownSec"]),
        "DurationSec": number(skill["durationSec"]),
        "Includes": list(skill["includes"]),
        "GrantsWhileActive": list(skill["grantsWhileActive"]),
        "MinionSkills": list(skill["minionSkills"]),
        "StatModifiers": [modifier(m) for m in skill["statModifiers"]],
        "Damage": [damage(d) for d in skill["damage"]],
        "Healing": [healing(h) for h in skill["healing"]],
        "Mechanics": list(skill["mechanics"]),
        "Description": skill["description"],
    }


def render(skills):
    return json.dumps([row(s) for s in skills], indent="\t", ensure_ascii=False) + "\n"


def main(argv):
    check = "--check" in argv[1:]
    skills = json.loads(SOURCE.read_text(encoding="utf-8"))
    text = render(skills)
    if check:
        current = OUTPUT.read_text(encoding="utf-8") if OUTPUT.exists() else None
        if current != text:
            print(f"{OUTPUT.relative_to(REPO)} is out of date; run Tools/export_skill_table.py")
            return 1
        print(f"{OUTPUT.relative_to(REPO)} is up to date ({len(skills)} rows)")
        return 0
    OUTPUT.write_text(text, encoding="utf-8")
    print(f"Wrote {OUTPUT.relative_to(REPO)} ({len(skills)} rows)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
