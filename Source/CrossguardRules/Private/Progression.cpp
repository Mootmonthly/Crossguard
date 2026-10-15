#include "CrossguardRules/Progression.h"

namespace Crossguard
{
	int ClampLevel(const FProgressionConfig& Config, int Level)
	{
		if (Level < 1)
		{
			return 1;
		}
		return Level > Config.MaxLevel ? Config.MaxLevel : Level;
	}

	int AttributePointsAtLevel(const FProgressionConfig& Config, int Level)
	{
		return Config.StartingAttributePoints + Config.AttributePointsPerLevel * (ClampLevel(Config, Level) - 1);
	}

	int SkillCapacityAtLevel(const FProgressionConfig& Config, int Level)
	{
		return Config.StartingSkillCapacity + Config.SkillCapacityPerLevel * (ClampLevel(Config, Level) - 1);
	}

	EAllocationError ValidateAllocation(const FProgressionConfig& Config, int Level, const FAttributes& Allocation)
	{
		if (Allocation.Str < 0 || Allocation.Agi < 0 || Allocation.Int < 0)
		{
			return EAllocationError::NegativeAttribute;
		}
		if (Allocation.Total() > AttributePointsAtLevel(Config, Level))
		{
			return EAllocationError::TooManyPoints;
		}
		return EAllocationError::None;
	}

	bool RequiresRespec(const FAttributes& Current, const FAttributes& Requested)
	{
		return Requested.Str < Current.Str || Requested.Agi < Current.Agi || Requested.Int < Current.Int;
	}
}
