#pragma once

#include "ActiveGameplayEffectHandle.h"
#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "CrossguardRules/Items.h"
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

// Main-hand and off-hand weapons and the six armor slots (docs/item-data-schema.md). Dual
// wielding: any one-handed weapon fits the off hand.
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

	// Whether Item (empty: unequip) can go in Slot with the other hand as it is.
	ECrossguardEquipError CheckEquip(ECrossguardEquipmentSlot Slot, const FCrossguardWeaponItem& Item) const;

	// Server. Validates, then puts Item in Slot (empty: unequips). Does not touch the inventory.
	ECrossguardEquipError Equip(ECrossguardEquipmentSlot Slot, const FCrossguardWeaponItem& Item);

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

	// One entry per ECrossguardArmorSlot, in enum order.
	UPROPERTY(ReplicatedUsing = OnRep_Equipment)
	TArray<FCrossguardArmorItem> Armor;

	// Server only.
	FActiveGameplayEffectHandle EquipmentEffectHandle;
};
