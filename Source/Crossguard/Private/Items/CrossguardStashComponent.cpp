#include "Items/CrossguardStashComponent.h"

#include "Crossguard.h"
#include "Game/CrossguardPlayerState.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "Skills/CrossguardLoadoutComponent.h"

namespace
{
	FCrossguardInventoryResult StashFail(ECrossguardInventoryError Error)
	{
		FCrossguardInventoryResult Result;
		Result.Error = Error;
		return Result;
	}
}

UCrossguardStashComponent::UCrossguardStashComponent()
{
	SetIsReplicatedByDefault(true);
}

void UCrossguardStashComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(UCrossguardStashComponent, Tabs, COND_OwnerOnly);
}

void UCrossguardStashComponent::BeginPlay()
{
	Super::BeginPlay();

	if (IsServer() && Tabs.IsEmpty())
	{
		Tabs.SetNum(FMath::Max(1, Crossguard::FInventoryConfig().StartingStashTabs));
	}
}

bool UCrossguardStashComponent::IsServer() const
{
	return GetOwner() != nullptr && GetOwner()->HasAuthority();
}

FIntPoint UCrossguardStashComponent::GetTabSize() const
{
	const Crossguard::FInventoryConfig Config;
	return FIntPoint(Config.StashTabWidth, Config.StashTabHeight);
}

void UCrossguardStashComponent::GrantStashTab()
{
	if (IsServer())
	{
		Tabs.AddDefaulted();
		OnStashChanged.Broadcast();
	}
}

void UCrossguardStashComponent::CopyFrom(const UCrossguardStashComponent& Other)
{
	if (IsServer())
	{
		Tabs = Other.Tabs;
		NextItemId = Other.NextItemId;
		OnStashChanged.Broadcast();
	}
}

Crossguard::FInventoryGrid UCrossguardStashComponent::BuildGrid(int32 Tab, int32 IgnoreId) const
{
	const FIntPoint Size = GetTabSize();
	Crossguard::FInventoryGrid Grid(Crossguard::FGridSize{Size.X, Size.Y});
	if (Tabs.IsValidIndex(Tab))
	{
		for (const FCrossguardInventoryItem& Item : Tabs[Tab].Items)
		{
			if (Item.Id != IgnoreId)
			{
				Grid.Place(Item.Id, {Item.Size.X, Item.Size.Y}, {Item.Position.X, Item.Position.Y, Item.bRotated});
			}
		}
	}
	return Grid;
}

bool UCrossguardStashComponent::Locate(int32 StashItemId, int32& OutTab, int32& OutIndex) const
{
	for (int32 Tab = 0; Tab < Tabs.Num(); ++Tab)
	{
		const int32 Index = Tabs[Tab].Items.IndexOfByPredicate([StashItemId](const FCrossguardInventoryItem& Item) { return Item.Id == StashItemId; });
		if (StashItemId != 0 && Index != INDEX_NONE)
		{
			OutTab = Tab;
			OutIndex = Index;
			return true;
		}
	}
	return false;
}

bool UCrossguardStashComponent::Insert(FCrossguardInventoryItem Item, int32 Tab, FIntPoint Position, bool bRotated, bool bAnywhere)
{
	const Crossguard::FGridSize Size{Item.Size.X, Item.Size.Y};
	if (Size.Width <= 0 || Size.Height <= 0)
	{
		return false;
	}
	if (bAnywhere)
	{
		bool bPlaced = false;
		for (int32 Candidate = 0; Candidate < Tabs.Num() && !bPlaced; ++Candidate)
		{
			Crossguard::FGridPlacement Spot;
			if (BuildGrid(Candidate).FindFreeSpot(Size, true, Spot))
			{
				Tab = Candidate;
				Position = FIntPoint(Spot.X, Spot.Y);
				bRotated = Spot.bRotated;
				bPlaced = true;
			}
		}
		if (!bPlaced)
		{
			return false;
		}
	}
	else if (!Tabs.IsValidIndex(Tab) || !BuildGrid(Tab).CanPlace(Size, {Position.X, Position.Y, bRotated}))
	{
		return false;
	}

	Item.Id = NextItemId++;
	Item.Container = ECrossguardInventoryContainer::Main;
	Item.Position = Position;
	Item.bRotated = bRotated;
	Item.bWasEquipped = false;
	Tabs[Tab].Items.Add(Item);
	return true;
}

