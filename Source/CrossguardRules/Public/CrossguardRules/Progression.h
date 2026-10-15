// Levels, attribute points and Skill Capacity.

#pragma once

#include "CrossguardRules/Config.h"
#include "CrossguardRules/RulesApi.h"

namespace Crossguard
{
	struct FAttributes
	{
		int Str = 0;
		int Agi = 0;
		int Int = 0;

		int Total() const { return Str + Agi + Int; }
	};

	enum class EAllocationError
	{
		None,
		NegativeAttribute,
		TooManyPoints,
	};

	// Level is clamped to [1, MaxLevel].
	CROSSGUARDRULES_API int ClampLevel(const FProgressionConfig& Config, int Level);
	CROSSGUARDRULES_API int AttributePointsAtLevel(const FProgressionConfig& Config, int Level);
	CROSSGUARDRULES_API int SkillCapacityAtLevel(const FProgressionConfig& Config, int Level);

	// Attributes start at 0 and are assigned freely. Unspent points are allowed.
	CROSSGUARDRULES_API EAllocationError ValidateAllocation(
		const FProgressionConfig& Config, int Level, const FAttributes& Allocation);

	// Spending unspent points is free; lowering any attribute is a respec (see Experience.h).
	CROSSGUARDRULES_API bool RequiresRespec(const FAttributes& Current, const FAttributes& Requested);
}
