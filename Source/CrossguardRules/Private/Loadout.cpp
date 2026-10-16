#include "CrossguardRules/Loadout.h"

#include <algorithm>

namespace CrossguardLoadoutSources
{
	// Adds Id's chain and walks into what it includes. Chain: the bundles above Id. A skill already on
	// the chain is a cycle and is skipped.
	void Visit(const Crossguard::FSkillCatalog& Catalog, const std::string& Id, std::vector<std::string>& Chain,
		Crossguard::FLoadoutSources& Sources)
	{
		const Crossguard::FSkillDefinition* Skill = Catalog.Find(Id);
		if (Skill == nullptr || std::find(Chain.begin(), Chain.end(), Id) != Chain.end())
		{
			return;
		}
		Sources[Id].push_back(Chain);
		Chain.push_back(Id);
		for (const std::string& Included : Skill->Includes)
		{
			Visit(Catalog, Included, Chain, Sources);
		}
		Chain.pop_back();
	}
}

namespace Crossguard
{
	bool FSkillCatalog::Add(const FSkillDefinition& Skill)
	{
		if (Skill.Id.empty())
		{
			return false;
		}
		return Skills.emplace(Skill.Id, Skill).second;
	}

	const FSkillDefinition* FSkillCatalog::Find(const std::string& Id) const
	{
		const auto It = Skills.find(Id);
		return It == Skills.end() ? nullptr : &It->second;
	}

	std::unordered_set<std::string> FSkillCatalog::Collect(const std::string& Id, bool bFollowGrants) const
	{
		std::unordered_set<std::string> Result;
		std::vector<std::string> Pending{Id};
		while (!Pending.empty())
		{
			const std::string Current = Pending.back();
			Pending.pop_back();
			const FSkillDefinition* Skill = Find(Current);
			// The visited check also stops cycles.
			if (Skill == nullptr || !Result.insert(Current).second)
			{
				continue;
			}
			Pending.insert(Pending.end(), Skill->Includes.begin(), Skill->Includes.end());
			if (bFollowGrants)
			{
				Pending.insert(Pending.end(), Skill->GrantsWhileActive.begin(), Skill->GrantsWhileActive.end());
			}
		}
		return Result;
	}

	std::unordered_set<std::string> FSkillCatalog::EffectiveSkills(const std::string& Id) const
	{
		return Collect(Id, false);
	}

	std::unordered_set<std::string> FSkillCatalog::ProvidedSkills(const std::string& Id) const
	{
		std::unordered_set<std::string> Result = Collect(Id, true);
		Result.erase(Id);
		return Result;
	}

	ELearnError LearnSkillGem(const FSkillCatalog& Catalog, FSkillLibrary& Library, const std::string& SkillId, bool bInRaid)
	{
		if (bInRaid)
		{
			return ELearnError::InRaid;
		}
		if (Catalog.Find(SkillId) == nullptr)
		{
			return ELearnError::UnknownSkill;
		}
		return Library.Add(SkillId) ? ELearnError::None : ELearnError::AlreadyLearned;
	}

	FLoadoutResult ValidateLoadout(
		const FSkillCatalog& Catalog, const std::vector<std::string>& Equipped, int Capacity, const FSkillLibrary* Library)
	{
		FLoadoutResult Result;

		for (const std::string& Id : Equipped)
		{
			const FSkillDefinition* Skill = Catalog.Find(Id);
			if (Skill == nullptr)
			{
				Result.Error = ELoadoutError::UnknownSkill;
				Result.SkillId = Id;
				Result.TotalCost = 0;
				return Result;
			}
			Result.TotalCost += Skill->CapacityCost;
		}

		if (Library != nullptr)
		{
			for (const std::string& Id : Equipped)
			{
				if (!Library->Contains(Id))
				{
					Result.Error = ELoadoutError::NotLearned;
					Result.SkillId = Id;
					return Result;
				}
			}
		}

		for (size_t I = 0; I < Equipped.size(); ++I)
		{
			for (size_t J = 0; J < I; ++J)
			{
				if (Equipped[I] == Equipped[J])
				{
					Result.Error = ELoadoutError::DuplicateSkill;
					Result.SkillId = Equipped[I];
					return Result;
				}
			}
		}

		for (const std::string& Provider : Equipped)
		{
			const std::unordered_set<std::string> Provided = Catalog.ProvidedSkills(Provider);
			for (const std::string& Id : Equipped)
			{
				if (Provided.count(Id) != 0)
				{
					Result.Error = ELoadoutError::SupersededSkill;
					Result.SkillId = Id;
					Result.OtherSkillId = Provider;
					return Result;
				}
			}
		}

		if (Result.TotalCost > Capacity)
		{
			Result.Error = ELoadoutError::OverCapacity;
		}
		return Result;
	}

	std::unordered_set<std::string> ResolveLoadoutEffects(const FSkillCatalog& Catalog, const std::vector<std::string>& Equipped)
	{
		std::unordered_set<std::string> Result;
		for (const std::string& Id : Equipped)
		{
			const std::unordered_set<std::string> Effective = Catalog.EffectiveSkills(Id);
			Result.insert(Effective.begin(), Effective.end());
		}
		return Result;
	}

	FLoadoutSources ResolveLoadoutSources(const FSkillCatalog& Catalog, const std::vector<std::string>& Equipped)
	{
		FLoadoutSources Sources;
		for (const std::string& Id : Equipped)
		{
			std::vector<std::string> Chain;
			CrossguardLoadoutSources::Visit(Catalog, Id, Chain, Sources);
		}
		return Sources;
	}
}
