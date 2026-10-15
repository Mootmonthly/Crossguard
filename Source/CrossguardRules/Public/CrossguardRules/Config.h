// Tunable numbers for progression and attribute scaling.
//
// Scaling values are PLACEHOLDERS: linear around the 10-point reference until real formulas
// exist (see docs/design-decisions.md). Change them here only.

#pragma once

namespace Crossguard
{
	struct FProgressionConfig
	{
		int StartingAttributePoints = 30;
		int AttributePointsPerLevel = 3;
		int StartingSkillCapacity = 6;
		int SkillCapacityPerLevel = 1;
		// Season-start cap; rises during a season.
		int MaxLevel = 15;

		// XP for level 1 -> 2; each later level costs XpGrowth times the previous one.
		int FirstLevelXp = 50;
		double XpGrowth = 1.1;
	};

	// XP per source (see docs/design-decisions.md, Experience).
	struct FXpConfig
	{
		int BasicMonster = 5;
		int BossMonster = 50;
		int PlayerKill = 25;
		int GatherNode = 10;
		// Crafting and enchanting: PerRarityStep × (rarity level + 1).
		int CraftPerRarityStep = 10;
		int EnchantPerRarityStep = 10;
		int MinQuest = 10;
		int MaxQuest = 100;
		// Successful extraction.
		int Extract = 35;
	};

	struct FPortalConfig
	{
		// Seconds after the raid starts before anyone can summon an extract portal.
		double UnlockSeconds = 600.0;
		// Channel before the portal opens. Damage does not interrupt it.
		double ChannelSeconds = 60.0;
		// How long the portal stays open once it opens.
		double OpenSeconds = 30.0;
		// The summoner's team must keep someone alive within this distance (cm) of the portal
		// while it channels, or it collapses.
		double KeepAliveRadius = 1000.0;
	};

	struct FEconomyConfig
	{
		// Seconds to pick a lock at 0% Dexterity (10 AGI). Lockpicking cannot fail.
		double LockpickSeconds = 5.0;
		// Floor on (1 + Dexterity) so very low AGI cannot make a lock take forever.
		double MinDexterityFactor = 0.1;
	};

	struct FInventoryConfig
	{
		// Raid inventory: 50 squares. Shape is a placeholder.
		int MainWidth = 10;
		int MainHeight = 5;
		// Backpack / Quiver: Width × (rarity level + 1) squares, i.e. 5..30. Shape is a placeholder.
		int ContainerWidth = 5;
		// Stash: per character, between raids only. More tabs are bought with in-game currency.
		int StashTabWidth = 10;
		int StashTabHeight = 30;
		int StartingStashTabs = 1;
		// Extra tabs cost StashTabPrice gold each, at most MaxPurchasedStashTabs per character.
		int StashTabPrice = 3000;
		int MaxPurchasedStashTabs = 3;
	};

	struct FScalingConfig
	{
		// Attribute value at which every "baseline" in the Stats doc holds.
		int ReferenceAttribute = 10;

		// STR
		double BaseMaxHp = 100.0;
		double MaxHpPerStr = 5.0;
		double AdbPerStr = 0.02;
		double ArPerStr = 0.01;
		double PhbPerStr = 0.02;
		double ActiveCastTimeReductionPerStr = 0.01;
		double ActiveCdrPerStr = 0.01;

		// AGI
		double BaseMoveSpeed = 1.0;
		double MoveSpeedPerAgi = 0.005;
		double ActionSpeedPerAgi = 0.01;
		double DexterityPerAgi = 0.02;
		double ArtCastTimeReductionPerAgi = 0.01;
		double ArtCdrPerAgi = 0.01;

		// INT
		double MdbPerInt = 0.02;
		double MrPerInt = 0.01;
		double MhbPerInt = 0.02;
		double SpellCastTimeReductionPerInt = 0.01;
		double SpellCdrPerInt = 0.01;

		// Cap on the summed CDR from all sources. Cast time reduction is uncapped.
		double CdrCap = 0.50;
	};

	struct FMatchConfig
	{
		// Largest premade team that can enter a match together.
		int MaxTeamSize = 8;
	};

	struct FItemConfig
	{
		// Affix slots by rarity level (Common..Mystic).
		int AffixSlotsByRarity[6] = {0, 1, 2, 3, 4, 5};
		// Floor on (1 + ActionSpeed) so very low AGI cannot stall attacks.
		double MinActionSpeedFactor = 0.1;
	};
}
