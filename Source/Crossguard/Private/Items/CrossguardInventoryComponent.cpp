#include "Items/CrossguardInventoryComponent.h"

#include "AbilitySystem/CrossguardEffects.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Character/CrossguardCharacter.h"
#include "Combat/CrossguardCosmetics.h"
#include "Combat/CrossguardThrownProjectile.h"
#include "Combat/CrossguardWeaponAttackAbility.h"
#include "Crossguard.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Items/CrossguardLootContainer.h"
#include "Items/CrossguardWorldItem.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Skills/CrossguardLoadoutComponent.h"
#include "Skills/CrossguardSkillCatalog.h"

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
			|| Category == ECrossguardItemCategory::Throwable || Category == ECrossguardItemCategory::Material
			|| Category == ECrossguardItemCategory::Currency || Category == ECrossguardItemCategory::Trinket
			|| Category == ECrossguardItemCategory::Tool;
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
static_assert(static_cast<int>(ECrossguardItemCategory::Currency) == static_cast<int>(Crossguard::EItemCategory::Currency), "Item category enums out of sync");
static_assert(static_cast<int>(ECrossguardItemCategory::Trinket) == static_cast<int>(Crossguard::EItemCategory::Trinket), "Item category enums out of sync");
static_assert(static_cast<int>(ECrossguardItemCategory::Tool) == static_cast<int>(Crossguard::EItemCategory::Tool), "Item category enums out of sync");
static_assert(static_cast<int>(ECrossguardItemCategory::Other) == static_cast<int>(Crossguard::EItemCategory::Other), "Item category enums out of sync");
static_assert(static_cast<int>(ECrossguardInventoryContainer::Main) == static_cast<int>(Crossguard::EContainerKind::Main), "Container enums out of sync");
static_assert(static_cast<int>(ECrossguardInventoryContainer::Backpack) == static_cast<int>(Crossguard::EContainerKind::Backpack), "Container enums out of sync");
static_assert(static_cast<int>(ECrossguardInventoryContainer::Quiver) == static_cast<int>(Crossguard::EContainerKind::Quiver), "Container enums out of sync");

UCrossguardInventoryComponent::UCrossguardInventoryComponent()
{
	SetIsReplicatedByDefault(true);
	Hotbar.SetNum(Crossguard::HotbarSlotCount);
	ThrownProjectileClass = ACrossguardThrownProjectile::StaticClass();
}

void UCrossguardInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(UCrossguardInventoryComponent, Items, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UCrossguardInventoryComponent, EquippedBackpack, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UCrossguardInventoryComponent, EquippedQuiver, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UCrossguardInventoryComponent, OpenContainer, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UCrossguardInventoryComponent, OpenContainerItems, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UCrossguardInventoryComponent, ReadiedItemId, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UCrossguardInventoryComponent, Hotbar, COND_OwnerOnly);
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
	case ECrossguardItemCategory::Currency:
	case ECrossguardItemCategory::Trinket:
	case ECrossguardItemCategory::Tool:
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

int32 UCrossguardInventoryComponent::LookUpMaxStack(const FCrossguardInventoryItem& Item) const
{
	const FCrossguardItemRow* Row = IsTableItem(Item.Category) && ItemTable != nullptr && !Item.ItemId.IsNone()
		? ItemTable->FindRow<FCrossguardItemRow>(Item.ItemId, TEXT("Inventory"), false)
		: nullptr;
	return Row != nullptr ? FMath::Max(1, Row->MaxStack) : 1;
}

bool UCrossguardInventoryComponent::ResolveItemData(FCrossguardInventoryItem& Item) const
{
	Crossguard::FGridSize GridSize;
	if (!GetGridSize(Item, GridSize))
	{
		return false;
	}
	Item.Size = FIntPoint(GridSize.Width, GridSize.Height);
	Item.MaxStack = LookUpMaxStack(Item);
	Item.Count = FMath::Clamp(Item.Count, 1, Item.MaxStack);
	return true;
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
		if (Item.Container == Container && Item.Id != IgnoreId)
		{
			Grid.Place(Item.Id, {Item.Size.X, Item.Size.Y}, {Item.Position.X, Item.Position.Y, Item.bRotated});
		}
	}
	return Grid;
}

bool UCrossguardInventoryComponent::FindPlacement(FCrossguardInventoryItem& Item) const
{
	const Crossguard::FGridSize Size{Item.Size.X, Item.Size.Y};
	const ECrossguardInventoryContainer* Order = Item.Category == ECrossguardItemCategory::Ammo ? AmmoOrder : AllContainers;
	for (int32 OrderIndex = 0; OrderIndex < static_cast<int32>(UE_ARRAY_COUNT(AllContainers)); ++OrderIndex)
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
	if (!ResolveItemData(Item) || !FindPlacement(Item))
	{
		return false;
	}
	Item.Id = NextItemId++;
	Items.Add(Item);
	return true;
}

int32 UCrossguardInventoryComponent::AddItem(const FCrossguardInventoryItem& NewItem)
{
	const int32 Remaining = AddItemSilently(NewItem);
	if (IsServer())
	{
		OnInventoryChanged.Broadcast();
	}
	return Remaining;
}

