#pragma once

#include "AbilitySystem/CrossguardGameplayAbility.h"
#include "CoreMinimal.h"

#include "CrossguardWeaponAttackAbility.generated.h"

class ACrossguardProjectile;
struct FCrossguardWeaponRow;
struct FCrossguardWeaponItem;

// Basic attack with the main-hand weapon. Damage = the weapon's AD or MD at its rarity, through the
// damage formula. The ability stays active for the attack interval (weapon speed after Action
// Speed), which is what limits the attack rate.
//
// Delivery by weapon:
// - MeleeSwing: sphere sweep in front of the character; hits every actor with an ability system.
// - Hitscan: line trace from the eyes (wand).
// - Projectile: spawns ProjectileClasses[weapon id], or DefaultProjectileClass (bow, staff).
//
// Hit detection runs on the server. Placeholder until animations and weapon collision exist.
UCLASS()
class CROSSGUARD_API UCrossguardWeaponAttackAbility : public UCrossguardGameplayAbility
{
	GENERATED_BODY()

public:
	UCrossguardWeaponAttackAbility();

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
