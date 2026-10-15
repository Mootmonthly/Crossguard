#include "AbilitySystem/CrossguardPassiveAbility.h"

#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"

UCrossguardPassiveAbility::UCrossguardPassiveAbility()
{
	SkillCategory = ECrossguardSkillCategory::Passive;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

void UCrossguardPassiveAbility::TryActivatePassive(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) const
{
	// Once, on the server, with an avatar. Both hooks below may call this; IsActive guards repeats.
	if (ActorInfo != nullptr && ActorInfo->IsNetAuthority() && ActorInfo->AvatarActor.IsValid()
		&& ActorInfo->AbilitySystemComponent.IsValid() && !Spec.IsActive())
	{
		ActorInfo->AbilitySystemComponent->TryActivateAbility(Spec.Handle, false);
	}
}

void UCrossguardPassiveAbility::OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	Super::OnGiveAbility(ActorInfo, Spec);
	TryActivatePassive(ActorInfo, Spec);
}

void UCrossguardPassiveAbility::OnAvatarSet(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	Super::OnAvatarSet(ActorInfo, Spec);
	TryActivatePassive(ActorInfo, Spec);
}

void UCrossguardPassiveAbility::OnRemoveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	// Backstop if the ability is removed without ending.
	RemoveAppliedEffects(ActorInfo);
	Super::OnRemoveAbility(ActorInfo, Spec);
}

void UCrossguardPassiveAbility::RemoveAppliedEffects(const FGameplayAbilityActorInfo* ActorInfo)
{
	if (ActorInfo != nullptr && ActorInfo->AbilitySystemComponent.IsValid())
	{
		for (const FActiveGameplayEffectHandle& Applied : AppliedEffects)
		{
			ActorInfo->AbilitySystemComponent->RemoveActiveGameplayEffect(Applied);
		}
	}
	AppliedEffects.Reset();
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
	RemoveAppliedEffects(ActorInfo);
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
