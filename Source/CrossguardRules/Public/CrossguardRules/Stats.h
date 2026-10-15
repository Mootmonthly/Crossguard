// Stats derived from attributes alone. Gear and skill modifiers are layered on top by GAS.
//
// Percent stats are fractions: 0.15 means +15%. Bonuses and resistances are 0 at the
// reference attribute value and go negative below it.

#pragma once

#include "CrossguardRules/Config.h"
#include "CrossguardRules/Progression.h"
#include "CrossguardRules/RulesApi.h"

namespace Crossguard
{
	enum class ESkillCategory
	{
		Active,
		Spell,
		Art,
		Passive,
	};

	struct FDerivedStats
	{
		// STR
		double MaxHp = 0.0;
		double Adb = 0.0;
		double Ar = 0.0;
		double Phb = 0.0;
		double ActiveCastTimeReduction = 0.0;
		double ActiveCdr = 0.0;

		// AGI
		double MoveSpeed = 0.0;
		double ActionSpeed = 0.0;
		double Dexterity = 0.0;
		double ArtCastTimeReduction = 0.0;
		double ArtCdr = 0.0;

		// INT
		double Mdb = 0.0;
		double Mr = 0.0;
		double Mhb = 0.0;
		double SpellCastTimeReduction = 0.0;
		double SpellCdr = 0.0;
	};

	CROSSGUARDRULES_API FDerivedStats DeriveStats(const FAttributes& Attributes, const FScalingConfig& Config);

	// Max HP after MHPB. Never below 1.
	CROSSGUARDRULES_API double ApplyMaxHpBonus(double BaseMaxHp, double Mhpb);

	// The attribute-derived CDR / cast time reduction for a skill category. Passive gets 0.
	CROSSGUARDRULES_API double CategoryCdr(const FDerivedStats& Stats, ESkillCategory Category);
	CROSSGUARDRULES_API double CategoryCastTimeReduction(const FDerivedStats& Stats, ESkillCategory Category);
}