int32 UCrossguardInventoryComponent::AddItemSilently(const FCrossguardInventoryItem& NewItem)
{
	int32 Remaining = FMath::Max(1, NewItem.Count);
	FCrossguardInventoryItem Item = NewItem;
	Item.Count = 1;
	if (!IsServer() || !ResolveItemData(Item))
	{
		return Remaining;
	}
	// A Skill Gem's rarity is its skill's (Skills.json), whatever the drop said.
	if (Item.Category == ECrossguardItemCategory::SkillGem)
	{
		const UCrossguardLoadoutComponent* Loadout = GetOwner() != nullptr ? GetOwner()->FindComponentByClass<UCrossguardLoadoutComponent>() : nullptr;
		const FCrossguardSkillRow* SkillRow = Loadout != nullptr && Loadout->SkillCatalog != nullptr ? Loadout->SkillCatalog->FindSkill(Item.ItemId) : nullptr;
		if (SkillRow != nullptr && SkillRow->bHasRarity)
		{
			Item.Rarity = SkillRow->Rarity;
		}
	}

	// Top up existing stacks of the same item and rarity first.
	for (FCrossguardInventoryItem& Stack : Items)
	{
		if (Remaining > 0 && Stack.CanStackWith(Item))
		{
			Crossguard::MergeStacks(Stack.Count, Remaining, Stack.MaxStack);
		}
	}

	while (Remaining > 0)
	{
		FCrossguardInventoryItem Stack = Item;
		Stack.Count = FMath::Min(Remaining, Item.MaxStack);
		if (!PlaceNewItem(Stack))
		{
			break;
		}
		Remaining -= Stack.Count;
	}
	return Remaining;
}

bool UCrossguardInventoryComponent::ExchangeItems(const TArray<int32>& GiveIds, const TArray<FCrossguardInventoryItem>& Receive, bool bApply,
	TArray<FCrossguardInventoryItem>* OutGiven)
{
	if (!IsServer())
	{
		return false;
	}
	// Tried on the real state, then put back unless it all worked and bApply. Nothing replicates in
	// between (same frame).
	const TArray<FCrossguardInventoryItem> SavedItems = Items;
	const int32 SavedNextItemId = NextItemId;
	TArray<FCrossguardInventoryItem> Given;
	bool bOk = true;
	for (const int32 GiveId : GiveIds)
	{
		const int32 Index = IndexOfItem(GiveId);
		// The readied item is in hand: not tradable until put away.
		if (Index == INDEX_NONE || GiveId == ReadiedItemId)
		{
			bOk = false;
			break;
		}
		Given.Add(Items[Index]);
		Items.RemoveAt(Index);
	}
	for (int32 Index = 0; bOk && Index < Receive.Num(); ++Index)
	{
		bOk = AddItemSilently(Receive[Index]) == 0;
	}
	if (!bOk || !bApply)
	{
		Items = SavedItems;
		NextItemId = SavedNextItemId;
	}
	else
	{
		ValidateHeldItem();
		OnInventoryChanged.Broadcast();
	}
	if (OutGiven != nullptr)
	{
		*OutGiven = Given;
	}
	return bOk;
}

int32 UCrossguardInventoryComponent::CountItem(FName ItemId) const
{
	int32 Total = 0;
	for (const FCrossguardInventoryItem& Item : Items)
	{
		if (IsTableItem(Item.Category) && Item.ItemId == ItemId)
		{
			Total += Item.Count;
		}
	}
	return Total;
}

bool UCrossguardInventoryComponent::ConsumeItem(FName ItemId, int32 Count)
{
	if (!IsServer() || ItemId.IsNone() || Count <= 0 || CountItem(ItemId) < Count)
	{
		return false;
	}
	int32 Remaining = Count;
	for (const ECrossguardInventoryContainer Container : AmmoOrder)
	{
		for (int32 Index = Items.Num() - 1; Index >= 0 && Remaining > 0; --Index)
		{
			FCrossguardInventoryItem& Item = Items[Index];
			if (Item.Container != Container || !IsTableItem(Item.Category) || Item.ItemId != ItemId)
			{
				continue;
			}
			const int32 Taken = FMath::Min(Remaining, Item.Count);
			Item.Count -= Taken;
			Remaining -= Taken;
			if (Item.Count <= 0)
			{
				Items.RemoveAt(Index);
			}
		}
	}
	OnInventoryChanged.Broadcast();
	return true;
}

int32 UCrossguardInventoryComponent::TakeAllOfCategory(ECrossguardItemCategory Category)
{
	int32 Taken = 0;
	if (!IsServer())
	{
		return Taken;
	}
	for (int32 Index = Items.Num() - 1; Index >= 0; --Index)
	{
		if (Items[Index].Category == Category)
		{
			Taken += Items[Index].Count;
			Items.RemoveAt(Index);
		}
	}
	ValidateHeldItem();
	OnInventoryChanged.Broadcast();
	return Taken;
}

