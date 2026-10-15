#include "Items/CrossguardInventoryComponent.h"

#include "Crossguard.h"
#include "Engine/DataTable.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "Skills/CrossguardLoadoutComponent.h"

namespace
{
	Crossguard::EItemCategory ToRulesCategory(ECrossguardItemCategory Category)
	{
		return static_cast<Crossguard::EItemCategory>(static_cast<uint8>(Category));
	}

	Crossguard::EContainerKind ToRulesContainer(ECrossguardInventoryContainer Container)
	{
		return static_cast<Crossguard::EContainerKind>(static_cast<uint8>(Container));
	}

	bool IsTableItem(ECrossguardItemCategory Category)
	{
		return Category == ECrossguardItemCategory::Ammo || Category == ECrossguardItemCategory::Consumable
			|| Category == ECrossguardItemCategory::Throwable || Category == ECrossguardItemCategory::Material;
	}

	FCrossguardInventoryResult Fail(ECrossguardInventoryError Error)
	{
		FCrossguardInventoryResult Result;
		Result.Error = Error;
		return Result;
	}

	FCrossguardInventoryResult FailEquip(ECrossguardEquipError Error)
	{
		FCrossguardInventoryResult Result;
		Result.Error = ECrossguardInventoryError::CannotEquip;
		Result.EquipError = Error;
		return Result;
	}

	constexpr ECrossguardInventoryContainer AllContainers[] = {
		ECrossguardInventoryContainer::Main, ECrossguardInventoryContainer::Backpack, ECrossguardInventoryContainer::Quiver};
	// Where AddItem looks first.
	constexpr ECrossguardInventoryContainer AmmoOrder[] = {
		ECrossguardInventoryContainer::Quiver, ECrossguardInventoryContainer::Main, ECrossguardInventoryContainer::Backpack};
}

static_assert(static_cast<int>(ECrossguardItemCategory::Weapon) == static_cast<int>(Crossguard::EItemCategory::Weapon), "Item category enums out of sync");
static_assert(static_cast<int>(ECrossguardItemCategory::Armor) == static_cast<int>(Crossguard::EItemCategory::Armor), "Item category enums out of sync");
static_assert(static_cast<int>(ECrossguardItemCategory::Ammo) == static_cast<int>(Crossguard::EItemCategory::Ammo), "Item category enums out of sync");
static_assert(static_cast<int>(ECrossguardItemCategory::SkillGem) == static_cast<int>(Crossguard::EItemCategory::SkillGem), "Item category enums out of sync");
static_assert(static_cast<int>(ECrossguardItemCategory::Container) == static_cast<int>(Crossguard::EItemCategory::Container), "Item category enums out of sync");
static_assert(static_cast<int>(ECrossguardItemCategory::Material) == static_cast<int>(Crossguard::EItemCategory::Material), "Item category enums out of sync");
static_assert(static_cast<int>(ECrossguardItemCategory::Consumable) == static_cast<int>(Crossguard::EItemCategory::Consumable), "Item category enums out of sync");
static_assert(static_cast<int>(ECrossguardItemCategory::Throwable) == static_cast<int>(Crossguard::EItemCategory::Throwable), "Item category enums out of sync");
static_assert(static_cast<int>(ECrossguardItemCategory::Other) == static_cast<int>(Crossguard::EItemCategory::Other), "Item category enums out of sync");
static_assert(static_cast<int>(ECrossguardInventoryContainer::Main) == static_cast<int>(Crossguard::EContainerKind::Main), "Container enums out of sync");
static_assert(static_cast<int>(ECrossguardInventoryContainer::Backpack) == static_cast<int>(Crossguard::EContainerKind::Backpack), "Container enums out of sync");
static_assert(static_cast<int>(ECrossguardInventoryContainer::Quiver) == static_cast<int>(Crossguard::EContainerKind::Quiver), "Container enums out of sync");

UCrossguardInventoryComponent::UCrossguardInventoryComponent()
{
	SetIsReplicatedByDefault(true);
}

void UCrossguardInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(UCrossguardInventoryComponent, Items, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UCrossguardInventoryComponent, EquippedBackpack, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UCrossguardInventoryComponent, EquippedQuiver, COND_OwnerOnly);
}

bool UCrossguardInventoryComponent::IsServer() const
{
	return GetOwner() != nullptr && GetOwner()->HasAuthority();
}

UCrossguardEquipmentComponent* UCrossguardInventoryComponent::GetEquipment() const
{
	return GetOwner() != nullptr ? GetOwner()->FindComponentByClass<UCrossguardEquipmentComponent>() : nullptr;
}

int32 UCrossguardInventoryComponent::IndexOfItem(int32 ItemId) const
{
	return ItemId == 0 ? INDEX_NONE : Items.IndexOfByPredicate([ItemId](const FCrossguardInventoryItem& Item) { return Item.Id == ItemId; });
}

bool UCrossguardInventoryComponent::FindItem(int32 ItemId, FCrossguardInventoryItem& OutItem) const
{
	const int32 Index = IndexOfItem(ItemId);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	OutItem = Items[Index];
	return true;
}

const FCrossguardInventoryItem& UCrossguardInventoryComponent::GetEquippedContainer(ECrossguardInventoryContainer Container) const
{
	static const FCrossguardInventoryItem Empty;
	switch (Container)
	{
	case ECrossguardInventoryContainer::Backpack: return EquippedBackpack;
	case ECrossguardInventoryContainer::Quiver: return EquippedQuiver;
	case ECrossguardInventoryContainer::Main: break;
	}
	return Empty;
}

FIntPoint UCrossguardInventoryComponent::GetContainerSize(ECrossguardInventoryContainer Container) const
{
	const Crossguard::FInventoryConfig Config;
	if (Container != ECrossguardInventoryContainer::Main && GetEquippedContainer(Container).IsEmpty())
	{
		return FIntPoint::ZeroValue;
	}
	const ECrossguardRarity Rarity = GetEquippedContainer(Container).Rarity;
	const Crossguard::FGridSize Size = Crossguard::ContainerSize(Config, ToRulesContainer(Container), static_cast<Crossguard::ERarity>(static_cast<uint8>(Rarity)));
	return FIntPoint(Size.Width, Size.Height);
}

bool UCrossguardInventoryComponent::GetGridSize(const FCrossguardInventoryItem& Item, Crossguard::FGridSize& OutSize) const
{
	const UCrossguardEquipmentComponent* Equipment = GetEquipment();
	switch (Item.Category)
	{
	case ECrossguardItemCategory::Weapon:
	{
		const FCrossguardWeaponRow* Row = Equipment != nullptr && Equipment->WeaponTable != nullptr && !Item.Weapon.IsEmpty()
			? Equipment->WeaponTable->FindRow<FCrossguardWeaponRow>(Item.Weapon.WeaponId, TEXT("Inventory"), false)
			: nullptr;
		if (Row == nullptr)
		{
			return false;
		}
		OutSize = {Row->GridWidth, Row->GridHeight};
		return true;
	}
	case ECrossguardItemCategory::Armor:
	{
		const FCrossguardArmorRow* Row = Equipment != nullptr && Equipment->ArmorTable != nullptr && !Item.Armor.IsEmpty()
			? Equipment->ArmorTable->FindRow<FCrossguardArmorRow>(Item.Armor.ArmorId, TEXT("Inventory"), false)
			: nullptr;
		if (Row == nullptr)
		{
			return false;
		}
		OutSize = {Row->GridWidth, Row->GridHeight};
		return true;
	}
	case ECrossguardItemCategory::SkillGem:
		OutSize = {1, 1};
		return !Item.ItemId.IsNone();
	case ECrossguardItemCategory::Container:
	{
		if (Item.ContainerKind == ECrossguardInventoryContainer::Main)
		{
			return false;
		}
		const FIntPoint Size = Item.ContainerKind == ECrossguardInventoryContainer::Backpack ? BackpackItemSize : QuiverItemSize;
		OutSize = {Size.X, Size.Y};
		return Size.X > 0 && Size.Y > 0;
	}
	case ECrossguardItemCategory::Ammo:
	case ECrossguardItemCategory::Consumable:
	case ECrossguardItemCategory::Throwable:
	case ECrossguardItemCategory::Material:
	case ECrossguardItemCategory::Other:
		break;
	}

	const FCrossguardItemRow* Row = ItemTable != nullptr && !Item.ItemId.IsNone()
		? ItemTable->FindRow<FCrossguardItemRow>(Item.ItemId, TEXT("Inventory"), false)
		: nullptr;
	if (Row == nullptr || Row->Category != Item.Category)
	{
		return false;
	}
	OutSize = {Row->GridWidth, Row->GridHeight};
	return true;
}

