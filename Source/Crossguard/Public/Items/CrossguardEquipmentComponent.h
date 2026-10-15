#pragma once

#include "ActiveGameplayEffectHandle.h"
#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "CrossguardRules/Items.h"
#include "GameplayTagContainer.h"
#include "Items/CrossguardItemTypes.h"

#include "CrossguardEquipmentComponent.generated.h"

class UCrossguardAbilitySystemComponent;
class UDataTable;

UENUM(BlueprintType)
enum class ECrossguardEquipError : uint8
{
	None,
	UnknownWeapon,
	UnknownArmor,
	// The armor piece belongs in a different slot.
	WrongArmorSlot,
	ShieldInMainHand,
	TwoHandedInOffHand,
	TwoHandedWithOffHand,
	NotReady,
};

// Mirrors Crossguard::ESecondaryAction: what right click does.
UENUM(BlueprintType)
enum class ECrossguardSecondaryAction : uint8
{
	None,
	OffHandAttack,
	Block,
	Parry,
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCrossguardEquipmentChanged);

// Two weapon sets (keys 1 and 2), each a main hand and an off hand, and the six armor slots
// (docs/item-data-schema.md). Only the active set is in hand: its weapons attack and count for
// stats, tags and skill requirements. Dual wielding: any one-handed weapon fits the off hand.
//
// Changes are server only: players equip through UCrossguardInventoryComponent, which moves the item
// between the inventory and the slot. The server validates every change, then re-applies one UCrossguardEquipmentEffect holding the
// items' stats (shield AR, armor stats, affixes, empty armor slots) and the Equipment.Wielding.* and
// Equipment.Armor.* tags that skill effects and requirements check.
UCLASS(ClassGroup = (Crossguard), meta = (BlueprintSpawnableComponent))
class CROSSGUARD_API UCrossguardEquipmentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCrossguardEquipmentComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Imported from Data/Items/WeaponTable.json. Row struct: CrossguardWeaponRow.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment", meta = (RequiredAssetDataTags = "RowStructure=/Script/Crossguard.CrossguardWeaponRow"))
	TObjectPtr<UDataTable> WeaponTable;

	// Imported from Data/Items/ArmorTable.json. Row struct: CrossguardArmorRow.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment", meta = (RequiredAssetDataTags = "RowStructure=/Script/Crossguard.CrossguardArmorRow"))
	TObjectPtr<UDataTable> ArmorTable;

	// Imported from Data/Items/AffixTable.json. Row struct: CrossguardAffixRow.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment", meta = (RequiredAssetDataTags = "RowStructure=/Script/Crossguard.CrossguardAffixRow"))
	TObjectPtr<UDataTable> AffixTable;

	// Whether Item (empty: unequip) can go in Slot of weapon set Set (-1: the active set) with that
	// set's other hand as it is.
	ECrossguardEquipError CheckEquip(ECrossguardEquipmentSlot Slot, const FCrossguardWeaponItem& Item, int32 Set = -1) const;

	// Server. Validates, then puts Item in Slot of weapon set Set (-1: active; empty: unequips). Does
	// not touch the inventory.
	ECrossguardEquipError Equip(ECrossguardEquipmentSlot Slot, const FCrossguardWeaponItem& Item, int32 Set = -1);

	// 0 or 1 (keys 1 and 2).
	UFUNCTION(BlueprintPure, Category = "Equipment")
	int32 GetActiveWeaponSet() const { return ActiveWeaponSet; }

	// A weapon in either set.
	UFUNCTION(BlueprintPure, Category = "Equipment")
	const FCrossguardWeaponItem& GetWeaponInSet(int32 Set, ECrossguardEquipmentSlot Slot) const;

	// Draws weapon set Set (0 or 1): 1 s, faster with Dexterity, during which nothing attacks. Also
	// draws the active set when the hands hold an item or nothing. Runs on the server; clients
	// forward it.
	UFUNCTION(BlueprintCallable, Category = "Equipment")
	void RequestSwitchWeaponSet(int32 Set);

	// The active set is drawn and in hand: weapon attacks are allowed.
	UFUNCTION(BlueprintPure, Category = "Equipment")
	bool AreWeaponsReady() const { return bWeaponsDrawn && !bDrawingWeapons; }

	UFUNCTION(BlueprintPure, Category = "Equipment")
	bool IsDrawingWeapons() const { return bDrawingWeapons; }

	// Server. The hands take an item (or nothing): the weapons go away until a set is drawn again.
	void HolsterWeapons();

	// Server. A throwable that counts as a weapon type (throwing knife: Dagger) is in hand: that type
	// counts as wielded for skill requirements, masteries and Equipment.Wielding tags. bHolding
	// false clears it.
	void SetHeldThrowable(bool bHolding, ECrossguardWeaponType Type);

	// Equipment.Wielding.<Type>.
	static FGameplayTag GetWieldingTag(ECrossguardWeaponType Type);

	UFUNCTION(BlueprintPure, Category = "Equipment")
	const FCrossguardWeaponItem& GetItem(ECrossguardEquipmentSlot Slot) const;

	// Null if the slot is empty or the weapon is not in the table.
	const FCrossguardWeaponRow* GetWeaponRow(ECrossguardEquipmentSlot Slot) const;

	// True if the equipped weapons satisfy any one of the types. Empty list: true.
	UFUNCTION(BlueprintPure, Category = "Equipment")
	bool SatisfiesWeaponRequirement(const TArray<ECrossguardWeaponType>& AnyOf) const;

	// Seconds between attacks with the weapon in Slot, after Action Speed. 0 if it cannot attack.
	UFUNCTION(BlueprintPure, Category = "Equipment")
	float GetAttackInterval(ECrossguardEquipmentSlot Slot) const;

	UFUNCTION(BlueprintPure, Category = "Equipment")
	ECrossguardSecondaryAction GetSecondaryAction() const;

	// Whether Item (empty: unequip) can go in Slot.
	ECrossguardEquipError CheckEquipArmor(ECrossguardArmorSlot Slot, const FCrossguardArmorItem& Item) const;

	// Server. Validates, then puts Item in Slot (empty: unequips). Does not touch the inventory.
	ECrossguardEquipError EquipArmor(ECrossguardArmorSlot Slot, const FCrossguardArmorItem& Item);

	// The slot a piece belongs in. False if the piece is not in the table.
	bool GetArmorSlotFor(FName ArmorId, ECrossguardArmorSlot& OutSlot) const;

	UFUNCTION(BlueprintPure, Category = "Equipment|Armor")
	const FCrossguardArmorItem& GetArmorItem(ECrossguardArmorSlot Slot) const;

	// Null if the slot is empty or the piece is not in the table.
	const FCrossguardArmorRow* GetArmorRow(ECrossguardArmorSlot Slot) const;

	UFUNCTION(BlueprintPure, Category = "Equipment|Armor")
	bool SatisfiesArmorRequirement(ECrossguardArmorRequirement Requirement) const;

	// All six slots count ([Cor Leonis]).
	UFUNCTION(BlueprintPure, Category = "Equipment|Armor")
	int32 GetEmptyArmorSlotCount() const;

	// Server only. Applies the equipment effect for the current (possibly empty) equipment, so
	// stats like EmptyArmorSlots are right before the first equip. Call after the ability system
	// is initialized.
	void InitializeEquipmentEffect();

	UPROPERTY(BlueprintAssignable, Category = "Equipment")
	FCrossguardEquipmentChanged OnEquipmentChanged;