UCrossguardInventoryComponent* UCrossguardStashComponent::GetUsableInventory(ECrossguardInventoryError& OutError) const
{
	const APlayerState* PlayerState = Cast<APlayerState>(GetOwner());
	const ACrossguardPlayerState* Progression = Cast<ACrossguardPlayerState>(PlayerState);
	if (Progression != nullptr && Progression->IsInRaid())
	{
		// The stash is only reachable between raids.
		OutError = ECrossguardInventoryError::BetweenRaidsOnly;
		return nullptr;
	}
	const APawn* Pawn = PlayerState != nullptr ? PlayerState->GetPawn() : nullptr;
	UCrossguardInventoryComponent* Inventory = Pawn != nullptr ? Pawn->FindComponentByClass<UCrossguardInventoryComponent>() : nullptr;
	OutError = Inventory != nullptr ? ECrossguardInventoryError::None : ECrossguardInventoryError::NotReady;
	return Inventory;
}

// --- Requests -------------------------------------------------------------------------------------

void UCrossguardStashComponent::RequestMoveInStash(int32 StashItemId, int32 Tab, FIntPoint Position, bool bRotated)
{
	if (IsServer())
	{
		Finish(MoveInStash(StashItemId, Tab, Position, bRotated));
	}
	else
	{
		ServerMoveInStash(StashItemId, Tab, Position, bRotated);
	}
}

void UCrossguardStashComponent::ServerMoveInStash_Implementation(int32 StashItemId, int32 Tab, FIntPoint Position, bool bRotated)
{
	Finish(MoveInStash(StashItemId, Tab, Position, bRotated));
}

void UCrossguardStashComponent::RequestStore(int32 InventoryItemId, int32 Tab, FIntPoint Position, bool bRotated)
{
	if (IsServer())
	{
		Finish(Store(InventoryItemId, Tab, Position, bRotated, false));
	}
	else
	{
		ServerStore(InventoryItemId, Tab, Position, bRotated, false);
	}
}

void UCrossguardStashComponent::RequestStoreAnywhere(int32 InventoryItemId)
{
	if (IsServer())
	{
		Finish(Store(InventoryItemId, 0, FIntPoint::ZeroValue, false, true));
	}
	else
	{
		ServerStore(InventoryItemId, 0, FIntPoint::ZeroValue, false, true);
	}
}

void UCrossguardStashComponent::ServerStore_Implementation(int32 InventoryItemId, int32 Tab, FIntPoint Position, bool bRotated, bool bAnywhere)
{
	Finish(Store(InventoryItemId, Tab, Position, bRotated, bAnywhere));
}

void UCrossguardStashComponent::RequestRetrieve(int32 StashItemId)
{
	if (IsServer())
	{
		Finish(Retrieve(StashItemId));
	}
	else
	{
		ServerRetrieve(StashItemId);
	}
}

void UCrossguardStashComponent::ServerRetrieve_Implementation(int32 StashItemId)
{
	Finish(Retrieve(StashItemId));
}

void UCrossguardStashComponent::RequestUseStashItem(int32 StashItemId)
{
	if (IsServer())
	{
		Finish(UseStashItem(StashItemId));
	}
	else
	{
		ServerUseStashItem(StashItemId);
	}
}

void UCrossguardStashComponent::ServerUseStashItem_Implementation(int32 StashItemId)
{
	Finish(UseStashItem(StashItemId));
}

void UCrossguardStashComponent::ClientStashRejected_Implementation(const FCrossguardInventoryResult& Result)
{
	OnStashRejected.Broadcast(Result);
}

void UCrossguardStashComponent::OnRep_Tabs()
{
	OnStashChanged.Broadcast();
}

void UCrossguardStashComponent::Finish(const FCrossguardInventoryResult& Result)
{
	if (Result.IsValid())
	{
		OnStashChanged.Broadcast();
		return;
	}
	UE_LOG(LogCrossguard, Log, TEXT("Stash request rejected for %s: error %d (progression %d)"), *GetNameSafe(GetOwner()),
		static_cast<int32>(Result.Error), static_cast<int32>(Result.ProgressionError));
	// Reaches the owning client; on a listen server host it runs locally.
	ClientStashRejected(Result);
}

// --- Operations (server) --------------------------------------------------------------------------

