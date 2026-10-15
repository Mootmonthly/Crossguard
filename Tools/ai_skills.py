"""Which skills a mob can have (docs/mob-data-schema.md). Used by Tools/validate_mobs.py and
Tools/sync_mob_forge.py (the Mob Forge's skill picker).

A skill is AI-usable if the monster AI can use it on its target (AI_ACTIVE_SKILLS), or if it is a
data-only passive: category Passive, no included skills, and no native C++ class.
"""
import json
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SKILLS = REPO / "Data" / "Skills" / "Skills.json"

# Mirrors the C++ skills that override ShouldAIUse (Source/Crossguard/Private/Skills).
AI_ACTIVE_SKILLS = {"slash", "mortal_strike", "shade_blow", "gouge", "neckbite", "purge_shot", "hunters_leap", "eldritch_shield"}

# Next-attack skills ride on one kind of attack, so only that combat role ever uses them (C++
# UCrossguardNextAttackAbility::ShouldAIUse). Skills not listed suit every role.
SKILL_ROLES = {
    "slash": {"Warrior"},
    "mortal_strike": {"Warrior"},
    "shade_blow": {"Warrior"},
    "gouge": {"Warrior"},
    "neckbite": {"Warrior"},
    "purge_shot": {"Ranged"},
}

# Mirrors the Passive-category skills in the native registry (CrossguardNativeSkills.cpp): their
# effect lives in C++ that only runs for players.
NATIVE_PASSIVES = {"hunters_tracking", "multiscale", "splinter_of_winter"}


def load_skills(path=SKILLS):
    """Skill id -> skill object from Skills.json."""
    return {s["id"]: s for s in json.loads(Path(path).read_text(encoding="utf-8"))}


def is_ai_usable(skill):
    if skill["id"] in AI_ACTIVE_SKILLS:
        return True
    return skill.get("category") == "Passive" and not skill.get("includes") and skill["id"] not in NATIVE_PASSIVES


def roles_for(skill_id):
    """Combat roles that can use the skill, or None for all."""
    roles = SKILL_ROLES.get(skill_id)
    return sorted(roles) if roles else None
