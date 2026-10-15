#include "Combat/CrossguardWeaponAttackAbility.h"

#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "AbilitySystem/CrossguardEffects.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Combat/CrossguardProjectile.h"
#include "Engine/World.h"
#include "Items/CrossguardEquipmentComponent.h"

// "Weapon" trace channel, defined in Config/DefaultEngine.ini. Blocked by default.
#define CROSSGUARD_TRACE_WEAPON ECC_GameTraceChannel1

UCrossguardWeaponAttackAbility::UCrossguardWeaponAttackAbility()
{
	// Not a skill: no cooldown effect, no CDR. The attack interval is the rate limit.
	CooldownGameplayEffectClass = nullptr;
	DefaultProjectileClass = ACrossguardProjectile::StaticClass();
}

void UCrossguardWeaponAttackAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	const AActor* Avatar = ActorInfo != nullptr ? ActorInfo->AvatarActor.Get() : nullptr;
	const UCrossguardEquipmentComponent* Equipment = Avatar != nullptr ? Avatar->FindComponentByClass<UCrossguardEquipmentComponent>() : nullptr;
	const FCrossguardWeaponRow* Weapon = Equipment != nullptr ? Equipment->GetWeaponRow(ECrossguardEquipmentSlot::MainHand) : nullptr;
	const float Interval = Equipment != nullptr ? Equipment->GetAttackInterval() : 0.0f;

	if (Weapon == nullptr || Interval <= 0.0f || !CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (ActorInfo->IsNetAuthority())
	{
		const FCrossguardWeaponItem& Item = Equipment->GetItem(ECrossguardEquipmentSlot::MainHand);
		const FGameplayEffectSpecHandle Spec = MakeWeaponDamageSpec(*Weapon, Item);
		switch (Weapon->Delivery)
		{
		case ECrossguardAttackDelivery::MeleeSwing:
			PerformMeleeSwing(Spec);
			break;
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

	UAbilityTask_WaitDelay* Recovery = UAbilityTask_WaitDelay::WaitDelay(this, Interval);
	Recovery->OnFinish.AddDynamic(this, &UCrossguardWeaponAttackAbility::OnAttackRecovered);
	Recovery->ReadyForActivation();
}

void UCrossguardWeaponAttackAbility::OnAttackRecovered()
{
	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
}

FGameplayEffectSpecHandle UCrossguardWeaponAttackAbility::MakeWeaponDamageSpec(const FCrossguardWeaponRow& Weapon, const FCrossguardWeaponItem& Item) const
{
	FGameplayEffectSpecHandle Spec = MakeOutgoingGameplayEffectSpec(UCrossguardDamageEffect::StaticClass(), GetAbilityLevel());
	if (!Spec.IsValid())
	{
		return Spec;
	}
	const float Value = UCrossguardItemLibrary::GetWeaponValue(Weapon, Item.Rarity);
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

void UCrossguardWeaponAttackAbility::ApplyDamageTo(AActor* Target, const FGameplayEffectSpecHandle& Spec) const
{
	// TODO: skip allies once teams exist (no friendly fire).
	if (Target == nullptr || Target == GetAvatarActorFromActorInfo() || !Spec.IsValid())
	{
		return;
	}
	if (UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target))
	{
		GetAbilitySystemComponentFromActorInfo()->ApplyGameplayEffectSpecToTarget(*Spec.Data, TargetASC);
	}
}

void UCrossguardWeaponAttackAbility::PerformMeleeSwing(const FGameplayEffectSpecHandle& Spec) const
{
	AActor* Avatar = GetAvatarActorFromActorInfo();
	UWorld* World = Avatar != nullptr ? Avatar->GetWorld() : nullptr;
	if (World == nullptr)
	{
		return;
	}

	const FVector Start = Avatar->GetActorLocation();
	const FVector End = Start + Avatar->GetActorForwardVector() * MeleeRange;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CrossguardMeleeSwing), false, Avatar);

	// Object-type sweep: returns every pawn in the path. A channel sweep would stop at the first
	// blocking pawn.
	TArray<FHitResult> Hits;
	World->SweepMultiByObjectType(Hits, Start, End, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeSphere(MeleeRadius), Params);

	TSet<AActor*> Damaged;
	for (const FHitResult& Hit : Hits)
	{
		AActor* Target = Hit.GetActor();
		if (Target != nullptr && !Damaged.Contains(Target))
		{
			Damaged.Add(Target);
			ApplyDamageTo(Target, Spec);
		}
	}
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
