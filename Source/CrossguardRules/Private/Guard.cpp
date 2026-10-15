#include "CrossguardRules/Guard.h"

#include <cmath>

namespace Crossguard
{
	namespace
	{
		struct FPenalty
		{
			double Damage = 0.0;
			double Stun = 0.0;
		};

		FPenalty PenaltyFor(const FGuardConfig& Config, int Difference)
		{
			if (Difference <= 0)
			{
				return {};
			}
			if (Difference == 1)
			{
				return {Config.DamageAtDifference1, 0.0};
			}
			if (Difference == 2)
			{
				return {Config.DamageAtDifference2, Config.StunAtDifference2};
			}
			return {Config.DamageAtDifference3, Config.StunAtDifference3};
		}
	}

	FGuardOutcome ResolveGuard(const FGuardConfig& Config, int AttackerImpactPower, int DefenderImpactPower, bool bShield, bool bPiercing)
	{
		FGuardOutcome Outcome;
		const int Difference = AttackerImpactPower - DefenderImpactPower;
		if (Difference > 0)
		{
			// The attacker wins: part of the hit goes through, and a stun from 2 up.
			const FPenalty Penalty = PenaltyFor(Config, Difference);
			Outcome.DefenderDamage = Penalty.Damage;
			Outcome.DefenderStunSeconds = Penalty.Stun;
			if (bPiercing && Outcome.DefenderDamage < Config.PiercingDamage)
			{
				Outcome.DefenderDamage = Config.PiercingDamage;
			}
			return Outcome;
		}
		// A tie or the defender wins: the hit is stopped.
		Outcome.DefenderDamage = bPiercing ? Config.PiercingDamage : 0.0;
		const FPenalty Penalty = PenaltyFor(Config, -Difference);
		// Ruling (mirror): the attacker suffers the loser's share of the defender's weapon; a shield
		// deals none, but still stuns.
		Outcome.AttackerDamage = bShield ? 0.0 : Penalty.Damage;
		Outcome.AttackerStunSeconds = Penalty.Stun;
		Outcome.bParryBonus = !bShield;
		return Outcome;
	}

	bool HitMeetsGuard(const FGuardConfig& Config, double GuardX, double GuardY, double GuardZ, double IncomingX, double IncomingY,
		double IncomingZ)
	{
		const double GuardLength = std::sqrt(GuardX * GuardX + GuardY * GuardY + GuardZ * GuardZ);
		const double IncomingLength = std::sqrt(IncomingX * IncomingX + IncomingY * IncomingY + IncomingZ * IncomingZ);
		if (GuardLength <= 0.0 || IncomingLength <= 0.0)
		{
			return false;
		}
		const double Cosine = (GuardX * IncomingX + GuardY * IncomingY + GuardZ * IncomingZ) / (GuardLength * IncomingLength);
		return Cosine >= std::cos(Config.GuardHalfAngleDegrees * 3.14159265358979323846 / 180.0);
	}
}
