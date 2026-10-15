#include "Skills/CrossguardWeaponSkills.h"

#include "Abilities/Tasks/AbilityTask_ApplyRootMotionJumpForce.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "AbilitySystem/CrossguardAttributeSet.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/RootMotionSource.h"
#include "Character/CrossguardCharacter.h"
#include "EngineUtils.h"
#include "Game/CrossguardTeamLibrary.h"
#include "Items/CrossguardEquipmentComponent.h"
#include "Items/CrossguardItemTypes.h"
#include "Skills/CrossguardWall.h"
#include "TimerManager.h"

namespace
{
	// Data/Skills/Skills.json and docs/design-decisions.md (rulings on these skills).
	constexpr float BuffSeconds = 8.0f;
	constexpr float QuickshotMoveSpeed = 0.05f;
	constexpr float QuickshotActionSpeed = 0.25f;
	constexpr float CleaveWeaponFraction = 0.85f;
	constexpr float CleaveRange = 300.0f;
	constexpr float CleaveHalfAngleDegrees = 60.0f;
	constexpr int32 TripleThrustCount = 3;
	constexpr float TripleThrustInterval = 0.15f;
	constexpr float TripleThrustBonus = 15.0f;
	constexpr float TripleThrustReach = 250.0f;
	constexpr float TripleThrustRadius = 40.0f;
	constexpr float PartingDamage = 65.0f;
	constexpr float PartingLength = 800.0f;
	constexpr float PartingHalfWidth = 100.0f;
	constexpr float PartingKnockUpSpeed = 700.0f;
	constexpr float PartingKnockUpSeconds = 1.0f;
	constexpr float PartingWallHeight = 300.0f;
	constexpr float PartingWallThickness = 50.0f;
	constexpr float PartingWallGap = 60.0f;
	constexpr float LeapCmPerAgility = 50.0f;
	constexpr float LeapHeight = 150.0f;
	constexpr float LeapSeconds = 0.6f;
	constexpr float LionRange = 3000.0f;
	constexpr float LionStopShort = 150.0f;
	constexpr float LionHeight = 200.0f;
	constexpr float LionStrength = 1.2f;
	constexpr float LionBuffSeconds = 6.0f;

	// The main-hand weapon's damage as a spec from Ability: Fraction of its value, plus AttackBonus.
	FGameplayEffectSpecHandle MakeMainHandSpec(const UCrossguardSkillAbility& Ability, const AActor* Avatar, float Fraction, float AttackBonus)
	{
		const UCrossguardEquipmentComponent* Equipment = Avatar != nullptr ? Avatar->FindComponentByClass<UCrossguardEquipmentComponent>() : nullptr;
		const FCrossguardWeaponRow* Weapon = Equipment != nullptr ? Equipment->GetWeaponRow(ECrossguardEquipmentSlot::MainHand) : nullptr;
		if (Weapon == nullptr)
		{
			return Ability.MakeDamageSpec(AttackBonus, 0.0f);
		}
		const float Value = UCrossguardItemLibrary::GetWeaponValue(*Weapon, Equipment->GetItem(ECrossguardEquipmentSlot::MainHand).Rarity) * Fraction;
		switch (Weapon->Stat)
		{
		case ECrossguardWeaponStat::AttackDamage:
			return Ability.MakeDamageSpec(Value + AttackBonus, 0.0f);
		case ECrossguardWeaponStat::MagicDamage:
			return Ability.MakeDamageSpec(AttackBonus, Value);
		case ECrossguardWeaponStat::AttackResistance:
			break;
		}
		return Ability.MakeDamageSpec(AttackBonus, 0.0f);
	}

	// The ground under Point: world geometry only, from 2 m above (slopes), else 90 cm down.
	FVector GroundUnder(const UWorld& World, const FVector& Point, const FCollisionQueryParams& Params)
	{
		FHitResult Ground;
		const FVector Start = Point + FVector(0.0f, 0.0f, 200.0f);
		return World.LineTraceSingleByObjectType(Ground, Start, Point - FVector(0.0f, 0.0f, 500.0f), FCollisionObjectQueryParams(ECC_WorldStatic), Params)
			? Ground.ImpactPoint : Point - FVector(0.0f, 0.0f, 90.0f);
	}

