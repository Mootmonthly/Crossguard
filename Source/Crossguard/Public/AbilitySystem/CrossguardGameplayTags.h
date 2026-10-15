#pragma once

#include "NativeGameplayTags.h"

// Native gameplay tags used by C++. Designers can add more in the editor.
namespace CrossguardTags
{
	// Skill category (docs/design-decisions.md, "Skill categories"). Exactly one per ability.
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Skill_Category_Active);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Skill_Category_Spell);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Skill_Category_Art);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Skill_Category_Passive);

	// Weapon tag and other filters that CDR sources target.
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Skill_Weapon_Melee);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Skill_Weapon_Ranged);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Skill_Minion);

	// SetByCaller magnitudes.
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Cooldown);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Damage_Attack);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Damage_Magic);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Healing_Physical);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Healing_Magical);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Coefficient);

	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Dead);
}
