#include "CrossguardRules/Loadout.h"

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

	std::unordered_set<std::string> FSkillCatalog::EffectiveSkills(const std::string& Id) const
	{
		std::unordered_set<std::string> Result;
		std::vector<std::string> Pending{Id};
		while (!Pending.empty())
		{
			const std::string Current = Pending.back();
			Pending.pop_back();
			const FSkillDefinition* Skill = Find(Current);
			// The visited check also stops include cycles.
			if (Skill == nullptr || !Result.insert(Current).second)
			{
				continue;
			}
			Pending.insert(Pending.end(), Skill->Includes.begin(), Skill->Includes.end());
		}
		return Result;
	}

	FLoadoutResult ValidateLoadout(const FSkillCatalog& Catalog, const std::vector<std::string>& Equipped, int Capacity)
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

		std::vector<std::unordered_set<std::string>> Effective;
		Effective.reserve(Equipped.size());
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
			Effective.push_back(Catalog.EffectiveSkills(Equipped[I]));
		}

		for (size_t I = 0; I < Equipped.size(); ++I)
		{
			for (size_t J = 0; J < I; ++J)
			{
				for (const std::string& Granted : Effective[I])
				{
					if (Effective[J].count(Granted) != 0)
					{
						Result.Error = ELoadoutError::OverlappingSkills;
						Result.SkillId = Equipped[I];
						Result.OtherSkillId = Equipped[J];
						return Result;
					}
				}
			}
		}

		if (Result.TotalCost > Capacity)
		{
			Result.Error = ELoadoutError::OverCapacity;
		}
		return Result;
	}
}
