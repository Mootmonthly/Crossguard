#include "Combat/CrossguardGuard.h"

#include "AbilitySystem/CrossguardEffects.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Character/CrossguardAICharacter.h"
#include "Character/CrossguardCharacter.h"
#include "Crossguard.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/CrossguardTeamLibrary.h"
#include "GameFramework/Pawn.h"
#include "Items/CrossguardEquipmentComponent.h"
#include "Items/CrossguardItemTypes.h"
#include "Skills/CrossguardSkillAbility.h"

namespace
{
	// Assumption: an AI raises its guard against half of the melee attacks aimed at it, for this long.
	constexpr float AIGuardChance = 0.5f;
	constexpr float AIGuardSeconds = 1.0f;
	// A player's swing threatens AIs this far either side of where it faces.
	constexpr float AISwingHalfAngleDegrees = 60.0f;
	// Long enough to outlast any guard: removed when the guard ends.
	constexpr float GuardSlowSeconds = 3600.0f;

	// What a raised guard is made of.
	struct FGuardInfo
	{
		int32 ImpactPower = 0;
		bool bShield = false;
		// The defender's weapon damage, for the mirror rule (0 for a shield).
		float WeaponDamage = 0.0f;
	};

	bool GetGuardInfo(const AActor* Defender, FGuardInfo& Out)
	{
		if (const ACrossguardAICharacter* AI = Cast<ACrossguardAICharacter>(Defender))
		{
			Out.ImpactPower = AI->ImpactPower;
			Out.WeaponDamage = AI->AttackDamage;
			return true;
		}
		const UCrossguardEquipmentComponent* Equipment = Defender != nullptr ? Defender->FindComponentByClass<UCrossguardEquipmentComponent>() : nullptr;
		if (Equipment == nullptr)
		{
			return false;
		}
		switch (Equipment->GetSecondaryAction())
		{
		case ECrossguardSecondaryAction::Block:
			if (const FCrossguardWeaponRow* Shield = Equipment->GetWeaponRow(ECrossguardEquipmentSlot::OffHand))
			{
				Out.ImpactPower = Shield->ImpactPower;
				Out.bShield = true;
				return true;
			}
			return false;
		case ECrossguardSecondaryAction::Parry:
			if (const FCrossguardWeaponRow* Weapon = Equipment->GetWeaponRow(ECrossguardEquipmentSlot::MainHand))
			{
				Out.ImpactPower = Weapon->ImpactPower;
				Out.WeaponDamage = Weapon->Stat == ECrossguardWeaponStat::AttackDamage
					? UCrossguardItemLibrary::GetWeaponValue(*Weapon, Equipment->GetItem(ECrossguardEquipmentSlot::MainHand).Rarity) : 0.0f;
				return true;
			}
			return false;
		default:
			return false;
		}
	}

	// Where the guard faces: a player's view (the mouse moves it); an AI's body.
	FVector GuardFacing(const AActor* Defender)
	{
		if (const ACrossguardAICharacter* AI = Cast<ACrossguardAICharacter>(Defender))
		{
			// Toward the swing it raised it against, whichever way it turns meanwhile.
			return AI->GetGuardDirection();
		}
		const APawn* Pawn = Cast<APawn>(Defender);
		return Pawn != nullptr ? Pawn->GetBaseAimRotation().Vector() : Defender->GetActorForwardVector();
	}

	// Pushes a character back along Away (and its owning client, which predicts its movement).
	void Bounce(AActor* Actor, const FVector& Away)
	{
		ACrossguardCharacter* Character = Cast<ACrossguardCharacter>(Actor);
		if (Character == nullptr)
		{
			return;
		}
		const FVector Velocity = Away.GetSafeNormal2D() * static_cast<float>(CrossguardGuard::Config().BounceSpeed);
		Character->LaunchCharacter(Velocity, true, false);
		if (Character->IsPlayerControlled() && !Character->IsLocallyControlled())
		{
			Character->ClientLaunch(Velocity);
		}
	}

