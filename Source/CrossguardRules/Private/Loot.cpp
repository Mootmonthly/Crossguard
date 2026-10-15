#include "CrossguardRules/Loot.h"

#include "CrossguardRandom.h"

#include <algorithm>
#include <map>

namespace Crossguard
{
	namespace
	{
		using Detail::FSplitMix64;

		// An index into Weights by weight; -1 if every weight is 0.
		int PickWeighted(FSplitMix64& Rng, const int* Weights, size_t Count)
		{
			int64_t Total = 0;
			for (size_t I = 0; I < Count; ++I)
			{
				Total += std::max(Weights[I], 0);
			}
			if (Total <= 0)
			{
				return -1;
			}
			int64_t Roll = static_cast<int64_t>(Rng.NextIndex(static_cast<size_t>(Total)));
			for (size_t I = 0; I < Count; ++I)
			{
				Roll -= std::max(Weights[I], 0);
				if (Roll < 0)
				{
					return static_cast<int>(I);
				}
			}
			return static_cast<int>(Count) - 1;
		}

		int RollRange(FSplitMix64& Rng, int Min, int Max)
		{
			if (Max <= Min)
			{
				return Min;
			}
			return Min + static_cast<int>(Rng.NextIndex(static_cast<size_t>(Max - Min + 1)));
		}

		// A random gem of Wanted; if there is none, the nearest rarity below (a missing rarity never
		// makes a drop rarer), and only then the nearest above.
		bool PickGem(FSplitMix64& Rng, const std::map<int, std::vector<std::string>>& ByRarity, int Wanted, FLootDrop& Out)
		{
			std::vector<int> Order;
			for (int Level = Wanted; Level >= 0; --Level)
			{
				Order.push_back(Level);
			}
			for (int Level = Wanted + 1; Level < 6; ++Level)
			{
				Order.push_back(Level);
			}
			for (const int Level : Order)
			{
				const auto Found = ByRarity.find(Level);
				if (Found != ByRarity.end() && !Found->second.empty())
				{
					Out.Id = Found->second[Rng.NextIndex(Found->second.size())];
					Out.Rarity = static_cast<ERarity>(Level);
					return true;
				}
			}
			return false;
		}
	}

	std::vector<FLootDrop> RollLoot(const FLootTable& Table, const FSkillGemRarities& Gems, uint64_t Seed)
	{
		FSplitMix64 Rng{Seed};
		std::vector<FLootDrop> Drops;

		// Sorted ids per rarity level, so the result does not depend on hash map order.
		std::map<int, std::vector<std::string>> GemsByRarity;
		for (const auto& [Id, Rarity] : Gems)
		{
			GemsByRarity[static_cast<int>(Rarity)].push_back(Id);
		}
		for (auto& [Level, Ids] : GemsByRarity)
		{
			std::sort(Ids.begin(), Ids.end());
		}

		// Weights: the entries, then "nothing" last.
		std::vector<int> Weights;
		Weights.reserve(Table.Entries.size() + 1);
		for (const FLootEntry& Entry : Table.Entries)
		{
			Weights.push_back(Entry.Weight);
		}
		Weights.push_back(Table.NothingWeight);

		const int Rolls = RollRange(Rng, std::max(Table.RollsMin, 0), std::max(Table.RollsMax, 0));
		for (int RollIndex = 0; RollIndex < Rolls; ++RollIndex)
		{
			const int Pick = PickWeighted(Rng, Weights.data(), Weights.size());
			if (Pick < 0 || static_cast<size_t>(Pick) >= Table.Entries.size())
			{
				continue;
			}
			const FLootEntry& Entry = Table.Entries[static_cast<size_t>(Pick)];
			FLootDrop Drop;
			Drop.Kind = Entry.Kind;
			Drop.Id = Entry.Id;
			Drop.Count = std::max(1, RollRange(Rng, Entry.CountMin, Entry.CountMax));
			Drop.Seed = Rng.Next();

			switch (Entry.Kind)
			{
			case ELootKind::Gold:
				Drop.Rarity = ERarity::Common;
				break;
			case ELootKind::SkillGem:
			{
				// A gem is one item; its rarity is its skill's.
				Drop.Count = 1;
				if (!Entry.Id.empty())
				{
					const auto Found = Gems.find(Entry.Id);
					Drop.Rarity = Found != Gems.end() ? Found->second : ERarity::Common;
					break;
				}
				const int Wanted = std::max(PickWeighted(Rng, Table.RarityWeights.data(), Table.RarityWeights.size()), 0);
				if (!PickGem(Rng, GemsByRarity, Wanted, Drop))
				{
					continue;
				}
				break;
			}
			case ELootKind::Weapon:
			case ELootKind::Armor:
			case ELootKind::Item:
				Drop.Rarity = static_cast<ERarity>(std::max(PickWeighted(Rng, Table.RarityWeights.data(), Table.RarityWeights.size()), 0));
				break;
			}
			Drops.push_back(Drop);
		}
		return Drops;
	}
}
