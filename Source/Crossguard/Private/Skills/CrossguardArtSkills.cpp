#include "Skills/CrossguardArtSkills.h"

#include "Abilities/Tasks/AbilityTask_ApplyRootMotionConstantForce.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "AbilitySystem/CrossguardEffects.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Character/CrossguardAICharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/RootMotionSource.h"
#include "Items/CrossguardEquipmentComponent.h"
#include "TimerManager.h"

namespace
{
	// Data/Skills/Skills.json and docs/design-decisions.md (rulings on these skills).
	constexpr float ShootingStarDistance = 1000.0f;
	constexpr float ShootingStarSeconds = 0.3f;
	constexpr float ShootingStarDamage = 50.0f;
	constexpr float ShootingStarBlindRange = 3000.0f;
	constexpr float ShootingStarBlindHalfAngleDegrees = 45.0f;
	constexpr float ShootingStarBlindSeconds = 2.0f;

	constexpr float DragonSeconds = 45.0f;
	constexpr float DragonMaxHealthBonus = 0.5f;
	constexpr float DragonResistance = 0.25f;

	constexpr float FlareBreathSeconds = 3.0f;
	constexpr float FlareBreathTickSeconds = 0.25f;
	constexpr float FlareBreathRange = 600.0f;
	constexpr float FlareBreathHalfAngleDegrees = 30.0f;
	constexpr float FlareBreathFirstHit = 40.0f;
	constexpr float FlareBreathPerSecond = 25.0f;
	constexpr float FlareBreathCoefficient = 0.5f;

	constexpr float FlightSeconds = 20.0f;

	// AI use (assumptions, cm).
	constexpr float ArtAIShootingStarMin = 300.0f;
	constexpr float ArtAIDragonRange = 1500.0f;

	ACharacter* AsCharacter(const FGameplayAbilityActorInfo* ActorInfo)
	{
		return ActorInfo != nullptr ? Cast<ACharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	}
}

// --- Shooting Star --------------------------------------------------------------------------------

UCrossguardShootingStarAbility::UCrossguardShootingStarAbility()
{
	SkillId = TEXT("shooting_star");
	SkillCategory = ECrossguardSkillCategory::Art;
	BaseCooldownSeconds = 60.0f;
	CooldownTags.AddTag(CrossguardTags::Cooldown_Skill_ShootingStar);
	RequiredWeapons.Add(ECrossguardWeaponType::Sword);
	SetAssetTags(FGameplayTagContainer(CrossguardTags::Skill_Weapon_Melee));
}

bool UCrossguardShootingStarAbility::ShouldAIUse(const ACrossguardAICharacter& User, const AActor& Target) const
{
	return FVector::Dist(User.GetActorLocation(), Target.GetActorLocation()) >= ArtAIShootingStarMin
		&& AIHasInRange(User, Target, ShootingStarDistance * 0.9f);
}

void UCrossguardShootingStarAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	ACharacter* Character = AsCharacter(ActorInfo);
	if (Character == nullptr)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	if (!CommitOrCancel(Handle, ActorInfo, ActivationInfo))
	{
		return;
	}

	DashStart = Character->GetActorLocation();
	// The flash goes off as the dash starts.
	if (ActorInfo->IsNetAuthority())
	{
		ApplyBlind();
	}

	// Through characters, not walls. Server and owning client both, so movement prediction agrees.
	UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	PreviousPawnResponse = Capsule->GetCollisionResponseToChannel(ECC_Pawn);
	Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	bIgnoringPawns = true;

	FVector Direction = Character->GetActorForwardVector();
	Direction.Z = 0.0f;
	Direction = Direction.GetSafeNormal();
	const float MaxWalkSpeed = Character->GetCharacterMovement()->MaxWalkSpeed;
	UAbilityTask_ApplyRootMotionConstantForce* Dash = UAbilityTask_ApplyRootMotionConstantForce::ApplyRootMotionConstantForce(this,
		TEXT("ShootingStar"), Direction, ShootingStarDistance / ShootingStarSeconds, ShootingStarSeconds, false, nullptr,
		ERootMotionFinishVelocityMode::ClampVelocity, FVector::ZeroVector, MaxWalkSpeed, false);
	Dash->OnFinish.AddDynamic(this, &UCrossguardShootingStarAbility::OnDashFinished);
	Dash->ReadyForActivation();
}

