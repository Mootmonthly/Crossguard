#pragma once

#include "ActiveGameplayEffectHandle.h"
#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "CrossguardRules/Inventory.h"
#include "Game/CrossguardPlayerState.h"
#include "Items/CrossguardEquipmentComponent.h"
#include "Items/CrossguardItemTypes.h"

#include "CrossguardInventoryComponent.generated.h"

class ACrossguardLootContainer;
class ACrossguardThrownProjectile;
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
	// Dead characters cannot use their inventory; their body is looted instead.
	OwnerDead,
	// Stash actions are only allowed between raids.
	BetweenRaidsOnly,
	NotEnoughGold,
	// A limit is reached (e.g. the most stash tabs a character can buy).
	AtCap,
	// A vendor does not buy this item.
	NotSellable,
	// No container is open, or it is too far away.
	NoContainerOpen,
	OutOfRange,
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

	// Container: sets its size (Common 5 squares ... Mystic 30). Ammo, Consumable, Throwable,
	// Material: stacks only combine items of the same id and rarity. Weapons and armor use the
	// rarity in Weapon / Armor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	ECrossguardRarity Rarity = ECrossguardRarity::Common;

	// Stack size, up to MaxStack.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item", meta = (ClampMin = "1"))
	int32 Count = 1;

	// Set by the server from the item data when the item enters an inventory: unrotated size in
	// squares, and the most one stack holds.
	UPROPERTY(BlueprintReadOnly, Category = "Item")
	FIntPoint Size = FIntPoint::ZeroValue;

	UPROPERTY(BlueprintReadOnly, Category = "Item")
	int32 MaxStack = 1;

	// Where it sits. Set by the server.
	UPROPERTY(BlueprintReadOnly, Category = "Item")
	ECrossguardInventoryContainer Container = ECrossguardInventoryContainer::Main;

	// Top-left square.
	UPROPERTY(BlueprintReadOnly, Category = "Item")
	FIntPoint Position = FIntPoint::ZeroValue;

	// Width and height swapped.
	UPROPERTY(BlueprintReadOnly, Category = "Item")
	bool bRotated = false;

	// On a body: the item was worn when its owner died, so a revive puts it back on. WornSlot is the
	// hand for weapons; armor and containers know their own slot.
	UPROPERTY(BlueprintReadOnly, Category = "Item")
	bool bWasEquipped = false;

	UPROPERTY(BlueprintReadOnly, Category = "Item")
	ECrossguardEquipmentSlot WornSlot = ECrossguardEquipmentSlot::MainHand;

	// Weapon set (0 or 1) a worn weapon was in.
	UPROPERTY(BlueprintReadOnly, Category = "Item")
	int32 WornSet = 0;

	bool IsEmpty() const { return Id == 0; }

	// Stacks combine only the same item at the same rarity, and only stackable items.
	bool CanStackWith(const FCrossguardInventoryItem& Other) const
	{
		return MaxStack > 1 && Category == Other.Category && ItemId == Other.ItemId && Rarity == Other.Rarity;
	}
};

// A hotbar key (3-7): which item it uses. Remembers the item type and rarity, so a new stack of the
// same item fills it.
USTRUCT(BlueprintType)
struct CROSSGUARD_API FCrossguardHotbarSlot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Hotbar")
	ECrossguardItemCategory Category = ECrossguardItemCategory::Other;

	// None: empty slot.
	UPROPERTY(BlueprintReadOnly, Category = "Hotbar")
	FName ItemId;

	UPROPERTY(BlueprintReadOnly, Category = "Hotbar")
	ECrossguardRarity Rarity = ECrossguardRarity::Common;

	bool IsEmpty() const { return ItemId.IsNone(); }
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCrossguardInventoryChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCrossguardInventoryRejected, const FCrossguardInventoryResult&, Result);

