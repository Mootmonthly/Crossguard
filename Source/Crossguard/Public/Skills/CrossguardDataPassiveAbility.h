#pragma once

#include "AbilitySystem/CrossguardPassiveAbility.h"
#include "CoreMinimal.h"

#include "CrossguardDataPassiveAbility.generated.h"

class UCrossguardSkillCatalog;

// Which skill a UCrossguardDataPassiveAbility plays: the source object of its ability spec.
UCLASS()
class CROSSGUARD_API UCrossguardSkillGrant : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FName SkillId;

	UPROPERTY()
	TObjectPtr<UCrossguardSkillCatalog> Catalog;
};

// A Passive skill built from its data row: no hand-made ability or effect needed. The loadout
// grants it for every Passive skill without an ability class in the catalog.
//
// Applies the row's Self stat modifiers through the native stat effects, and re-applies them
// whenever the equipment changes:
// - Wielding conditions (masteries) count only while one of the weapon types is held.
// - Armor conditions count only while the armor requirement is met.
// - PerEmptyArmorSlot values ([Cor Leonis]) are multiplied by the empty armor slots.
// - Flat and Percent add to the attribute; Multiply multiplies it.
// - CDR with a Melee / Ranged / Minion filter goes to that CDR; with a category filter, to that
//   category; with no filter, to all three categories. Cast time likewise, without Melee / Ranged /
//   Minion.
//
// Not covered (logged once): modifiers that target others (Party, Target, AlliesInRadius),
// on-activation or next-attack conditions, free-text conditions, and the base-amount stats (AD,
// MD, PH, MH). Those skills still need a hand-made ability.
UCLASS()
class CROSSGUARD_API UCrossguardDataPassiveAbility : public UCrossguardPassiveAbility
{
	GENERATED_BODY()

public:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	UFUNCTION()
	void Refresh();

	void RemoveStatEffects();

	FName PlayedSkillId;
	TArray<FActiveGameplayEffectHandle> StatEffects;
};
