// Blocking (shields) and parrying (weapons that can parry), held with right click
// (docs/design-decisions.md, Block and parry). When an Attack Damage hit meets a raised guard, the
// two Impact Powers decide: the higher wins by the difference. Engine-free; the game decides
// whether a hit reaches the guard (its surface faces the hit) and applies the outcome.

#pragma once

#include "CrossguardRules/RulesApi.h"

namespace Crossguard
{
	struct FGuardConfig
	{
		// Ruling: what the loser of a collision suffers, by Impact Power difference.
		double DamageAtDifference1 = 0.25;
		double DamageAtDifference2 = 0.5;
		double StunAtDifference2 = 0.5;
		// Difference 3 or more.
		double DamageAtDifference3 = 1.0;
		double StunAtDifference3 = 1.0;
		// Ruling: movement speed while guarding.
		double GuardMoveSpeedMultiplier = 0.5;
		// Ruling: a parry the defender did not lose: +50% Attack Damage Bonus on their next attack
		// and +10% Action Speed for 3 s.
		double ParryAttackDamageBonus = 0.5;
		double ParryActionSpeed = 0.10;
		double ParryBonusSeconds = 3.0;
		// Skill ruling ([Triple Thrust], piercing): a hit the guard would stop deals this much instead.
		double PiercingDamage = 0.5;
		// Assumption: how far off the guard's facing a hit can come from and still meet it (degrees).
		double GuardHalfAngleDegrees = 50.0;
		// User-approved placeholder: Impact Power of natural attacks (claws, fists).
		int UnarmedImpactPower = 3;
		// Ruling: two swinging weapons that meet collide too. Assumption, until swings are animated:
		// a melee swing's blade is in the way this long after it starts.
		double ClashWindowSeconds = 0.25;
		// Ruling: every collision clanks and bounces both back. Assumption: the push (cm/s).
		double BounceSpeed = 300.0;
	};

	struct FGuardOutcome
	{
		// Share of the incoming hit's Attack Damage the defender still takes (0: stopped).
		double DefenderDamage = 1.0;
		double DefenderStunSeconds = 0.0;
		// Share of the defender's own weapon damage the attacker takes back (0 for a shield).
		double AttackerDamage = 0.0;
		double AttackerStunSeconds = 0.0;
		// The defender parried and did not lose: the parry bonus.
		bool bParryBonus = false;
	};

	// One Attack Damage hit against a raised guard that it reached. bShield: a shield's guard (it
	// deals no damage back and gives no parry bonus). bPiercing: the hit passes through a guard that
	// would stop it, for PiercingDamage.
	CROSSGUARDRULES_API FGuardOutcome ResolveGuard(const FGuardConfig& Config, int AttackerImpactPower, int DefenderImpactPower,
		bool bShield, bool bPiercing);

	// Whether a hit arriving from IncomingDirection (unit vector from the defender toward where the
	// hit comes from) meets a guard facing GuardDirection (unit vector).
	CROSSGUARDRULES_API bool HitMeetsGuard(const FGuardConfig& Config, double GuardX, double GuardY, double GuardZ, double IncomingX,
		double IncomingY, double IncomingZ);
}
