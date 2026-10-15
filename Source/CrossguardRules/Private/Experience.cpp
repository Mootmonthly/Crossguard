#include "CrossguardRules/Experience.h"

#include <cmath>

namespace Crossguard
{
	int XpForSource(const FXpConfig& Config, EXpSource Source, int QuestXp, ERarity Rarity)
	{
		switch (Source)
		{
		case EXpSource::BasicMonster:
			return Config.BasicMonster;
		case EXpSource::BossMonster:
			return Config.BossMonster;
		case EXpSource::PlayerKill:
			return Config.PlayerKill;
		case EXpSource::Quest:
			return QuestXp < Config.MinQuest ? Config.MinQuest : (QuestXp > Config.MaxQuest ? Config.MaxQuest : QuestXp);
		case EXpSource::GatherNode:
			return Config.GatherNode;
		case EXpSource::Craft:
			return Config.CraftPerRarityStep * (RarityLevel(Rarity) + 1);
		case EXpSource::Enchant:
			return Config.EnchantPerRarityStep * (RarityLevel(Rarity) + 1);
		case EXpSource::Extract:
			return Config.Extract;
		}
		return 0;
	}

	int XpToNextLevel(const FProgressionConfig& Config, int Level)
	{
		const int Steps = Level < 1 ? 0 : Level - 1;
		const double Exact = Config.FirstLevelXp * std::pow(Config.XpGrowth, Steps);
		// The epsilon keeps exact halves (60.5) rounding up despite floating-point error.
		return static_cast<int>(std::floor(Exact + 0.5 + 1e-9));
	}

	int TotalXpForLevel(const FProgressionConfig& Config, int Level)
	{
		int Total = 0;
		for (int L = 1; L < Level; ++L)
		{
			Total += XpToNextLevel(Config, L);
		}
		return Total;
	}

	int LevelForTotalXp(const FProgressionConfig& Config, int TotalXp)
	{
		int Level = 1;
		int Needed = 0;
		while (Level < Config.MaxLevel)
		{
			Needed += XpToNextLevel(Config, Level);
			if (TotalXp < Needed)
			{
				break;
			}
			++Level;
		}
		return Level;
	}

	void FRaidXp::Add(int Amount)
	{
		if (Amount > 0)
		{
			PendingXp += Amount;
		}
	}

	int FRaidXp::Extract(const FXpConfig& Config)
	{
		const int Banked = PendingXp + Config.Extract;
		PendingXp = 0;
		return Banked;
	}

	int FRaidXp::FailExtract()
	{
		const int Lost = PendingXp;
		PendingXp = 0;
		return Lost;
	}

	ERespecError CanRespec(bool bInRaid, int RespecTokens)
	{
		if (bInRaid)
		{
			return ERespecError::InRaid;
		}
		if (RespecTokens < 1)
		{
			return ERespecError::NoRespecToken;
		}
		return ERespecError::None;
	}
}