	void ApplyToTarget(UAbilitySystemComponent* Source, AActor* Target, const FGameplayEffectSpecHandle& Spec)
	{
		UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
		if (Source != nullptr && TargetASC != nullptr && Spec.IsValid())
		{
			Source->ApplyGameplayEffectSpecToTarget(*Spec.Data, TargetASC);
		}
	}

	FGameplayTagContainer MeleeTags()
	{
		return FGameplayTagContainer(CrossguardTags::Skill_Weapon_Melee);
	}
}

// --- Next-attack skills ---------------------------------------------------------------------------

void UCrossguardNextAttackAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitOrCancel(Handle, ActorInfo, ActivationInfo))
	{
		return;
	}
	if (ActorInfo->IsNetAuthority())
	{
		UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
		ApplyStatus(ASC, ASC, ReadyTag, ReadySeconds);
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

UCrossguardSlashAbility::UCrossguardSlashAbility()
{
	SkillId = TEXT("slash");
	SkillCategory = ECrossguardSkillCategory::Active;
	BaseCooldownSeconds = 4.0f;
	CooldownTags.AddTag(CrossguardTags::Cooldown_Skill_Slash);
	RequiredWeapons = {ECrossguardWeaponType::Sword, ECrossguardWeaponType::Dagger};
	SetAssetTags(MeleeTags());
	ReadyTag = CrossguardTags::State_NextAttack_Slash;
}

UCrossguardMortalStrikeAbility::UCrossguardMortalStrikeAbility()
{
	SkillId = TEXT("mortal_strike");
	SkillCategory = ECrossguardSkillCategory::Active;
	BaseCooldownSeconds = 30.0f;
	CooldownTags.AddTag(CrossguardTags::Cooldown_Skill_MortalStrike);
	RequiredWeapons = {ECrossguardWeaponType::AnyMelee};
	SetAssetTags(MeleeTags());
	ReadyTag = CrossguardTags::State_NextAttack_MortalStrike;
}

UCrossguardShadeBlowAbility::UCrossguardShadeBlowAbility()
{
	SkillId = TEXT("shade_blow");
	SkillCategory = ECrossguardSkillCategory::Active;
	BaseCooldownSeconds = 10.0f;
	CooldownTags.AddTag(CrossguardTags::Cooldown_Skill_ShadeBlow);
	RequiredWeapons = {ECrossguardWeaponType::Sword, ECrossguardWeaponType::Axe};
	SetAssetTags(MeleeTags());
	ReadyTag = CrossguardTags::State_NextAttack_ShadeBlow;
}

UCrossguardGougeAbility::UCrossguardGougeAbility()
{
	SkillId = TEXT("gouge");
	SkillCategory = ECrossguardSkillCategory::Active;
	BaseCooldownSeconds = 7.0f;
	CooldownTags.AddTag(CrossguardTags::Cooldown_Skill_Gouge);
	RequiredWeapons = {ECrossguardWeaponType::Dagger};
	SetAssetTags(MeleeTags());
	ReadyTag = CrossguardTags::State_NextAttack_Gouge;
}

UCrossguardPurgeShotAbility::UCrossguardPurgeShotAbility()
{
	SkillId = TEXT("purge_shot");
	SkillCategory = ECrossguardSkillCategory::Active;
	BaseCooldownSeconds = 35.0f;
	CooldownTags.AddTag(CrossguardTags::Cooldown_Skill_PurgeShot);
	RequiredWeapons = {ECrossguardWeaponType::Bow, ECrossguardWeaponType::Crossbow};
	SetAssetTags(FGameplayTagContainer(CrossguardTags::Skill_Weapon_Ranged));
	ReadyTag = CrossguardTags::State_NextAttack_PurgeShot;
}

// --- Bow buffs ------------------------------------------------------------------------------------

UCrossguardQuickshotAbility::UCrossguardQuickshotAbility()
{
	SkillId = TEXT("quickshot");
	SkillCategory = ECrossguardSkillCategory::Active;
	BaseCooldownSeconds = 25.0f;
	CooldownTags.AddTag(CrossguardTags::Cooldown_Skill_Quickshot);
	RequiredWeapons.Add(ECrossguardWeaponType::Bow);
	SetAssetTags(FGameplayTagContainer(CrossguardTags::Skill_Weapon_Ranged));
}

void UCrossguardQuickshotAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitOrCancel(Handle, ActorInfo, ActivationInfo))
	{
		return;
	}
	if (ActorInfo->IsNetAuthority())
	{
		UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
		const TMap<FName, float> Bonus = {{TEXT("MoveSpeed"), QuickshotMoveSpeed}, {TEXT("ActionSpeed"), QuickshotActionSpeed}};
		ApplyTimedStats(ASC, ASC, Bonus, BuffSeconds, false);
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

UCrossguardArcaneArrowsAbility::UCrossguardArcaneArrowsAbility()
{
	SkillId = TEXT("arcane_arrows");
	SkillCategory = ECrossguardSkillCategory::Active;
	BaseCooldownSeconds = 25.0f;
	CooldownTags.AddTag(CrossguardTags::Cooldown_Skill_ArcaneArrows);
	RequiredWeapons.Add(ECrossguardWeaponType::Bow);
	SetAssetTags(FGameplayTagContainer(CrossguardTags::Skill_Weapon_Ranged));
}

void UCrossguardArcaneArrowsAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitOrCancel(Handle, ActorInfo, ActivationInfo))
	{
		return;
	}
	if (ActorInfo->IsNetAuthority())
	{
		UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
		ApplyStatus(ASC, ASC, CrossguardTags::State_ArcaneArrows, BuffSeconds);
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

// --- Cleave ---------------------------------------------------------------------------------------

UCrossguardCleaveAbility::UCrossguardCleaveAbility()
{
	SkillId = TEXT("cleave");
	SkillCategory = ECrossguardSkillCategory::Art;
	BaseCooldownSeconds = 10.0f;
	CooldownTags.AddTag(CrossguardTags::Cooldown_Skill_Cleave);
	RequiredWeapons.Add(ECrossguardWeaponType::AnyMelee);
	SetAssetTags(MeleeTags());
}

void UCrossguardCleaveAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitOrCancel(Handle, ActorInfo, ActivationInfo))
	{
		return;
	}
	AActor* Avatar = ActorInfo->AvatarActor.Get();
	UWorld* World = Avatar != nullptr ? Avatar->GetWorld() : nullptr;
	if (ActorInfo->IsNetAuthority() && World != nullptr)
	{
		const FGameplayEffectSpecHandle Spec = MakeMainHandSpec(*this, Avatar, CleaveWeaponFraction, 0.0f);
		FVector Forward = Avatar->GetActorForwardVector();
		Forward.Z = 0.0f;
		Forward = Forward.GetSafeNormal();
		const float MinDot = FMath::Cos(FMath::DegreesToRadians(CleaveHalfAngleDegrees));

		FCollisionQueryParams Params(SCENE_QUERY_STAT(CrossguardCleave), false, Avatar);
		TArray<FOverlapResult> Overlaps;
		World->OverlapMultiByObjectType(Overlaps, Avatar->GetActorLocation(), FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
			FCollisionShape::MakeSphere(CleaveRange), Params);
		// Everyone in the arc, allies too, once each.
		TSet<AActor*> Hit;
		for (const FOverlapResult& Overlap : Overlaps)
		{
			AActor* Target = Overlap.GetActor();
			if (Target == nullptr || Target == Avatar || Hit.Contains(Target))
			{
				continue;
			}
			FVector ToTarget = Target->GetActorLocation() - Avatar->GetActorLocation();
			ToTarget.Z = 0.0f;
			if (FVector::DotProduct(Forward, ToTarget.GetSafeNormal()) < MinDot)
			{
				continue;
			}
			Hit.Add(Target);
			ApplyToTarget(ActorInfo->AbilitySystemComponent.Get(), Target, Spec);
		}
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

// --- Triple Thrust --------------------------------------------------------------------------------

UCrossguardTripleThrustAbility::UCrossguardTripleThrustAbility()
{
	SkillId = TEXT("triple_thrust");
	SkillCategory = ECrossguardSkillCategory::Art;
	BaseCooldownSeconds = 12.0f;
	CooldownTags.AddTag(CrossguardTags::Cooldown_Skill_TripleThrust);
	RequiredWeapons.Add(ECrossguardWeaponType::Spear);
	SetAssetTags(MeleeTags());
}

void UCrossguardTripleThrustAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitOrCancel(Handle, ActorInfo, ActivationInfo))
	{
		return;
	}
	if (ActorInfo->IsNetAuthority())
	{
		ThrustsLeft = TripleThrustCount;
		Thrust();
		GetWorld()->GetTimerManager().SetTimer(ThrustTimer, FTimerDelegate::CreateUObject(this, &UCrossguardTripleThrustAbility::Thrust),
			TripleThrustInterval, true);
	}
	UAbilityTask_WaitDelay* Done = UAbilityTask_WaitDelay::WaitDelay(this, TripleThrustInterval * TripleThrustCount);
	Done->OnFinish.AddDynamic(this, &UCrossguardTripleThrustAbility::OnThrustsDone);
	Done->ReadyForActivation();
}

