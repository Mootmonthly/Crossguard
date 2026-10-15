#include "CrossguardRules/Economy.h"

namespace Crossguard
{
	int SellPrice(int SellValue, int SellPerRarity, ERarity Rarity)
	{
		if (SellValue <= 0)
		{
			return 0;
		}
		return SellValue + (SellPerRarity > 0 ? SellPerRarity : 0) * RarityLevel(Rarity);
	}

	int GearSellPrice(const FEconomyConfig& Config, int Squares, ERarity Rarity)
	{
		return Squares > 0 ? Config.GearGoldPerSquare * Squares * (RarityLevel(Rarity) + 1) : 0;
	}

	bool CanRevive(const FDeathConfig& Config, double SecondsSinceDeath)
	{
		return SecondsSinceDeath >= 0.0 && SecondsSinceDeath < Config.ReviveWindowSeconds;
	}

	double LockpickSeconds(const FEconomyConfig& Config, double Dexterity)
	{
		const double Factor = 1.0 + Dexterity;
		return Config.LockpickSeconds / (Factor > Config.MinDexterityFactor ? Factor : Config.MinDexterityFactor);
	}
}