bool UCrossguardInventoryComponent::RemoveItem(int32 ItemId)
{
	const int32 Index = IndexOfItem(ItemId);
	if (!IsServer() || Index == INDEX_NONE)
	{
		return false;
	}
	Items.RemoveAt(Index);
	ValidateHeldItem();
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

void UCrossguardInventoryComponent::RequestSplitItem(int32 ItemId, int32 Count)
{
	if (IsServer())
	{
		Finish(SplitItem(ItemId, Count));
	}
	else
	{
		ServerSplitItem(ItemId, Count);
	}
}

void UCrossguardInventoryComponent::ServerSplitItem_Implementation(int32 ItemId, int32 Count)
{
	Finish(SplitItem(ItemId, Count));
}

FCrossguardInventoryResult UCrossguardInventoryComponent::SplitItem(int32 ItemId, int32 Count)
{
	if (IsOwnerDead())
	{
		return Fail(ECrossguardInventoryError::OwnerDead);
	}
	const int32 Index = IndexOfItem(ItemId);
	if (Index == INDEX_NONE)
	{
		return Fail(ECrossguardInventoryError::UnknownItem);
	}
	if (Count < 1 || Count >= Items[Index].Count)
	{
		return Fail(ECrossguardInventoryError::CannotSplit);
	}
	// The new stack: the same item (affixes, rarity) with Count of them, in the first free spot;
	// its own container is tried first so it lands beside the stack it came from.
	FCrossguardInventoryItem Piece = Items[Index];
	Piece.Count = Count;
	Crossguard::FGridSize Size;
	Crossguard::FGridPlacement Spot;
	if (GetGridSize(Piece, Size) && BuildGrid(Piece.Container).FindFreeSpot(Size, true, Spot))
	{
		Piece.Position = FIntPoint(Spot.X, Spot.Y);
		Piece.bRotated = Spot.bRotated;
	}
	else if (!FindPlacement(Piece))
	{
		return Fail(ECrossguardInventoryError::NoRoom);
	}
	Piece.Id = NextItemId++;
	Items[Index].Count -= Count;
	Items.Add(Piece);
	return {};
}

void UCrossguardInventoryComponent::RequestEquipItem(int32 ItemId, ECrossguardEquipmentSlot Slot, int32 WeaponSet)
{
	if (IsServer())
	{
		Finish(EquipItem(ItemId, Slot, WeaponSet));
	}
	else
	{
		ServerEquipItem(ItemId, Slot, WeaponSet);
	}
}

void UCrossguardInventoryComponent::ServerEquipItem_Implementation(int32 ItemId, ECrossguardEquipmentSlot Slot, int32 WeaponSet)
{
	Finish(EquipItem(ItemId, Slot, WeaponSet));
}

void UCrossguardInventoryComponent::RequestUnequipWeapon(ECrossguardEquipmentSlot Slot, int32 WeaponSet)
{
	if (IsServer())
	{
		Finish(UnequipWeapon(Slot, WeaponSet));
	}
	else
	{
		ServerUnequipWeapon(Slot, WeaponSet);
	}
}

void UCrossguardInventoryComponent::ServerUnequipWeapon_Implementation(ECrossguardEquipmentSlot Slot, int32 WeaponSet)
{
	Finish(UnequipWeapon(Slot, WeaponSet));
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

void UCrossguardInventoryComponent::RequestDropItem(int32 ItemId)
{
	if (IsServer())
	{
		Finish(DropItem(ItemId));
	}
	else
	{
		ServerDropItem(ItemId);
	}
}

void UCrossguardInventoryComponent::ServerDropItem_Implementation(int32 ItemId)
{
	Finish(DropItem(ItemId));
}

FCrossguardInventoryResult UCrossguardInventoryComponent::DropItem(int32 ItemId)
{
	if (IsOwnerDead())
	{
		return Fail(ECrossguardInventoryError::OwnerDead);
	}
	const int32 Index = IndexOfItem(ItemId);
	const AActor* OwnerActor = GetOwner();
	if (Index == INDEX_NONE || OwnerActor == nullptr)
	{
		return Fail(ECrossguardInventoryError::UnknownItem);
	}
	// The stack in hand: put it away first, which also stops a bandage channel.
	if (Items[Index].Id == ReadiedItemId)
	{
		ReleaseHeldItem();
	}

	// On the ground about 1.5 m ahead (far enough to be in view when picking it up), never through a
	// wall: world geometry only, the ground found from 2 m above (stairs), else at hand height.
	UWorld* World = GetWorld();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CrossguardDropItem), false, OwnerActor);
	const FVector From = OwnerActor->GetActorLocation();
	FVector Ahead = From + OwnerActor->GetActorForwardVector() * 150.0f;
	FHitResult Wall;
	if (World->LineTraceSingleByObjectType(Wall, From, Ahead, FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		Ahead = Wall.ImpactPoint - OwnerActor->GetActorForwardVector() * 30.0f;
	}
	FHitResult Ground;
	const FVector Spot = World->LineTraceSingleByObjectType(Ground, Ahead + FVector(0.0f, 0.0f, 200.0f), Ahead - FVector(0.0f, 0.0f, 300.0f),
		FCollisionObjectQueryParams(ECC_WorldStatic), Params) ? Ground.ImpactPoint + FVector(0.0f, 0.0f, 12.5f) : Ahead;

	// The next inventory assigns its own id and place.
	FCrossguardInventoryItem Dropped = Items[Index];
	Dropped.bWasEquipped = false;
	const FTransform Transform(OwnerActor->GetActorRotation(), Spot);
	const TSubclassOf<ACrossguardWorldItem> Class = WorldItemClass != nullptr ? WorldItemClass
		: TSubclassOf<ACrossguardWorldItem>(ACrossguardWorldItem::StaticClass());
	ACrossguardWorldItem* WorldItem = World->SpawnActorDeferred<ACrossguardWorldItem>(Class, Transform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (WorldItem == nullptr)
	{
		return Fail(ECrossguardInventoryError::NotReady);
	}
	WorldItem->SetItem(Dropped);
	WorldItem->FinishSpawning(Transform);

	Items.RemoveAt(Index);
	return FCrossguardInventoryResult();
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
	ValidateHeldItem();
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
	if (IsOwnerDead())
	{
		return Fail(ECrossguardInventoryError::OwnerDead);
	}
	const int32 Index = IndexOfItem(ItemId);
	if (Index == INDEX_NONE)
	{
		return Fail(ECrossguardInventoryError::UnknownItem);
	}
	const Crossguard::FGridSize Size{Items[Index].Size.X, Items[Index].Size.Y};
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
	if (TargetIndex != INDEX_NONE && Items[TargetIndex].Position == Position && Items[TargetIndex].CanStackWith(Items[Index]))
	{
		const int32 Moved = Crossguard::MergeStacks(Items[TargetIndex].Count, Items[Index].Count, Items[TargetIndex].MaxStack);
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

FCrossguardInventoryResult UCrossguardInventoryComponent::EquipItem(int32 ItemId, ECrossguardEquipmentSlot Slot, int32 WeaponSet)
{
	if (IsOwnerDead())
	{
		return Fail(ECrossguardInventoryError::OwnerDead);
	}
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
		const ECrossguardEquipError Error = Equipment->CheckEquip(Slot, Item.Weapon, WeaponSet);
		if (Error != ECrossguardEquipError::None)
		{
			return FailEquip(Error);
		}
		Previous.Category = ECrossguardItemCategory::Weapon;
		Previous.Weapon = Equipment->GetWeaponInSet(WeaponSet, Slot);
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
		Error = Equipment->Equip(Slot, Item.Weapon, WeaponSet);
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

FCrossguardInventoryResult UCrossguardInventoryComponent::UnequipWeapon(ECrossguardEquipmentSlot Slot, int32 WeaponSet)
{
	if (IsOwnerDead())
	{
		return Fail(ECrossguardInventoryError::OwnerDead);
	}
	UCrossguardEquipmentComponent* Equipment = GetEquipment();
	if (Equipment == nullptr)
	{
		return Fail(ECrossguardInventoryError::NotReady);
	}
	FCrossguardInventoryItem Item;
	Item.Category = ECrossguardItemCategory::Weapon;
	Item.Weapon = Equipment->GetWeaponInSet(WeaponSet, Slot);
	if (Item.Weapon.IsEmpty())
	{
		return Fail(ECrossguardInventoryError::UnknownItem);
	}
	const ECrossguardEquipError Error = Equipment->CheckEquip(Slot, FCrossguardWeaponItem(), WeaponSet);
	if (Error != ECrossguardEquipError::None)
	{
		return FailEquip(Error);
	}
	const TArray<FCrossguardInventoryItem> Before = Items;
	const int32 NextIdBefore = NextItemId;
	if (!PlaceNewItem(Item))
	{
		return Fail(ECrossguardInventoryError::NoRoom);
	}
	// Never leave the weapon in both places.
	const ECrossguardEquipError Unequipped = Equipment->Equip(Slot, FCrossguardWeaponItem(), WeaponSet);
	if (Unequipped != ECrossguardEquipError::None)
	{
		Items = Before;
		NextItemId = NextIdBefore;
		return FailEquip(Unequipped);
	}
	return {};
}

FCrossguardInventoryResult UCrossguardInventoryComponent::UnequipArmor(ECrossguardArmorSlot Slot)
{
	if (IsOwnerDead())
	{
		return Fail(ECrossguardInventoryError::OwnerDead);
	}
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
	const TArray<FCrossguardInventoryItem> Before = Items;
	const int32 NextIdBefore = NextItemId;
	if (!PlaceNewItem(Item))
	{
		return Fail(ECrossguardInventoryError::NoRoom);
	}
	const ECrossguardEquipError Unequipped = Equipment->EquipArmor(Slot, FCrossguardArmorItem());
	if (Unequipped != ECrossguardEquipError::None)
	{
		Items = Before;
		NextItemId = NextIdBefore;
		return FailEquip(Unequipped);
	}
	return {};
}

FCrossguardInventoryResult UCrossguardInventoryComponent::UnequipContainer(ECrossguardInventoryContainer Container)
{
	if (IsOwnerDead())
	{
		return Fail(ECrossguardInventoryError::OwnerDead);
	}
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
	if (IsOwnerDead())
	{
		return Fail(ECrossguardInventoryError::OwnerDead);
	}
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

// --- Death and loot containers -------------------------------------------------------------------

bool UCrossguardInventoryComponent::IsOwnerDead() const
{
	const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
	return ASC != nullptr && ASC->HasMatchingGameplayTag(CrossguardTags::State_Dead);
}

bool UCrossguardInventoryComponent::CanReach(const ACrossguardLootContainer* Container) const
{
	return Container != nullptr && GetOwner() != nullptr && !IsOwnerDead()
		&& FVector::Dist(GetOwner()->GetActorLocation(), Container->GetActorLocation()) <= Container->InteractRange;
}

bool UCrossguardInventoryComponent::OpenLootContainer(ACrossguardLootContainer* Container)
{
	if (!IsServer() || !CanReach(Container))
	{
		return false;
	}
	SetOpenContainer(Container);
	return true;
}

void UCrossguardInventoryComponent::SetOpenContainer(ACrossguardLootContainer* Container)
{
	if (IsValid(OpenContainer))
	{
		OpenContainer->OnContentsChanged.RemoveDynamic(this, &UCrossguardInventoryComponent::OnOpenContainerContentsChanged);
	}
	OpenContainer = IsValid(Container) ? Container : nullptr;
	if (OpenContainer != nullptr)
	{
		OpenContainer->OnContentsChanged.AddUniqueDynamic(this, &UCrossguardInventoryComponent::OnOpenContainerContentsChanged);
	}
	OnOpenContainerContentsChanged();
}

void UCrossguardInventoryComponent::OnOpenContainerContentsChanged()
{
	// Walked away (or it is gone): closed, so its contents stop reaching this player.
	if (OpenContainer != nullptr && (!IsValid(OpenContainer) || !CanReach(OpenContainer)))
	{
		SetOpenContainer(nullptr);
		return;
	}
	// Only the looter sees what is inside (replicated to the owner only).
	OpenContainerItems = OpenContainer != nullptr ? OpenContainer->GetItems() : TArray<FCrossguardInventoryItem>();
	OnOpenContainerChanged.Broadcast();
}

void UCrossguardInventoryComponent::RequestCloseContainer()
{
	if (IsServer())
	{
		SetOpenContainer(nullptr);
	}
	else
	{
		ServerCloseContainer();
	}
}

void UCrossguardInventoryComponent::ServerCloseContainer_Implementation()
{
	RequestCloseContainer();
}

void UCrossguardInventoryComponent::OnRep_OpenContainer()
{
	OnOpenContainerChanged.Broadcast();
}

void UCrossguardInventoryComponent::RequestTakeFromContainer(int32 ContainerItemId)
{
	if (IsServer())
	{
		Finish(TakeFromContainer(ContainerItemId));
	}
	else
	{
		ServerTakeFromContainer(ContainerItemId);
	}
}

void UCrossguardInventoryComponent::ServerTakeFromContainer_Implementation(int32 ContainerItemId)
{
	Finish(TakeFromContainer(ContainerItemId));
}

void UCrossguardInventoryComponent::RequestPutInContainer(int32 ItemId)
{
	if (IsServer())
	{
		Finish(PutInContainer(ItemId));
	}
	else
	{
		ServerPutInContainer(ItemId);
	}
}

void UCrossguardInventoryComponent::ServerPutInContainer_Implementation(int32 ItemId)
{
	Finish(PutInContainer(ItemId));
}

FCrossguardInventoryResult UCrossguardInventoryComponent::TakeFromContainer(int32 ContainerItemId)
{
	if (IsOwnerDead())
	{
		return Fail(ECrossguardInventoryError::OwnerDead);
	}
	if (OpenContainer == nullptr)
	{
		return Fail(ECrossguardInventoryError::NoContainerOpen);
	}
	if (!CanReach(OpenContainer))
	{
		return Fail(ECrossguardInventoryError::OutOfRange);
	}
	FCrossguardInventoryItem Item;
	if (!OpenContainer->FindItem(ContainerItemId, Item))
	{
		return Fail(ECrossguardInventoryError::UnknownItem);
	}

	const int32 Left = AddItem(Item);
	if (Left >= Item.Count)
	{
		return Fail(ECrossguardInventoryError::NoRoom);
	}
	OpenContainer->SetItemCount(ContainerItemId, Left);
	return {};
}

FCrossguardInventoryResult UCrossguardInventoryComponent::PutInContainer(int32 ItemId)
{
	if (IsOwnerDead())
	{
		return Fail(ECrossguardInventoryError::OwnerDead);
	}
	if (OpenContainer == nullptr)
	{
		return Fail(ECrossguardInventoryError::NoContainerOpen);
	}
	if (!CanReach(OpenContainer))
	{
		return Fail(ECrossguardInventoryError::OutOfRange);
	}
	const int32 Index = IndexOfItem(ItemId);
	if (Index == INDEX_NONE)
	{
		return Fail(ECrossguardInventoryError::UnknownItem);
	}
	if (!OpenContainer->AddItem(Items[Index]))
	{
		return Fail(ECrossguardInventoryError::NoRoom);
	}
	Items.RemoveAt(Index);
	return {};
}

TArray<FCrossguardInventoryItem> UCrossguardInventoryComponent::TakeEverything()
{
	TArray<FCrossguardInventoryItem> Taken;
	if (!IsServer())
	{
		return Taken;
	}
	StopUse();
	HoldItem(0);

	// Equipped gear first, so it sits at the top of the body's grid. Off hand before main hand:
	// an off-hand weapon without a main hand may not validate on its own.
	if (UCrossguardEquipmentComponent* Equipment = GetEquipment())
	{
		for (int32 Set = 0; Set < Crossguard::WeaponSetCount; ++Set)
		{
			for (const ECrossguardEquipmentSlot Slot : {ECrossguardEquipmentSlot::OffHand, ECrossguardEquipmentSlot::MainHand})
			{
				FCrossguardInventoryItem Item;
				Item.Category = ECrossguardItemCategory::Weapon;
				Item.Weapon = Equipment->GetWeaponInSet(Set, Slot);
				Item.bWasEquipped = true;
				Item.WornSlot = Slot;
				Item.WornSet = Set;
				if (!Item.Weapon.IsEmpty() && ResolveItemData(Item)
					&& Equipment->Equip(Slot, FCrossguardWeaponItem(), Set) == ECrossguardEquipError::None)
				{
					Taken.Add(Item);
				}
			}
		}
		for (uint8 SlotIndex = 0; SlotIndex < static_cast<uint8>(Crossguard::ArmorSlotCount); ++SlotIndex)
		{
			const ECrossguardArmorSlot Slot = static_cast<ECrossguardArmorSlot>(SlotIndex);
			FCrossguardInventoryItem Item;
			Item.Category = ECrossguardItemCategory::Armor;
			Item.Armor = Equipment->GetArmorItem(Slot);
			Item.bWasEquipped = true;
			if (!Item.Armor.IsEmpty() && ResolveItemData(Item) && Equipment->EquipArmor(Slot, FCrossguardArmorItem()) == ECrossguardEquipError::None)
			{
				Taken.Add(Item);
			}
		}
	}

	for (FCrossguardInventoryItem* Slot : {&EquippedBackpack, &EquippedQuiver})
	{
		if (!Slot->IsEmpty() && ResolveItemData(*Slot))
		{
			Slot->bWasEquipped = true;
			Taken.Add(*Slot);
		}
		*Slot = FCrossguardInventoryItem();
	}

	for (FCrossguardInventoryItem& Item : Items)
	{
		Item.bWasEquipped = false;
		Taken.Add(Item);
	}
	Items.Reset();
	SetOpenContainer(nullptr);
	OnInventoryChanged.Broadcast();
	return Taken;
}

TArray<FCrossguardInventoryItem> UCrossguardInventoryComponent::RestoreFromBody(const TArray<FCrossguardInventoryItem>& BodyItems)
{
	TArray<FCrossguardInventoryItem> Leftover;
	UCrossguardEquipmentComponent* Equipment = GetEquipment();
	if (!IsServer())
	{
		return BodyItems;
	}

	TArray<FCrossguardInventoryItem> Loose;
	// Containers first: what was in them goes back into them.
	for (const FCrossguardInventoryItem& Item : BodyItems)
	{
		FCrossguardInventoryItem* Slot = Item.ContainerKind == ECrossguardInventoryContainer::Quiver ? &EquippedQuiver : &EquippedBackpack;
		if (Item.bWasEquipped && Item.Category == ECrossguardItemCategory::Container && Item.ContainerKind != ECrossguardInventoryContainer::Main && Slot->IsEmpty())
		{
			*Slot = Item;
			Slot->bWasEquipped = false;
		}
		else
		{
			Loose.Add(Item);
		}
	}

	// Then worn gear, main hand before off hand so a dual-wield setup validates.
	Loose.StableSort([](const FCrossguardInventoryItem& A, const FCrossguardInventoryItem& B)
	{
		return (A.bWasEquipped && !B.bWasEquipped)
			|| (A.bWasEquipped == B.bWasEquipped && A.WornSlot == ECrossguardEquipmentSlot::MainHand && B.WornSlot != ECrossguardEquipmentSlot::MainHand);
	});
	for (FCrossguardInventoryItem Item : Loose)
	{
		bool bWorn = false;
		if (Item.bWasEquipped && Equipment != nullptr)
		{
			if (Item.Category == ECrossguardItemCategory::Weapon && Equipment->GetWeaponInSet(Item.WornSet, Item.WornSlot).IsEmpty())
			{
				bWorn = Equipment->Equip(Item.WornSlot, Item.Weapon, Item.WornSet) == ECrossguardEquipError::None;
			}
			else if (Item.Category == ECrossguardItemCategory::Armor)
			{
				ECrossguardArmorSlot ArmorSlot = ECrossguardArmorSlot::Head;
				bWorn = Equipment->GetArmorSlotFor(Item.Armor.ArmorId, ArmorSlot) && Equipment->GetArmorItem(ArmorSlot).IsEmpty()
					&& Equipment->EquipArmor(ArmorSlot, Item.Armor) == ECrossguardEquipError::None;
			}
		}
		if (bWorn)
		{
			continue;
		}

		Item.bWasEquipped = false;
		const int32 Left = AddItem(Item);
		if (Left > 0)
		{
			Item.Count = Left;
			Leftover.Add(Item);
		}
	}

	OnInventoryChanged.Broadcast();
	return Leftover;
}

// --- Readying and using consumables ---------------------------------------------------------------

void UCrossguardInventoryComponent::RequestReadyItem(int32 ItemId)
{
	if (IsServer())
	{
		Finish(ReadyItem(ItemId));
	}
	else
	{
		ServerReadyItem(ItemId);
	}
}

void UCrossguardInventoryComponent::ServerReadyItem_Implementation(int32 ItemId)
{
	Finish(ReadyItem(ItemId));
}

FCrossguardInventoryResult UCrossguardInventoryComponent::ReadyItem(int32 ItemId)
{
	if (IsOwnerDead())
	{
		return Fail(ECrossguardInventoryError::OwnerDead);
	}
	if (ChannelTimer.IsValid())
	{
		return Fail(ECrossguardInventoryError::NotReady);
	}
	const int32 Index = IndexOfItem(ItemId);
	if (ItemId != 0 && (Index == INDEX_NONE || !Crossguard::CanAssignToHotbar(ToRulesCategory(Items[Index].Category))))
	{
		return Fail(ItemId != 0 && Index != INDEX_NONE ? ECrossguardInventoryError::NotUsable : ECrossguardInventoryError::UnknownItem);
	}
	HoldItem(ItemId);
	return {};
}

void UCrossguardInventoryComponent::HoldItem(int32 ItemId)
{
	ReadiedItemId = ItemId;
	UCrossguardEquipmentComponent* Equipment = GetEquipment();
	if (Equipment == nullptr)
	{
		return;
	}
	const int32 Index = IndexOfItem(ItemId);
	const FCrossguardItemRow* Row = Index != INDEX_NONE && ItemTable != nullptr
		? ItemTable->FindRow<FCrossguardItemRow>(Items[Index].ItemId, TEXT("Inventory"), false)
		: nullptr;
	if (ItemId != 0)
	{
		Equipment->HolsterWeapons();
	}
	// A throwing knife in hand counts as a dagger (skills, masteries, tags).
	const bool bCountsAsWeapon = Row != nullptr && Row->Category == ECrossguardItemCategory::Throwable && Row->bHasCountsAsWeapon;
	Equipment->SetHeldThrowable(bCountsAsWeapon, bCountsAsWeapon ? Row->CountsAsWeapon : ECrossguardWeaponType::Dagger);
}

void UCrossguardInventoryComponent::ContinueWith(const FCrossguardInventoryItem& Used)
{
	const FCrossguardInventoryItem* Next = Items.FindByPredicate([&Used](const FCrossguardInventoryItem& Item)
	{
		return Item.Category == Used.Category && Item.ItemId == Used.ItemId && Item.Rarity == Used.Rarity;
	});
	HoldItem(Next != nullptr ? Next->Id : 0);
}

void UCrossguardInventoryComponent::ValidateHeldItem()
{
	if (IsServer() && ReadiedItemId != 0 && IndexOfItem(ReadiedItemId) == INDEX_NONE)
	{
		HoldItem(0);
	}
}

void UCrossguardInventoryComponent::ReleaseHeldItem(bool bUpdateEquipment)
{
	if (!IsServer())
	{
		return;
	}
	// A channel in progress stops (the bandage is already used up); the hands do not move on to the
	// next one.
	if (ChannelTimer.IsValid())
	{
		if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner()))
		{
			ASC->RemoveActiveGameplayEffect(ChannelEffect);
		}
		GetWorld()->GetTimerManager().ClearTimer(ChannelTimer);
		ChannelEffect = FActiveGameplayEffectHandle();
	}
	ReadiedItemId = 0;
	ChannelItem = FCrossguardInventoryItem();
	UCrossguardEquipmentComponent* Equipment = GetEquipment();
	if (bUpdateEquipment && Equipment != nullptr)
	{
		Equipment->SetHeldThrowable(false, ECrossguardWeaponType::Dagger);
	}
	OnInventoryChanged.Broadcast();
}

void UCrossguardInventoryComponent::RequestStartUse()
{
	if (IsServer())
	{
		Finish(StartUse());
	}
	else
	{
		ServerStartUse();
	}
}

void UCrossguardInventoryComponent::ServerStartUse_Implementation()
{
	Finish(StartUse());
}

void UCrossguardInventoryComponent::RequestStopUse()
{
	if (IsServer())
	{
		StopUse();
	}
	else
	{
		ServerStopUse();
	}
}

void UCrossguardInventoryComponent::ServerStopUse_Implementation()
{
	StopUse();
}

FCrossguardInventoryResult UCrossguardInventoryComponent::StartUse()
{
	if (IsOwnerDead())
	{
		return Fail(ECrossguardInventoryError::OwnerDead);
	}
	if (ChannelTimer.IsValid())
	{
		return Fail(ECrossguardInventoryError::NotReady);
	}
	const int32 Index = IndexOfItem(ReadiedItemId);
	if (Index == INDEX_NONE)
	{
		HoldItem(0);
		return Fail(ECrossguardInventoryError::UnknownItem);
	}

	const FCrossguardInventoryItem Item = Items[Index];
	const FCrossguardItemRow* Row = ItemTable != nullptr ? ItemTable->FindRow<FCrossguardItemRow>(Item.ItemId, TEXT("Inventory"), false) : nullptr;
	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
	// Stunned: nothing; guarding: no throwing (it is an attack).
	if (ASC != nullptr && ASC->HasMatchingGameplayTag(CrossguardTags::State_Stunned))
	{
		return Fail(ECrossguardInventoryError::NotReady);
	}
	if (Row != nullptr && Row->Category == ECrossguardItemCategory::Throwable)
	{
		if (ASC != nullptr && ASC->HasMatchingGameplayTag(CrossguardTags::State_Guarding))
		{
			return Fail(ECrossguardInventoryError::NotReady);
		}
		return Throw(Index, *Row);
	}
	if (Row == nullptr || Row->HealAmount <= 0.0f || Row->HealDuration <= 0.0f)
	{
		return Fail(ECrossguardInventoryError::NotUsable);
	}
	if (ASC == nullptr)
	{
		return Fail(ECrossguardInventoryError::NotReady);
	}

	// Used up as soon as it is used.
	if (--Items[Index].Count <= 0)
	{
		Items.RemoveAt(Index);
	}

	const float Healing = Row->HealAmount + Row->HealPerRarity * UCrossguardItemLibrary::GetRarityLevel(Item.Rarity);
	const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UCrossguardHealOverTimeEffect::StaticClass(), 1.0f, ASC->MakeEffectContext());
	if (Spec.IsValid())
	{
		UCrossguardHealOverTimeEffect::SetHealing(*Spec.Data, Healing, Row->HealDuration);
		const FActiveGameplayEffectHandle Applied = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
		if (Row->UseTime > 0.0f)
		{
			ChannelEffect = Applied;
		}
	}

	if (Row->UseTime > 0.0f)
	{
		// Channel: the hands stay busy until it finishes or left click is released.
		ChannelItem = Item;
		GetWorld()->GetTimerManager().SetTimer(ChannelTimer, FTimerDelegate::CreateUObject(this, &UCrossguardInventoryComponent::FinishChannel), Row->UseTime, false);
	}
	else
	{
		ContinueWith(Item);
	}
	return {};
}

void UCrossguardInventoryComponent::StopUse()
{
	if (!ChannelTimer.IsValid())
	{
		return;
	}
	// Released early: the rest of the healing is lost.
	if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner()))
	{
		ASC->RemoveActiveGameplayEffect(ChannelEffect);
	}
	FinishChannel();
}

void UCrossguardInventoryComponent::FinishChannel()
{
	GetWorld()->GetTimerManager().ClearTimer(ChannelTimer);
	ChannelEffect = FActiveGameplayEffectHandle();
	ContinueWith(ChannelItem);
	OnInventoryChanged.Broadcast();
}

// --- Hotbar ---------------------------------------------------------------------------------------

void UCrossguardInventoryComponent::RequestAssignHotbarSlot(int32 Slot, int32 ItemId)
{
	if (IsServer())
	{
		Finish(AssignHotbarSlot(Slot, ItemId));
	}
	else
	{
		ServerAssignHotbarSlot(Slot, ItemId);
	}
}

void UCrossguardInventoryComponent::ServerAssignHotbarSlot_Implementation(int32 Slot, int32 ItemId)
{
	Finish(AssignHotbarSlot(Slot, ItemId));
}

void UCrossguardInventoryComponent::RequestClearHotbarSlot(int32 Slot)
{
	// Clearing is assigning nothing.
	RequestAssignHotbarSlot(Slot, 0);
}

void UCrossguardInventoryComponent::RequestUseHotbarSlot(int32 Slot)
{
	if (IsServer())
	{
		Finish(UseHotbarSlot(Slot));
	}
	else
	{
		ServerUseHotbarSlot(Slot);
	}
}

void UCrossguardInventoryComponent::ServerUseHotbarSlot_Implementation(int32 Slot)
{
	Finish(UseHotbarSlot(Slot));
}

FCrossguardInventoryResult UCrossguardInventoryComponent::AssignHotbarSlot(int32 Slot, int32 ItemId)
{
	if (!Hotbar.IsValidIndex(Slot))
	{
		return Fail(ECrossguardInventoryError::InvalidPosition);
	}
	if (ItemId == 0)
	{
		Hotbar[Slot] = FCrossguardHotbarSlot();
		return {};
	}
	const int32 Index = IndexOfItem(ItemId);
	if (Index == INDEX_NONE)
	{
		return Fail(ECrossguardInventoryError::UnknownItem);
	}
	if (!Crossguard::CanAssignToHotbar(ToRulesCategory(Items[Index].Category)))
	{
		return Fail(ECrossguardInventoryError::NotUsable);
	}
	Hotbar[Slot].Category = Items[Index].Category;
	Hotbar[Slot].ItemId = Items[Index].ItemId;
	Hotbar[Slot].Rarity = Items[Index].Rarity;
	return {};
}

FCrossguardInventoryResult UCrossguardInventoryComponent::UseHotbarSlot(int32 Slot)
{
	if (!Hotbar.IsValidIndex(Slot) || Hotbar[Slot].IsEmpty())
	{
		return Fail(ECrossguardInventoryError::UnknownItem);
	}
	const FCrossguardHotbarSlot& Assigned = Hotbar[Slot];
	const FCrossguardInventoryItem* Stack = Items.FindByPredicate([&Assigned](const FCrossguardInventoryItem& Item)
	{
		return Item.Category == Assigned.Category && Item.ItemId == Assigned.ItemId && Item.Rarity == Assigned.Rarity;
	});
	if (Stack == nullptr)
	{
		return Fail(ECrossguardInventoryError::UnknownItem);
	}
	// Pressing the key of the item in hand puts it away.
	return ReadyItem(Stack->Id == ReadiedItemId ? 0 : Stack->Id);
}

FCrossguardInventoryResult UCrossguardInventoryComponent::Throw(int32 Index, const FCrossguardItemRow& Row)
{
	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
	APawn* Thrower = Cast<APawn>(GetOwner());
	if (ASC == nullptr || Thrower == nullptr || ThrownProjectileClass == nullptr || !Items.IsValidIndex(Index))
	{
		return Fail(ECrossguardInventoryError::NotReady);
	}

	const int32 RarityLevel = UCrossguardItemLibrary::GetRarityLevel(Items[Index].Rarity);
	const FGameplayTag DamageTag = Row.ThrowDamageType == ECrossguardDamageType::Magic ? CrossguardTags::SetByCaller_Damage_Magic
		: CrossguardTags::SetByCaller_Damage_Attack;
	const auto MakeDamageSpec = [ASC, &DamageTag, &Row](float Amount)
	{
		FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UCrossguardDamageEffect::StaticClass(), 1.0f, ASC->MakeEffectContext());
		if (Spec.IsValid())
		{
			Spec.Data->SetSetByCallerMagnitude(DamageTag, Amount);
			// A throwing knife's hit is a dagger hit for on-hit skill effects.
			if (Row.bHasCountsAsWeapon)
			{
				Spec.Data->AddDynamicAssetTag(UCrossguardEquipmentComponent::GetWieldingTag(Row.CountsAsWeapon));
			}
		}
		return Spec;
	};

	FVector EyeLocation;
	FRotator EyeRotation;
	Thrower->GetActorEyesViewPoint(EyeLocation, EyeRotation);
	const FTransform SpawnTransform(EyeRotation, EyeLocation + EyeRotation.Vector() * 50.0f);
	ACrossguardThrownProjectile* Projectile = GetWorld()->SpawnActorDeferred<ACrossguardThrownProjectile>(ThrownProjectileClass, SpawnTransform,
		Thrower, Thrower, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Projectile == nullptr)
	{
		return Fail(ECrossguardInventoryError::NotReady);
	}
	Projectile->ExplosionRadius = Row.ThrowRadiusMeters * 100.0f;
	// A thrown weapon is an attack with its type (owner rulings: a throwing knife is a dagger attack, a
	// throwing axe an axe attack): every readied next-attack skill a melee attack with that weapon would
	// use is used up, hit or miss (Slash, Gouge, Chop Down, Shade Blow, Mortal Strike; not a parry's
	// riposte), and [Splinter of Winter] rides on a knife. The throw's damage is the weapon damage that
	// Slash and Chop Down raise. Bombs carry none.
	FCrossguardAttackRiders Riders;
	const bool bWeaponThrow = Row.bHasCountsAsWeapon && Projectile->ExplosionRadius <= 0.0f;
	if (bWeaponThrow)
	{
		Riders = UCrossguardWeaponAttackAbility::GatherRiders(FCrossguardRiderAttack::ForThrown(Row.CountsAsWeapon), ASC);
	}
	Projectile->DamageSpec = MakeDamageSpec((Row.ThrowDamage + Row.ThrowDamagePerRarity * RarityLevel) * Riders.WeaponMultiplier + Riders.WeaponFlat);
	if (bWeaponThrow && Projectile->DamageSpec.IsValid())
	{
		FGameplayEffectSpec& Damage = *Projectile->DamageSpec.Data;
		Damage.SetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_Attack,
			Damage.GetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_Attack, false, 0.0f) + Riders.AttackAdd);
		Damage.SetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_Magic,
			Damage.GetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_Magic, false, 0.0f) + Riders.MagicAdd);
	}
	Projectile->OnHitSpecs = Riders.OnHitSpecs;
	Projectile->bCancelSkillsOnHit = Riders.bCancelSkills;
	Projectile->bSilenceOnHit = Riders.bSilence;
	Projectile->bGroundFlyingOnHit = Riders.bGroundFlying;
	Projectile->bMortalStrikeOnHit = Riders.bMortalStrike;
	// Guards (block and parry) stop a thrown weapon by its Impact Power; never an explosion.
	Projectile->ImpactPower = Projectile->ExplosionRadius > 0.0f ? 0 : Row.ImpactPower;
	if (Row.ThrowDotPerSecond > 0.0f && Row.ThrowDotSeconds > 0.0f)
	{
		Projectile->DotSpec = MakeDamageSpec(Row.ThrowDotPerSecond);
		Projectile->DotSeconds = Row.ThrowDotSeconds;
	}
	// Art from data (Items.json projectileMesh, ...), over the projectile Blueprint's.
	Projectile->Visual.MergeFrom(CrossguardCosmetics::ProjectileVisualFromItem(Row));
	Projectile->FinishSpawning(SpawnTransform);
	// Art hook: the throw starts on every machine (server side; not predicted).
	if (ACrossguardCharacter* ThrowerCharacter = Cast<ACrossguardCharacter>(Thrower))
	{
		FCrossguardAttackCue Cue;
		Cue.Kind = ECrossguardAttackKind::Throw;
		Cue.bHasWeapon = Row.bHasCountsAsWeapon;
		Cue.WeaponType = Row.CountsAsWeapon;
		ThrowerCharacter->NotifyAttackStarted(Cue);
	}

	// One is used up per throw; the hands stay on the next one if any is left.
	const FCrossguardInventoryItem Thrown = Items[Index];
	if (--Items[Index].Count <= 0)
	{
		Items.RemoveAt(Index);
	}
	ContinueWith(Thrown);
	return {};
}
