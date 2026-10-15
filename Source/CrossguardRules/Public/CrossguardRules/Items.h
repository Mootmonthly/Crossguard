// Item rarity, weapon stats, equipment slots and affix rolling (docs/item-data-schema.md).

#pragma once

#include "CrossguardRules/Config.h"
#include "CrossguardRules/RulesApi.h"

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
		Staff,
		Hammer,
		Shield,
		Bow,
		Crossbow,
		Sling,
		Throwing,
		Wand,
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
		// Only a shield can go in the off hand (prototype; dual wielding undecided).
		OffHandNotShield,
		TwoHandedWithOffHand,
		// AnyMelee / TwoHandedMelee are requirement wildcards, not items.
		WildcardType,
	};

	CROSSGUARDRULES_API EEquipError ValidateEquipment(const FEquipment& Equipment);

	// True if the equipment satisfies any one of the listed weapon types. Empty list: true.
	// A concrete type matches either hand regardless of handedness (a two-handed sword is a Sword).
	CROSSGUARDRULES_API bool SatisfiesWeaponRequirement(const std::vector<EWeaponType>& AnyOf, const FEquipment& Equipment);

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
