#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "CrossguardRules/Inventory.h"
#include "Game/CrossguardPlayerState.h"
#include "Items/CrossguardEquipmentComponent.h"
#include "Items/CrossguardItemTypes.h"

#include "CrossguardInventoryComponent.generated.h"

class UDataTable;

// Mirrors Crossguard::EContainerKind.
UENUM(BlueprintType)
enum class ECrossguardInventoryContainer : uint8
{
	// The 50-square raid inventory.
	Main,
	// Only while a Backpack is equipped.
	Backpack,
	// Only while a Quiver is equipped. Arrows and Bolts only.
	Quiver,
};

UENUM(BlueprintType)
enum class ECrossguardInventoryError : uint8
{
	None,
	// No item with that id, or its data row is missing.
	UnknownItem,
	NoRoom,
	// Outside the grid or overlapping another item.
	InvalidPosition,
	// That container does not take this item (e.g. a sword in the Quiver).
	WrongContainer,
	// The Backpack or Quiver is not equipped.
	NoContainer,
	// A Backpack or Quiver must be empty to unequip or swap it.
	ContainerNotEmpty,
	// See EquipError.
	CannotEquip,
	NotUsable,
	// See ProgressionError (e.g. learning a Skill Gem during a raid).
	CannotLearn,
	NotReady,
};

USTRUCT(BlueprintType)
struct CROSSGUARD_API FCrossguardInventoryResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	ECrossguardInventoryError Error = ECrossguardInventoryError::None;

	// Set when Error is CannotEquip.
	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	ECrossguardEquipError EquipError = ECrossguardEquipError::None;

	// Set when Error is CannotLearn.
	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	ECrossguardProgressionError ProgressionError = ECrossguardProgressionError::None;

	bool IsValid() const { return Error == ECrossguardInventoryError::None; }
};

// One stack in the inventory. Which fields matter depends on Category.
USTRUCT(BlueprintType)
struct CROSSGUARD_API FCrossguardInventoryItem
{
	GENERATED_BODY()

	// Assigned by the server, unique within the inventory. 0: no item.
	UPROPERTY(BlueprintReadOnly, Category = "Item")
	int32 Id = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	ECrossguardItemCategory Category = ECrossguardItemCategory::Other;

	// Category Weapon.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FCrossguardWeaponItem Weapon;

	// Category Armor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FCrossguardArmorItem Armor;

	// Ammo, Consumable, Throwable, Material: row in the item table (e.g. "arrow").
	// SkillGem: the skill id it teaches.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FName ItemId;

	// Category Container: Backpack or Quiver.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	ECrossguardInventoryContainer ContainerKind = ECrossguardInventoryContainer::Backpack;

	// Category Container: sets its size (Common 5 squares ... Mystic 30).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	ECrossguardRarity Rarity = ECrossguardRarity::Common;

	// Stack size, up to the item table's MaxStack. 1 for everything else.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item", meta = (ClampMin = "1"))
	int32 Count = 1;

	// Where it sits. Set by the server.
	UPROPERTY(BlueprintReadOnly, Category = "Item")
	ECrossguardInventoryContainer Container = ECrossguardInventoryContainer::Main;

	// Top-left square.
	UPROPERTY(BlueprintReadOnly, Category = "Item")
	FIntPoint Position = FIntPoint::ZeroValue;

	// Width and height swapped.
	UPROPERTY(BlueprintReadOnly, Category = "Item")
	bool bRotated = false;

	bool IsEmpty() const { return Id == 0; }
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCrossguardInventoryChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCrossguardInventoryRejected, const FCrossguardInventoryResult&, Result);