FIntPoint UCrossguardInventoryComponent::GetItemSize(const FCrossguardInventoryItem& Item) const
{
	Crossguard::FGridSize Size;
	return GetGridSize(Item, Size) ? FIntPoint(Size.Width, Size.Height) : FIntPoint::ZeroValue;
}

int32 UCrossguardInventoryComponent::GetMaxStack(const FCrossguardInventoryItem& Item) const
{
	const FCrossguardItemRow* Row = IsTableItem(Item.Category) && ItemTable != nullptr && !Item.ItemId.IsNone()
		? ItemTable->FindRow<FCrossguardItemRow>(Item.ItemId, TEXT("Inventory"), false)
		: nullptr;
	return Row != nullptr ? FMath::Max(1, Row->MaxStack) : 1;
}

bool UCrossguardInventoryComponent::CanStackWith(const FCrossguardInventoryItem& A, const FCrossguardInventoryItem& B) const
{
	return IsTableItem(A.Category) && A.Category == B.Category && A.ItemId == B.ItemId && GetMaxStack(A) > 1;
}

bool UCrossguardInventoryComponent::Accepts(ECrossguardInventoryContainer Container, ECrossguardItemCategory Category) const
{
	return GetContainerSize(Container).X > 0 && Crossguard::ContainerAccepts(ToRulesContainer(Container), ToRulesCategory(Category));
}

Crossguard::FInventoryGrid UCrossguardInventoryComponent::BuildGrid(ECrossguardInventoryContainer Container, int32 IgnoreId) const
{
	const FIntPoint Size = GetContainerSize(Container);
	Crossguard::FInventoryGrid Grid(Crossguard::FGridSize{Size.X, Size.Y});
	for (const FCrossguardInventoryItem& Item : Items)
	{
		Crossguard::FGridSize ItemSize;
		if (Item.Container == Container && Item.Id != IgnoreId && GetGridSize(Item, ItemSize))
		{
			Grid.Place(Item.Id, ItemSize, {Item.Position.X, Item.Position.Y, Item.bRotated});
		}
	}
	return Grid;
}

bool UCrossguardInventoryComponent::FindPlacement(FCrossguardInventoryItem& Item) const
{
	Crossguard::FGridSize Size;
	if (!GetGridSize(Item, Size))
	{
		return false;
	}
	const ECrossguardInventoryContainer* Order = Item.Category == ECrossguardItemCategory::Ammo ? AmmoOrder : AllContainers;
	for (int32 OrderIndex = 0; OrderIndex < UE_ARRAY_COUNT(AllContainers); ++OrderIndex)
	{
		const ECrossguardInventoryContainer Container = Order[OrderIndex];
		Crossguard::FGridPlacement Spot;
		if (Accepts(Container, Item.Category) && BuildGrid(Container).FindFreeSpot(Size, true, Spot))
		{
			Item.Container = Container;
			Item.Position = FIntPoint(Spot.X, Spot.Y);
			Item.bRotated = Spot.bRotated;
			return true;
		}
	}
	return false;
}

bool UCrossguardInventoryComponent::PlaceNewItem(FCrossguardInventoryItem Item)
{
	if (!FindPlacement(Item))
	{
		return false;
	}
	Item.Id = NextItemId++;
	Items.Add(Item);
	return true;
}

