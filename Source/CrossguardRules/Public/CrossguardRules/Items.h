// Item rarity, weapon stats, equipment slots and affix rolling (docs/item-data-schema.md).

#pragma once

#include "CrossguardRules/Config.h"
#include "CrossguardRules/RulesApi.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Crossguard
{
	enum class ERarity
	{
		Common,
		Uncommon,
		Rare,
		Epic,
		Legendary,
		Mystic,
	};

	constexpr int RarityCount = 6;

	// Common = 0 ... Mystic = 5.
	CROSSGUARDRULES_API int RarityLevel(ERarity Rarity);

	// baseValue + perRarity × level.
	CROSSGUARDRULES_API double ValueAtRarity(double BaseValue, double PerRarity, ERarity Rarity);

	CROSSGUARDRULES_API int AffixSlots(const FItemConfig& Config, ERarity Rarity);

	// (PerSeconds / Attacks) / max(1 + ActionSpeed, MinActionSpeedFactor). 0 if Attacks <= 0.
	CROSSGUARDRULES_API double AttackInterval(const FItemConfig& Config, double Attacks, double PerSeconds, double ActionSpeed);

	// Concrete weapon types plus the two requirement-only wildcards.
	enum class EWeaponType
	{
		Sword,
		Axe,
		Dagger,
		Spear,
		Mace,
		// Blunt two-handed melee (Trauma Mastery).
		Quarterstaff,
		Hammer,
		Shield,
		Bow,
		Crossbow,
		Sling,
		Throwing,
		Wand,
		// Two-handed magic casting weapon.
		MagicStaff,
		// Requirement only: any equipped melee weapon.
		AnyMelee,
		// Requirement only: a two-handed melee weapon in the main hand.
		TwoHandedMelee,
	};

	enum class EHandedness
	{
		OneHanded,
		TwoHanded,
	};

	struct FEquippedWeapon
	{
		EWeaponType Type = EWeaponType::Sword;
		EHandedness Handedness = EHandedness::OneHanded;
		bool bMelee = true;
		// Has a basic attack (shields do not).
		bool bAttacks = true;
		// Can parry with right click when the other hand is empty.
		bool bCanParry = false;
	};

	struct FEquipment
	{
		std::optional<FEquippedWeapon> MainHand;
		std::optional<FEquippedWeapon> OffHand;
	};

	enum class EEquipError
	{
		None,
		// Shields go in the off hand.
		ShieldInMainHand,
		// Two-handed weapons go in the main hand.
		TwoHandedInOffHand,
		// A two-handed main hand leaves no hand free (pavises come later).
		TwoHandedWithOffHand,
		// AnyMelee / TwoHandedMelee are requirement wildcards, not items.
		WildcardType,
	};

	CROSSGUARDRULES_API EEquipError ValidateEquipment(const FEquipment& Equipment);

	// What right click does (docs/design-decisions.md, "Items and weapons").
	enum class ESecondaryAction
	{
		None,
		// Basic attack with the off-hand weapon (dual wielding).
		OffHandAttack,
		// Shield in the off hand.
		Block,
		// Empty off hand and a main-hand weapon that can parry.
		Parry,
	};

	CROSSGUARDRULES_API ESecondaryAction SecondaryAction(const FEquipment& Equipment);

	// True if the equipment satisfies any one of the listed weapon types. Empty list: true.
	// A concrete type matches either hand regardless of handedness (a two-handed sword is a Sword).
	CROSSGUARDRULES_API bool SatisfiesWeaponRequirement(const std::vector<EWeaponType>& AnyOf, const FEquipment& Equipment);

	// Armor (docs/item-data-schema.md, "Armor slots and requirements").
	enum class EArmorSlot
	{
		Head,
		Chest,
		Hands,
		Legs,
		Feet,
		// Capes; later backpacks and quivers. Not armor for plate requirements.
		Back,
	};

	constexpr int ArmorSlotCount = 6;

	enum class EArmorType
	{
		Cloth,
		Leather,
		Plate,
	};

	enum class EArmorRequirement
	{
		None,
		// At least one plate piece ([Armor Mastery]).
		Plate,
		// Plate in Head, Chest, Hands, Legs and Feet; Back excluded ([The Mithril Knight]).
		PlateAllSlots,
	};

	// Armor type worn in each slot, indexed by EArmorSlot; empty if nothing is worn.
	using FArmorLoadout = std::array<std::optional<EArmorType>, ArmorSlotCount>;

	// All six slots count ([Cor Leonis]).
	CROSSGUARDRULES_API int CountEmptyArmorSlots(const FArmorLoadout& Armor);

	CROSSGUARDRULES_API bool SatisfiesArmorRequirement(EArmorRequirement Requirement, const FArmorLoadout& Armor);

	// Affixes. Values roll uniformly in [Min, Max], snapped down to a multiple of Step above Min.
	struct FAffixDefinition
	{
		std::string Id;
		double Min = 0.0;
		double Max = 0.0;
		// 0: no snapping.
		double Step = 0.0;
	};

	struct FRolledAffix
	{
		std::string Id;
		double Value = 0.0;
	};

	// Picks min(Count, pool size) distinct affixes. Deterministic for a given Seed on every
	// platform (own PRNG; std distributions differ between standard libraries).
	CROSSGUARDRULES_API std::vector<FRolledAffix> RollAffixes(const std::vector<FAffixDefinition>& Pool, int Count, uint64_t Seed);
}
