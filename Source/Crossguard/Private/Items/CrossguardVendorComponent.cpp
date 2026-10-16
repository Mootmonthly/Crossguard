#include "Items/CrossguardVendorComponent.h"

#include "Crossguard.h"
#include "CrossguardRules/Economy.h"
#include "Engine/DataTable.h"
#include "Game/CrossguardPlayerState.h"
#include "GameFramework/Pawn.h"
#include "Items/CrossguardStashComponent.h"

namespace
{
	FCrossguardInventoryResult VendorFail(ECrossguardInventoryError Error)
	{
		FCrossguardInventoryResult Result;
		Result.Error = Error;
		return Result;
	}
}

UCrossguardVendorComponent::UCrossguardVendorComponent()
{
	SetIsReplicatedByDefault(true);
}

UCrossguardInventoryComponent* UCrossguardVendorComponent::GetInventory() const
{
	const APlayerState* PlayerState = Cast<APlayerState>(GetOwner());
	const APawn* Pawn = PlayerState != nullptr ? PlayerState->GetPawn() : nullptr;
	return Pawn != nullptr ? Pawn->FindComponentByClass<UCrossguardInventoryComponent>() : nullptr;
}

int32 UCrossguardVendorComponent::GetSellPrice(const FCrossguardInventoryItem& Item) const
{
	// Gear: 5 gold per square per rarity step. Size is set once the item is in an inventory or stash.
	const Crossguard::ERarity GearRarity = static_cast<Crossguard::ERarity>(static_cast<uint8>(
		Item.Category == ECrossguardItemCategory::Weapon ? Item.Weapon.Rarity
		: Item.Category == ECrossguardItemCategory::Armor ? Item.Armor.Rarity : Item.Rarity));
	if (Item.Category == ECrossguardItemCategory::Weapon || Item.Category == ECrossguardItemCategory::Armor
		|| Item.Category == ECrossguardItemCategory::Container)
	{
		return Crossguard::GearSellPrice(Crossguard::FEconomyConfig(), Item.Size.X * Item.Size.Y, GearRarity);
	}

	const UCrossguardInventoryComponent* Inventory = GetInventory();
	const UDataTable* Table = Inventory != nullptr ? Inventory->ItemTable.Get() : nullptr;
	const FCrossguardItemRow* Row = Table != nullptr && !Item.ItemId.IsNone()
		? Table->FindRow<FCrossguardItemRow>(Item.ItemId, TEXT("Vendor"), false)
		: nullptr;
	if (Row == nullptr || Row->Category != Item.Category)
	{
		return 0;
	}
	const int32 Each = Crossguard::SellPrice(Row->SellValue, Row->SellPerRarity, static_cast<Crossguard::ERarity>(static_cast<uint8>(Item.Rarity)));
	return Each * FMath::Max(1, Item.Count);
}

void UCrossguardVendorComponent::RequestSell(ECrossguardItemSource Source, int32 ItemId)
{
	if (GetOwner() == nullptr || !GetOwner()->HasAuthority())
	{
		ServerSell(Source, ItemId);
		return;
	}
	ServerSell_Implementation(Source, ItemId);
}

void UCrossguardVendorComponent::ServerSell_Implementation(ECrossguardItemSource Source, int32 ItemId)
{
	int32 Gold = 0;
	const FCrossguardInventoryResult Result = Sell(Source, ItemId, Gold);
	if (Result.IsValid())
	{
		OnSold.Broadcast(Gold);
		ClientSold(Gold);
		return;
	}
	UE_LOG(LogCrossguard, Log, TEXT("Sale rejected for %s: error %d"), *GetNameSafe(GetOwner()), static_cast<int32>(Result.Error));
	ClientSellRejected(Result);
}

void UCrossguardVendorComponent::ClientSold_Implementation(int32 Gold)
{
	// The listen-server host already broadcast in ServerSell.
	if (GetOwner() != nullptr && !GetOwner()->HasAuthority())
	{
		OnSold.Broadcast(Gold);
	}
}

void UCrossguardVendorComponent::ClientSellRejected_Implementation(const FCrossguardInventoryResult& Result)
{
	OnSellRejected.Broadcast(Result);
}

FCrossguardInventoryResult UCrossguardVendorComponent::Sell(ECrossguardItemSource Source, int32 ItemId, int32& OutGold)
{
	ACrossguardPlayerState* Progression = Cast<ACrossguardPlayerState>(GetOwner());
	UCrossguardInventoryComponent* Inventory = GetInventory();
	UCrossguardStashComponent* Stash = Progression != nullptr ? Progression->GetStash() : nullptr;
	if (Progression == nullptr || Inventory == nullptr || Stash == nullptr)
	{
		return VendorFail(ECrossguardInventoryError::NotReady);
	}
	if (Progression->IsInRaid())
	{
		return VendorFail(ECrossguardInventoryError::BetweenRaidsOnly);
	}

	FCrossguardInventoryItem Item;
	const bool bFound = Source == ECrossguardItemSource::Inventory ? Inventory->FindItem(ItemId, Item) : Stash->FindStashItem(ItemId, Item);
	if (!bFound)
	{
		return VendorFail(ECrossguardInventoryError::UnknownItem);
	}
	OutGold = GetSellPrice(Item);
	if (OutGold <= 0)
	{
		return VendorFail(ECrossguardInventoryError::NotSellable);
	}
	// Banked gold is an int32: refuse rather than wrap around (the item stays).
	if (!Progression->CanReceiveGold(OutGold))
	{
		return VendorFail(ECrossguardInventoryError::AtCap);
	}

	const bool bRemoved = Source == ECrossguardItemSource::Inventory ? Inventory->RemoveItem(ItemId) : Stash->RemoveStashItem(ItemId);
	if (!bRemoved)
	{
		return VendorFail(ECrossguardInventoryError::UnknownItem);
	}
	Progression->AddGold(OutGold);
	return {};
}
