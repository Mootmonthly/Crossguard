// Experience: XP sources, the level curve, raid XP held until extraction, and respec.

#pragma once

#include "CrossguardRules/Config.h"
#include "CrossguardRules/Items.h"
#include "CrossguardRules/RulesApi.h"

namespace Crossguard
{
	enum class EXpSource
	{
		BasicMonster,
		BossMonster,
		PlayerKill,
		// Amount set per quest, clamped to [MinQuest, MaxQuest].
		Quest,
		GatherNode,
		// Scaled by the item's rarity.
		Craft,
		Enchant,
		Extract,
	};

	// QuestXp is read for Quest only; Rarity for Craft and Enchant only.
	CROSSGUARDRULES_API int XpForSource(const FXpConfig& Config, EXpSource Source, int QuestXp = 0, ERarity Rarity = ERarity::Common);

	// XP to go from Level to Level + 1: FirstLevelXp × XpGrowth^(Level − 1), rounded to the nearest
	// whole XP (halves up). Levels below 1 count as 1. Not capped by MaxLevel.
	CROSSGUARDRULES_API int XpToNextLevel(const FProgressionConfig& Config, int Level);

	// Total XP needed to reach Level from level 1 (0 for level 1).
	CROSSGUARDRULES_API int TotalXpForLevel(const FProgressionConfig& Config, int Level);

	// Level reached with this much total XP, capped at MaxLevel. XP keeps accumulating past the cap,
	// so a season raising MaxLevel levels characters up from XP they already earned.
	CROSSGUARDRULES_API int LevelForTotalXp(const FProgressionConfig& Config, int TotalXp);

	// XP earned during one raid. Held until the raid ends: extracting banks it, failing to extract
	// loses it. Dying alone does not lose it (a teammate can revive you).
	class CROSSGUARDRULES_API FRaidXp
	{
	public:
		// Ignores non-positive amounts.
		void Add(int Amount);
		int Pending() const { return PendingXp; }

		// Returns the XP to bank (pending plus the extraction reward) and resets.
		int Extract(const FXpConfig& Config);
		// Returns the XP lost and resets.
		int FailExtract();

	private:
		int PendingXp = 0;
	};

	enum class ERespecError
	{
		None,
		InRaid,
		NotEnoughCurrency,
	};

	// Respec clears the attribute allocation for RespecFee currency. Only between raids.
	CROSSGUARDRULES_API ERespecError CanRespec(const FProgressionConfig& Config, bool bInRaid, int Currency);
}
