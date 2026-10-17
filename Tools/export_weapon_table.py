#!/usr/bin/env python3
"""Export Data/Items/Weapons.json to Unreal data table JSON (FCrossguardWeaponRow rows).

Usage:
  python3 Tools/export_weapon_table.py           # write Data/Items/WeaponTable.json
  python3 Tools/export_weapon_table.py --check   # exit 1 if WeaponTable.json is out of date

Weapons.json is the source of truth. Import WeaponTable.json in the editor as a Data Table with
row struct CrossguardWeaponRow.
"""

import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SOURCE = REPO / "Data" / "Items" / "Weapons.json"
OUTPUT = REPO / "Data" / "Items" / "WeaponTable.json"


def vec(values, keys=("X", "Y", "Z")):
    return {k: v for k, v in zip(keys, values)}


def row(weapon):
    speed = weapon["attackSpeed"] or {}
    transform = weapon.get("relativeTransform") or {}
    scale = transform.get("scale", 1)
    scale = scale if isinstance(scale, list) else [scale] * 3
    return {
        "Name": weapon["id"],
        "DisplayName": weapon["name"],
        "WeaponType": weapon["weaponType"],
        "Handedness": weapon["handedness"],
        "Range": weapon["range"],
        "GridWidth": weapon["gridWidth"],
        "GridHeight": weapon["gridHeight"],
        "Stat": weapon["stat"],
        "BaseValue": weapon["baseValue"],
        "PerRarity": weapon["perRarity"],
        "Attacks": speed.get("attacks", 0),
        "PerSeconds": speed.get("perSeconds", 0),
        "Delivery": weapon["delivery"],
        "AmmoId": weapon["ammo"] or "None",  # Unreal imports "None" as an empty FName
        "bCanParry": weapon["canParry"],
        "ImpactPower": weapon["impactPower"],
        # Art (optional in the source): an empty path imports as no mesh.
        "Mesh": weapon.get("mesh") or "",
        "AttachSocket": weapon.get("attachSocket") or "None",
        "MeshOffset": vec(transform.get("location", [0, 0, 0])),
        "MeshRotation": vec(transform.get("rotation", [0, 0, 0]), ("Pitch", "Yaw", "Roll")),
        "MeshScale": vec(scale),
        # What a projectile weapon without ammo (the staff) shoots looks like (`projectile`).
        "ProjectileMesh": weapon.get("projectile", {}).get("mesh", ""),
        "ProjectileTrailVfx": weapon.get("projectile", {}).get("trailVfx", ""),
        "ProjectileImpactVfx": weapon.get("projectile", {}).get("impactVfx", ""),
        "ProjectileImpactSound": weapon.get("projectile", {}).get("impactSound", ""),
    }


def render(weapons):
    return json.dumps([row(w) for w in weapons], indent="\t", ensure_ascii=False) + "\n"


def main(argv):
    check = "--check" in argv[1:]
    weapons = json.loads(SOURCE.read_text(encoding="utf-8"))
    text = render(weapons)
    if check:
        current = OUTPUT.read_text(encoding="utf-8") if OUTPUT.exists() else None
        if current != text:
            print(f"{OUTPUT.relative_to(REPO)} is out of date; run Tools/export_weapon_table.py")
            return 1
        print(f"{OUTPUT.relative_to(REPO)} is up to date ({len(weapons)} rows)")
        return 0
    OUTPUT.write_text(text, encoding="utf-8")
    print(f"Wrote {OUTPUT.relative_to(REPO)} ({len(weapons)} rows)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