// The raid inventory: a 10 x 5 grid plus the equipped Backpack and Quiver grids
// (docs/design-decisions.md, Inventory). Server authoritative: clients ask by item id and the
// server moves its own copy, so a client can never equip or use an item it does not have.
// Equipping moves an item between the grid and UCrossguardEquipmentComponent.
//
// On death everything (equipped gear included) moves into a lootable body (ACrossguardLootContainer).
//
// Not built yet: dropping and picking up single items in the world, the stash and guild bank,
// splitting stacks, using consumables.
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

	// Thrown by left click with a throwable readied. Set a Blueprint subclass for meshes and speed.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory")
	TSubclassOf<ACrossguardThrownProjectile> ThrownProjectileClass;

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
	int32 AddItem(const FCrossguardInventoryItem& NewItem);

	// How many of a table item (any rarity) the inventory holds, across every container.
	UFUNCTION(BlueprintPure, Category = "Inventory")
	int32 CountItem(FName ItemId) const;

	// Server. Uses up Count of a table item (any rarity): Quiver first, then the main grid, then
	// the Backpack. False, changing nothing, if there are not enough.
	bool ConsumeItem(FName ItemId, int32 Count = 1);

	// Server. Removes every stack of a category (e.g. Currency on extraction) and returns how many
	// items they held.
	int32 TakeAllOfCategory(ECrossguardItemCategory Category);

	// Server. Removes a whole stack. False if there is no such item.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory")
	bool RemoveItem(int32 ItemId);

	// Client requests: run on the server (clients forward them). Rejections reach the owning
	// client through OnInventoryRejected.

	// Moves or rotates an item. Dropping a stack onto the top-left square of the same item merges
	// as many as fit.
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void RequestMoveItem(int32 ItemId, ECrossguardInventoryContainer Container, FIntPoint Position, bool bRotated);

	// Weapons go in Slot of weapon set WeaponSet (-1: the one in hand); armor in its own slot; a
	// Backpack or Quiver in its container slot. What was there moves into the inventory.
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void RequestEquipItem(int32 ItemId, ECrossguardEquipmentSlot Slot = ECrossguardEquipmentSlot::MainHand, int32 WeaponSet = -1);

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void RequestUnequipWeapon(ECrossguardEquipmentSlot Slot, int32 WeaponSet = -1);

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void RequestUnequipArmor(ECrossguardArmorSlot Slot);

	// The container must be empty.
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void RequestUnequipContainer(ECrossguardInventoryContainer Container);

	// Right click. Skill Gem: learns its skill (between raids only) and consumes the gem.
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void RequestUseItem(int32 ItemId);

	// Hotbar keys 3-7 (slots 0-4) hold consumables and throwables.
	UFUNCTION(BlueprintPure, Category = "Inventory|Hotbar")
	const TArray<FCrossguardHotbarSlot>& GetHotbar() const { return Hotbar; }

	// Assigns the item type and rarity of an inventory stack to a slot (consumables and throwables).
	UFUNCTION(BlueprintCallable, Category = "Inventory|Hotbar")
	void RequestAssignHotbarSlot(int32 Slot, int32 ItemId);

	UFUNCTION(BlueprintCallable, Category = "Inventory|Hotbar")
	void RequestClearHotbarSlot(int32 Slot);

	// The slot's key: readies the first matching stack (pressing it again puts it away).
	UFUNCTION(BlueprintCallable, Category = "Inventory|Hotbar")
	void RequestUseHotbarSlot(int32 Slot);

	// Consumables and throwables: ready a stack in hand, then left click uses or throws one.
	// After a use the hands go back to the weapon. 0 puts the item away.
	UFUNCTION(BlueprintCallable, Category = "Inventory|Use")
	void RequestReadyItem(int32 ItemId);

	// 0 when the hands hold the weapon.
	UFUNCTION(BlueprintPure, Category = "Inventory|Use")
	int32 GetReadiedItemId() const { return ReadiedItemId; }

	// Left click pressed / released with an item readied. Uses one: the healing starts at once and
	// runs over the item's heal duration. Items with a use time (bandages) channel: releasing early
	// stops the healing, and the item is already used up.
	UFUNCTION(BlueprintCallable, Category = "Inventory|Use")
	void RequestStartUse();

	UFUNCTION(BlueprintCallable, Category = "Inventory|Use")
	void RequestStopUse();

	// Loot containers (bodies, later chests). Open one with the character's interact key.
	UFUNCTION(BlueprintPure, Category = "Inventory|Loot")
	ACrossguardLootContainer* GetOpenContainer() const { return OpenContainer; }

	// Server. Opens a container if the owner is alive and in range. False otherwise.
	bool OpenLootContainer(ACrossguardLootContainer* Container);

	UFUNCTION(BlueprintCallable, Category = "Inventory|Loot")
	void RequestCloseContainer();

	// Moves a stack from the open container into the inventory (stacking first). Whatever does not
	// fit stays in the container.
	UFUNCTION(BlueprintCallable, Category = "Inventory|Loot")
	void RequestTakeFromContainer(int32 ContainerItemId);

	// Moves a stack from the inventory into the open container.
	UFUNCTION(BlueprintCallable, Category = "Inventory|Loot")
	void RequestPutInContainer(int32 ItemId);

	// Server. Death: empties the inventory, the Backpack and Quiver slots and every equipment slot,
	// and returns all of it as items with their data filled in. Worn items are marked bWasEquipped.
	TArray<FCrossguardInventoryItem> TakeEverything();

	// Server. Revive: puts worn items back on (containers first, so their contents fit), then adds
	// the rest. Returns what could not be placed.
	TArray<FCrossguardInventoryItem> RestoreFromBody(const TArray<FCrossguardInventoryItem>& BodyItems);

	UPROPERTY(BlueprintAssignable, Category = "Inventory")
	FCrossguardInventoryChanged OnInventoryChanged;

	// The open container changed (opened, closed).
	UPROPERTY(BlueprintAssignable, Category = "Inventory|Loot")
	FCrossguardInventoryChanged OnOpenContainerChanged;

	UPROPERTY(BlueprintAssignable, Category = "Inventory")
	FCrossguardInventoryRejected OnInventoryRejected;

