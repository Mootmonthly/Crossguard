// Damage, healing, cooldown and cast time formulas (docs/design-decisions.md, "Formulas").

#pragma once

#include "CrossguardRules/RulesApi.h"

#include <vector>

namespace Crossguard
{
	struct FDamageInput
	{
		// Weapon AD or the skill's listed damage.
		double Base = 0.0;
		// AAD for Attack Damage, AMD for Magic Damage.
		double Flat = 0.0;
		// ADB or MDB of the attacker.
		double Bonus = 0.0;
		// Fraction of Bonus that applies. 1.0 when the skill lists none.
		double Coefficient = 1.0;
		// AR or MR of the target.
		double Resistance = 0.0;
		// AP of the attacker: fraction of positive Resistance removed, clamped to [0, 1].
		double Penetration = 0.0;
	};

	// (Base + Flat) × (1 + Bonus × Coefficient) × (1 − Resistance × (1 − Penetration)).
	// Negative resistance increases damage and is not reduced by penetration.
	// Each factor is floored at 0, so damage is never negative.
	CROSSGUARDRULES_API double CalculateDamage(const FDamageInput& Input);

	// (Base + Flat) × (1 + Bonus × Coefficient). Never negative.
	CROSSGUARDRULES_API double CalculateHealing(double Base, double Flat, double Bonus, double Coefficient = 1.0);

	// Sums every applicable reduction (additive). No lower bound: a negative total lengthens
	// the cooldown or cast time. Use for cast time, which is uncapped.
	CROSSGUARDRULES_API double SumReductions(const std::vector<double>& Sources);

	// SumReductions, capped. Use for CDR.
	CROSSGUARDRULES_API double CombineReductions(const std::vector<double>& Sources, double Cap);

	// Base × (1 − Reduction). Never negative.
	CROSSGUARDRULES_API double ApplyReduction(double BaseSeconds, double Reduction);
}
