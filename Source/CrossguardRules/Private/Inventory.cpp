#include "CrossguardRules/Inventory.h"

#include <cstddef>
#include <initializer_list>

namespace Crossguard
{
	namespace
	{
		FGridSize Rotate(FGridSize Size, bool bRotated)
		{
			return bRotated ? FGridSize{Size.Height, Size.Width} : Size;
		}
	}

	FGridSize ContainerSize(const FInventoryConfig& Config, EContainerKind Kind, ERarity Rarity)
	{
		if (Kind == EContainerKind::Main)
		{
			return {Config.MainWidth, Config.MainHeight};
		}
		return {Config.ContainerWidth, RarityLevel(Rarity) + 1};
	}

	bool ContainerAccepts(EContainerKind Kind, EItemCategory Category)
	{
		return Kind != EContainerKind::Quiver || Category == EItemCategory::Ammo;
	}

	FGridSize FGridItem::Footprint() const
	{
		return Rotate(Size, Placement.bRotated);
	}

	FInventoryGrid::FInventoryGrid(FGridSize Size)
	{
		if (Size.Width > 0 && Size.Height > 0)
		{
			GridSize = Size;
			Squares.assign(static_cast<size_t>(Size.Squares()), 0);
		}
	}

	bool FInventoryGrid::CanPlace(FGridSize ItemSize, FGridPlacement At, int IgnoreId) const
	{
		const FGridSize Footprint = Rotate(ItemSize, At.bRotated);
		if (Footprint.Width <= 0 || Footprint.Height <= 0 || At.X < 0 || At.Y < 0
			|| At.X + Footprint.Width > GridSize.Width || At.Y + Footprint.Height > GridSize.Height)
		{
			return false;
		}
		for (int Y = At.Y; Y < At.Y + Footprint.Height; ++Y)
		{
			for (int X = At.X; X < At.X + Footprint.Width; ++X)
			{
				const int Occupant = Squares[static_cast<size_t>(Y * GridSize.Width + X)];
				if (Occupant != 0 && Occupant != IgnoreId)
				{
					return false;
				}
			}
		}
		return true;
	}

	bool FInventoryGrid::Place(int Id, FGridSize ItemSize, FGridPlacement At)
	{
		if (Id <= 0 || Find(Id) != nullptr || !CanPlace(ItemSize, At))
		{
			return false;
		}
		GridItems.push_back({Id, ItemSize, At});
		Fill(GridItems.back(), Id);
		return true;
	}

	bool FInventoryGrid::Move(int Id, FGridPlacement At)
	{
		for (FGridItem& Item : GridItems)
		{
			if (Item.Id != Id)
			{
				continue;
			}
			if (!CanPlace(Item.Size, At, Id))
			{
				return false;
			}
			Fill(Item, 0);
			Item.Placement = At;
			Fill(Item, Id);
			return true;
		}
		return false;
	}

	bool FInventoryGrid::Remove(int Id)
	{
		for (size_t I = 0; I < GridItems.size(); ++I)
		{
			if (GridItems[I].Id == Id)
			{
				Fill(GridItems[I], 0);
				GridItems.erase(GridItems.begin() + static_cast<std::ptrdiff_t>(I));
				return true;
			}
		}
		return false;
	}

	bool FInventoryGrid::FindFreeSpot(FGridSize ItemSize, bool bAllowRotation, FGridPlacement& OutPlacement) const
	{
		for (int Y = 0; Y < GridSize.Height; ++Y)
		{
			for (int X = 0; X < GridSize.Width; ++X)
			{
				for (const bool bRotated : {false, true})
				{
					if (bRotated && (!bAllowRotation || ItemSize.Width == ItemSize.Height))
					{
						continue;
					}
					const FGridPlacement Candidate{X, Y, bRotated};
					if (CanPlace(ItemSize, Candidate))
					{
						OutPlacement = Candidate;
						return true;
					}
				}
			}
		}
		return false;
	}

	int FInventoryGrid::ItemAt(int X, int Y) const
	{
		if (X < 0 || Y < 0 || X >= GridSize.Width || Y >= GridSize.Height)
		{
			return 0;
		}
		return Squares[static_cast<size_t>(Y * GridSize.Width + X)];
	}

	const FGridItem* FInventoryGrid::Find(int Id) const
	{
		for (const FGridItem& Item : GridItems)
		{
			if (Item.Id == Id)
			{
				return &Item;
			}
		}
		return nullptr;
	}

	int FInventoryGrid::FreeSquares() const
	{
		int Free = 0;
		for (const int Occupant : Squares)
		{
			Free += Occupant == 0 ? 1 : 0;
		}
		return Free;
	}

	void FInventoryGrid::Fill(const FGridItem& Item, int Id)
	{
		const FGridSize Footprint = Item.Footprint();
		for (int Y = Item.Placement.Y; Y < Item.Placement.Y + Footprint.Height; ++Y)
		{
			for (int X = Item.Placement.X; X < Item.Placement.X + Footprint.Width; ++X)
			{
				Squares[static_cast<size_t>(Y * GridSize.Width + X)] = Id;
			}
		}
	}
}
