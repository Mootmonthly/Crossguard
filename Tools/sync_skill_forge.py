#!/usr/bin/env python3
"""Apply pending Skill Forge edits to Data/Skills/Skills.json.

The Skill Forge (a claude.ai artifact) stores one document per skill in its "skills" collection:
    {"skill": {...schema object...}, "meta": {"updatedAt", "syncedAt", "deleted"}}
A document is pending when it is deleted, never synced, or updated after its last sync.

Usage:
  python3 Tools/sync_skill_forge.py <export_dir> [--dry-run]

<export_dir> holds the exported documents as JSON files (any depth), either the document body
itself or a wrapper {"id", "data": body, ...}. The script applies pending documents, validates the
result with Tools/validate_skills.py, and only then writes Skills.json and the Unreal table. It
prints a JSON report: applied ids, deleted ids, and errors. Exit 1 writes nothing.
"""

import json
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SKILLS = REPO / "Data" / "Skills" / "Skills.json"
TOOLS = REPO / "Tools"


def load_documents(export_dir):
    docs = {}
    for path in sorted(Path(export_dir).rglob("*.json")):
        raw = json.loads(path.read_text(encoding="utf-8"))
        body = raw.get("data", raw) if isinstance(raw, dict) else None
        if not isinstance(body, dict) or not isinstance(body.get("skill"), dict):
            continue
        doc_id = raw.get("id") or path.stem
        docs[doc_id] = body
    return docs


# Field order of Skills.json (docs/skill-data-schema.md); the forge's database returns keys sorted.
FIELD_ORDER = ["id", "name", "capacityCost", "category", "rarity", "weaponTag", "tags", "requirements", "cooldownSec", "durationSec",
               "includes", "grantsWhileActive", "minionSkills", "statModifiers", "damage", "healing", "mechanics",
               "description", "source", "notes"]
REQUIREMENT_ORDER = ["weapons", "armor", "items", "forms"]
MODIFIER_ORDER = ["stat", "op", "value", "target", "skillFilter", "condition", "durationSec"]
DAMAGE_ORDER = ["type", "amount", "coefficient", "kind", "durationSec", "hits"]
HEALING_ORDER = ["type", "amount", "coefficient", "full", "canRevive"]


def ordered(obj, order):
    """obj with the keys in order first, any others after in their own order."""
    return {**{k: obj[k] for k in order if k in obj}, **{k: v for k, v in obj.items() if k not in order}}


def normalize(skill):
    # Documents saved before a field existed.
    skill.setdefault("rarity", None)
    skill = ordered(skill, FIELD_ORDER)
    if isinstance(skill.get("requirements"), dict):
        skill["requirements"] = ordered(skill["requirements"], REQUIREMENT_ORDER)
    for field, order in (("statModifiers", MODIFIER_ORDER), ("damage", DAMAGE_ORDER), ("healing", HEALING_ORDER)):
        if isinstance(skill.get(field), list):
            skill[field] = [ordered(x, order) if isinstance(x, dict) else x for x in skill[field]]
    return skill


def is_pending(meta):
    meta = meta or {}
    if meta.get("deleted"):
        return True
    synced = meta.get("syncedAt")
    return not synced or (meta.get("updatedAt") or "") > synced


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    dry_run = "--dry-run" in argv[2:]
    docs = load_documents(argv[1])
    skills = {s["id"]: s for s in json.loads(SKILLS.read_text(encoding="utf-8"))}

    report = {"applied": [], "deleted": [], "errors": []}
    for doc_id, body in sorted(docs.items()):
        if not is_pending(body.get("meta")):
            continue
        skill = body["skill"]
        if skill.get("id") != doc_id:
            report["errors"].append(f"[{doc_id}] document id and skill id differ ({skill.get('id')!r})")
            continue
        if body["meta"].get("deleted"):
            # A never-synced skill has nothing to remove; its document is still cleaned up.
            skills.pop(doc_id, None)
            report["deleted"].append(doc_id)
        else:
            skills[doc_id] = normalize(skill)
            report["applied"].append(doc_id)

    if report["errors"]:
        print(json.dumps(report, indent=2))
        return 1

    merged = [skills[k] for k in sorted(skills)]
    text = json.dumps(merged, indent=2, ensure_ascii=False) + "\n"
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8") as tmp:
        tmp.write(text)
    check = subprocess.run([sys.executable, str(TOOLS / "validate_skills.py"), tmp.name], capture_output=True, text=True)
    Path(tmp.name).unlink()
    if check.returncode != 0:
        report["errors"] = [line for line in check.stdout.splitlines() if line.startswith("ERROR")]
        print(json.dumps(report, indent=2))
        return 1

    if not dry_run:
        SKILLS.write_text(text, encoding="utf-8")
        subprocess.run([sys.executable, str(TOOLS / "export_skill_table.py")], check=True, capture_output=True)
    report["dryRun"] = dry_run
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
