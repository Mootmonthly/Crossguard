"""Which skills a mob can have (docs/mob-data-schema.md). Used by Tools/validate_mobs.py and
Tools/sync_mob_forge.py (the Mob Forge's skill picker).

Nearly every skill is AI-usable: in C++ every active skill overrides UCrossguardSkillAbility::ShouldAIUse
(so the monster AI knows when to use it on its target), passives are always on, and bundles grant
their included skills. Only the skills in NOT_AI_SKILLS cannot be given to mobs.
"""
import json
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SKILLS = REPO / "Data" / "Skills" / "Skills.json"

# Skill id -> why a mob cannot have it. Kept with reasons so the validator and docs can say why.
NOT_AI_SKILLS = {
    "flight": "AI cannot fly",
    "hunters_tracking": "a player's screen effect",
}

# Skills that ride on one kind of attack or weapon, so only that combat role ever uses them (C++
# ShouldAIUse). Skills not listed suit every role.
SKILL_ROLES = {
    "slash": {"Warrior"},
    "mortal_strike": {"Warrior"},
    "shade_blow": {"Warrior"},
    "gouge": {"Warrior"},
    "neckbite": {"Warrior"},
    "chop_down": {"Warrior"},
    "purge_shot": {"Ranged"},
    "arcane_arrows": {"Ranged"},
}


def load_skills(path=SKILLS):
    """Skill id -> skill object from Skills.json."""
    return {s["id"]: s for s in json.loads(Path(path).read_text(encoding="utf-8"))}


def is_ai_usable(skill):
    return skill["id"] not in NOT_AI_SKILLS


def roles_for(skill_id):
    """Combat roles that can use the skill, or None for all."""
    roles = SKILL_ROLES.get(skill_id)
    return sorted(roles) if roles else None