void UCrossguardTripleThrustAbility::Thrust()
{
	if (ThrustsLeft <= 0)
	{
		GetWorld()->GetTimerManager().ClearTimer(ThrustTimer);
		return;
	}
	--ThrustsLeft;
	AActor* Avatar = GetAvatarActorFromActorInfo();
	UWorld* World = Avatar != nullptr ? Avatar->GetWorld() : nullptr;
	if (World == nullptr)
	{
		return;
	}
	FVector Start;
	FVector Direction;
	GetAim(Start, Direction);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CrossguardTripleThrust), false, Avatar);
	TArray<FHitResult> Hits;
	World->SweepMultiByObjectType(Hits, Start, Start + Direction * TripleThrustReach, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeSphere(TripleThrustRadius), Params);
	const FGameplayEffectSpecHandle Spec = MakeMainHandSpec(*this, Avatar, 1.0f, TripleThrustBonus);
	TSet<AActor*> Hit;
	for (const FHitResult& Result : Hits)
	{
		AActor* Target = Result.GetActor();
		if (Target != nullptr && Target != Avatar && !Hit.Contains(Target))
		{
			Hit.Add(Target);
			ApplyToTarget(GetAbilitySystemComponentFromActorInfo(), Target, Spec);
		}
	}
}

void UCrossguardTripleThrustAbility::OnThrustsDone()
{
	EndOnTimer();
}

void UCrossguardTripleThrustAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// A thrust still due when the timer and the wait finish together lands before the end.
	if (ActorInfo != nullptr && ActorInfo->IsNetAuthority() && !bWasCancelled)
	{
		while (ThrustsLeft > 0)
		{
			Thrust();
		}
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ThrustTimer);
	}
	ThrustsLeft = 0;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

// --- Parting the Land -----------------------------------------------------------------------------

UCrossguardPartingTheLandAbility::UCrossguardPartingTheLandAbility()
{
	SkillId = TEXT("parting_the_land");
	SkillCategory = ECrossguardSkillCategory::Active;
	BaseCooldownSeconds = 60.0f;
	CooldownTags.AddTag(CrossguardTags::Cooldown_Skill_PartingTheLand);
	RequiredWeapons.Add(ECrossguardWeaponType::TwoHandedMelee);
	SetAssetTags(MeleeTags());
}

void UCrossguardPartingTheLandAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitOrCancel(Handle, ActorInfo, ActivationInfo))
	{
		return;
	}
	AActor* Avatar = ActorInfo->AvatarActor.Get();
	UWorld* World = Avatar != nullptr ? Avatar->GetWorld() : nullptr;
	UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
	if (ActorInfo->IsNetAuthority() && World != nullptr && ASC != nullptr)
	{
		const FRotator Facing(0.0f, Avatar->GetActorRotation().Yaw, 0.0f);
		const FVector Forward = Facing.Vector();
		const FVector Right = FRotationMatrix(Facing).GetUnitAxis(EAxis::Y);
		const FVector Origin = Avatar->GetActorLocation();

		// The strip ahead: a box swept from the caster to 8 m out.
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CrossguardPartingTheLand), false, Avatar);
		TArray<FHitResult> Hits;
		World->SweepMultiByObjectType(Hits, Origin, Origin + Forward * PartingLength, Facing.Quaternion(),
			FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeBox(FVector(10.0f, PartingHalfWidth, 100.0f)), Params);
		const FGameplayEffectSpecHandle Spec = MakeDamageSpec(PartingDamage, 0.0f, 1.0f);
		TSet<AActor*> Hit;
		for (const FHitResult& Result : Hits)
		{
			AActor* Target = Result.GetActor();
			UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
			if (Target == Avatar || TargetASC == nullptr || Hit.Contains(Target))
			{
				continue;
			}
			Hit.Add(Target);
			ApplyToTarget(ASC, Target, Spec);
			// Knock-up: crowd control.
			if (!IsCrowdControlImmune(TargetASC))
			{
				ApplyStatus(ASC, TargetASC, CrossguardTags::State_KnockedUp, PartingKnockUpSeconds);
				if (ACharacter* Character = Cast<ACharacter>(Target))
				{
					Character->LaunchCharacter(FVector(0.0f, 0.0f, PartingKnockUpSpeed), false, true);
				}
				// A player's own client predicts its movement: launch there too.
				if (ACrossguardCharacter* Player = Cast<ACrossguardCharacter>(Target); Player != nullptr && Player->IsPlayerControlled())
				{
					Player->ClientLaunch(FVector(0.0f, 0.0f, PartingKnockUpSpeed));
				}
			}
		}

		// A Wall on each side of the strip, as long as it.
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = Avatar;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		for (const float Side : {-1.0f, 1.0f})
		{
			// Clear of the strip by a capsule's width, so nobody hit ends up inside a wall.
			const FVector Center = Origin + Forward * (PartingLength * 0.5f)
				+ Right * Side * (PartingHalfWidth + PartingWallGap + PartingWallThickness * 0.5f);
			const FVector Feet = GroundUnder(*World, Center, Params);
			if (ACrossguardWall* Wall = World->SpawnActor<ACrossguardWall>(ACrossguardWall::StaticClass(), Feet, Facing, SpawnParams))
			{
				Wall->SetWallSize(FVector(PartingLength, PartingWallThickness, PartingWallHeight));
			}
		}
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