FCrossguardInventoryResult UCrossguardStashComponent::MoveInStash(int32 StashItemId, int32 Tab, FIntPoint Position, bool bRotated)
{
	ECrossguardInventoryError Error = ECrossguardInventoryError::None;
	if (GetUsableInventory(Error) == nullptr)
	{
		return StashFail(Error);
	}
	int32 FromTab = 0;
	int32 FromIndex = 0;
	if (!Locate(StashItemId, FromTab, FromIndex))
	{
		return StashFail(ECrossguardInventoryError::UnknownItem);
	}
	if (!Tabs.IsValidIndex(Tab))
	{
		return StashFail(ECrossguardInventoryError::NoContainer);
	}

	FCrossguardInventoryItem& Item = Tabs[FromTab].Items[FromIndex];
	const Crossguard::FInventoryGrid Grid = BuildGrid(Tab, StashItemId);

	// Dropped onto the top-left square of the same item at the same rarity: merge.
	int32 TargetTab = 0;
	int32 TargetIndex = 0;
	if (Locate(Grid.ItemAt(Position.X, Position.Y), TargetTab, TargetIndex))
	{
		FCrossguardInventoryItem& Target = Tabs[TargetTab].Items[TargetIndex];
		if (Target.Position == Position && Target.CanStackWith(Item))
		{
			if (Crossguard::MergeStacks(Target.Count, Item.Count, Target.MaxStack) == 0)
			{
				return StashFail(ECrossguardInventoryError::InvalidPosition);
			}
			if (Item.Count <= 0)
			{
				Tabs[FromTab].Items.RemoveAt(FromIndex);
			}
			return {};
		}
	}

	if (!Grid.CanPlace({Item.Size.X, Item.Size.Y}, {Position.X, Position.Y, bRotated}))
	{
		return StashFail(ECrossguardInventoryError::InvalidPosition);
	}
	FCrossguardInventoryItem Moved = Item;
	Moved.Position = Position;
	Moved.bRotated = bRotated;
	Tabs[FromTab].Items.RemoveAt(FromIndex);
	Tabs[Tab].Items.Add(Moved);
	return {};
}

FCrossguardInventoryResult UCrossguardStashComponent::Store(int32 InventoryItemId, int32 Tab, FIntPoint Position, bool bRotated, bool bAnywhere)
{
	ECrossguardInventoryError Error = ECrossguardInventoryError::None;
	UCrossguardInventoryComponent* Inventory = GetUsableInventory(Error);
	if (Inventory == nullptr)
	{
		return StashFail(Error);
	}
	FCrossguardInventoryItem Item;
	if (!Inventory->FindItem(InventoryItemId, Item))
	{
		return StashFail(ECrossguardInventoryError::UnknownItem);
	}
	if (!bAnywhere && !Tabs.IsValidIndex(Tab))
	{
		return StashFail(ECrossguardInventoryError::NoContainer);
	}
	if (!Insert(Item, Tab, Position, bRotated, bAnywhere))
	{
		return StashFail(bAnywhere ? ECrossguardInventoryError::NoRoom : ECrossguardInventoryError::InvalidPosition);
	}
	Inventory->RemoveItem(InventoryItemId);
	return {};
}

FCrossguardInventoryResult UCrossguardStashComponent::Retrieve(int32 StashItemId)
{
	ECrossguardInventoryError Error = ECrossguardInventoryError::None;
	UCrossguardInventoryComponent* Inventory = GetUsableInventory(Error);
	if (Inventory == nullptr)
	{
		return StashFail(Error);
	}
	int32 Tab = 0;
	int32 Index = 0;
	if (!Locate(StashItemId, Tab, Index))
	{
		return StashFail(ECrossguardInventoryError::UnknownItem);
	}

	FCrossguardInventoryItem& Item = Tabs[Tab].Items[Index];
	const int32 Left = Inventory->AddItem(Item);
	if (Left >= Item.Count)
	{
		return StashFail(ECrossguardInventoryError::NoRoom);
	}
	if (Left <= 0)
	{
		Tabs[Tab].Items.RemoveAt(Index);
	}
	else
	{
		Item.Count = Left;
	}
	return {};
}

FCrossguardInventoryResult UCrossguardStashComponent::UseStashItem(int32 StashItemId)
{
	ECrossguardInventoryError Error = ECrossguardInventoryError::None;
	const UCrossguardInventoryComponent* Inventory = GetUsableInventory(Error);
	if (Inventory == nullptr)
	{
		return StashFail(Error);
	}
	int32 Tab = 0;
	int32 Index = 0;
	if (!Locate(StashItemId, Tab, Index))
	{
		return StashFail(ECrossguardInventoryError::UnknownItem);
	}
	if (Tabs[Tab].Items[Index].Category != ECrossguardItemCategory::SkillGem)
	{
		return StashFail(ECrossguardInventoryError::NotUsable);
	}

	ACrossguardPlayerState* Progression = Cast<ACrossguardPlayerState>(GetOwner());
	const UCrossguardLoadoutComponent* Loadout = Inventory->GetOwner()->FindComponentByClass<UCrossguardLoadoutComponent>();
	if (Progression == nullptr || Loadout == nullptr)
	{
		return StashFail(ECrossguardInventoryError::NotReady);
	}
	const ECrossguardProgressionError Learned = Progression->LearnSkill(Tabs[Tab].Items[Index].ItemId, Loadout->SkillCatalog);
	if (Learned != ECrossguardProgressionError::None)
	{
		FCrossguardInventoryResult Result;
		Result.Error = ECrossguardInventoryError::CannotLearn;
		Result.ProgressionError = Learned;
		return Result;
	}

	// Learned: the gem is consumed.
	if (--Tabs[Tab].Items[Index].Count <= 0)
	{
		Tabs[Tab].Items.RemoveAt(Index);
	}
	return {};
}