protected:
	UFUNCTION()
	void OnRep_Equipment();

	UFUNCTION(Server, Reliable)
	void ServerSwitchWeaponSet(int32 Set);

	void FinishDrawing();

private:
	void RefreshEquipmentEffect();
	const FCrossguardArmorRow* FindArmor(FName ArmorId) const;
	Crossguard::FArmorLoadout BuildArmorLoadout() const;

	const FCrossguardWeaponRow* FindWeapon(FName WeaponId) const;
	Crossguard::FEquipment BuildRulesEquipment(const FCrossguardWeaponItem& Main, const FCrossguardWeaponItem& Off) const;
	UCrossguardAbilitySystemComponent* GetAbilitySystem() const;

	UPROPERTY(ReplicatedUsing = OnRep_Equipment)
	FCrossguardWeaponItem MainHand;

	UPROPERTY(ReplicatedUsing = OnRep_Equipment)
	FCrossguardWeaponItem OffHand;

	// The set not in hand. Swapped with MainHand / OffHand on a switch.
	UPROPERTY(ReplicatedUsing = OnRep_Equipment)
	FCrossguardWeaponItem StowedMainHand;

	UPROPERTY(ReplicatedUsing = OnRep_Equipment)
	FCrossguardWeaponItem StowedOffHand;

	UPROPERTY(ReplicatedUsing = OnRep_Equipment)
	int32 ActiveWeaponSet = 0;

	bool IsActiveSet(int32 Set) const { return Set < 0 || Set == ActiveWeaponSet; }

	UPROPERTY(ReplicatedUsing = OnRep_Equipment)
	bool bWeaponsDrawn = true;

	UPROPERTY(ReplicatedUsing = OnRep_Equipment)
	bool bDrawingWeapons = false;

	UPROPERTY(ReplicatedUsing = OnRep_Equipment)
	bool bHoldingThrowable = false;

	UPROPERTY(ReplicatedUsing = OnRep_Equipment)
	ECrossguardWeaponType HeldThrowableType = ECrossguardWeaponType::Dagger;

	// Server only.
	FTimerHandle DrawTimer;

	// One entry per ECrossguardArmorSlot, in enum order.
	UPROPERTY(ReplicatedUsing = OnRep_Equipment)
	TArray<FCrossguardArmorItem> Armor;

	// Server only.
	FActiveGameplayEffectHandle EquipmentEffectHandle;
};
