// Grid inventory: items take up a rectangle of squares and can be rotated a quarter turn.

#pragma once

#include "CrossguardRules/Config.h"
#include "CrossguardRules/Items.h"
#include "CrossguardRules/RulesApi.h"

#include <vector>

namespace Crossguard
{
	enum class EItemCategory
	{
		Weapon,
		Armor,
		// Arrows and Bolts: the only items a Quiver holds.
		Ammo,
		SkillGem,
		// Backpacks and Quivers.
		Container,
		Material,
		// Potions, bandages.
		Consumable,
		// Explosive bottles, throwing knives and axes, javelins.
		Throwable,
		// Gold coins. Extracting with them moves them into the character's gold balance.
		Currency,
		// Valuables sold to vendors (gemstones).
		Trinket,
		Other,
	};

	enum class EContainerKind
	{
		// The 50-square raid inventory.
		Main,
		Backpack,
		Quiver,
	};

	struct FGridSize
	{
		int Width = 1;
		int Height = 1;

		int Squares() const { return Width * Height; }
	};

	// Main: MainWidth × MainHeight. Backpack and Quiver: ContainerWidth × (rarity level + 1).
	CROSSGUARDRULES_API FGridSize ContainerSize(const FInventoryConfig& Config, EContainerKind Kind, ERarity Rarity = ERarity::Common);

	CROSSGUARDRULES_API bool ContainerAccepts(EContainerKind Kind, EItemCategory Category);

	// Moves as many as fit from a source stack onto a target stack of the same item, up to MaxStack.
	// Returns the number moved. Counts below 0 are treated as 0.
	CROSSGUARDRULES_API int MergeStacks(int& TargetCount, int& SourceCount, int MaxStack);

	enum class EStashTabPurchaseError
	{
		None,
		// Bought between raids only.
		InRaid,
		// MaxPurchasedStashTabs reached.
		AtCap,
		NotEnoughGold,
	};

	CROSSGUARDRULES_API EStashTabPurchaseError CanBuyStashTab(const FInventoryConfig& Config, int PurchasedTabs, int Gold, bool bInRaid);

	// Top-left square of the item's footprint. Rotated swaps width and height.
	struct FGridPlacement
	{
		int X = 0;
		int Y = 0;
		bool bRotated = false;
	};

	struct FGridItem
	{
		int Id = 0;
		// Unrotated size.
		FGridSize Size;
		FGridPlacement Placement;

		FGridSize Footprint() const;
	};

	// Items are identified by a caller-chosen id > 0, unique within the grid.
	class CROSSGUARDRULES_API FInventoryGrid
	{
	public:
		FInventoryGrid() = default;
		explicit FInventoryGrid(FGridSize Size);

		FGridSize Size() const { return GridSize; }

		// True if the footprint lies inside the grid and every square is empty or holds IgnoreId.
		bool CanPlace(FGridSize ItemSize, FGridPlacement At, int IgnoreId = 0) const;

		// Fails on an invalid or duplicate id, a non-positive size, or no room.
		bool Place(int Id, FGridSize ItemSize, FGridPlacement At);
		// Moves or rotates an item already in the grid; it may overlap its own old squares.
		bool Move(int Id, FGridPlacement At);
		bool Remove(int Id);

		// First spot scanning rows top to bottom, left to right; at each square the unrotated
		// footprint is tried before the rotated one.
		bool FindFreeSpot(FGridSize ItemSize, bool bAllowRotation, FGridPlacement& OutPlacement) const;

		// 0 if the square is empty or outside the grid.
		int ItemAt(int X, int Y) const;
		const FGridItem* Find(int Id) const;
		const std::vector<FGridItem>& Items() const { return GridItems; }
		bool IsEmpty() const { return GridItems.empty(); }
		int FreeSquares() const;

	private:
		void Fill(const FGridItem& Item, int Id);

		FGridSize GridSize{0, 0};
		// Row-major item id per square.
		std::vector<int> Squares;
		std::vector<FGridItem> GridItems;
	};
}