int32 UCrossguardInventoryComponent::AddItem(const FCrossguardInventoryItem& Item)
{
	Crossguard::FGridSize Size;
	int32 Remaining = FMath::Max(1, Item.Count);
	if (!IsServer() || !GetGridSize(Item, Size))
	{
		return Remaining;
	}

	// Top up existing stacks, in the order new stacks would be placed.
	const int32 MaxStack = GetMaxStack(Item);
	if (MaxStack > 1)
	{
		for (FCrossguardInventoryItem& Stack : Items)
		{
			if (Remaining > 0 && CanStackWith(Stack, Item))
			{
				Crossguard::MergeStacks(Stack.Count, Remaining, MaxStack);
			}
		}
	}

	while (Remaining > 0)
	{
		FCrossguardInventoryItem Stack = Item;
		Stack.Count = FMath::Min(Remaining, MaxStack);
		if (!PlaceNewItem(Stack))
		{
			break;
		}
		Remaining -= Stack.Count;
	}

	OnInventoryChanged.Broadcast();
	return Remaining;
}

bool UCrossguardInventoryComponent::RemoveItem(int32 ItemId)
{
	const int32 Index = IndexOfItem(ItemId);
	if (!IsServer() || Index == INDEX_NONE)
	{
		return false;
	}
	Items.RemoveAt(Index);
	OnInventoryChanged.Broadcast();
	return true;
}

// --- Requests -------------------------------------------------------------------------------------

void UCrossguardInventoryComponent::RequestMoveItem(int32 ItemId, ECrossguardInventoryContainer Container, FIntPoint Position, bool bRotated)
{
	if (IsServer())
	{
		Finish(MoveItem(ItemId, Container, Position, bRotated));
	}
	else
	{
		ServerMoveItem(ItemId, Container, Position, bRotated);
	}
}

void UCrossguardInventoryComponent::ServerMoveItem_Implementation(int32 ItemId, ECrossguardInventoryContainer Container, FIntPoint Position, bool bRotated)
{
	Finish(MoveItem(ItemId, Container, Position, bRotated));
}

void UCrossguardInventoryComponent::RequestEquipItem(int32 ItemId, ECrossguardEquipmentSlot Slot)
{
	if (IsServer())
	{
		Finish(EquipItem(ItemId, Slot));
	}
	else
	{
		ServerEquipItem(ItemId, Slot);
	}
}

void UCrossguardInventoryComponent::ServerEquipItem_Implementation(int32 ItemId, ECrossguardEquipmentSlot Slot)
{
	Finish(EquipItem(ItemId, Slot));
}

void UCrossguardInventoryComponent::RequestUnequipWeapon(ECrossguardEquipmentSlot Slot)
{
	if (IsServer())
	{
		Finish(UnequipWeapon(Slot));
	}
	else
	{
		ServerUnequipWeapon(Slot);
	}
}

void UCrossguardInventoryComponent::ServerUnequipWeapon_Implementation(ECrossguardEquipmentSlot Slot)
{
	Finish(UnequipWeapon(Slot));
}

void UCrossguardInventoryComponent::RequestUnequipArmor(ECrossguardArmorSlot Slot)
{
	if (IsServer())
	{
		Finish(UnequipArmor(Slot));
	}
	else
	{
		ServerUnequipArmor(Slot);
	}
}

void UCrossguardInventoryComponent::ServerUnequipArmor_Implementation(ECrossguardArmorSlot Slot)
{
	Finish(UnequipArmor(Slot));
}

void UCrossguardInventoryComponent::RequestUnequipContainer(ECrossguardInventoryContainer Container)
{
	if (IsServer())
	{
		Finish(UnequipContainer(Container));
	}
	else
	{
		ServerUnequipContainer(Container);
	}
}

void UCrossguardInventoryComponent::ServerUnequipContainer_Implementation(ECrossguardInventoryContainer Container)
{
	Finish(UnequipContainer(Container));
}

void UCrossguardInventoryComponent::RequestUseItem(int32 ItemId)
{
	if (IsServer())
	{
		Finish(UseItem(ItemId));
	}
	else
	{
		ServerUseItem(ItemId);
	}
}

