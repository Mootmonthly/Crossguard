#!/usr/bin/env python3
"""Apply pending Mob Forge edits to Data/Mobs/Mobs.json.

The Mob Forge (a claude.ai artifact) stores one document per mob in its "mobs" collection:
    {"mob": {...schema object...}, "meta": {"updatedAt", "syncedAt", "deleted"}}
A document is pending when it is deleted, never synced, or updated after its last sync.

Usage:
  python3 Tools/sync_mob_forge.py <export_dir> [--dry-run]
  python3 Tools/sync_mob_forge.py --catalog <out.json>

<export_dir> holds the exported documents as JSON files (any depth). The script applies pending
documents, validates the result with Tools/validate_mobs.py, and only then writes Mobs.json, the
mob table and the loot table. It prints a JSON report: applied ids, deleted ids, and errors. Exit 1
writes nothing.

--catalog writes the forge's "refs/catalog" document: the weapon, armor, item and skill ids and
names the drop, weapon and skill editors offer. Weapons also carry weaponType, handedness and range
(the weapon picker's role rules) and impactPower. Skills also carry their gem rarity, category, aiUsable and roles (null: any combat role)
(Tools/ai_skills.py).
"""

import json
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ai_skills import is_ai_usable, load_skills, roles_for  # noqa: E402

REPO = Path(__file__).resolve().parent.parent
MOBS = REPO / "Data" / "Mobs" / "Mobs.json"
TOOLS = REPO / "Tools"

# Field order of Mobs.json (docs/mob-data-schema.md); the forge's database returns keys sorted.
FIELD_ORDER = ["id", "name", "kind", "combatRole", "weapon", "health", "attackDamage", "attackInterval", "attackRange",
               "fireballInterval", "keepDistance", "aggroRange", "leashRange", "moveSpeed", "scale", "skills", "loot",
               "description", "notes"]
LOOT_ORDER = ["rolls", "nothingWeight", "rarityWeights", "entries"]
RARITY_ORDER = ["Common", "Uncommon", "Rare", "Epic", "Legendary", "Mystic"]
ENTRY_ORDER = ["kind", "id", "armorType", "weight", "count"]


def ordered(obj, order):
    return {**{k: obj[k] for k in order if k in obj}, **{k: v for k, v in obj.items() if k not in order}}


def normalize(mob):
    # Documents saved before a field existed.
    mob.setdefault("skills", [])
    mob.setdefault("weapon", None)
    mob = ordered(mob, FIELD_ORDER)
    loot = mob.get("loot")
    if isinstance(loot, dict):
        loot = ordered(loot, LOOT_ORDER)
        if isinstance(loot.get("rarityWeights"), dict):
            loot["rarityWeights"] = ordered(loot["rarityWeights"], RARITY_ORDER)
        if isinstance(loot.get("entries"), list):
            loot["entries"] = [ordered(e, ENTRY_ORDER) if isinstance(e, dict) else e for e in loot["entries"]]
        mob["loot"] = loot
    return mob


def load_documents(export_dir):
    docs = {}
    for path in sorted(Path(export_dir).rglob("*.json")):
        raw = json.loads(path.read_text(encoding="utf-8"))
        body = raw.get("data", raw) if isinstance(raw, dict) else None
        if not isinstance(body, dict) or not isinstance(body.get("mob"), dict):
            continue
        docs[raw.get("id") or path.stem] = body
    return docs


def is_pending(meta):
    meta = meta or {}
    if meta.get("deleted"):
        return True
    synced = meta.get("syncedAt")
    return not synced or (meta.get("updatedAt") or "") > synced


def catalog():
    def names(path):
        return [{"id": e["id"], "name": e["name"]} for e in json.loads(path.read_text(encoding="utf-8"))]

    items = REPO / "Data" / "Items"
    weapons = [{"id": w["id"], "name": w["name"], "weaponType": w["weaponType"], "handedness": w["handedness"], "range": w["range"],
                "impactPower": w["impactPower"]}
               for w in json.loads((items / "Weapons.json").read_text(encoding="utf-8"))]
    skills = [{"id": s["id"], "name": s["name"], "rarity": s["rarity"], "category": s["category"], "aiUsable": is_ai_usable(s),
               "roles": roles_for(s["id"])}
              for s in load_skills().values()]
    return {"weapons": weapons, "armor": names(items / "Armor.json"),
            "items": names(items / "Items.json"), "skills": skills}


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    if argv[1] == "--catalog":
        if len(argv) != 3:
            print(__doc__)
            return 2
        Path(argv[2]).write_text(json.dumps(catalog(), indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        print(f"Wrote {argv[2]}")
        return 0
    dry_run = "--dry-run" in argv[2:]
    docs = load_documents(argv[1])
    mobs = {m["id"]: m for m in json.loads(MOBS.read_text(encoding="utf-8"))}

    report = {"applied": [], "deleted": [], "errors": []}
    for doc_id, body in sorted(docs.items()):
        if not is_pending(body.get("meta")):
            continue
        mob = body["mob"]
        if mob.get("id") != doc_id:
            report["errors"].append(f"[{doc_id}] document id and mob id differ ({mob.get('id')!r})")
            continue
        if body["meta"].get("deleted"):
            mobs.pop(doc_id, None)
            report["deleted"].append(doc_id)
        else:
            mobs[doc_id] = normalize(mob)
            report["applied"].append(doc_id)

    if report["errors"]:
        print(json.dumps(report, indent=2))
        return 1

    merged = [mobs[k] for k in sorted(mobs)]
    text = json.dumps(merged, indent=2, ensure_ascii=False) + "\n"
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8") as tmp:
        tmp.write(text)
    check = subprocess.run([sys.executable, str(TOOLS / "validate_mobs.py"), tmp.name], capture_output=True, text=True)
    Path(tmp.name).unlink()
    if check.returncode != 0:
        report["errors"] = [line for line in check.stdout.splitlines() if line.startswith("ERROR")]
        print(json.dumps(report, indent=2))
        return 1

    if not dry_run:
        MOBS.write_text(text, encoding="utf-8")
        subprocess.run([sys.executable, str(TOOLS / "export_mob_table.py")], check=True, capture_output=True)
        subprocess.run([sys.executable, str(TOOLS / "export_loot_table.py")], check=True, capture_output=True)
    report["dryRun"] = dry_run
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
