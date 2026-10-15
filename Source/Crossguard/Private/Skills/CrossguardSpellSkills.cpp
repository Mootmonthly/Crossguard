#include "Skills/CrossguardSpellSkills.h"

#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "AbilitySystem/CrossguardAttributeSet.h"
#include "AbilitySystem/CrossguardEffects.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Character/CrossguardMinion.h"
#include "Combat/CrossguardSpellProjectile.h"
#include "Engine/World.h"

namespace
{
	// Data/Skills/Skills.json and docs/design-decisions.md (rulings on these skills).
	constexpr float FireballSpeed = 3000.0f;
	constexpr float FireballLifeSeconds = 10.0f;
	constexpr float FireballSplashRadius = 200.0f;
	constexpr float HealRange = 1500.0f;
	constexpr float MuteRange = 2000.0f;
	constexpr float MuteSeconds = 10.0f;
	constexpr float MuteDamagePerSecond = 5.0f;
	constexpr float EldritchShieldAmount = 45.0f;
	constexpr float EldritchShieldSeconds = 12.0f;
	constexpr float EldritchShieldHealthCost = 10.0f;
	// Assumption: how long the readied attack waits before it is lost.
	constexpr float ChopDownReadySeconds = 10.0f;
	constexpr int32 SkeletonWarriors = 5;
	constexpr float SkeletonWarriorHealth = 40.0f;
	constexpr float SkeletonMageHealth = 30.0f;
	constexpr float SkeletonSeconds = 60.0f;
	// Assumption: how far from the caster they appear.
	constexpr float SkeletonRingRadius = 250.0f;
}

// --- Fireball -------------------------------------------------------------------------------------

UCrossguardFireballAbility::UCrossguardFireballAbility()
{
	SkillId = TEXT("fireball");
	SkillCategory = ECrossguardSkillCategory::Spell;
	BaseCooldownSeconds = 15.0f;
	CooldownTags.AddTag(CrossguardTags::Cooldown_Skill_Fireball);
	ProjectileClass = ACrossguardSpellProjectile::StaticClass();
}

void UCrossguardFireballAbility::LaunchFireball(UAbilitySystemComponent* Caster, TSubclassOf<ACrossguardSpellProjectile> Class,
	const FVector& Location, const FVector& Direction)
{
	AActor* Avatar = Caster != nullptr ? Caster->GetAvatarActor() : nullptr;
	UWorld* World = Avatar != nullptr ? Avatar->GetWorld() : nullptr;
	if (World == nullptr || Class == nullptr)
	{
		return;
	}
	const auto MakeMagic = [Caster](float Damage, float Coefficient)
	{
		FGameplayEffectSpecHandle Spec = Caster->MakeOutgoingSpec(UCrossguardDamageEffect::StaticClass(), 1.0f, Caster->MakeEffectContext());
		if (Spec.IsValid())
		{
			Spec.Data->SetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_Magic, Damage);
			Spec.Data->SetSetByCallerMagnitude(CrossguardTags::SetByCaller_Coefficient, Coefficient);
		}
		return Spec;
	};

	const FTransform SpawnTransform(Direction.Rotation(), Location + Direction * 60.0f);
	ACrossguardSpellProjectile* Fireball = World->SpawnActorDeferred<ACrossguardSpellProjectile>(Class, SpawnTransform, Avatar,
		Cast<APawn>(Avatar), ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Fireball == nullptr)
	{
		return;
	}
	Fireball->DamageSpec = MakeMagic(50.0f, 0.5f);
	Fireball->SplashSpec = MakeMagic(15.0f, 1.0f);
	Fireball->SplashRadius = FireballSplashRadius;
	Fireball->SetFlight(FireballSpeed, FireballLifeSeconds);
	Fireball->FinishSpawning(SpawnTransform);
}

void UCrossguardFireballAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitOrCancel(Handle, ActorInfo, ActivationInfo))
	{
		return;
	}
	if (ActorInfo->IsNetAuthority())
	{
		FVector Location;
		FVector Direction;
		GetAim(Location, Direction);
		LaunchFireball(ActorInfo->AbilitySystemComponent.Get(), ProjectileClass, Location, Direction);
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

// --- Heal -----------------------------------------------------------------------------------------

UCrossguardHealAbility::UCrossguardHealAbility()
{
	SkillId = TEXT("heal");
	SkillCategory = ECrossguardSkillCategory::Spell;
	BaseCooldownSeconds = 12.0f;
	CooldownTags.AddTag(CrossguardTags::Cooldown_Skill_Heal);
}

void UCrossguardHealAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitOrCancel(Handle, ActorInfo, ActivationInfo))
	{
		return;
	}
	if (ActorInfo->IsNetAuthority())
	{
		// Whoever is under the crosshair, ally or enemy; yourself otherwise.
		AActor* Target = FindAimedTarget(HealRange);
		UAbilitySystemComponent* TargetASC = Target != nullptr ? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target)
			: ActorInfo->AbilitySystemComponent.Get();
		const FGameplayEffectSpecHandle Spec = MakeHealingSpec(HealAmount);
		if (TargetASC != nullptr && Spec.IsValid())
		{
			TargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
		}
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

// --- Mute -----------------------------------------------------------------------------------------

UCrossguardMuteAbility::UCrossguardMuteAbility()
{
	SkillId = TEXT("mute");
	SkillCategory = ECrossguardSkillCategory::Spell;
	BaseCooldownSeconds = 180.0f;
	CooldownTags.AddTag(CrossguardTags::Cooldown_Skill_Mute);
}

void UCrossguardMuteAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	// Nobody under the crosshair: fail without using the cooldown.
	AActor* Target = FindAimedTarget(MuteRange);
	UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
	if (TargetASC == nullptr)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	if (!CommitOrCancel(Handle, ActorInfo, ActivationInfo))
	{
		return;
	}
	if (ActorInfo->IsNetAuthority())
	{
		const FGameplayEffectSpecHandle Spec = MakeMuteSpec(ActorInfo->AbilitySystemComponent.Get(), MuteSeconds, MuteDamagePerSecond);
		if (Spec.IsValid())
		{
			TargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
			// Skills already running stop too (Flight, Dragonification, a shield, a breath).
			CancelActiveSkills(TargetASC);
		}
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

// --- Eldritch Shield ------------------------------------------------------------------------------

UCrossguardEldritchShieldAbility::UCrossguardEldritchShieldAbility()
{
	SkillId = TEXT("eldritch_shield");
	SkillCategory = ECrossguardSkillCategory::Spell;
	BaseCooldownSeconds = 25.0f;
	CooldownTags.AddTag(CrossguardTags::Cooldown_Skill_EldritchShield);
}

void UCrossguardEldritchShieldAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitOrCancel(Handle, ActorInfo, ActivationInfo))
	{
		return;
	}
	UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
	if (ActorInfo->IsNetAuthority() && ASC != nullptr)
	{
		const float Health = ASC->GetNumericAttribute(UCrossguardAttributeSet::GetHealthAttribute());
		ASC->SetNumericAttributeBase(UCrossguardAttributeSet::GetHealthAttribute(), FMath::Max(1.0f, Health - EldritchShieldHealthCost));
		ASC->SetNumericAttributeBase(UCrossguardAttributeSet::GetAttackShieldAttribute(), EldritchShieldAmount);
	}

	// The ability stays active while the barrier lasts; ending it removes what is left.
	UAbilityTask_WaitDelay* Expire = UAbilityTask_WaitDelay::WaitDelay(this, EldritchShieldSeconds);
	Expire->OnFinish.AddDynamic(this, &UCrossguardEldritchShieldAbility::OnShieldExpired);
	Expire->ReadyForActivation();
}

void UCrossguardEldritchShieldAbility::OnShieldExpired()
{
	EndOnTimer();
}

void UCrossguardEldritchShieldAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	UAbilitySystemComponent* ASC = ActorInfo != nullptr ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (ASC != nullptr && ActorInfo->IsNetAuthority())
	{
		ASC->SetNumericAttributeBase(UCrossguardAttributeSet::GetAttackShieldAttribute(), 0.0f);
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

// --- Chop Down ------------------------------------------------------------------------------------

UCrossguardChopDownAbility::UCrossguardChopDownAbility()
{
	SkillId = TEXT("chop_down");
	SkillCategory = ECrossguardSkillCategory::Active;
	BaseCooldownSeconds = 25.0f;
	CooldownTags.AddTag(CrossguardTags::Cooldown_Skill_ChopDown);
	RequiredWeapons.Add(ECrossguardWeaponType::Axe);
	SetAssetTags(FGameplayTagContainer(CrossguardTags::Skill_Weapon_Melee));
}

void UCrossguardChopDownAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitOrCancel(Handle, ActorInfo, ActivationInfo))
	{
		return;
	}
	if (ActorInfo->IsNetAuthority())
	{
		UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
		ApplyStatus(ASC, ASC, CrossguardTags::State_ChopDownReady, ChopDownReadySeconds);
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

// --- Summon Skeletons -----------------------------------------------------------------------------

UCrossguardSummonSkeletonsAbility::UCrossguardSummonSkeletonsAbility()
{
	SkillId = TEXT("summon_skeletons");
	SkillCategory = ECrossguardSkillCategory::Spell;
	BaseCooldownSeconds = 30.0f;
	CooldownTags.AddTag(CrossguardTags::Cooldown_Skill_SummonSkeletons);
	WarriorClass = ACrossguardMinion::StaticClass();
	MageClass = ACrossguardMinion::StaticClass();
	SetAssetTags(FGameplayTagContainer(CrossguardTags::Skill_Minion));
}

void UCrossguardSummonSkeletonsAbility::OnRemoveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	// Unequipped: the skeletons go with the skill.
	for (const TWeakObjectPtr<ACrossguardMinion>& Old : Minions)
	{
		if (ACrossguardMinion* Minion = Old.Get())
		{
			Minion->Dismiss();
		}
	}
	Minions.Reset();
	Super::OnRemoveAbility(ActorInfo, Spec);
}

void UCrossguardSummonSkeletonsAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitOrCancel(Handle, ActorInfo, ActivationInfo))
	{
		return;
	}
	const AActor* Avatar = ActorInfo->AvatarActor.Get();
	if (ActorInfo->IsNetAuthority() && Avatar != nullptr)
	{
		// The previous ones still standing die.
		for (const TWeakObjectPtr<ACrossguardMinion>& Old : Minions)
		{
			if (ACrossguardMinion* Minion = Old.Get())
			{
				Minion->Dismiss();
			}
		}
		Minions.Reset();

		// In a ring around the caster, the Mage behind.
		const FVector Center = Avatar->GetActorLocation();
		const FRotator Facing(0.0f, Avatar->GetActorRotation().Yaw, 0.0f);
		for (int32 Index = 0; Index < SkeletonWarriors; ++Index)
		{
			const float Yaw = -90.0f + 180.0f * Index / (SkeletonWarriors - 1);
			const FVector Offset = Facing.RotateVector(FRotator(0.0f, Yaw, 0.0f).Vector() * SkeletonRingRadius);
			Summon(WarriorClass, ECrossguardMinionRole::Warrior, SkeletonWarriorHealth, Center + Offset);
		}
		Summon(MageClass, ECrossguardMinionRole::Mage, SkeletonMageHealth, Center - Facing.Vector() * SkeletonRingRadius);
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

void UCrossguardSummonSkeletonsAbility::Summon(TSubclassOf<ACrossguardMinion> Class, ECrossguardMinionRole Role, float Health,
	const FVector& Location)
{
	ACrossguardCharacter* Summoner = Cast<ACrossguardCharacter>(GetAvatarActorFromActorInfo());
	UWorld* World = Summoner != nullptr ? Summoner->GetWorld() : nullptr;
	if (World == nullptr || Class == nullptr)
	{
		return;
	}
	const FTransform Transform(Summoner->GetActorRotation(), Location);
	// Nudged out of walls and each other; skipped if there is no room.
	ACrossguardMinion* Minion = World->SpawnActorDeferred<ACrossguardMinion>(Class, Transform, Summoner, Summoner,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding);
	if (Minion == nullptr)
	{
		return;
	}
	Minion->MinionRole = Role;
	Minion->MinionHealth = Health;
	Minion->SetSummoner(Summoner);
	Minion->FinishSpawning(Transform);
	if (IsValid(Minion))
	{
		Minion->SetLifeSpan(SkeletonSeconds);
		Minions.Add(Minion);
	}
}
