// Loot tables (docs/loot-data-schema.md): what a monster drops or a chest holds. Engine-free; the
// game turns each FLootDrop into an inventory item (weapons and armor roll their affixes then).

#pragma once

#include "CrossguardRules/Items.h"
#include "CrossguardRules/RulesApi.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Crossguard
{
	enum class ELootKind
	{
		Weapon,
		Armor,
		// A row of the item table: consumables, ammo, materials, gemstones, ...
		Item,
		SkillGem,
		// Gold coins (Count of them). No rarity.
		Gold,
	};

	struct FLootEntry
	{
		ELootKind Kind = ELootKind::Item;
		// Weapon / armor / item / skill id. A Skill Gem entry with an empty Id is "any Skill Gem":
		// it rolls a rarity, then one of the skills of that rarity (none at that rarity: the nearest
		// rarity below, else above).
		std::string Id;
		int Weight = 1;
		int CountMin = 1;
		int CountMax = 1;
	};

	struct FLootTable
	{
		// Drops per roll of the table, inclusive.
		int RollsMin = 1;
		int RollsMax = 1;
		// Weight of "nothing" against the entries, per drop.
		int NothingWeight = 0;
		// Rarity weights, Common .. Mystic, for weapons, armor, items and "any Skill Gem".
		std::array<int, 6> RarityWeights{1, 0, 0, 0, 0, 0};
		std::vector<FLootEntry> Entries;
	};

	struct FLootDrop
	{
		ELootKind Kind = ELootKind::Item;
		std::string Id;
		ERarity Rarity = ERarity::Common;
		int Count = 1;
		// For the game's own rolls on this drop (affixes), so a seed gives the same loot.
		uint64_t Seed = 0;
	};

	// Skill id -> its Skill Gem rarity. Skills without one are not dropped as "any Skill Gem";
	// named ones drop as Common.
	using FSkillGemRarities = std::unordered_map<std::string, ERarity>;

	// Rolls the table. Same table, gems and seed: same drops.
	CROSSGUARDRULES_API std::vector<FLootDrop> RollLoot(const FLootTable& Table, const FSkillGemRarities& Gems, uint64_t Seed);
}