void UCrossguardShootingStarAbility::OnDashFinished()
{
	if (IsActive() && GetCurrentActorInfo()->IsNetAuthority())
	{
		DamagePath();
	}
	// The server's dash may finish after the client's: only the server's end replicates, so the
	// client never cuts the server's dash (and its damage) short.
	EndOnTimer();
}

void UCrossguardShootingStarAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	ACharacter* Character = AsCharacter(ActorInfo);
	if (bIgnoringPawns && Character != nullptr)
	{
		Character->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, PreviousPawnResponse);
	}
	bIgnoringPawns = false;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UCrossguardShootingStarAbility::DamagePath() const
{
	ACharacter* Character = AsCharacter(GetCurrentActorInfo());
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	UWorld* World = Character != nullptr ? Character->GetWorld() : nullptr;
	const FGameplayEffectSpecHandle Spec = MakeDamageSpec(ShootingStarDamage, 0.0f, 1.0f);
	if (World == nullptr || ASC == nullptr || !Spec.IsValid())
	{
		return;
	}
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CrossguardShootingStar), false, Character);
	TArray<FHitResult> Hits;
	World->SweepMultiByObjectType(Hits, DashStart, Character->GetActorLocation(), FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Params);

	// Everyone in the path, allies too, once each.
	TSet<AActor*> Damaged;
	for (const FHitResult& Hit : Hits)
	{
		AActor* Target = Hit.GetActor();
		UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
		if (TargetASC != nullptr && !Damaged.Contains(Target))
		{
			Damaged.Add(Target);
			ASC->ApplyGameplayEffectSpecToTarget(*Spec.Data, TargetASC);
		}
	}
}

void UCrossguardShootingStarAbility::ApplyBlind() const
{
	AActor* Avatar = GetAvatarActorFromActorInfo();
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	UWorld* World = Avatar != nullptr ? Avatar->GetWorld() : nullptr;
	if (World == nullptr || ASC == nullptr)
	{
		return;
	}
	FVector CasterEyes;
	FRotator CasterRotation;
	Avatar->GetActorEyesViewPoint(CasterEyes, CasterRotation);
	const float MinDot = FMath::Cos(FMath::DegreesToRadians(ShootingStarBlindHalfAngleDegrees));

	// Everyone looking at the caster, allies too.
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* Pawn = *It;
		UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn);
		if (Pawn == Avatar || TargetASC == nullptr || TargetASC->HasMatchingGameplayTag(CrossguardTags::State_Dead))
		{
			continue;
		}
		FVector Eyes;
		FRotator View;
		Pawn->GetActorEyesViewPoint(Eyes, View);
		const FVector ToCaster = CasterEyes - Eyes;
		if (ToCaster.SizeSquared() > FMath::Square(ShootingStarBlindRange)
			|| FVector::DotProduct(View.Vector(), ToCaster.GetSafeNormal()) < MinDot)
		{
			continue;
		}
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CrossguardShootingStarSight), false, Pawn);
		Params.AddIgnoredActor(Avatar);
		if (World->LineTraceTestByChannel(Eyes, CasterEyes, ECC_Visibility, Params))
		{
			continue;
		}
		ApplyStatus(ASC, TargetASC, CrossguardTags::State_Blinded, ShootingStarBlindSeconds);
	}
}

// --- Dragonification ------------------------------------------------------------------------------

UCrossguardDragonificationAbility::UCrossguardDragonificationAbility()
{
	SkillId = TEXT("dragonification");
	SkillCategory = ECrossguardSkillCategory::Art;
	BaseCooldownSeconds = 180.0f;
	CooldownTags.AddTag(CrossguardTags::Cooldown_Skill_Dragonification);
	// Predicted locally; the stats effect carries the tag to everyone else.
	ActivationOwnedTags.AddTag(CrossguardTags::State_Dragonified);
	FlareBreathClass = UCrossguardFlareBreathAbility::StaticClass();
	FlightClass = UCrossguardFlightAbility::StaticClass();
}