	// Weapons met: both bounce back from the point of impact, and every machine plays the clank.
	void Clank(AActor* Defender, const FCrossguardHit& Hit)
	{
		const FVector Point = Hit.bRanged ? Defender->GetActorLocation() : (Defender->GetActorLocation() + Hit.Origin) * 0.5f;
		Bounce(Defender, Defender->GetActorLocation() - Hit.Origin);
		if (!Hit.bRanged && Hit.Attacker != nullptr)
		{
			Bounce(Hit.Attacker, Hit.Attacker->GetActorLocation() - Defender->GetActorLocation());
		}
		if (ACrossguardCharacter* Character = Cast<ACrossguardCharacter>(Defender))
		{
			Character->MulticastWeaponClank(Point);
		}
	}

	void CancelGuard(UAbilitySystemComponent* ASC)
	{
		const FGameplayTagContainer GuardTag(CrossguardTags::Ability_Guard);
		ASC->CancelAbilities(&GuardTag);
	}
}

namespace CrossguardGuard
{
	const Crossguard::FGuardConfig& Config()
	{
		static const Crossguard::FGuardConfig GuardConfig;
		return GuardConfig;
	}

	int32 ImpactPowerOf(const AActor* Attacker, ECrossguardEquipmentSlot Slot)
	{
		if (const ACrossguardAICharacter* AI = Cast<ACrossguardAICharacter>(Attacker))
		{
			return AI->ImpactPower;
		}
		const UCrossguardEquipmentComponent* Equipment = Attacker != nullptr ? Attacker->FindComponentByClass<UCrossguardEquipmentComponent>() : nullptr;
		const FCrossguardWeaponRow* Weapon = Equipment != nullptr ? Equipment->GetWeaponRow(Slot) : nullptr;
		return Weapon != nullptr ? Weapon->ImpactPower : Config().UnarmedImpactPower;
	}

	bool CanGuard(const AActor* Actor)
	{
		FGuardInfo Unused;
		return GetGuardInfo(Actor, Unused);
	}

	void Stun(UAbilitySystemComponent* Source, UAbilitySystemComponent* Target, float Seconds)
	{
		// Not on a body (a revive would come back rooted).
		if (Target == nullptr || Seconds <= 0.0f || UCrossguardSkillAbility::IsCrowdControlImmune(Target)
			|| Target->HasMatchingGameplayTag(CrossguardTags::State_Dead))
		{
			return;
		}
		// Rooted for the duration (Movement Speed × 0), with the state tag.
		const TMap<FName, float> Rooted = {{TEXT("MoveSpeed"), 0.0f}};
		UCrossguardSkillAbility::ApplyTimedStats(Source != nullptr ? Source : Target, Target, Rooted, Seconds, true, CrossguardTags::State_Stunned);
		CancelGuard(Target);
		UCrossguardSkillAbility::CancelActiveSkills(Target);
	}

	float ApplyHit(UAbilitySystemComponent* Source, AActor* Target, const FGameplayEffectSpecHandle& Spec, const FCrossguardHit& Hit)
	{
		UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
		if (TargetASC == nullptr || !Spec.IsValid())
		{
			return 0.0f;
		}
		const auto ApplyAsIs = [Source, TargetASC, &Spec]()
		{
			if (Source != nullptr)
			{
				Source->ApplyGameplayEffectSpecToTarget(*Spec.Data, TargetASC);
			}
			else
			{
				TargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
			}
		};

		const float Attack = Spec.Data->GetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_Attack, false, 0.0f);
		// What is in the way: a raised guard, or (melee only) the target's own swing, still moving
		// (two swinging weapons collide too).
		FGuardInfo Guard;
		FVector Facing = FVector::ZeroVector;
		bool bClash = false;
		const bool bGuarding = TargetASC->HasMatchingGameplayTag(CrossguardTags::State_Guarding) && GetGuardInfo(Target, Guard);
		if (bGuarding)
		{
			Facing = GuardFacing(Target);
		}
		else if (const ACrossguardCharacter* Swinger = Cast<ACrossguardCharacter>(Target); Swinger != nullptr && !Hit.bRanged)
		{
			bClash = Swinger->GetActiveSwing(Target->GetWorld()->GetTimeSeconds(), Config().ClashWindowSeconds, Guard.ImpactPower,
				Guard.WeaponDamage, Facing);
		}
		if (Attack <= 0.0f || Hit.ImpactPower <= 0 || (!bGuarding && !bClash))
		{
			ApplyAsIs();
			return 1.0f;
		}
		// Only what reaches the guard's (or swinging blade's) surface: from where it faces.
		const FVector Incoming = (Hit.Origin - Target->GetActorLocation()).GetSafeNormal();
		if (!Crossguard::HitMeetsGuard(Config(), Facing.X, Facing.Y, Facing.Z, Incoming.X, Incoming.Y, Incoming.Z))
		{
			ApplyAsIs();
			return 1.0f;
		}

