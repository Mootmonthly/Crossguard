#include "Combat/CrossguardWeaponAttackAbility.h"

#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "AbilitySystem/CrossguardEffects.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Combat/CrossguardProjectile.h"
#include "Engine/World.h"
#include "Items/CrossguardEquipmentComponent.h"
#include "Items/CrossguardInventoryComponent.h"
#include "Skills/CrossguardSkillAbility.h"

// "Weapon" trace channel, defined in Config/DefaultEngine.ini. Blocked by default.
#define CROSSGUARD_TRACE_WEAPON ECC_GameTraceChannel1

UCrossguardWeaponAttackAbility::UCrossguardWeaponAttackAbility()
{
	// Not a skill: no cooldown effect, no CDR. The attack interval is the rate limit.
	CooldownGameplayEffectClass = nullptr;
	// Basic attacks still work while Muted.
	ActivationBlockedTags.RemoveTag(CrossguardTags::State_Muted);
	DefaultProjectileClass = ACrossguardProjectile::StaticClass();
}

namespace
{
	const UCrossguardEquipmentComponent* FindEquipment(const FGameplayAbilityActorInfo* ActorInfo)
	{
		const AActor* Avatar = ActorInfo != nullptr ? ActorInfo->AvatarActor.Get() : nullptr;
		return Avatar != nullptr ? Avatar->FindComponentByClass<UCrossguardEquipmentComponent>() : nullptr;
	}

	UCrossguardInventoryComponent* FindInventory(const FGameplayAbilityActorInfo* ActorInfo)
	{
		const AActor* Avatar = ActorInfo != nullptr ? ActorInfo->AvatarActor.Get() : nullptr;
		return Avatar != nullptr ? Avatar->FindComponentByClass<UCrossguardInventoryComponent>() : nullptr;
	}

	// [Chop Down]: 110% damage, 50% slow for 2 s and Grounded flying targets.
	constexpr float ChopDownDamageMultiplier = 1.1f;
	constexpr float ChopDownSlow = 0.5f;
	constexpr float ChopDownSlowSeconds = 2.0f;

	// Weapons without ammo always have it. Characters without an inventory (AI) shoot freely.
	bool HasAmmo(const FCrossguardWeaponRow& Weapon, const FGameplayAbilityActorInfo* ActorInfo)
	{
		const UCrossguardInventoryComponent* Inventory = FindInventory(ActorInfo);
		return Weapon.AmmoId.IsNone() || Inventory == nullptr || Inventory->CountItem(Weapon.AmmoId) > 0;
	}
}

bool UCrossguardWeaponAttackAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}
	const UCrossguardEquipmentComponent* Equipment = FindEquipment(ActorInfo);
	const FCrossguardWeaponRow* Weapon = Equipment != nullptr ? Equipment->GetWeaponRow(Slot) : nullptr;
	return Weapon != nullptr && Weapon->Delivery != ECrossguardAttackDelivery::None && Equipment->GetAttackInterval(Slot) > 0.0f
		&& Equipment->AreWeaponsReady() && HasAmmo(*Weapon, ActorInfo);
}

void UCrossguardWeaponAttackAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	const UCrossguardEquipmentComponent* Equipment = FindEquipment(ActorInfo);
	const FCrossguardWeaponRow* Weapon = Equipment != nullptr ? Equipment->GetWeaponRow(Slot) : nullptr;
	const float Interval = Equipment != nullptr ? Equipment->GetAttackInterval(Slot) : 0.0f;

	if (Weapon == nullptr || Interval <= 0.0f || !CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (ActorInfo->IsNetAuthority())
	{
		// Each shot uses up one ammo (bows: one arrow).
		UCrossguardInventoryComponent* Inventory = FindInventory(ActorInfo);
		if (!Weapon->AmmoId.IsNone() && Inventory != nullptr && !Inventory->ConsumeItem(Weapon->AmmoId))
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}

		// [Chop Down] readies the next melee axe attack, which uses it up whether or not it hits.
		UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
		const bool bChopDown = ASC != nullptr && Weapon->WeaponType == ECrossguardWeaponType::Axe
			&& Weapon->Delivery == ECrossguardAttackDelivery::MeleeSwing && ASC->HasMatchingGameplayTag(CrossguardTags::State_ChopDownReady);

		const FCrossguardWeaponItem& Item = Equipment->GetItem(Slot);
		const FGameplayEffectSpecHandle Spec = MakeWeaponDamageSpec(*Weapon, Item, bChopDown ? ChopDownDamageMultiplier : 1.0f);
		switch (Weapon->Delivery)
		{
		case ECrossguardAttackDelivery::MeleeSwing:
		{
			const TArray<AActor*> Hit = PerformMeleeSwing(Spec);
			if (bChopDown)
			{
				ASC->RemoveActiveEffectsWithGrantedTags(FGameplayTagContainer(CrossguardTags::State_ChopDownReady));
				for (AActor* Target : Hit)
				{
					UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
					if (TargetASC == nullptr)
					{
						continue;
					}
					UCrossguardSkillAbility::ApplySlow(ASC, TargetASC, ChopDownSlow, ChopDownSlowSeconds);
					// Grounded: Flight ends and they fall.
					if (TargetASC->HasMatchingGameplayTag(CrossguardTags::State_Flying))
					{
						const FGameplayTagContainer FlightTag(CrossguardTags::Ability_Flight);
						TargetASC->CancelAbilities(&FlightTag);
					}
				}
			}
			break;
		}
		case ECrossguardAttackDelivery::Hitscan:
			PerformHitscan(Spec);
			break;
		case ECrossguardAttackDelivery::Projectile:
			SpawnProjectile(Spec, Item.WeaponId);
			break;
		case ECrossguardAttackDelivery::None:
			break;
		}
	}

	float RecoverySeconds = Interval;
	if (!ActorInfo->IsNetAuthority())
	{
		RecoverySeconds = FMath::Max(Interval - ClientRecoveryTolerance, 0.0f);
	}
	UAbilityTask_WaitDelay* Recovery = UAbilityTask_WaitDelay::WaitDelay(this, RecoverySeconds);
	Recovery->OnFinish.AddDynamic(this, &UCrossguardWeaponAttackAbility::OnAttackRecovered);
	Recovery->ReadyForActivation();
}

UCrossguardOffHandAttackAbility::UCrossguardOffHandAttackAbility()
{
	Slot = ECrossguardEquipmentSlot::OffHand;
}

void UCrossguardWeaponAttackAbility::OnAttackRecovered()
{
	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
}

FGameplayEffectSpecHandle UCrossguardWeaponAttackAbility::MakeWeaponDamageSpec(const FCrossguardWeaponRow& Weapon, const FCrossguardWeaponItem& Item,
	float Multiplier) const
{
	FGameplayEffectSpecHandle Spec = MakeOutgoingGameplayEffectSpec(UCrossguardDamageEffect::StaticClass(), GetAbilityLevel());
	if (!Spec.IsValid())
	{
		return Spec;
	}
	const float Value = UCrossguardItemLibrary::GetWeaponValue(Weapon, Item.Rarity) * Multiplier;
	switch (Weapon.Stat)
	{
	case ECrossguardWeaponStat::AttackDamage:
		Spec.Data->SetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_Attack, Value);
		break;
	case ECrossguardWeaponStat::MagicDamage:
		Spec.Data->SetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_Magic, Value);
		break;
	case ECrossguardWeaponStat::AttackResistance:
		break;
	}
	return Spec;
}

