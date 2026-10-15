#include "Skills/CrossguardSupportSkills.h"

#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "AbilitySystem/CrossguardAttributeSet.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Character/CrossguardCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/CrossguardTeamLibrary.h"
#include "Skills/CrossguardLightOrb.h"
#include "Skills/CrossguardWall.h"
#include "TimerManager.h"

namespace
{
	// Data/Skills/Skills.json and docs/design-decisions.md (rulings on these skills).
	constexpr float BenedictionRange = 1500.0f;
	constexpr float BenedictionBodyAngleDegrees = 10.0f;
	constexpr float UnleashSeconds = 15.0f;
	constexpr float LightHeight = 120.0f;
	constexpr float WallDistance = 300.0f;
	constexpr float StealthSeconds = 12.0f;
	constexpr float StealthExitSeconds = 3.0f;
	constexpr float StealthExitMoveSpeed = 0.1f;
	constexpr float StealthExitAttackBonus = 0.25f;
	constexpr float ImmutableRange = 1000.0f;
	constexpr float ImmutableSeconds = 5.0f;

	bool IsDead(const AActor* Actor)
	{
		const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor);
		return ASC == nullptr || ASC->HasMatchingGameplayTag(CrossguardTags::State_Dead);
	}
}

// --- Greater Heal ---------------------------------------------------------------------------------

UCrossguardGreaterHealAbility::UCrossguardGreaterHealAbility()
{
	SkillId = TEXT("greater_heal");
	BaseCooldownSeconds = 20.0f;
	CooldownTags.Reset();
	CooldownTags.AddTag(CrossguardTags::Cooldown_Skill_GreaterHeal);
	HealAmount = 55.0f;
}

// --- Benediction ----------------------------------------------------------------------------------

UCrossguardBenedictionAbility::UCrossguardBenedictionAbility()
{
	SkillId = TEXT("benediction");
	SkillCategory = ECrossguardSkillCategory::Spell;
	BaseCooldownSeconds = 120.0f;
	CooldownTags.AddTag(CrossguardTags::Cooldown_Skill_Benediction);
}

ACrossguardCharacter* UCrossguardBenedictionAbility::FindAimedBody(float Range) const
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	UWorld* World = Avatar != nullptr ? Avatar->GetWorld() : nullptr;
	if (World == nullptr)
	{
		return nullptr;
	}
	FVector Eyes;
	FVector Aim;
	GetAim(Eyes, Aim);
	const float MinDot = FMath::Cos(FMath::DegreesToRadians(BenedictionBodyAngleDegrees));

	ACrossguardCharacter* Best = nullptr;
	float BestDot = MinDot;
	for (TActorIterator<ACrossguardCharacter> It(World); It; ++It)
	{
		ACrossguardCharacter* Candidate = *It;
		if (Candidate == Avatar || !IsDead(Candidate) || Candidate->IsPermanentlyDead() || !Candidate->CanBeRevived())
		{
			continue;
		}
		// The ragdoll, not the capsule: the body may have slid.
		const FVector ToBody = Candidate->GetMesh()->GetComponentLocation() - Eyes;
		const float Dot = FVector::DotProduct(Aim, ToBody.GetSafeNormal());
		if (ToBody.SizeSquared() <= FMath::Square(Range) && Dot >= BestDot)
		{
			BestDot = Dot;
			Best = Candidate;
		}
	}
	return Best;
}

void UCrossguardBenedictionAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitOrCancel(Handle, ActorInfo, ActivationInfo))
	{
		return;
	}
	if (ActorInfo->IsNetAuthority())
	{
		// The nearer of a living character under the crosshair and a body near it.
		AActor* const AimedLiving = FindAimedTarget(BenedictionRange);
		AActor* Living = AimedLiving;
		ACrossguardCharacter* Body = FindAimedBody(BenedictionRange);
		const FVector From = ActorInfo->AvatarActor->GetActorLocation();
		if (Living != nullptr && Body != nullptr)
		{
			const bool bBodyNearer = FVector::DistSquared(From, Body->GetMesh()->GetComponentLocation())
				< FVector::DistSquared(From, Living->GetActorLocation());
			if (bBodyNearer)
			{
				Living = nullptr;
			}
			else
			{
				Body = nullptr;
			}
		}
		const bool bRevived = Body != nullptr && Body->Revive(1.0f);
		if (!bRevived)
		{
			// Full HP: heal by Max HP, no healing bonus. A failed revive heals whoever was aimed at.
			UAbilitySystemComponent* TargetASC = AimedLiving != nullptr ? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(AimedLiving)
				: ActorInfo->AbilitySystemComponent.Get();
			if (TargetASC != nullptr)
			{
				const float MaxHealth = TargetASC->GetNumericAttribute(UCrossguardAttributeSet::GetMaxHealthAttribute());
				const FGameplayEffectSpecHandle Spec = MakeHealingSpec(MaxHealth, 0.0f);
				if (Spec.IsValid())
				{
					TargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
				}
			}
		}
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

// --- Unleash --------------------------------------------------------------------------------------

UCrossguardUnleashAbility::UCrossguardUnleashAbility()
{
	SkillId = TEXT("unleash");
	SkillCategory = ECrossguardSkillCategory::Active;
	BaseCooldownSeconds = 90.0f;
	CooldownTags.AddTag(CrossguardTags::Cooldown_Skill_Unleash);
}

void UCrossguardUnleashAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitOrCancel(Handle, ActorInfo, ActivationInfo))
	{
		return;
	}
	if (ActorInfo->IsNetAuthority())
	{
		// Multiplies the current values, gear and other buffs included; derived stats follow.
		UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
		const TMap<FName, float> Doubled = {{TEXT("Strength"), 2.0f}, {TEXT("Agility"), 2.0f}, {TEXT("Intelligence"), 2.0f}};
		ApplyTimedStats(ASC, ASC, Doubled, UnleashSeconds, true);
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

// --- Light ----------------------------------------------------------------------------------------

UCrossguardLightAbility::UCrossguardLightAbility()
{
	SkillId = TEXT("light");
	SkillCategory = ECrossguardSkillCategory::Spell;
	BaseCooldownSeconds = 30.0f;
	CooldownTags.AddTag(CrossguardTags::Cooldown_Skill_Light);
	OrbClass = ACrossguardLightOrb::StaticClass();
}

void UCrossguardLightAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitOrCancel(Handle, ActorInfo, ActivationInfo))
	{
		return;
	}
	AActor* Avatar = ActorInfo->AvatarActor.Get();
	if (ActorInfo->IsNetAuthority() && Avatar != nullptr && OrbClass != nullptr)
	{
		if (ACrossguardLightOrb* Old = Orb.Get())
		{
			Old->Destroy();
		}
		FActorSpawnParameters Params;
		Params.Owner = Avatar;
		ACrossguardLightOrb* NewOrb = Avatar->GetWorld()->SpawnActor<ACrossguardLightOrb>(OrbClass, Avatar->GetActorTransform(), Params);
		if (NewOrb != nullptr)
		{
			NewOrb->AttachToActor(Avatar, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
			NewOrb->SetActorRelativeLocation(FVector(0.0f, 0.0f, LightHeight));
			Orb = NewOrb;
		}
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

// --- Wall -----------------------------------------------------------------------------------------

UCrossguardWallAbility::UCrossguardWallAbility()
{
	SkillId = TEXT("wall");
	SkillCategory = ECrossguardSkillCategory::Spell;
	BaseCooldownSeconds = 10.0f;
	CooldownTags.AddTag(CrossguardTags::Cooldown_Skill_Wall);
	WallClass = ACrossguardWall::StaticClass();
}

void UCrossguardWallAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitOrCancel(Handle, ActorInfo, ActivationInfo))
	{
		return;
	}
	AActor* Avatar = ActorInfo->AvatarActor.Get();
	UWorld* World = Avatar != nullptr ? Avatar->GetWorld() : nullptr;
	if (ActorInfo->IsNetAuthority() && World != nullptr && WallClass != nullptr)
	{
		const FRotator Facing(0.0f, Avatar->GetActorRotation().Yaw, 0.0f);
		const FVector Center = Avatar->GetActorLocation() + Facing.Vector() * WallDistance;
		// On the ground under that point; the wall runs across your view.
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CrossguardWall), false, Avatar);
		// World geometry only, from 2 m above (slopes).
		FHitResult Ground;
		const FVector Feet = World->LineTraceSingleByObjectType(Ground, Center + FVector(0.0f, 0.0f, 200.0f), Center - FVector(0.0f, 0.0f, 500.0f),
			FCollisionObjectQueryParams(ECC_WorldStatic), Params) ? Ground.ImpactPoint : Center - FVector(0.0f, 0.0f, 90.0f);
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = Avatar;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		World->SpawnActor<ACrossguardWall>(WallClass, Feet, Facing + FRotator(0.0f, 90.0f, 0.0f), SpawnParams);
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

