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
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCrossguardEquipRejected, ECrossguardEquipmentSlot, Slot, ECrossguardEquipError, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCrossguardArmorEquipRejected, ECrossguardArmorSlot, Slot, ECrossguardEquipError, Error);

// Main-hand and off-hand weapons and the six armor slots (docs/item-data-schema.md). Dual
// wielding: any one-handed weapon fits the off hand.
//
// The server validates every change, then re-applies one UCrossguardEquipmentEffect holding the
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

	// Runs on the server; clients forward the request. Rejections reach the owning client.
	UFUNCTION(BlueprintCallable, Category = "Equipment")
	void RequestEquip(ECrossguardEquipmentSlot Slot, const FCrossguardWeaponItem& Item);

	UFUNCTION(BlueprintCallable, Category = "Equipment")
	void RequestUnequip(ECrossguardEquipmentSlot Slot);

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

	// Equips into the piece's own slot. Runs on the server; clients forward the request.
	UFUNCTION(BlueprintCallable, Category = "Equipment|Armor")
	void RequestEquipArmor(const FCrossguardArmorItem& Item);

	UFUNCTION(BlueprintCallable, Category = "Equipment|Armor")
	void RequestUnequipArmor(ECrossguardArmorSlot Slot);

	UFUNCTION(BlueprintPure, Category = "Equipment|Armor")
	const FCrossguardArmorItem& GetArmorItem(ECrossguardArmorSlot Slot) const;

	// Null if the slot is empty or the piece is not in the table.
	const FCrossguardArmorRow* GetArmorRow(ECrossguardArmorSlot Slot) const;

	UFUNCTION(BlueprintPure, Category = "Equipment|Armor")
	bool SatisfiesArmorRequirement(ECrossguardArmorRequirement Requirement) const;

	// All six slots count ([Cor Leonis]).
	UFUNCTION(BlueprintPure, Category = "Equipment|Armor")
	int32 GetEmptyArmorSlotCount() const;

	UPROPERTY(BlueprintAssignable, Category = "Equipment|Armor")
	FCrossguardArmorEquipRejected OnArmorEquipRejected;

	// Server only. Applies the equipment effect for the current (possibly empty) equipment, so
	// stats like EmptyArmorSlots are right before the first equip. Call after the ability system
	// is initialized.
	void InitializeEquipmentEffect();

	UPROPERTY(BlueprintAssignable, Category = "Equipment")
	FCrossguardEquipmentChanged OnEquipmentChanged;

	UPROPERTY(BlueprintAssignable, Category = "Equipment")
	FCrossguardEquipRejected OnEquipRejected;

protected:
	UFUNCTION(Server, Reliable)
	void ServerEquip(ECrossguardEquipmentSlot Slot, const FCrossguardWeaponItem& Item);

	UFUNCTION(Client, Reliable)
	void ClientEquipRejected(ECrossguardEquipmentSlot Slot, ECrossguardEquipError Error);

	UFUNCTION(Server, Reliable)
	void ServerEquipArmor(ECrossguardArmorSlot Slot, const FCrossguardArmorItem& Item);

	UFUNCTION(Client, Reliable)
	void ClientArmorEquipRejected(ECrossguardArmorSlot Slot, ECrossguardEquipError Error);

	UFUNCTION()
	void OnRep_Equipment();

private:
	// Server only. An empty Item unequips.
	void ApplyEquip(ECrossguardEquipmentSlot Slot, const FCrossguardWeaponItem& Item);
	// Server only. An empty Item unequips Slot.
	void ApplyArmorEquip(ECrossguardArmorSlot Slot, const FCrossguardArmorItem& Item);
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
