#include "CrossguardRules/Combat.h"

namespace Crossguard
{
	namespace
	{
		double FloorZero(double Value)
		{
			return Value < 0.0 ? 0.0 : Value;
		}

		double Clamp01(double Value)
		{
			return Value < 0.0 ? 0.0 : (Value > 1.0 ? 1.0 : Value);
		}
	}

	double CalculateDamage(const FDamageInput& Input)
	{
		const double Raw = FloorZero(Input.Base + Input.Flat);
		const double BonusFactor = FloorZero(1.0 + Input.Bonus * Input.Coefficient);
		const double EffectiveResistance = Input.Resistance > 0.0
			? Input.Resistance * (1.0 - Clamp01(Input.Penetration))
			: Input.Resistance;
		const double ResistanceFactor = FloorZero(1.0 - EffectiveResistance);
		return Raw * BonusFactor * ResistanceFactor;
	}

	double CalculateHealing(double Base, double Flat, double Bonus, double Coefficient)
	{
		return FloorZero(Base + Flat) * FloorZero(1.0 + Bonus * Coefficient);
	}

	double SumReductions(const std::vector<double>& Sources)
	{
		double Total = 0.0;
		for (const double Source : Sources)
		{
			Total += Source;
		}
		return Total;
	}

	double CombineReductions(const std::vector<double>& Sources, double Cap)
	{
		const double Total = SumReductions(Sources);
		return Total > Cap ? Cap : Total;
	}

	double ApplyReduction(double BaseSeconds, double Reduction)
	{
		return FloorZero(BaseSeconds * (1.0 - Reduction));
	}
}