		const Crossguard::FGuardOutcome Outcome = Crossguard::ResolveGuard(Config(), Hit.ImpactPower, Guard.ImpactPower, Guard.bShield, Hit.bPiercing);
		UE_LOG(LogCrossguard, Log, TEXT("%s %s %s's hit (Impact Power %d vs %d): %.0f%% through"), *GetNameSafe(Target),
			bClash ? TEXT("clashed with") : Guard.bShield ? TEXT("blocked") : TEXT("parried"), *GetNameSafe(Hit.Attacker), Hit.ImpactPower,
			Guard.ImpactPower, Outcome.DefenderDamage * 100.0);
		Clank(Target, Hit);

		// The defender's share; Magic Damage always goes through.
		const float Magic = Spec.Data->GetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_Magic, false, 0.0f);
		if (Outcome.DefenderDamage > 0.0 || Magic > 0.0f)
		{
			FGameplayEffectSpec Reduced(*Spec.Data);
			Reduced.SetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_AttackShare, static_cast<float>(Outcome.DefenderDamage));
			if (Source != nullptr)
			{
				Source->ApplyGameplayEffectSpecToTarget(Reduced, TargetASC);
			}
			else
			{
				TargetASC->ApplyGameplayEffectSpecToSelf(Reduced);
			}
		}
		// Art hook: a hit stopped whole still reaches the defender's OnDamageTaken (0, blocked); one
		// partly through is reported by the damage itself.
		if (Outcome.DefenderDamage <= 0.0 && Magic <= 0.0f)
		{
			if (ACrossguardCharacter* Defender = Cast<ACrossguardCharacter>(Target))
			{
				const FVector Toward = (Hit.Origin - Target->GetActorLocation()).GetSafeNormal2D();
				Defender->NotifyDamageTaken(0.0f, Hit.Attacker, Target->GetActorLocation() + Toward * Target->GetSimpleCollisionRadius(), true, false);
			}
		}
		Stun(Source, TargetASC, static_cast<float>(Outcome.DefenderStunSeconds));

		// Mirror rule: the losing attacker takes the defender's weapon (a shield: nothing) and a stun.
		// Assumption: only in melee (a blocked arrow cannot reach back to its archer).
		UAbilitySystemComponent* AttackerASC = Hit.bRanged ? nullptr : UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Hit.Attacker);
		if (AttackerASC != nullptr && Outcome.AttackerDamage > 0.0 && Guard.WeaponDamage > 0.0f)
		{
			const FGameplayEffectSpecHandle Back = TargetASC->MakeOutgoingSpec(UCrossguardDamageEffect::StaticClass(), 1.0f, TargetASC->MakeEffectContext());
			if (Back.IsValid())
			{
				Back.Data->SetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_Attack, Guard.WeaponDamage);
				Back.Data->SetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_AttackShare, static_cast<float>(Outcome.AttackerDamage));
				Back.Data->SetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_Magic, 0.0f);
				TargetASC->ApplyGameplayEffectSpecToTarget(*Back.Data, AttackerASC);
			}
		}
		Stun(TargetASC, AttackerASC, static_cast<float>(Outcome.AttackerStunSeconds));

		// A parry the defender did not lose: the riposte (a clash is no parry).
		if (Outcome.bParryBonus && !bClash)
		{
			const float Seconds = static_cast<float>(Config().ParryBonusSeconds);
			UCrossguardSkillAbility::ApplyStatus(TargetASC, TargetASC, CrossguardTags::State_NextAttack_Riposte, Seconds);
			// One speed bonus at a time: a new parry restarts it.
			TargetASC->RemoveActiveEffectsWithGrantedTags(FGameplayTagContainer(CrossguardTags::State_RiposteSpeed));
			const TMap<FName, float> Faster = {{TEXT("ActionSpeed"), static_cast<float>(Config().ParryActionSpeed)}};
			UCrossguardSkillAbility::ApplyTimedStats(TargetASC, TargetASC, Faster, Seconds, false, CrossguardTags::State_RiposteSpeed);
		}
		return static_cast<float>(Outcome.DefenderDamage);
	}

	void NotifyMeleeAttack(AActor* Attacker, float Reach, AActor* Target)
	{
		UWorld* World = Attacker != nullptr ? Attacker->GetWorld() : nullptr;
		if (World == nullptr || !Attacker->HasAuthority())
		{
			return;
		}
		// Only AIs the swing is aimed at: Target, or those in reach in front of the attacker; never
		// the attacker's allies.
		const auto React = [Attacker](ACrossguardAICharacter* AI)
		{
			if (AI != nullptr && AI != Attacker && AI->IsAlive() && !UCrossguardTeamLibrary::AreAllies(Attacker, AI) && FMath::FRand() < AIGuardChance)
			{
				AI->RaiseGuard(Attacker, AIGuardSeconds);
			}
		};
		if (Target != nullptr)
		{
			React(Cast<ACrossguardAICharacter>(Target));
			return;
		}
		const float MinDot = FMath::Cos(FMath::DegreesToRadians(AISwingHalfAngleDegrees));
		for (TActorIterator<ACrossguardAICharacter> It(World); It; ++It)
		{
			const FVector ToAI = It->GetActorLocation() - Attacker->GetActorLocation();
			if (ToAI.Size() <= Reach + It->GetSimpleCollisionRadius()
				&& FVector::DotProduct(Attacker->GetActorForwardVector(), ToAI.GetSafeNormal2D()) >= MinDot)
			{
				React(*It);
			}
		}
	}
}