// The raid inventory: a 10 x 5 grid plus the equipped Backpack and Quiver grids
// (docs/design-decisions.md, Inventory). Server authoritative: clients ask by item id and the
// server moves its own copy, so a client can never equip or use an item it does not have.
// Equipping moves an item between the grid and UCrossguardEquipmentComponent.
//
// Not built yet: dropping and picking up items in the world, the death drop, the stash and guild
// bank, splitting stacks, using consumables.
UCLASS(ClassGroup = (Crossguard), meta = (BlueprintSpawnableComponent))
class CROSSGUARD_API UCrossguardInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCrossguardInventoryComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Imported from Data/Items/ItemTable.json. Row struct: CrossguardItemRow. Weapon and armor sizes
	// come from the equipment component's tables.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory", meta = (RequiredAssetDataTags = "RowStructure=/Script/Crossguard.CrossguardItemRow"))
	TObjectPtr<UDataTable> ItemTable;

	// Squares a Backpack / Quiver item takes while not equipped. Placeholders.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory")
	FIntPoint BackpackItemSize = FIntPoint(2, 2);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory")
	FIntPoint QuiverItemSize = FIntPoint(2, 2);

	UFUNCTION(BlueprintPure, Category = "Inventory")
	const TArray<FCrossguardInventoryItem>& GetItems() const { return Items; }

	UFUNCTION(BlueprintPure, Category = "Inventory")
	bool FindItem(int32 ItemId, FCrossguardInventoryItem& OutItem) const;

	// Width x height in squares; 0 x 0 for a Backpack or Quiver that is not equipped.
	UFUNCTION(BlueprintPure, Category = "Inventory")
	FIntPoint GetContainerSize(ECrossguardInventoryContainer Container) const;

	// The equipped Backpack or Quiver item; empty (Id 0) if none.
	UFUNCTION(BlueprintPure, Category = "Inventory")
	const FCrossguardInventoryItem& GetEquippedContainer(ECrossguardInventoryContainer Container) const;

	// Unrotated size in squares; 0 x 0 if the item's data is missing.
	UFUNCTION(BlueprintPure, Category = "Inventory")
	FIntPoint GetItemSize(const FCrossguardInventoryItem& Item) const;

	// Server. Adds the item wherever it fits: stackables top up existing stacks first; ammo tries the
	// Quiver first, then the main grid, then the Backpack. Returns how many did not fit (0: all).
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory")
	int32 AddItem(const FCrossguardInventoryItem& Item);

	// Server. Removes a whole stack. False if there is no such item.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory")
	bool RemoveItem(int32 ItemId);

	// Client requests: run on the server (clients forward them). Rejections reach the owning
	// client through OnInventoryRejected.

	// Moves or rotates an item. Dropping a stack onto the top-left square of the same item merges
	// as many as fit.
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void RequestMoveItem(int32 ItemId, ECrossguardInventoryContainer Container, FIntPoint Position, bool bRotated);

	// Weapons go in Slot; armor in its own slot; a Backpack or Quiver in its container slot. What
	// was there moves into the inventory.
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void RequestEquipItem(int32 ItemId, ECrossguardEquipmentSlot Slot = ECrossguardEquipmentSlot::MainHand);

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void RequestUnequipWeapon(ECrossguardEquipmentSlot Slot);

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void RequestUnequipArmor(ECrossguardArmorSlot Slot);

	// The container must be empty.
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void RequestUnequipContainer(ECrossguardInventoryContainer Container);

	// Right click. Skill Gem: learns its skill (between raids only) and consumes the gem.
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void RequestUseItem(int32 ItemId);

	UPROPERTY(BlueprintAssignable, Category = "Inventory")
	FCrossguardInventoryChanged OnInventoryChanged;

	UPROPERTY(BlueprintAssignable, Category = "Inventory")
	FCrossguardInventoryRejected OnInventoryRejected;

protected:
	UFUNCTION(Server, Reliable)
	void ServerMoveItem(int32 ItemId, ECrossguardInventoryContainer Container, FIntPoint Position, bool bRotated);

	UFUNCTION(Server, Reliable)
	void ServerEquipItem(int32 ItemId, ECrossguardEquipmentSlot Slot);

	UFUNCTION(Server, Reliable)
	void ServerUnequipWeapon(ECrossguardEquipmentSlot Slot);

	UFUNCTION(Server, Reliable)
	void ServerUnequipArmor(ECrossguardArmorSlot Slot);

	UFUNCTION(Server, Reliable)
	void ServerUnequipContainer(ECrossguardInventoryContainer Container);

	UFUNCTION(Server, Reliable)
	void ServerUseItem(int32 ItemId);

	UFUNCTION(Client, Reliable)
	void ClientInventoryRejected(const FCrossguardInventoryResult& Result);

	UFUNCTION()
	void OnRep_Inventory();

private:
	FCrossguardInventoryResult MoveItem(int32 ItemId, ECrossguardInventoryContainer Container, FIntPoint Position, bool bRotated);
	FCrossguardInventoryResult EquipItem(int32 ItemId, ECrossguardEquipmentSlot Slot);
	FCrossguardInventoryResult EquipContainer(const FCrossguardInventoryItem& Item);
	FCrossguardInventoryResult UnequipWeapon(ECrossguardEquipmentSlot Slot);
	FCrossguardInventoryResult UnequipArmor(ECrossguardArmorSlot Slot);
	FCrossguardInventoryResult UnequipContainer(ECrossguardInventoryContainer Container);
	FCrossguardInventoryResult UseItem(int32 ItemId);

	// Server: logs and sends a failure to the owning client. Success broadcasts OnInventoryChanged.
	void Finish(const FCrossguardInventoryResult& Result);

	bool GetGridSize(const FCrossguardInventoryItem& Item, Crossguard::FGridSize& OutSize) const;
	int32 GetMaxStack(const FCrossguardInventoryItem& Item) const;
	bool CanStackWith(const FCrossguardInventoryItem& A, const FCrossguardInventoryItem& B) const;
	bool Accepts(ECrossguardInventoryContainer Container, ECrossguardItemCategory Category) const;
	// The occupied squares of one container, leaving out IgnoreId.
	Crossguard::FInventoryGrid BuildGrid(ECrossguardInventoryContainer Container, int32 IgnoreId = 0) const;
	// Sets Item's placement to the first free spot (rotation allowed). Does not add it.
	bool FindPlacement(FCrossguardInventoryItem& Item) const;
	// Gives the item a new id, finds it a spot and adds it. False if it does not fit.
	bool PlaceNewItem(FCrossguardInventoryItem Item);
	int32 IndexOfItem(int32 ItemId) const;
	UCrossguardEquipmentComponent* GetEquipment() const;
	bool IsServer() const;

	UPROPERTY(ReplicatedUsing = OnRep_Inventory)
	TArray<FCrossguardInventoryItem> Items;

	UPROPERTY(ReplicatedUsing = OnRep_Inventory)
	FCrossguardInventoryItem EquippedBackpack;

	UPROPERTY(ReplicatedUsing = OnRep_Inventory)
	FCrossguardInventoryItem EquippedQuiver;

	// Server only.
	int32 NextItemId = 1;
};
