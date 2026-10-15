#include "CrossguardRules/Items.h"

#include <cmath>

namespace Crossguard
{
	namespace
	{
		// SplitMix64: small, fast, identical output on every platform.
		struct FSplitMix64
		{
			uint64_t State;

			uint64_t Next()
			{
				uint64_t Z = (State += 0x9E3779B97F4A7C15ull);
				Z = (Z ^ (Z >> 30)) * 0xBF58476D1CE4E5B9ull;
				Z = (Z ^ (Z >> 27)) * 0x94D049BB133111EBull;
				return Z ^ (Z >> 31);
			}

			// Uniform in [0, 1).
			double NextUnit()
			{
				return static_cast<double>(Next() >> 11) * (1.0 / 9007199254740992.0);
			}

			// Uniform in [0, Bound). Bound > 0.
			size_t NextIndex(size_t Bound)
			{
				return static_cast<size_t>(NextUnit() * static_cast<double>(Bound));
			}
		};

		bool IsWildcard(EWeaponType Type)
		{
			return Type == EWeaponType::AnyMelee || Type == EWeaponType::TwoHandedMelee;
		}

		bool Matches(EWeaponType Required, const FEquipment& Equipment)
		{
			const auto Holds = [&Equipment](auto&& Predicate)
			{
				return (Equipment.MainHand && Predicate(*Equipment.MainHand)) || (Equipment.OffHand && Predicate(*Equipment.OffHand));
			};
			switch (Required)
			{
			case EWeaponType::AnyMelee:
				return Holds([](const FEquippedWeapon& Weapon) { return Weapon.bMelee; });
			case EWeaponType::TwoHandedMelee:
				return Equipment.MainHand && Equipment.MainHand->bMelee && Equipment.MainHand->Handedness == EHandedness::TwoHanded;
			default:
				return Holds([Required](const FEquippedWeapon& Weapon) { return Weapon.Type == Required; });
			}
		}
	}

	int RarityLevel(ERarity Rarity)
	{
		return static_cast<int>(Rarity);
	}

	double ValueAtRarity(double BaseValue, double PerRarity, ERarity Rarity)
	{
		return BaseValue + PerRarity * RarityLevel(Rarity);
	}

	int AffixSlots(const FItemConfig& Config, ERarity Rarity)
	{
		return Config.AffixSlotsByRarity[RarityLevel(Rarity)];
	}

	double AttackInterval(const FItemConfig& Config, double Attacks, double PerSeconds, double ActionSpeed)
	{
		if (Attacks <= 0.0)
		{
			return 0.0;
		}
		const double Factor = 1.0 + ActionSpeed;
		return (PerSeconds / Attacks) / (Factor < Config.MinActionSpeedFactor ? Config.MinActionSpeedFactor : Factor);
	}

	EEquipError ValidateEquipment(const FEquipment& Equipment)
	{
		if ((Equipment.MainHand && IsWildcard(Equipment.MainHand->Type)) || (Equipment.OffHand && IsWildcard(Equipment.OffHand->Type)))
		{
			return EEquipError::WildcardType;
		}
		if (Equipment.MainHand && Equipment.MainHand->Type == EWeaponType::Shield)
		{
			return EEquipError::ShieldInMainHand;
		}
		if (Equipment.OffHand)
		{
			if (Equipment.OffHand->Handedness == EHandedness::TwoHanded)
			{
				return EEquipError::TwoHandedInOffHand;
			}
			if (Equipment.MainHand && Equipment.MainHand->Handedness == EHandedness::TwoHanded)
			{
				return EEquipError::TwoHandedWithOffHand;
			}
		}
		return EEquipError::None;
	}

	ESecondaryAction SecondaryAction(const FEquipment& Equipment)
	{
		if (Equipment.OffHand)
		{
			if (Equipment.OffHand->Type == EWeaponType::Shield)
			{
				return ESecondaryAction::Block;
			}
			return Equipment.OffHand->bAttacks ? ESecondaryAction::OffHandAttack : ESecondaryAction::None;
		}
		if (Equipment.MainHand && Equipment.MainHand->bCanParry)
		{
			return ESecondaryAction::Parry;
		}
		return ESecondaryAction::None;
	}

	bool SatisfiesWeaponRequirement(const std::vector<EWeaponType>& AnyOf, const FEquipment& Equipment)
	{
		if (AnyOf.empty())
		{
			return true;
		}
		for (const EWeaponType Required : AnyOf)
		{
			if (Matches(Required, Equipment))
			{
				return true;
			}
		}
		return false;
	}

	std::vector<FRolledAffix> RollAffixes(const std::vector<FAffixDefinition>& Pool, int Count, uint64_t Seed)
	{
		FSplitMix64 Rng{Seed};
		std::vector<size_t> Remaining(Pool.size());
		for (size_t I = 0; I < Remaining.size(); ++I)
		{
			Remaining[I] = I;
		}

		std::vector<FRolledAffix> Result;
		while (Count-- > 0 && !Remaining.empty())
		{
			const size_t Pick = Rng.NextIndex(Remaining.size());
			const FAffixDefinition& Affix = Pool[Remaining[Pick]];
			Remaining[Pick] = Remaining.back();
			Remaining.pop_back();

			double Value;
			if (Affix.Step > 0.0 && Affix.Max > Affix.Min)
			{
				// Every grid value from Min to Max inclusive is equally likely. The tolerance keeps
				// Max on the grid despite rounding, including ranges stored as float in Unreal data
				// tables (0.05f - 0.02f) / 0.01f can come out just under 3.
				const size_t Steps = static_cast<size_t>(std::floor((Affix.Max - Affix.Min) / Affix.Step + 1e-4));
				Value = Affix.Min + static_cast<double>(Rng.NextIndex(Steps + 1)) * Affix.Step;
			}
			else
			{
				Value = Affix.Min + Rng.NextUnit() * (Affix.Max - Affix.Min);
			}
			Result.push_back({Affix.Id, Value});
		}
		return Result;
	}
}
