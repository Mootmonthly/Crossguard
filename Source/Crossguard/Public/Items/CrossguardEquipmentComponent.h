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

// Main-hand and off-hand weapons (docs/item-data-schema.md, "Equipment slots"). Dual wielding:
// any one-handed weapon fits the off hand.
//
// The server validates every change, then re-applies one UCrossguardEquipmentEffect holding the
// items' stats (shield AR) and the Equipment.Wielding.* tags that mastery effects and skill
// requirements check.
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

	UPROPERTY(BlueprintAssignable, Category = "Equipment")
	FCrossguardEquipmentChanged OnEquipmentChanged;

	UPROPERTY(BlueprintAssignable, Category = "Equipment")
	FCrossguardEquipRejected OnEquipRejected;

protected:
	UFUNCTION(Server, Reliable)
	void ServerEquip(ECrossguardEquipmentSlot Slot, const FCrossguardWeaponItem& Item);

	UFUNCTION(Client, Reliable)
	void ClientEquipRejected(ECrossguardEquipmentSlot Slot, ECrossguardEquipError Error);

	UFUNCTION()
	void OnRep_Equipment();

private:
	// Server only. An empty Item unequips.
	void ApplyEquip(ECrossguardEquipmentSlot Slot, const FCrossguardWeaponItem& Item);
	void RefreshEquipmentEffect();

	const FCrossguardWeaponRow* FindWeapon(FName WeaponId) const;
	Crossguard::FEquipment BuildRulesEquipment(const FCrossguardWeaponItem& Main, const FCrossguardWeaponItem& Off) const;
	UCrossguardAbilitySystemComponent* GetAbilitySystem() const;

	UPROPERTY(ReplicatedUsing = OnRep_Equipment)
	FCrossguardWeaponItem MainHand;

	UPROPERTY(ReplicatedUsing = OnRep_Equipment)
	FCrossguardWeaponItem OffHand;

	// Server only.
	FActiveGameplayEffectHandle EquipmentEffectHandle;
};
