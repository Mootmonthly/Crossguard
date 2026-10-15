#pragma once

#include "AbilitySystem/CrossguardGameplayAbility.h"
#include "CoreMinimal.h"
#include "Items/CrossguardItemTypes.h"

#include "CrossguardWeaponAttackAbility.generated.h"

class ACrossguardProjectile;
struct FCrossguardWeaponRow;
struct FCrossguardWeaponItem;

// Basic attack with the weapon in Slot (left click: main hand; right click when dual wielding:
// off hand, see UCrossguardOffHandAttackAbility). Each hand keeps its own attack interval. Damage = the weapon's AD or MD at its rarity, through the
// damage formula. The ability stays active for the attack interval (weapon speed after Action
// Speed), which is what limits the attack rate.
//
// Delivery by weapon:
// - MeleeSwing: sphere sweep in front of the character; hits every actor with an ability system.
// - Hitscan: line trace from the eyes (wand).
// - Projectile: spawns ProjectileClasses[weapon id], or DefaultProjectileClass (bow, staff).
//
// Weapons with an AmmoId (bows: "arrow") use up one per attack from the inventory, Quiver first,
// and cannot attack without it. Characters without an inventory (AI) shoot freely.
//
// Hit detection runs on the server. Placeholder until animations and weapon collision exist.
UCLASS()
class CROSSGUARD_API UCrossguardWeaponAttackAbility : public UCrossguardGameplayAbility
{
	GENERATED_BODY()

public:
	UCrossguardWeaponAttackAbility();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Attack")
	ECrossguardEquipmentSlot Slot = ECrossguardEquipmentSlot::MainHand;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Attack", meta = (ClampMin = "0"))
	float MeleeRange = 220.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Attack", meta = (ClampMin = "0"))
	float MeleeRadius = 60.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Attack", meta = (ClampMin = "0"))
	float HitscanRange = 5000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Attack")
	TSubclassOf<ACrossguardProjectile> DefaultProjectileClass;

	// Weapon id -> projectile (e.g. "bow" -> fast arrow, "staff_two_handed" -> slower bolt).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Attack")
	TMap<FName, TSubclassOf<ACrossguardProjectile>> ProjectileClasses;

	// Seconds trimmed from the attack recovery on the owning client so a held attack button is not
	// rejected by a server that still sees the previous attack active.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Attack", meta = (ClampMin = "0"))
	float ClientRecoveryTolerance = 0.05f;

	// Fails without an attacking weapon in Slot, so the client never predicts a doomed attack.
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

private:
	FGameplayEffectSpecHandle MakeWeaponDamageSpec(const FCrossguardWeaponRow& Weapon, const FCrossguardWeaponItem& Item) const;
	void PerformMeleeSwing(const FGameplayEffectSpecHandle& Spec) const;
	void PerformHitscan(const FGameplayEffectSpecHandle& Spec) const;
	void SpawnProjectile(const FGameplayEffectSpecHandle& Spec, FName WeaponId) const;
	void ApplyDamageTo(AActor* Target, const FGameplayEffectSpecHandle& Spec) const;

	UFUNCTION()
	void OnAttackRecovered();
};

// Off-hand basic attack (dual wielding).
UCLASS()
class CROSSGUARD_API UCrossguardOffHandAttackAbility : public UCrossguardWeaponAttackAbility
{
	GENERATED_BODY()

public:
	UCrossguardOffHandAttackAbility();
};
