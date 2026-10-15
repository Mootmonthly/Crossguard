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
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Damage_Attack);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Damage_Magic);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Healing_Physical);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Healing_Magical);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Coefficient);

	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Dead);

	// Granted by the equipment effect while the weapon type is held (either hand). AnyMelee: any
	// melee weapon; TwoHandedMelee: a two-handed melee main hand. Use in effect tag requirements,
	// e.g. Sword Mastery's modifiers require Equipment.Wielding.Sword.
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equipment_Wielding_Sword);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equipment_Wielding_Axe);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equipment_Wielding_Dagger);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equipment_Wielding_Spear);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equipment_Wielding_Mace);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equipment_Wielding_Quarterstaff);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equipment_Wielding_Hammer);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equipment_Wielding_Shield);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equipment_Wielding_Bow);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equipment_Wielding_Crossbow);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equipment_Wielding_Sling);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equipment_Wielding_Throwing);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equipment_Wielding_Wand);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equipment_Wielding_MagicStaff);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equipment_Wielding_AnyMelee);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equipment_Wielding_TwoHandedMelee);
}