protected:
	UFUNCTION(Server, Reliable)
	void ServerMoveItem(int32 ItemId, ECrossguardInventoryContainer Container, FIntPoint Position, bool bRotated);

	UFUNCTION(Server, Reliable)
	void ServerEquipItem(int32 ItemId, ECrossguardEquipmentSlot Slot, int32 WeaponSet);

	UFUNCTION(Server, Reliable)
	void ServerUnequipWeapon(ECrossguardEquipmentSlot Slot, int32 WeaponSet);

	UFUNCTION(Server, Reliable)
	void ServerUnequipArmor(ECrossguardArmorSlot Slot);

	UFUNCTION(Server, Reliable)
	void ServerUnequipContainer(ECrossguardInventoryContainer Container);

	UFUNCTION(Server, Reliable)
	void ServerUseItem(int32 ItemId);

	UFUNCTION(Server, Reliable)
	void ServerReadyItem(int32 ItemId);

	UFUNCTION(Server, Reliable)
	void ServerAssignHotbarSlot(int32 Slot, int32 ItemId);

	UFUNCTION(Server, Reliable)
	void ServerUseHotbarSlot(int32 Slot);

	UFUNCTION(Server, Reliable)
	void ServerStartUse();

	UFUNCTION(Server, Reliable)
	void ServerStopUse();

	UFUNCTION(Server, Reliable)
	void ServerCloseContainer();

	UFUNCTION(Server, Reliable)
	void ServerTakeFromContainer(int32 ContainerItemId);

	UFUNCTION(Server, Reliable)
	void ServerPutInContainer(int32 ItemId);

	UFUNCTION(Client, Reliable)
	void ClientInventoryRejected(const FCrossguardInventoryResult& Result);

	UFUNCTION()
	void OnRep_OpenContainer();

	UFUNCTION()
	void OnRep_Inventory();

private:
	FCrossguardInventoryResult MoveItem(int32 ItemId, ECrossguardInventoryContainer Container, FIntPoint Position, bool bRotated);
	FCrossguardInventoryResult EquipItem(int32 ItemId, ECrossguardEquipmentSlot Slot, int32 WeaponSet);
	FCrossguardInventoryResult EquipContainer(const FCrossguardInventoryItem& Item);
	FCrossguardInventoryResult UnequipWeapon(ECrossguardEquipmentSlot Slot, int32 WeaponSet);
	FCrossguardInventoryResult UnequipArmor(ECrossguardArmorSlot Slot);
	FCrossguardInventoryResult UnequipContainer(ECrossguardInventoryContainer Container);
	FCrossguardInventoryResult UseItem(int32 ItemId);
	FCrossguardInventoryResult ReadyItem(int32 ItemId);
	FCrossguardInventoryResult AssignHotbarSlot(int32 Slot, int32 ItemId);
	FCrossguardInventoryResult UseHotbarSlot(int32 Slot);
	FCrossguardInventoryResult StartUse();
	// Throws one of the readied throwable stack at Index.
	FCrossguardInventoryResult Throw(int32 Index, const FCrossguardItemRow& Row);
	void StopUse();
	void FinishChannel();
	FCrossguardInventoryResult TakeFromContainer(int32 ContainerItemId);
	FCrossguardInventoryResult PutInContainer(int32 ItemId);
	// The open container, if it still exists and the owner is in range.
	bool CanReach(const ACrossguardLootContainer* Container) const;
	bool IsOwnerDead() const;

	// Server: logs and sends a failure to the owning client. Success broadcasts OnInventoryChanged.
	void Finish(const FCrossguardInventoryResult& Result);

	bool GetGridSize(const FCrossguardInventoryItem& Item, Crossguard::FGridSize& OutSize) const;
	int32 LookUpMaxStack(const FCrossguardInventoryItem& Item) const;
	// Fills Size and MaxStack from the data tables. False if the item's data is missing.
	bool ResolveItemData(FCrossguardInventoryItem& Item) const;
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

	UPROPERTY(ReplicatedUsing = OnRep_OpenContainer)
	TObjectPtr<ACrossguardLootContainer> OpenContainer;

	UPROPERTY(ReplicatedUsing = OnRep_Inventory)
	int32 ReadiedItemId = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Inventory)
	TArray<FCrossguardHotbarSlot> Hotbar;

	// Server only: the channel in progress.
	FActiveGameplayEffectHandle ChannelEffect;
	FTimerHandle ChannelTimer;

	// Server only.
	int32 NextItemId = 1;
};
