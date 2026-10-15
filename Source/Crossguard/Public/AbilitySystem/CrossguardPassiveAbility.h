#pragma once

#include "AbilitySystem/CrossguardGameplayAbility.h"
#include "CoreMinimal.h"
#include "GameplayEffectTypes.h"

#include "CrossguardPassiveAbility.generated.h"

class UGameplayEffect;

// Base class for Passive skills (masteries, Heroic Might, Antimagic Garb, ...).
//
// Activates on the server as soon as it is granted and stays active. While active it applies
// PassiveEffects to the owner (Infinite-duration effects holding the stat modifiers); they are
// removed when the ability ends, e.g. when the skill is unequipped.
UCLASS(Abstract)
class CROSSGUARD_API UCrossguardPassiveAbility : public UCrossguardGameplayAbility
{
	GENERATED_BODY()

public:
	UCrossguardPassiveAbility();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Passive")
	TArray<TSubclassOf<UGameplayEffect>> PassiveEffects;

	virtual void OnAvatarSet(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	TArray<FActiveGameplayEffectHandle> AppliedEffects;
};
