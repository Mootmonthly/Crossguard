#include "AbilitySystem/CrossguardGameplayAbility.h"

#include "AbilitySystem/CrossguardEffects.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "Character/CrossguardAICharacter.h"
#include "Items/CrossguardEquipmentComponent.h"
#include "CrossguardRules/Combat.h"

UCrossguardGameplayAbility::UCrossguardGameplayAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	CooldownGameplayEffectClass = UCrossguardCooldownEffect::StaticClass();
	// No casting while dead, Muted, Silenced, Stunned, or in dragon form (Dragonification's own skills opt
	// back in).
	ActivationBlockedTags.AddTag(CrossguardTags::State_Dead);
	ActivationBlockedTags.AddTag(CrossguardTags::State_Muted);
	ActivationBlockedTags.AddTag(CrossguardTags::State_Silenced);
	ActivationBlockedTags.AddTag(CrossguardTags::State_Stunned);
	ActivationBlockedTags.AddTag(CrossguardTags::State_Dragonified);
}

bool UCrossguardGameplayAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}
	// An AI's skills come with it (a summon's, a monster's): no weapon or armor requirements.
	if (ActorInfo != nullptr && Cast<ACrossguardAICharacter>(ActorInfo->AvatarActor.Get()) != nullptr)
	{
		return true;
	}
	// Every Spell needs a casting weapon in hand (a Wand or a Two-Handed Staff), except in dragon
	// form, where Flare Breath and Flight are cast without hands.
	TArray<ECrossguardWeaponType> Weapons = RequiredWeapons;
	const UAbilitySystemComponent* ASC = ActorInfo != nullptr ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (SkillCategory == ECrossguardSkillCategory::Spell && Weapons.IsEmpty()
		&& (ASC == nullptr || !ASC->HasMatchingGameplayTag(CrossguardTags::State_Dragonified)))
	{
		Weapons = {ECrossguardWeaponType::Wand, ECrossguardWeaponType::MagicStaff};
	}
	if (Weapons.IsEmpty() && RequiredArmor == ECrossguardArmorRequirement::None)
	{
		return true;
	}
	const AActor* Avatar = ActorInfo != nullptr ? ActorInfo->AvatarActor.Get() : nullptr;
	const UCrossguardEquipmentComponent* Equipment = Avatar != nullptr ? Avatar->FindComponentByClass<UCrossguardEquipmentComponent>() : nullptr;
	return Equipment != nullptr && Equipment->SatisfiesWeaponRequirement(Weapons) && Equipment->SatisfiesArmorRequirement(RequiredArmor);
}

const UCrossguardAbilitySystemComponent* UCrossguardGameplayAbility::GetCrossguardAbilitySystemComponent() const
{
	// GetCurrentActorInfo asserts on non-instanced abilities such as the class default object.
	if (!IsInstantiated())
	{
		return nullptr;
	}
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
	SpecHandle.Data->SetSetByCallerMagnitude(CrossguardSetByCaller::Cooldown, ReducedCooldownFor(ASC));
	ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, SpecHandle);
}