void UCrossguardInventoryComponent::ServerUseItem_Implementation(int32 ItemId)
{
	Finish(UseItem(ItemId));
}

void UCrossguardInventoryComponent::ClientInventoryRejected_Implementation(const FCrossguardInventoryResult& Result)
{
	OnInventoryRejected.Broadcast(Result);
}

void UCrossguardInventoryComponent::OnRep_Inventory()
{
	OnInventoryChanged.Broadcast();
}

void UCrossguardInventoryComponent::Finish(const FCrossguardInventoryResult& Result)
{
	if (Result.IsValid())
	{
		OnInventoryChanged.Broadcast();
		return;
	}
	UE_LOG(LogCrossguard, Log, TEXT("Inventory request rejected for %s: error %d (equip %d, progression %d)"), *GetNameSafe(GetOwner()),
		static_cast<int32>(Result.Error), static_cast<int32>(Result.EquipError), static_cast<int32>(Result.ProgressionError));
	// Reaches the owning client; on a listen server host it runs locally.
	ClientInventoryRejected(Result);
}

// --- Operations (server) --------------------------------------------------------------------------

FCrossguardInventoryResult UCrossguardInventoryComponent::MoveItem(int32 ItemId, ECrossguardInventoryContainer Container, FIntPoint Position, bool bRotated)
{
	const int32 Index = IndexOfItem(ItemId);
	Crossguard::FGridSize Size;
	if (Index == INDEX_NONE || !GetGridSize(Items[Index], Size))
	{
		return Fail(ECrossguardInventoryError::UnknownItem);
	}
	if (GetContainerSize(Container).X == 0)
	{
		return Fail(ECrossguardInventoryError::NoContainer);
	}
	if (!Accepts(Container, Items[Index].Category))
	{
		return Fail(ECrossguardInventoryError::WrongContainer);
	}

	const Crossguard::FInventoryGrid Grid = BuildGrid(Container, ItemId);

	// Dropped onto the top-left square of a stack of the same item: merge.
	const int32 TargetIndex = IndexOfItem(Grid.ItemAt(Position.X, Position.Y));
	if (TargetIndex != INDEX_NONE && Items[TargetIndex].Position == Position && CanStackWith(Items[TargetIndex], Items[Index]))
	{
		const int32 Moved = Crossguard::MergeStacks(Items[TargetIndex].Count, Items[Index].Count, GetMaxStack(Items[Index]));
		if (Moved == 0)
		{
			return Fail(ECrossguardInventoryError::InvalidPosition);
		}
		if (Items[Index].Count == 0)
		{
			Items.RemoveAt(Index);
		}
		return {};
	}

	if (!Grid.CanPlace(Size, {Position.X, Position.Y, bRotated}))
	{
		return Fail(ECrossguardInventoryError::InvalidPosition);
	}
	FCrossguardInventoryItem& Item = Items[Index];
	Item.Container = Container;
	Item.Position = Position;
	Item.bRotated = bRotated;
	return {};
}

