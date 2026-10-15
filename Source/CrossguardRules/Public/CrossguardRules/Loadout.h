// Skill loadout validation: Capacity budget, duplicates and bundle conflicts.

#pragma once

#include "CrossguardRules/RulesApi.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Crossguard
{
	struct FSkillDefinition
	{
		std::string Id;
		int CapacityCost = 0;
		// Skills whose full effects this skill contains permanently (bundles).
		std::vector<std::string> Includes;
	};

	class CROSSGUARDRULES_API FSkillCatalog
	{
	public:
		// Returns false if the id is empty or already present.
		bool Add(const FSkillDefinition& Skill);
		const FSkillDefinition* Find(const std::string& Id) const;

		// The skill plus every skill it includes, transitively. Unknown ids are skipped.
		std::unordered_set<std::string> EffectiveSkills(const std::string& Id) const;

	private:
		std::unordered_map<std::string, FSkillDefinition> Skills;
	};

	enum class ELoadoutError
	{
		None,
		UnknownSkill,
		DuplicateSkill,
		// Two equipped skills would grant the same skill: a bundle plus one of its
		// components, or two bundles sharing a component.
		OverlappingSkills,
		OverCapacity,
	};

	struct FLoadoutResult
	{
		ELoadoutError Error = ELoadoutError::None;
		// The offending skill, and for OverlappingSkills the skill it overlaps with.
		std::string SkillId;
		std::string OtherSkillId;
		int TotalCost = 0;

		bool IsValid() const { return Error == ELoadoutError::None; }
	};

	// Reports the first error found, checking in this order: unknown, duplicate,
	// overlapping, over capacity. TotalCost is filled in whenever every skill is known.
	CROSSGUARDRULES_API FLoadoutResult ValidateLoadout(
		const FSkillCatalog& Catalog, const std::vector<std::string>& Equipped, int Capacity);
}