// --- Stealth --------------------------------------------------------------------------------------

UCrossguardStealthAbility::UCrossguardStealthAbility()
{
	SkillId = TEXT("stealth");
	SkillCategory = ECrossguardSkillCategory::Active;
	BaseCooldownSeconds = 20.0f;
	CooldownTags.AddTag(CrossguardTags::Cooldown_Skill_Stealth);
}

void UCrossguardStealthAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitOrCancel(Handle, ActorInfo, ActivationInfo))
	{
		return;
	}
	UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
	if (ActorInfo->IsNetAuthority() && ASC != nullptr)
	{
		// The effect carries State.Stealthed to every machine. A little longer than the ability,
		// which removes it on ending.
		const FGameplayEffectSpecHandle Spec = MakeStatusSpec(ASC, CrossguardTags::State_Stealthed, StealthSeconds + 1.0f);
		if (Spec.IsValid())
		{
			StealthHandle = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
		}
		ActivatedHandle = ASC->AbilityActivatedCallbacks.AddUObject(this, &UCrossguardStealthAbility::OnOtherAbilityActivated);
		HealthHandle = ASC->GetGameplayAttributeValueChangeDelegate(UCrossguardAttributeSet::GetHealthAttribute())
			.AddUObject(this, &UCrossguardStealthAbility::OnHealthChanged);
	}
	UAbilityTask_WaitDelay* Expire = UAbilityTask_WaitDelay::WaitDelay(this, StealthSeconds);
	Expire->OnFinish.AddDynamic(this, &UCrossguardStealthAbility::OnStealthExpired);
	Expire->ReadyForActivation();
}

void UCrossguardStealthAbility::OnStealthExpired()
{
	EndOnTimer();
}

void UCrossguardStealthAbility::OnOtherAbilityActivated(UGameplayAbility* Ability)
{
	// Attacking or casting gives you away; passives switching on do not.
	if (Ability != nullptr && Ability != this && !Ability->IsA<UCrossguardStealthAbility>() && !Ability->IsA<UCrossguardPassiveAbility>())
	{
		BreakStealth();
	}
}

void UCrossguardStealthAbility::OnHealthChanged(const FOnAttributeChangeData& Data)
{
	if (Data.NewValue <= 0.0f && IsActive())
	{
		// A killing blow: ends as cancelled (no exit bonus), before HandleDeath cancels it too.
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, true);
	}
	else if (Data.NewValue < Data.OldValue)
	{
		// Next tick: a hit's own Mute ([Purge Shot]) lands just after its damage, and then counts
		// as a Mute ending (no exit bonus).
		GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]() { BreakStealth(); }));
	}
}

void UCrossguardStealthAbility::BreakStealth()
{
	if (IsActive())
	{
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
	}
}

void UCrossguardStealthAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	UAbilitySystemComponent* ASC = ActorInfo != nullptr ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (ASC != nullptr && ActorInfo->IsNetAuthority())
	{
		ASC->AbilityActivatedCallbacks.Remove(ActivatedHandle);
		ASC->GetGameplayAttributeValueChangeDelegate(UCrossguardAttributeSet::GetHealthAttribute()).Remove(HealthHandle);
		ActivatedHandle.Reset();
		HealthHandle.Reset();
	}
	if (ASC != nullptr && ActorInfo->IsNetAuthority() && StealthHandle.IsValid())
	{
		ASC->RemoveActiveGameplayEffect(StealthHandle);
		StealthHandle.Invalidate();
		// The exit bonus, unless death or a Mute ended it. A killing or muting hit breaks stealth
		// before the death or Mute lands, so check the state as well as how it ended.
		const bool bDeadOrMuted = ASC->HasMatchingGameplayTag(CrossguardTags::State_Dead) || ASC->HasMatchingGameplayTag(CrossguardTags::State_Muted);
		if (!bWasCancelled && !bDeadOrMuted)
		{
			const TMap<FName, float> Bonus = {{TEXT("MoveSpeed"), StealthExitMoveSpeed}, {TEXT("AttackDamageBonus"), StealthExitAttackBonus}};
			ApplyTimedStats(ASC, ASC, Bonus, StealthExitSeconds, false);
		}
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

// --- Immutable ------------------------------------------------------------------------------------

UCrossguardImmutableAbility::UCrossguardImmutableAbility()
{
	SkillId = TEXT("immutable");
	SkillCategory = ECrossguardSkillCategory::Active;
	BaseCooldownSeconds = 60.0f;
	CooldownTags.AddTag(CrossguardTags::Cooldown_Skill_Immutable);
}

void UCrossguardImmutableAbility::OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	Super::OnGiveAbility(ActorInfo, Spec);
	// The always-on part (server; immunity is only checked there).
	if (ActorInfo != nullptr && ActorInfo->IsNetAuthority() && ActorInfo->AbilitySystemComponent.IsValid())
	{
		ActorInfo->AbilitySystemComponent->AddLooseGameplayTag(CrossguardTags::Passive_Immutable);
	}
}

void UCrossguardImmutableAbility::OnRemoveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	if (ActorInfo != nullptr && ActorInfo->IsNetAuthority() && ActorInfo->AbilitySystemComponent.IsValid())
	{
		ActorInfo->AbilitySystemComponent->RemoveLooseGameplayTag(CrossguardTags::Passive_Immutable);
	}
	Super::OnRemoveAbility(ActorInfo, Spec);
}

void UCrossguardImmutableAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitOrCancel(Handle, ActorInfo, ActivationInfo))
	{
		return;
	}
	AActor* Avatar = ActorInfo->AvatarActor.Get();
	UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
	if (ActorInfo->IsNetAuthority() && Avatar != nullptr && ASC != nullptr)
	{
		for (TActorIterator<ACrossguardCharacter> It(Avatar->GetWorld()); It; ++It)
		{
			ACrossguardCharacter* Candidate = *It;
			const bool bSide = Candidate == Avatar || UCrossguardTeamLibrary::AreAllies(Avatar, Candidate);
			if (bSide && !IsDead(Candidate) && FVector::Dist(Avatar->GetActorLocation(), Candidate->GetActorLocation()) <= ImmutableRange)
			{
				ApplyStatus(ASC, Candidate->GetAbilitySystemComponent(), CrossguardTags::State_CCImmune, ImmutableSeconds);
			}
		}
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

// --- Splinter of Winter ---------------------------------------------------------------------------

UCrossguardSplinterOfWinterAbility::UCrossguardSplinterOfWinterAbility()
{
	SkillId = TEXT("splinter_of_winter");
	// Active while equipped (passives activate on the server when granted).
	ActivationOwnedTags.AddTag(CrossguardTags::Passive_SplinterOfWinter);
}
