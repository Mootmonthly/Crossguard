#include "AbilitySystem/CrossguardGameplayAbility.h"

#include "AbilitySystem/CrossguardGameplayTags.h"
#include "CrossguardRules/Combat.h"

UCrossguardGameplayAbility::UCrossguardGameplayAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

const UCrossguardAbilitySystemComponent* UCrossguardGameplayAbility::GetCrossguardAbilitySystemComponent() const
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	return ActorInfo != nullptr ? Cast<UCrossguardAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get()) : nullptr;
}

float UCrossguardGameplayAbility::ReducedCooldownFor(const UCrossguardAbilitySystemComponent* ASC) const
{
	if (ASC == nullptr)
	{
		return BaseCooldownSeconds;
	}
	const float Reduction = ASC->GetCooldownReduction(SkillCategory, GetAssetTags());
	return static_cast<float>(Crossguard::ApplyReduction(BaseCooldownSeconds, Reduction));
}

float UCrossguardGameplayAbility::GetReducedCooldown() const
{
	return ReducedCooldownFor(GetCrossguardAbilitySystemComponent());
}

float UCrossguardGameplayAbility::GetReducedCastTime() const
{
	const UCrossguardAbilitySystemComponent* ASC = GetCrossguardAbilitySystemComponent();
	if (ASC == nullptr)
	{
		return BaseCastTimeSeconds;
	}
	return static_cast<float>(Crossguard::ApplyReduction(BaseCastTimeSeconds, ASC->GetCastTimeReduction(SkillCategory)));
}

const FGameplayTagContainer* UCrossguardGameplayAbility::GetCooldownTags() const
{
	// Scratch container; const_cast follows the standard GAS pattern for dynamic cooldown tags.
	FGameplayTagContainer* Combined = const_cast<FGameplayTagContainer*>(&CombinedCooldownTags);
	Combined->Reset();
	if (const FGameplayTagContainer* ParentTags = Super::GetCooldownTags())
	{
		Combined->AppendTags(*ParentTags);
	}
	Combined->AppendTags(CooldownTags);
	return Combined;
}

void UCrossguardGameplayAbility::ApplyCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo) const
{
	const UGameplayEffect* CooldownEffect = GetCooldownGameplayEffect();
	if (CooldownEffect == nullptr || BaseCooldownSeconds <= 0.0f)
	{
		return;
	}

	const FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(CooldownEffect->GetClass(), GetAbilityLevel());
	if (!SpecHandle.IsValid())
	{
		return;
	}
	SpecHandle.Data->DynamicGrantedTags.AppendTags(CooldownTags);
	const UCrossguardAbilitySystemComponent* ASC = ActorInfo != nullptr
		? Cast<UCrossguardAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get())
		: nullptr;
	SpecHandle.Data->SetSetByCallerMagnitude(CrossguardTags::SetByCaller_Cooldown, ReducedCooldownFor(ASC));
	ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, SpecHandle);
}
