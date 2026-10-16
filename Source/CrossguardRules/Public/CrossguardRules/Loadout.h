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

	// Skills a character has learned from Skill Gems. Only these can be equipped.
	class CROSSGUARDRULES_API FSkillLibrary
	{
	public:
		bool Contains(const std::string& Id) const { return Skills.count(Id) != 0; }
		// Returns false if already present.
		bool Add(const std::string& Id) { return !Id.empty() && Skills.insert(Id).second; }
		const std::unordered_set<std::string>& All() const { return Skills; }

	private:
		std::unordered_set<std::string> Skills;
	};

	enum class ELearnError
	{
		None,
		// Gems are learned only between raids.
		InRaid,
		UnknownSkill,
		// The skill is already in the library; the gem is kept.
		AlreadyLearned,
	};

	// Learns a Skill Gem's skill into the library. On None the caller consumes the gem; on any
	// error the gem is kept and the library is unchanged.
	CROSSGUARDRULES_API ELearnError LearnSkillGem(
		const FSkillCatalog& Catalog, FSkillLibrary& Library, const std::string& SkillId, bool bInRaid);

	enum class ELoadoutError
	{
		None,
		UnknownSkill,
		// Not in the character's Skill Library.
		NotLearned,
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

	// Reports the first error found, checking in this order: unknown, not learned, duplicate,
	// superseded, over capacity. TotalCost is filled in whenever every skill is known.
	// Library null skips the not-learned check (tests, editor debug loadouts).
	CROSSGUARDRULES_API FLoadoutResult ValidateLoadout(
		const FSkillCatalog& Catalog, const std::vector<std::string>& Equipped, int Capacity,
		const FSkillLibrary* Library = nullptr);

	// The permanent skill effects a loadout applies, each once.
	CROSSGUARDRULES_API std::unordered_set<std::string> ResolveLoadoutEffects(
		const FSkillCatalog& Catalog, const std::vector<std::string>& Equipped);

	// How each permanent effect of a loadout comes to it: one chain per way. An empty chain: the
	// skill is equipped itself. Otherwise the bundles it comes through, from the equipped one down to
	// the one that includes it. A bundle's requirement also switches off the skills it includes, so an
	// effect is on while one of its chains has every bundle's requirement met (checked by the caller:
	// the rules know no equipment). Two bundles sharing a component give it two chains. The keys are
	// ResolveLoadoutEffects' result.
	using FLoadoutSources = std::unordered_map<std::string, std::vector<std::vector<std::string>>>;
	CROSSGUARDRULES_API FLoadoutSources ResolveLoadoutSources(const FSkillCatalog& Catalog, const std::vector<std::string>& Equipped);
}
