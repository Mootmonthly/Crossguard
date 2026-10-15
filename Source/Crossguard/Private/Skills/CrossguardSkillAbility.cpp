#include "Skills/CrossguardSkillAbility.h"

#include "AbilitySystem/CrossguardAttributeSet.h"
#include "AbilitySystem/CrossguardEffects.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Character/CrossguardAICharacter.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"

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

FGameplayEffectSpecHandle UCrossguardSkillAbility::MakeStatusSpec(UAbilitySystemComponent* Source, FGameplayTag State, float Seconds)
{
	if (Source == nullptr || Seconds <= 0.0f)
	{
		return FGameplayEffectSpecHandle();
	}
	FGameplayEffectSpecHandle Spec = Source->MakeOutgoingSpec(UCrossguardStatusEffect::StaticClass(), 1.0f, Source->MakeEffectContext());
	if (Spec.IsValid())
	{
		Spec.Data->SetSetByCallerMagnitude(CrossguardSetByCaller::Duration, Seconds);
		Spec.Data->DynamicGrantedTags.AddTag(State);
	}
	return Spec;
}

FGameplayEffectSpecHandle UCrossguardSkillAbility::MakeSlowSpec(UAbilitySystemComponent* Source, float Fraction, float Seconds)
{
	if (Source == nullptr || Seconds <= 0.0f)
	{
		return FGameplayEffectSpecHandle();
	}
	FGameplayEffectSpecHandle Spec = Source->MakeOutgoingSpec(UCrossguardTimedStatsEffect::StaticClass(), 1.0f, Source->MakeEffectContext());
	if (Spec.IsValid())
	{
		for (const FName& Name : UCrossguardSkillStatsEffect::GetStatNames())
		{
			Spec.Data->SetSetByCallerMagnitude(Name, 0.0f);
		}
		Spec.Data->SetSetByCallerMagnitude(FName(TEXT("MoveSpeed")), -Fraction);
		Spec.Data->SetSetByCallerMagnitude(CrossguardSetByCaller::Duration, Seconds);
		Spec.Data->DynamicGrantedTags.AddTag(CrossguardTags::State_Slowed);
	}
	return Spec;
}

FGameplayEffectSpecHandle UCrossguardSkillAbility::MakeDamageOverTimeSpec(UAbilitySystemComponent* Source, float AttackPerSecond,
	float MagicPerSecond, float Seconds)
{
	if (Source == nullptr || Seconds <= 0.0f)
	{
		return FGameplayEffectSpecHandle();
	}
	FGameplayEffectSpecHandle Spec = Source->MakeOutgoingSpec(UCrossguardMuteEffect::StaticClass(), 1.0f, Source->MakeEffectContext());
	if (Spec.IsValid())
	{
		Spec.Data->SetSetByCallerMagnitude(CrossguardSetByCaller::Duration, Seconds);
		Spec.Data->SetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_Attack, AttackPerSecond);
		Spec.Data->SetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_Magic, MagicPerSecond);
		Spec.Data->SetSetByCallerMagnitude(CrossguardTags::SetByCaller_Coefficient, 1.0f);
	}
	return Spec;
}

FGameplayEffectSpecHandle UCrossguardSkillAbility::MakeMuteSpec(UAbilitySystemComponent* Source, float Seconds, float DamagePerSecond)
{
	FGameplayEffectSpecHandle Spec = MakeDamageOverTimeSpec(Source, 0.0f, DamagePerSecond, Seconds);
	if (Spec.IsValid())
	{
		Spec.Data->DynamicGrantedTags.AddTag(CrossguardTags::State_Muted);
	}
	return Spec;
}

bool UCrossguardSkillAbility::IsCrowdControl(const FGameplayTag& State)
{
	return State == CrossguardTags::State_Slowed || State == CrossguardTags::State_Blinded || State == CrossguardTags::State_KnockedUp
		|| State == CrossguardTags::State_Silenced;
}

bool UCrossguardSkillAbility::IsCrowdControlImmune(const UAbilitySystemComponent* Target)
{
	return Target != nullptr
		&& (Target->HasMatchingGameplayTag(CrossguardTags::State_CCImmune)
			|| (Target->HasMatchingGameplayTag(CrossguardTags::Passive_Immutable) && !Target->HasMatchingGameplayTag(CrossguardTags::State_Muted)));
}

void UCrossguardSkillAbility::ApplyOnHitSpec(const FGameplayEffectSpecHandle& Spec, UAbilitySystemComponent* Target)
{
	if (!Spec.IsValid() || Target == nullptr)
	{
		return;
	}
	if (IsCrowdControlImmune(Target))
	{
		for (const FGameplayTag& Granted : Spec.Data->DynamicGrantedTags)
		{
			if (IsCrowdControl(Granted))
			{
				return;
			}
		}
	}
	Target->ApplyGameplayEffectSpecToSelf(*Spec.Data);
}

void UCrossguardSkillAbility::ApplyStatus(UAbilitySystemComponent* Source, UAbilitySystemComponent* Target, FGameplayTag State, float Seconds)
{
	ApplyOnHitSpec(MakeStatusSpec(Source, State, Seconds), Target);
}