// --- Like A Lion, He Leapt -----------------------------------------------------------------------

UCrossguardLikeALionAbility::UCrossguardLikeALionAbility()
{
	SkillId = TEXT("like_a_lion_he_leapt");
	SkillCategory = ECrossguardSkillCategory::Art;
	BaseCooldownSeconds = 12.0f;
	CooldownTags.AddTag(CrossguardTags::Cooldown_Skill_LikeALion);
	RequiredWeapons.Add(ECrossguardWeaponType::AnyMelee);
	SetAssetTags(MeleeTags());
}

AActor* UCrossguardLikeALionAbility::FindLeapTarget() const
{
	AActor* Avatar = GetAvatarActorFromActorInfo();
	if (Avatar == nullptr)
	{
		return nullptr;
	}
	AActor* BestPlayer = nullptr;
	AActor* BestOther = nullptr;
	float BestPlayerDistance = FMath::Square(LionRange);
	float BestOtherDistance = BestPlayerDistance;
	for (TActorIterator<ACrossguardCharacter> It(Avatar->GetWorld()); It; ++It)
	{
		ACrossguardCharacter* Candidate = *It;
		const UAbilitySystemComponent* ASC = Candidate->GetAbilitySystemComponent();
		if (Candidate == Avatar || ASC == nullptr || UCrossguardTeamLibrary::AreAllies(Avatar, Candidate)
			|| ASC->HasMatchingGameplayTag(CrossguardTags::State_Dead) || ASC->HasMatchingGameplayTag(CrossguardTags::State_Stealthed))
		{
			continue;
		}
		const float Distance = FVector::DistSquared(Avatar->GetActorLocation(), Candidate->GetActorLocation());
		if (Candidate->IsPlayerControlled() && Distance < BestPlayerDistance)
		{
			BestPlayerDistance = Distance;
			BestPlayer = Candidate;
		}
		else if (!Candidate->IsPlayerControlled() && Distance < BestOtherDistance)
		{
			BestOtherDistance = Distance;
			BestOther = Candidate;
		}
	}
	return BestPlayer != nullptr ? BestPlayer : BestOther;
}

void UCrossguardLikeALionAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	const ACharacter* Character = Cast<ACharacter>(ActorInfo->AvatarActor.Get());
	// Server and owning client pick the same target from replicated positions; a rare disagreement
	// is corrected by the server's movement.
	const AActor* Target = FindLeapTarget();
	if (Character == nullptr || Target == nullptr)
	{
		// Nobody to leap at: no cooldown.
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	if (!CommitOrCancel(Handle, ActorInfo, ActivationInfo))
	{
		return;
	}
	if (ActorInfo->IsNetAuthority())
	{
		UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
		const TMap<FName, float> Stronger = {{TEXT("Strength"), LionStrength}};
		ApplyTimedStats(ASC, ASC, Stronger, LionBuffSeconds, true);
	}

	const FVector ToTarget = Target->GetActorLocation() - Character->GetActorLocation();
	const float Distance = FMath::Max(ToTarget.Size2D() - LionStopShort, 0.0f);
	const FRotator Facing(0.0f, ToTarget.Rotation().Yaw, 0.0f);
	UAbilityTask_ApplyRootMotionJumpForce* Leap = UAbilityTask_ApplyRootMotionJumpForce::ApplyRootMotionJumpForce(this, TEXT("LikeALion"),
		Facing, Distance, LionHeight, 0.4f + Distance / 2500.0f, 0.1f, true, ERootMotionFinishVelocityMode::ClampVelocity, FVector::ZeroVector,
		Character->GetCharacterMovement()->MaxWalkSpeed, nullptr, nullptr);
	Leap->OnFinish.AddDynamic(this, &UCrossguardLikeALionAbility::OnLeapFinished);
	Leap->ReadyForActivation();
}

void UCrossguardLikeALionAbility::OnLeapFinished()
{
	EndOnTimer();
}

// --- Hunter's Leap --------------------------------------------------------------------------------

UCrossguardHuntersLeapAbility::UCrossguardHuntersLeapAbility()
{
	SkillId = TEXT("hunters_leap");
	SkillCategory = ECrossguardSkillCategory::Art;
	BaseCooldownSeconds = 15.0f;
	CooldownTags.AddTag(CrossguardTags::Cooldown_Skill_HuntersLeap);
}

void UCrossguardHuntersLeapAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	const ACharacter* Character = Cast<ACharacter>(ActorInfo->AvatarActor.Get());
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
	const float Agility = ActorInfo->AbilitySystemComponent->GetNumericAttribute(UCrossguardAttributeSet::GetAgilityAttribute());
	const FRotator Facing(0.0f, Character->GetActorRotation().Yaw, 0.0f);
	UAbilityTask_ApplyRootMotionJumpForce* Leap = UAbilityTask_ApplyRootMotionJumpForce::ApplyRootMotionJumpForce(this, TEXT("HuntersLeap"),
		Facing, FMath::Max(Agility, 0.0f) * LeapCmPerAgility, LeapHeight, LeapSeconds, 0.1f, true,
		ERootMotionFinishVelocityMode::ClampVelocity, FVector::ZeroVector, Character->GetCharacterMovement()->MaxWalkSpeed, nullptr, nullptr);
	Leap->OnFinish.AddDynamic(this, &UCrossguardHuntersLeapAbility::OnLeapFinished);
	Leap->ReadyForActivation();
}

void UCrossguardHuntersLeapAbility::OnLeapFinished()
{
	EndOnTimer();
}