FCrossguardInventoryResult UCrossguardInventoryComponent::EquipItem(int32 ItemId, ECrossguardEquipmentSlot Slot)
{
	UCrossguardEquipmentComponent* Equipment = GetEquipment();
	const int32 Index = IndexOfItem(ItemId);
	if (Equipment == nullptr)
	{
		return Fail(ECrossguardInventoryError::NotReady);
	}
	if (Index == INDEX_NONE)
	{
		return Fail(ECrossguardInventoryError::UnknownItem);
	}

	const FCrossguardInventoryItem Item = Items[Index];
	if (Item.Category == ECrossguardItemCategory::Container)
	{
		return EquipContainer(Item);
	}

	// What comes out of the slot goes back into the inventory once the new item has left it.
	FCrossguardInventoryItem Previous;
	if (Item.Category == ECrossguardItemCategory::Weapon)
	{
		const ECrossguardEquipError Error = Equipment->CheckEquip(Slot, Item.Weapon);
		if (Error != ECrossguardEquipError::None)
		{
			return FailEquip(Error);
		}
		Previous.Category = ECrossguardItemCategory::Weapon;
		Previous.Weapon = Equipment->GetItem(Slot);
	}
	else if (Item.Category == ECrossguardItemCategory::Armor)
	{
		ECrossguardArmorSlot ArmorSlot = ECrossguardArmorSlot::Head;
		if (!Equipment->GetArmorSlotFor(Item.Armor.ArmorId, ArmorSlot))
		{
			return FailEquip(ECrossguardEquipError::UnknownArmor);
		}
		const ECrossguardEquipError Error = Equipment->CheckEquipArmor(ArmorSlot, Item.Armor);
		if (Error != ECrossguardEquipError::None)
		{
			return FailEquip(Error);
		}
		Previous.Category = ECrossguardItemCategory::Armor;
		Previous.Armor = Equipment->GetArmorItem(ArmorSlot);
	}
	else
	{
		return Fail(ECrossguardInventoryError::NotUsable);
	}

	const bool bHadPrevious = Item.Category == ECrossguardItemCategory::Weapon ? !Previous.Weapon.IsEmpty() : !Previous.Armor.IsEmpty();
	const TArray<FCrossguardInventoryItem> Before = Items;
	const int32 NextIdBefore = NextItemId;
	Items.RemoveAt(Index);
	if (bHadPrevious && !PlaceNewItem(Previous))
	{
		Items = Before;
		NextItemId = NextIdBefore;
		return Fail(ECrossguardInventoryError::NoRoom);
	}

	ECrossguardEquipError Error = ECrossguardEquipError::NotReady;
	if (Item.Category == ECrossguardItemCategory::Weapon)
	{
		Error = Equipment->Equip(Slot, Item.Weapon);
	}
	else
	{
		ECrossguardArmorSlot ArmorSlot = ECrossguardArmorSlot::Head;
		Equipment->GetArmorSlotFor(Item.Armor.ArmorId, ArmorSlot);
		Error = Equipment->EquipArmor(ArmorSlot, Item.Armor);
	}
	if (Error != ECrossguardEquipError::None)
	{
		Items = Before;
		NextItemId = NextIdBefore;
		return FailEquip(Error);
	}
	return {};
}

FCrossguardInventoryResult UCrossguardInventoryComponent::EquipContainer(const FCrossguardInventoryItem& Item)
{
	if (Item.ContainerKind == ECrossguardInventoryContainer::Main)
	{
		return Fail(ECrossguardInventoryError::UnknownItem);
	}
	FCrossguardInventoryItem& Slot = Item.ContainerKind == ECrossguardInventoryContainer::Backpack ? EquippedBackpack : EquippedQuiver;
	if (!Slot.IsEmpty() && !BuildGrid(Item.ContainerKind).IsEmpty())
	{
		return Fail(ECrossguardInventoryError::ContainerNotEmpty);
	}
	// The item cannot be inside the container it replaces: that container is empty.

	const TArray<FCrossguardInventoryItem> Before = Items;
	const FCrossguardInventoryItem SlotBefore = Slot;
	const int32 NextIdBefore = NextItemId;
	Items.RemoveAt(IndexOfItem(Item.Id));
	Slot = Item;
	if (!SlotBefore.IsEmpty() && !PlaceNewItem(SlotBefore))
	{
		Items = Before;
		Slot = SlotBefore;
		NextItemId = NextIdBefore;
		return Fail(ECrossguardInventoryError::NoRoom);
	}
	return {};
}

FCrossguardInventoryResult UCrossguardInventoryComponent::UnequipWeapon(ECrossguardEquipmentSlot Slot)
{
	UCrossguardEquipmentComponent* Equipment = GetEquipment();
	if (Equipment == nullptr)
	{
		return Fail(ECrossguardInventoryError::NotReady);
	}
	FCrossguardInventoryItem Item;
	Item.Category = ECrossguardItemCategory::Weapon;
	Item.Weapon = Equipment->GetItem(Slot);
	if (Item.Weapon.IsEmpty())
	{
		return Fail(ECrossguardInventoryError::UnknownItem);
	}
	const ECrossguardEquipError Error = Equipment->CheckEquip(Slot, FCrossguardWeaponItem());
	if (Error != ECrossguardEquipError::None)
	{
		return FailEquip(Error);
	}
	if (!PlaceNewItem(Item))
	{
		return Fail(ECrossguardInventoryError::NoRoom);
	}
	Equipment->Equip(Slot, FCrossguardWeaponItem());
	return {};
}

