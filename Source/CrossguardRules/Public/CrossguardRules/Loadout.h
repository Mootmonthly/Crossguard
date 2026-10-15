// Skill loadout validation: Capacity budget, duplicates and superseded skills.

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
		// Skills usable only while this skill is active (e.g. Dragonification's Flight).
		std::vector<std::string> GrantsWhileActive;
	};

	class CROSSGUARDRULES_API FSkillCatalog
	{
	public:
		// Returns false if the id is empty or already present.
		bool Add(const FSkillDefinition& Skill);
		const FSkillDefinition* Find(const std::string& Id) const;

		// The skill plus every skill it includes, transitively: the permanent effects it
		// provides. Unknown ids are skipped.
		std::unordered_set<std::string> EffectiveSkills(const std::string& Id) const;

		// Every skill this skill provides in any way (includes and grants-while-active,
		// transitively), excluding the skill itself. Equipping any of these alongside it is blocked.
		std::unordered_set<std::string> ProvidedSkills(const std::string& Id) const;

	private:
		std::unordered_set<std::string> Collect(const std::string& Id, bool bFollowGrants) const;

		std::unordered_map<std::string, FSkillDefinition> Skills;
	};

	enum class ELoadoutError
	{
		None,
		UnknownSkill,
		DuplicateSkill,
		// An equipped skill is already provided by another equipped skill (a bundle and one
		// of its components, or Dragonification and Flight). Two bundles sharing a component
		// are allowed; the shared component applies once.
		SupersededSkill,
		OverCapacity,
	};

	struct FLoadoutResult
	{
		ELoadoutError Error = ELoadoutError::None;
		// The offending skill, and for SupersededSkill the skill that provides it.
		std::string SkillId;
		std::string OtherSkillId;
		int TotalCost = 0;

		bool IsValid() const { return Error == ELoadoutError::None; }
	};

	// Reports the first error found, checking in this order: unknown, duplicate,
	// superseded, over capacity. TotalCost is filled in whenever every skill is known.
	CROSSGUARDRULES_API FLoadoutResult ValidateLoadout(
		const FSkillCatalog& Catalog, const std::vector<std::string>& Equipped, int Capacity);

	// The permanent skill effects a loadout applies, each once.
	CROSSGUARDRULES_API std::unordered_set<std::string> ResolveLoadoutEffects(
		const FSkillCatalog& Catalog, const std::vector<std::string>& Equipped);
}