bool UCrossguardDragonificationAbility::ShouldAIUse(const ACrossguardAICharacter& User, const AActor& Target) const
{
	return AIHasInRange(User, Target, ArtAIDragonRange, false);
}

void UCrossguardDragonificationAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitOrCancel(Handle, ActorInfo, ActivationInfo))
	{
		return;
	}
	UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
	if (ActorInfo->IsNetAuthority() && ASC != nullptr)
	{
		// A dragon has no hands: weapons go away (their stats with them).
		if (UCrossguardEquipmentComponent* Equipment = ActorInfo->AvatarActor->FindComponentByClass<UCrossguardEquipmentComponent>())
		{
			Equipment->HolsterWeapons();
		}

		const FGameplayEffectSpecHandle Spec = MakeOutgoingGameplayEffectSpec(UCrossguardTimedStatsEffect::StaticClass(), GetAbilityLevel());
		if (Spec.IsValid())
		{
			for (const FName& Name : UCrossguardSkillStatsEffect::GetStatNames())
			{
				Spec.Data->SetSetByCallerMagnitude(Name, 0.0f);
			}
			Spec.Data->SetSetByCallerMagnitude(FName(TEXT("MaxHealthBonus")), DragonMaxHealthBonus);
			Spec.Data->SetSetByCallerMagnitude(FName(TEXT("AttackResistance")), DragonResistance);
			Spec.Data->SetSetByCallerMagnitude(FName(TEXT("MagicResistance")), DragonResistance);
			// A little longer than the form, which removes it on ending.
			Spec.Data->SetSetByCallerMagnitude(CrossguardSetByCaller::Duration, DragonSeconds + 1.0f);
			Spec.Data->DynamicGrantedTags.AddTag(CrossguardTags::State_Dragonified);
			StatsHandle = ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, Spec);
		}

		for (const TSubclassOf<UCrossguardSkillAbility>& Class : {FlareBreathClass, FlightClass})
		{
			if (Class != nullptr)
			{
				GrantedSkills.Add(ASC->GiveAbility(FGameplayAbilitySpec(Class, 1, INDEX_NONE, this)));
			}
		}
	}

	UAbilityTask_WaitDelay* Expire = UAbilityTask_WaitDelay::WaitDelay(this, DragonSeconds);
	Expire->OnFinish.AddDynamic(this, &UCrossguardDragonificationAbility::OnFormExpired);
	Expire->ReadyForActivation();
}

void UCrossguardDragonificationAbility::OnFormExpired()
{
	EndOnTimer();
}

void UCrossguardDragonificationAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	UAbilitySystemComponent* ASC = ActorInfo != nullptr ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (ASC != nullptr && ActorInfo->IsNetAuthority())
	{
		ASC->RemoveActiveGameplayEffect(StatsHandle);
		// Clearing an active Flare Breath or Flight ends it (you fall).
		for (const FGameplayAbilitySpecHandle& Granted : GrantedSkills)
		{
			ASC->ClearAbility(Granted);
		}
	}
	StatsHandle.Invalidate();
	GrantedSkills.Reset();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

// --- Flare Breath ---------------------------------------------------------------------------------

UCrossguardFlareBreathAbility::UCrossguardFlareBreathAbility()
{
	SkillId = TEXT("flare_breath");
	SkillCategory = ECrossguardSkillCategory::Spell;
	BaseCooldownSeconds = 45.0f;
	CooldownTags.AddTag(CrossguardTags::Cooldown_Skill_FlareBreath);
	ActivationBlockedTags.RemoveTag(CrossguardTags::State_Dragonified);
}

bool UCrossguardFlareBreathAbility::ShouldAIUse(const ACrossguardAICharacter& User, const AActor& Target) const
{
	return AIHasInRange(User, Target, FlareBreathRange * 0.9f);
}

void UCrossguardFlareBreathAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitOrCancel(Handle, ActorInfo, ActivationInfo))
	{
		return;
	}
	Caught.Reset();
	if (ActorInfo->IsNetAuthority())
	{
		BreathTick();
		GetWorld()->GetTimerManager().SetTimer(TickTimer, FTimerDelegate::CreateUObject(this, &UCrossguardFlareBreathAbility::BreathTick),
			FlareBreathTickSeconds, true);
	}
	UAbilityTask_WaitDelay* Finish = UAbilityTask_WaitDelay::WaitDelay(this, FlareBreathSeconds);
	Finish->OnFinish.AddDynamic(this, &UCrossguardFlareBreathAbility::OnBreathFinished);
	Finish->ReadyForActivation();
}