FCrossguardInventoryResult UCrossguardInventoryComponent::UnequipArmor(ECrossguardArmorSlot Slot)
{
	UCrossguardEquipmentComponent* Equipment = GetEquipment();
	if (Equipment == nullptr)
	{
		return Fail(ECrossguardInventoryError::NotReady);
	}
	FCrossguardInventoryItem Item;
	Item.Category = ECrossguardItemCategory::Armor;
	Item.Armor = Equipment->GetArmorItem(Slot);
	if (Item.Armor.IsEmpty())
	{
		return Fail(ECrossguardInventoryError::UnknownItem);
	}
	const ECrossguardEquipError Error = Equipment->CheckEquipArmor(Slot, FCrossguardArmorItem());
	if (Error != ECrossguardEquipError::None)
	{
		return FailEquip(Error);
	}
	if (!PlaceNewItem(Item))
	{
		return Fail(ECrossguardInventoryError::NoRoom);
	}
	Equipment->EquipArmor(Slot, FCrossguardArmorItem());
	return {};
}

FCrossguardInventoryResult UCrossguardInventoryComponent::UnequipContainer(ECrossguardInventoryContainer Container)
{
	if (Container == ECrossguardInventoryContainer::Main)
	{
		return Fail(ECrossguardInventoryError::NoContainer);
	}
	FCrossguardInventoryItem& Slot = Container == ECrossguardInventoryContainer::Backpack ? EquippedBackpack : EquippedQuiver;
	if (Slot.IsEmpty())
	{
		return Fail(ECrossguardInventoryError::NoContainer);
	}
	if (!BuildGrid(Container).IsEmpty())
	{
		return Fail(ECrossguardInventoryError::ContainerNotEmpty);
	}

	// Empty the slot first so the container cannot be placed inside itself.
	const FCrossguardInventoryItem Item = Slot;
	Slot = FCrossguardInventoryItem();
	if (!PlaceNewItem(Item))
	{
		Slot = Item;
		return Fail(ECrossguardInventoryError::NoRoom);
	}
	return {};
}

FCrossguardInventoryResult UCrossguardInventoryComponent::UseItem(int32 ItemId)
{
	const int32 Index = IndexOfItem(ItemId);
	if (Index == INDEX_NONE)
	{
		return Fail(ECrossguardInventoryError::UnknownItem);
	}
	if (Items[Index].Category != ECrossguardItemCategory::SkillGem)
	{
		return Fail(ECrossguardInventoryError::NotUsable);
	}

	const APawn* Pawn = Cast<APawn>(GetOwner());
	ACrossguardPlayerState* Progression = Pawn != nullptr ? Pawn->GetPlayerState<ACrossguardPlayerState>() : nullptr;
	const UCrossguardLoadoutComponent* Loadout = GetOwner() != nullptr ? GetOwner()->FindComponentByClass<UCrossguardLoadoutComponent>() : nullptr;
	if (Progression == nullptr || Loadout == nullptr)
	{
		return Fail(ECrossguardInventoryError::NotReady);
	}

	const ECrossguardProgressionError Error = Progression->LearnSkill(Items[Index].ItemId, Loadout->SkillCatalog);
	if (Error != ECrossguardProgressionError::None)
	{
		FCrossguardInventoryResult Result;
		Result.Error = ECrossguardInventoryError::CannotLearn;
		Result.ProgressionError = Error;
		return Result;
	}

	// Learned: the gem is consumed.
	if (--Items[Index].Count <= 0)
	{
		Items.RemoveAt(Index);
	}
	return {};
}
