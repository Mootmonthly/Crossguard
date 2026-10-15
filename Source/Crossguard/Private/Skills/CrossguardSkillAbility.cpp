#include "Skills/CrossguardSkillAbility.h"

#include "AbilitySystem/CrossguardEffects.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Engine/World.h"

FGameplayEffectSpecHandle UCrossguardSkillAbility::MakeDamageSpec(float AttackDamage, float MagicDamage, float Coefficient) const
{
	FGameplayEffectSpecHandle Spec = MakeOutgoingGameplayEffectSpec(UCrossguardDamageEffect::StaticClass(), GetAbilityLevel());
	if (Spec.IsValid())
	{
		Spec.Data->SetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_Attack, AttackDamage);
		Spec.Data->SetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_Magic, MagicDamage);
		Spec.Data->SetSetByCallerMagnitude(CrossguardTags::SetByCaller_Coefficient, Coefficient);
	}
	return Spec;
}

FGameplayEffectSpecHandle UCrossguardSkillAbility::MakeHealingSpec(float Healing, float Coefficient) const
{
	FGameplayEffectSpecHandle Spec = MakeOutgoingGameplayEffectSpec(UCrossguardHealingEffect::StaticClass(), GetAbilityLevel());
	if (Spec.IsValid())
	{
		Spec.Data->SetSetByCallerMagnitude(CrossguardTags::SetByCaller_Healing_Magical, Healing);
		Spec.Data->SetSetByCallerMagnitude(CrossguardTags::SetByCaller_Coefficient, Coefficient);
	}
	return Spec;
}

void UCrossguardSkillAbility::ApplyStatus(UAbilitySystemComponent* Source, UAbilitySystemComponent* Target, FGameplayTag State, float Seconds)
{
	if (Source == nullptr || Target == nullptr || Seconds <= 0.0f)
	{
		return;
	}
	const FGameplayEffectSpecHandle Spec = Source->MakeOutgoingSpec(UCrossguardStatusEffect::StaticClass(), 1.0f, Source->MakeEffectContext());
	if (Spec.IsValid())
	{
		Spec.Data->SetSetByCallerMagnitude(CrossguardSetByCaller::Duration, Seconds);
		Spec.Data->DynamicGrantedTags.AddTag(State);
		Target->ApplyGameplayEffectSpecToSelf(*Spec.Data);
	}
}

void UCrossguardSkillAbility::ApplySlow(UAbilitySystemComponent* Source, UAbilitySystemComponent* Target, float Fraction, float Seconds)
{
	if (Source == nullptr || Target == nullptr || Seconds <= 0.0f)
	{
		return;
	}
	const FGameplayEffectSpecHandle Spec = Source->MakeOutgoingSpec(UCrossguardTimedStatsEffect::StaticClass(), 1.0f, Source->MakeEffectContext());
	if (!Spec.IsValid())
	{
		return;
	}
	for (const FName& Name : UCrossguardSkillStatsEffect::GetStatNames())
	{
		Spec.Data->SetSetByCallerMagnitude(Name, 0.0f);
	}
	Spec.Data->SetSetByCallerMagnitude(FName(TEXT("MoveSpeed")), -Fraction);
	Spec.Data->SetSetByCallerMagnitude(CrossguardSetByCaller::Duration, Seconds);
	Spec.Data->DynamicGrantedTags.AddTag(CrossguardTags::State_Slowed);
	Target->ApplyGameplayEffectSpecToSelf(*Spec.Data);
}

void UCrossguardSkillAbility::GetAim(FVector& OutLocation, FVector& OutDirection) const
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	FRotator Rotation = FRotator::ZeroRotator;
	OutLocation = FVector::ZeroVector;
	if (Avatar != nullptr)
	{
		Avatar->GetActorEyesViewPoint(OutLocation, Rotation);
	}
	OutDirection = Rotation.Vector();
}

AActor* UCrossguardSkillAbility::FindAimedTarget(float Range) const
{
	AActor* Avatar = GetAvatarActorFromActorInfo();
	UWorld* World = Avatar != nullptr ? Avatar->GetWorld() : nullptr;
	if (World == nullptr)
	{
		return nullptr;
	}
	FVector Start;
	FVector Direction;
	GetAim(Start, Direction);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(CrossguardAimedSkill), false, Avatar);
	TArray<FHitResult> Hits;
	// Pawns along the line; the first one with an ability system is the target.
	World->SweepMultiByObjectType(Hits, Start, Start + Direction * Range, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeSphere(20.0f), Params);
	for (const FHitResult& Hit : Hits)
	{
		AActor* Candidate = Hit.GetActor();
		if (Candidate != nullptr && Candidate != Avatar && UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Candidate) != nullptr)
		{
			return Candidate;
		}
	}
	return nullptr;
}

bool UCrossguardSkillAbility::CommitOrCancel(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return false;
	}
	return true;
}