void UCrossguardFlareBreathAbility::BreathTick()
{
	AActor* Avatar = GetAvatarActorFromActorInfo();
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	UWorld* World = Avatar != nullptr ? Avatar->GetWorld() : nullptr;
	if (World == nullptr || ASC == nullptr)
	{
		return;
	}
	FVector Origin;
	FVector Direction;
	GetAim(Origin, Direction);
	const float MinDot = FMath::Cos(FMath::DegreesToRadians(FlareBreathHalfAngleDegrees));

	FCollisionQueryParams Params(SCENE_QUERY_STAT(CrossguardFlareBreath), false, Avatar);
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, Origin, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeSphere(FlareBreathRange), Params);

	const FGameplayEffectSpecHandle FirstSpec = MakeDamageSpec(0.0f, FlareBreathFirstHit, FlareBreathCoefficient);
	const FGameplayEffectSpecHandle TickSpec = MakeDamageSpec(0.0f, FlareBreathPerSecond * FlareBreathTickSeconds, FlareBreathCoefficient);
	TSet<AActor*> Hit;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Target = Overlap.GetActor();
		UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
		if (TargetASC == nullptr || Target == Avatar || Hit.Contains(Target))
		{
			continue;
		}
		const FVector ToTarget = Target->GetActorLocation() - Origin;
		if (ToTarget.SizeSquared() > FMath::Square(FlareBreathRange) || FVector::DotProduct(Direction, ToTarget.GetSafeNormal()) < MinDot)
		{
			continue;
		}
		Hit.Add(Target);
		// The first catch burns harder; after that, damage per tick while inside. Allies too.
		const bool bFirst = !Caught.Contains(Target);
		Caught.Add(Target);
		const FGameplayEffectSpecHandle& Spec = bFirst ? FirstSpec : TickSpec;
		if (Spec.IsValid())
		{
			ASC->ApplyGameplayEffectSpecToTarget(*Spec.Data, TargetASC);
		}
	}
}

void UCrossguardFlareBreathAbility::OnBreathFinished()
{
	EndOnTimer();
}

void UCrossguardFlareBreathAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TickTimer);
	}
	Caught.Reset();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

// --- Flight ---------------------------------------------------------------------------------------

UCrossguardFlightAbility::UCrossguardFlightAbility()
{
	SkillId = TEXT("flight");
	SkillCategory = ECrossguardSkillCategory::Spell;
	BaseCooldownSeconds = 60.0f;
	CooldownTags.AddTag(CrossguardTags::Cooldown_Skill_Flight);
	ActivationBlockedTags.RemoveTag(CrossguardTags::State_Dragonified);
	ActivationOwnedTags.AddTag(CrossguardTags::State_Flying);
	// [Chop Down] cancels abilities with this tag.
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(CrossguardTags::Ability_Flight);
	SetAssetTags(AssetTags);
}

void UCrossguardFlightAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	ACharacter* Character = AsCharacter(ActorInfo);
	if (Character == nullptr)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	if (!CommitOrCancel(Handle, ActorInfo, ActivationInfo))
	{
		return;
	}
	// Server and owning client both, so movement prediction agrees.
	Character->GetCharacterMovement()->SetMovementMode(MOVE_Flying);

	UAbilityTask_WaitDelay* Expire = UAbilityTask_WaitDelay::WaitDelay(this, FlightSeconds);
	Expire->OnFinish.AddDynamic(this, &UCrossguardFlightAbility::OnFlightExpired);
	Expire->ReadyForActivation();
}

void UCrossguardFlightAbility::OnFlightExpired()
{
	EndOnTimer();
}

void UCrossguardFlightAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	ACharacter* Character = AsCharacter(ActorInfo);
	if (Character != nullptr && Character->GetCharacterMovement()->IsFlying())
	{
		Character->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
