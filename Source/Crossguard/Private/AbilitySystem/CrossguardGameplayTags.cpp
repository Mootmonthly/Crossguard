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

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Damage_Attack, "SetByCaller.Damage.Attack", "Base Attack Damage (weapon AD or skill amount).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Damage_Magic, "SetByCaller.Damage.Magic", "Base Magic Damage.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Healing_Physical, "SetByCaller.Healing.Physical", "Base Physical Healing.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Healing_Magical, "SetByCaller.Healing.Magical", "Base Magical Healing.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Coefficient, "SetByCaller.Coefficient", "Fraction of ADB/MDB/PHB/MHB that applies. Defaults to 1.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Dead, "State.Dead", "Health reached 0.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Wielding_Sword, "Equipment.Wielding.Sword", "Holding a Sword.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Wielding_Axe, "Equipment.Wielding.Axe", "Holding an Axe.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Wielding_Dagger, "Equipment.Wielding.Dagger", "Holding a Dagger.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Wielding_Spear, "Equipment.Wielding.Spear", "Holding a Spear.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Wielding_Mace, "Equipment.Wielding.Mace", "Holding a Mace.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Wielding_Quarterstaff, "Equipment.Wielding.Quarterstaff", "Holding a Quarterstaff.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Wielding_Hammer, "Equipment.Wielding.Hammer", "Holding a Hammer.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Wielding_Shield, "Equipment.Wielding.Shield", "Holding a Shield.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Wielding_Bow, "Equipment.Wielding.Bow", "Holding a Bow.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Wielding_Crossbow, "Equipment.Wielding.Crossbow", "Holding a Crossbow.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Wielding_Sling, "Equipment.Wielding.Sling", "Holding a Sling.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Wielding_Throwing, "Equipment.Wielding.Throwing", "Holding a Throwing.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Wielding_Wand, "Equipment.Wielding.Wand", "Holding a Wand.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Wielding_MagicStaff, "Equipment.Wielding.MagicStaff", "Holding a magic staff.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Wielding_AnyMelee, "Equipment.Wielding.AnyMelee", "Holding any melee weapon (shields count).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Wielding_TwoHandedMelee, "Equipment.Wielding.TwoHandedMelee", "Holding a two-handed melee weapon in the main hand.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Armor_Plate, "Equipment.Armor.Plate", "Wearing at least one plate piece.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Armor_PlateAllSlots, "Equipment.Armor.PlateAllSlots", "Plate in Head, Chest, Hands, Legs and Feet.");
}
