#include "CrossguardRules/Gathering.h"

namespace Crossguard
{
	EGatherStartError CanStartGather(bool bDepleted, bool bGatheringByOther, bool bGathererDead)
	{
		if (bGathererDead)
		{
			return EGatherStartError::GathererDead;
		}
		if (bDepleted)
		{
			return EGatherStartError::Depleted;
		}
		if (bGatheringByOther)
		{
			return EGatherStartError::Busy;
		}
		return EGatherStartError::None;
	}

	EGatherInterrupt CheckGatherInterrupt(const FGatherConfig& Config, bool bDead, double MovedDistance, double HealthLost, bool bAttacked,
		bool bInRange)
	{
		if (bDead)
		{
			return EGatherInterrupt::Died;
		}
		if (MovedDistance > Config.MoveTolerance)
		{
			return EGatherInterrupt::Moved;
		}
		if (HealthLost > 0.0)
		{
			return EGatherInterrupt::Damaged;
		}
		if (bAttacked)
		{
			return EGatherInterrupt::Attacked;
		}
		if (!bInRange)
		{
			return EGatherInterrupt::OutOfRange;
		}
		return EGatherInterrupt::None;
	}

	bool HasGatherNodeRespawned(const FGatherConfig& Config, double SecondsSinceDepleted)
	{
		return Config.RespawnSeconds > 0.0 && SecondsSinceDepleted >= Config.RespawnSeconds;
	}
}
