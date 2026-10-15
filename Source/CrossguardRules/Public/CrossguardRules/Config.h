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
