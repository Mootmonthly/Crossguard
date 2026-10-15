#include "AbilitySystem/CrossguardGameplayTags.h"

namespace CrossguardTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Skill_Category_Active, "Skill.Category.Active", "Activated skill that is neither a Spell nor an Art. STR cast time and CDR.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Skill_Category_Spell, "Skill.Category.Spell", "Spell. INT cast time and CDR.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Skill_Category_Art, "Skill.Category.Art", "Art. AGI cast time and CDR.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Skill_Category_Passive, "Skill.Category.Passive", "Never activated.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Skill_Weapon_Melee, "Skill.Weapon.Melee", "Requires a melee weapon.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Skill_Weapon_Ranged, "Skill.Weapon.Ranged", "Requires a ranged weapon.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Skill_Minion, "Skill.Minion", "Summons minions.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Cooldown, "SetByCaller.Cooldown", "Cooldown duration in seconds, after CDR.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Damage_Attack, "SetByCaller.Damage.Attack", "Base Attack Damage (weapon AD or skill amount).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Damage_Magic, "SetByCaller.Damage.Magic", "Base Magic Damage.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Healing_Physical, "SetByCaller.Healing.Physical", "Base Physical Healing.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Healing_Magical, "SetByCaller.Healing.Magical", "Base Magical Healing.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Coefficient, "SetByCaller.Coefficient", "Fraction of ADB/MDB/PHB/MHB that applies. Defaults to 1.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Dead, "State.Dead", "Health reached 0.");
}