// --- Guard ability --------------------------------------------------------------------------------

UCrossguardGuardAbility::UCrossguardGuardAbility()
{
	// Not a skill: no cooldown, and Mute / Silence do not stop it.
	CooldownGameplayEffectClass = nullptr;
	ActivationBlockedTags.RemoveTag(CrossguardTags::State_Muted);
	ActivationBlockedTags.RemoveTag(CrossguardTags::State_Silenced);
	ActivationOwnedTags.AddTag(CrossguardTags::State_Guarding);
	SetAssetTags(FGameplayTagContainer(CrossguardTags::Ability_Guard));
}

bool UCrossguardGuardAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	return Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags) && ActorInfo != nullptr
		&& CrossguardGuard::CanGuard(ActorInfo->AvatarActor.Get());
}

void UCrossguardGuardAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
	if (ActorInfo->IsNetAuthority() && ASC != nullptr)
	{
		// Half speed while held.
		const TMap<FName, float> Slowed = {{TEXT("MoveSpeed"), static_cast<float>(CrossguardGuard::Config().GuardMoveSpeedMultiplier)}};
		SlowHandle = UCrossguardSkillAbility::ApplyTimedStats(ASC, ASC, Slowed, GuardSlowSeconds, true);
	}
	// Held until released (or cancelled by a stun).
}

void UCrossguardGuardAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	UAbilitySystemComponent* ASC = ActorInfo != nullptr ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (ASC != nullptr && SlowHandle.IsValid())
	{
		ASC->RemoveActiveGameplayEffect(SlowHandle);
	}
	SlowHandle.Invalidate();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