void UCrossguardSkillAbility::ApplySlow(UAbilitySystemComponent* Source, UAbilitySystemComponent* Target, float Fraction, float Seconds)
{
	ApplyOnHitSpec(MakeSlowSpec(Source, Fraction, Seconds), Target);
}

FActiveGameplayEffectHandle UCrossguardSkillAbility::ApplyTimedStats(UAbilitySystemComponent* Source, UAbilitySystemComponent* Target,
	const TMap<FName, float>& Values, float Seconds, bool bMultiply, FGameplayTag State)
{
	if (Source == nullptr || Target == nullptr || Seconds <= 0.0f)
	{
		return FActiveGameplayEffectHandle();
	}
	const TSubclassOf<UGameplayEffect> EffectClass = bMultiply ? TSubclassOf<UGameplayEffect>(UCrossguardTimedMultiplierEffect::StaticClass())
		: TSubclassOf<UGameplayEffect>(UCrossguardTimedStatsEffect::StaticClass());
	const FGameplayEffectSpecHandle Spec = Source->MakeOutgoingSpec(EffectClass, 1.0f, Source->MakeEffectContext());
	if (!Spec.IsValid())
	{
		return FActiveGameplayEffectHandle();
	}
	for (const FName& Name : UCrossguardSkillStatsEffect::GetStatNames())
	{
		const float* Value = Values.Find(Name);
		Spec.Data->SetSetByCallerMagnitude(Name, Value != nullptr ? *Value : (bMultiply ? 1.0f : 0.0f));
	}
	Spec.Data->SetSetByCallerMagnitude(CrossguardSetByCaller::Duration, Seconds);
	if (State.IsValid())
	{
		Spec.Data->DynamicGrantedTags.AddTag(State);
	}
	return Target->ApplyGameplayEffectSpecToSelf(*Spec.Data);
}

void UCrossguardSkillAbility::CancelActiveSkills(UAbilitySystemComponent* ASC)
{
	if (ASC == nullptr)
	{
		return;
	}
	// Collected first: ending Dragonification removes the skills it granted.
	TArray<FGameplayAbilitySpecHandle> Running;
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.IsActive() && Spec.Ability != nullptr && Spec.Ability->IsA<UCrossguardSkillAbility>())
		{
			Running.Add(Spec.Handle);
		}
	}
	for (const FGameplayAbilitySpecHandle& Handle : Running)
	{
		ASC->CancelAbilityHandle(Handle);
	}
}

void UCrossguardSkillAbility::EndOnTimer()
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	if (ActorInfo == nullptr || !IsActive())
	{
		return;
	}
	EndAbility(GetCurrentAbilitySpecHandle(), ActorInfo, GetCurrentActivationInfo(), ActorInfo->IsNetAuthority(), false);
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
	// AI: straight at the target its skill was used on (TryUseSkills), not where its view lags.
	const ACrossguardAICharacter* AI = Cast<ACrossguardAICharacter>(Avatar);
	const AActor* AimTarget = AI != nullptr ? AI->GetSkillAimTarget() : nullptr;
	if (AimTarget != nullptr && AimTarget != Avatar)
	{
		OutDirection = (AimTarget->GetActorLocation() - OutLocation).GetSafeNormal();
	}
}

AActor* UCrossguardSkillAbility::FindAimedTarget(float Range) const
{
	AActor* Avatar = GetAvatarActorFromActorInfo();
	UWorld* World = Avatar != nullptr ? Avatar->GetWorld() : nullptr;
	if (World == nullptr)
	{
		return nullptr;
	}
	// AI aiming at itself (heals): nobody under the crosshair.
	if (const ACrossguardAICharacter* AI = Cast<ACrossguardAICharacter>(Avatar); AI != nullptr && AI->GetSkillAimTarget() == Avatar)
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
		const UAbilitySystemComponent* CandidateASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Candidate);
		if (Candidate != nullptr && Candidate != Avatar && CandidateASC != nullptr && !CandidateASC->HasMatchingGameplayTag(CrossguardTags::State_Dead))
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

bool UCrossguardSkillAbility::AIHasInRange(const ACrossguardAICharacter& User, const AActor& Target, float Range, bool bNeedsSight)
{
	if (FVector::Dist(User.GetActorLocation(), Target.GetActorLocation()) > Range)
	{
		return false;
	}
	const AController* Controller = User.GetController();
	return !bNeedsSight || (Controller != nullptr && Controller->LineOfSightTo(&Target));
}

bool UCrossguardSkillAbility::AIIsHurt(const ACrossguardAICharacter& User, float Fraction)
{
	const UAbilitySystemComponent* ASC = User.GetAbilitySystemComponent();
	if (ASC == nullptr)
	{
		return false;
	}
	const float MaxHealth = ASC->GetNumericAttribute(UCrossguardAttributeSet::GetMaxHealthAttribute());
	return MaxHealth > 0.0f && ASC->GetNumericAttribute(UCrossguardAttributeSet::GetHealthAttribute()) <= MaxHealth * Fraction;
}
