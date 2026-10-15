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
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Damage_AttackBonus);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Damage_Magic);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Healing_Physical);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Healing_Magical);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Coefficient);

	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Dead);

	// Status effects.
	// Mute: no skills can be cast and passive skills stop working (basic attacks still work).
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Muted);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Silenced);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Stunned);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Guarding);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Slowed);
	// The screen flashes white (Shooting Star); the HUD reacts to this tag.
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Blinded);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Flying);
	// Dragonification: weapons and equipped skills are blocked; Flare Breath and Flight work.
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Dragonified);
	// Chop Down: the next axe attack is empowered.
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_ChopDownReady);
	// Next-attack skills, other states and passive markers (descriptions in the .cpp).
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_NextAttack_Slash);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_NextAttack_MortalStrike);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_NextAttack_ShadeBlow);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_NextAttack_Gouge);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_NextAttack_PurgeShot);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_NextAttack_Neckbite);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_NextAttack_Riposte);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_ArcaneArrows);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_HealBlocked);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Stealthed);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_CCImmune);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_KnockedUp);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Passive_Immutable);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Passive_SplinterOfWinter);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Passive_HuntersTracking);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Multiscale);

	// Ability tags (cancel by tag).
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Flight);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Guard);

	// One cooldown tag per native skill.
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Skill_Dragonification);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Skill_FlareBreath);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Skill_Flight);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Skill_Fireball);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Skill_ChopDown);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Skill_SummonSkeletons);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Skill_SummonTiger);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Skill_SummonGreaterDemon);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Skill_EldritchShield);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Skill_ShootingStar);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Skill_Heal);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Skill_Mute);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Skill_Slash);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Skill_MortalStrike);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Skill_ShadeBlow);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Skill_Gouge);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Skill_PurgeShot);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Skill_ArcaneArrows);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Skill_Quickshot);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Skill_Cleave);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Skill_TripleThrust);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Skill_PartingTheLand);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Skill_HuntersLeap);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Skill_Neckbite);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Skill_GreaterHeal);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Skill_Benediction);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Skill_Unleash);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Skill_Light);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Skill_Wall);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Skill_Stealth);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Skill_Immutable);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Skill_LikeALion);

	// Picks locks without a lockpick item. Grant it from the lockpicking skill's passive effect.
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Lockpicking);

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

	// Armor requirements, granted by the equipment effect ([Armor Mastery], [The Mithril Knight]).
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equipment_Armor_Plate);
	CROSSGUARD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equipment_Armor_PlateAllSlots);
}