bool UCrossguardWeaponAttackAbility::ApplyDamageTo(AActor* Target, const FGameplayEffectSpecHandle& Spec) const
{
	// Friendly fire is always on: allies are hit too.
	if (Target == nullptr || Target == GetAvatarActorFromActorInfo() || !Spec.IsValid())
	{
		return false;
	}
	if (UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target))
	{
		GetAbilitySystemComponentFromActorInfo()->ApplyGameplayEffectSpecToTarget(*Spec.Data, TargetASC);
		return true;
	}
	return false;
}

TArray<AActor*> UCrossguardWeaponAttackAbility::PerformMeleeSwing(const FGameplayEffectSpecHandle& Spec) const
{
	TArray<AActor*> Damaged;
	AActor* Avatar = GetAvatarActorFromActorInfo();
	UWorld* World = Avatar != nullptr ? Avatar->GetWorld() : nullptr;
	if (World == nullptr)
	{
		return Damaged;
	}

	// Start one radius ahead so the sphere does not reach behind the character.
	const FVector Forward = Avatar->GetActorForwardVector();
	const FVector Start = Avatar->GetActorLocation() + Forward * MeleeRadius;
	const FVector End = Avatar->GetActorLocation() + Forward * FMath::Max(MeleeRange, MeleeRadius);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CrossguardMeleeSwing), false, Avatar);

	// Object-type sweep: returns every pawn in the path. A channel sweep would stop at the first
	// blocking pawn.
	TArray<FHitResult> Hits;
	World->SweepMultiByObjectType(Hits, Start, End, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeSphere(MeleeRadius), Params);

	TSet<AActor*> Seen;
	for (const FHitResult& Hit : Hits)
	{
		AActor* Target = Hit.GetActor();
		if (Target != nullptr && !Seen.Contains(Target))
		{
			Seen.Add(Target);
			if (ApplyDamageTo(Target, Spec))
			{
				Damaged.Add(Target);
			}
		}
	}
	return Damaged;
}

void UCrossguardWeaponAttackAbility::PerformHitscan(const FGameplayEffectSpecHandle& Spec) const
{
	AActor* Avatar = GetAvatarActorFromActorInfo();
	UWorld* World = Avatar != nullptr ? Avatar->GetWorld() : nullptr;
	if (World == nullptr)
	{
		return;
	}

	FVector EyeLocation;
	FRotator EyeRotation;
	Avatar->GetActorEyesViewPoint(EyeLocation, EyeRotation);
	const FVector End = EyeLocation + EyeRotation.Vector() * HitscanRange;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CrossguardHitscan), false, Avatar);

	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, EyeLocation, End, CROSSGUARD_TRACE_WEAPON, Params))
	{
		ApplyDamageTo(Hit.GetActor(), Spec);
	}
}

void UCrossguardWeaponAttackAbility::SpawnProjectile(const FGameplayEffectSpecHandle& Spec, FName WeaponId) const
{
	AActor* Avatar = GetAvatarActorFromActorInfo();
	UWorld* World = Avatar != nullptr ? Avatar->GetWorld() : nullptr;
	const TSubclassOf<ACrossguardProjectile>* Mapped = ProjectileClasses.Find(WeaponId);
	const TSubclassOf<ACrossguardProjectile> ProjectileClass = Mapped != nullptr ? *Mapped : DefaultProjectileClass;
	if (World == nullptr || ProjectileClass == nullptr)
	{
		return;
	}

	FVector EyeLocation;
	FRotator EyeRotation;
	Avatar->GetActorEyesViewPoint(EyeLocation, EyeRotation);
	const FTransform SpawnTransform(EyeRotation, EyeLocation + EyeRotation.Vector() * 50.0f);

	ACrossguardProjectile* Projectile = World->SpawnActorDeferred<ACrossguardProjectile>(ProjectileClass, SpawnTransform, Avatar,
		Cast<APawn>(Avatar), ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Projectile != nullptr)
	{
		Projectile->DamageSpec = Spec;
		Projectile->FinishSpawning(SpawnTransform);
	}
}
