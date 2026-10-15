#include "AbilitySystem/CrossguardPassiveAbility.h"

#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"

UCrossguardPassiveAbility::UCrossguardPassiveAbility()
{
	SkillCategory = ECrossguardSkillCategory::Passive;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

void UCrossguardPassiveAbility::OnAvatarSet(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	Super::OnAvatarSet(ActorInfo, Spec);

	// Called when granted (and when the avatar changes). Activate once, on the server.
	if (ActorInfo != nullptr && ActorInfo->IsNetAuthority() && !Spec.IsActive())
	{
		ActorInfo->AbilitySystemComponent->TryActivateAbility(Spec.Handle, false);
	}
}

void UCrossguardPassiveAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, false, true);
		return;
	}

	for (const TSubclassOf<UGameplayEffect>& EffectClass : PassiveEffects)
	{
		if (EffectClass == nullptr)
		{
			continue;
		}
		const FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(EffectClass, GetAbilityLevel());
		const FActiveGameplayEffectHandle Applied = ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, SpecHandle);
		if (Applied.IsValid())
		{
			AppliedEffects.Add(Applied);
		}
	}
	// Stays active until unequipped.
}

void UCrossguardPassiveAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (ActorInfo != nullptr && ActorInfo->AbilitySystemComponent.IsValid())
	{
		for (const FActiveGameplayEffectHandle& Applied : AppliedEffects)
		{
			ActorInfo->AbilitySystemComponent->RemoveActiveGameplayEffect(Applied);
		}
	}
	AppliedEffects.Reset();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
