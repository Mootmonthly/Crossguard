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
	// Other next-attack skills, Splinter of Winter and Arcane Arrows (Data/Skills/Skills.json).
	constexpr float SlashWeaponDamage = 7.0f;
	constexpr float MortalStrikeAttackDamage = 20.0f;
	constexpr float ShadeBlowMagicDamage = 20.0f;
	constexpr float ShadeBlowHealBlockSeconds = 5.0f;
	constexpr float GougeDamagePerSecond = 5.0f;
	constexpr float GougeSeconds = 5.0f;
	constexpr float GougeSlow = 0.05f;
	constexpr float PurgeShotMagicDamage = 10.0f;
	constexpr float PurgeShotMuteSeconds = 3.0f;
	constexpr float SplinterMagicDamage = 5.0f;
	constexpr float SplinterSlow = 0.1f;
	constexpr float SplinterSlowSeconds = 3.0f;
	constexpr float ArcaneArrowsMagicDamage = 5.0f;

	bool IsRangedWeapon(ECrossguardWeaponType Type)
	{
		return Type == ECrossguardWeaponType::Bow || Type == ECrossguardWeaponType::Crossbow;
	}

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

		UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
		const FCrossguardAttackRiders Riders = GatherRiders(*Weapon, ASC);
		const FCrossguardWeaponItem& Item = Equipment->GetItem(Slot);
		const FGameplayEffectSpecHandle Spec = MakeWeaponDamageSpec(*Weapon, Item, Riders);
		switch (Weapon->Delivery)
		{
		case ECrossguardAttackDelivery::MeleeSwing:
		{
			const TArray<AActor*> Hit = PerformMeleeSwing(Spec);
			bool bKilled = false;
			for (AActor* Target : Hit)
			{
				UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
				ApplyRiders(Riders, TargetASC);
				bKilled |= TargetASC != nullptr && TargetASC->HasMatchingGameplayTag(CrossguardTags::State_Dead);
			}
			// [Mortal Strike]: a kill gives the skill back.
			if (Riders.bMortalStrike && bKilled && ASC != nullptr)
			{
				ASC->RemoveActiveEffectsWithGrantedTags(FGameplayTagContainer(CrossguardTags::Cooldown_Skill_MortalStrike));
			}
			break;
		}
		case ECrossguardAttackDelivery::Hitscan:
			if (AActor* Target = PerformHitscan(Spec))
			{
				ApplyRiders(Riders, UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target));
			}
			break;
		case ECrossguardAttackDelivery::Projectile:
			SpawnProjectile(Spec, Item.WeaponId, Riders);
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

FCrossguardAttackRiders UCrossguardWeaponAttackAbility::GatherRiders(const FCrossguardWeaponRow& Weapon, UAbilitySystemComponent* ASC) const
{
	FCrossguardAttackRiders Riders;
	if (ASC == nullptr)
	{
		return Riders;
	}
	const bool bMelee = Weapon.Delivery == ECrossguardAttackDelivery::MeleeSwing;
	const bool bRanged = IsRangedWeapon(Weapon.WeaponType);
	FGameplayTagContainer UsedUp;
	// A readied next-attack skill is used up by the next qualifying attack, hit or miss.
	const auto Ready = [ASC, &UsedUp](const FGameplayTag& Tag, bool bQualifies)
	{
		if (bQualifies && ASC->HasMatchingGameplayTag(Tag))
		{
			UsedUp.AddTag(Tag);
			return true;
		}
		return false;
	};

	if (Ready(CrossguardTags::State_ChopDownReady, bMelee && Weapon.WeaponType == ECrossguardWeaponType::Axe))
	{
		Riders.WeaponMultiplier *= ChopDownDamageMultiplier;
		Riders.OnHitSpecs.Add(UCrossguardSkillAbility::MakeSlowSpec(ASC, ChopDownSlow, ChopDownSlowSeconds));
		Riders.bGroundFlying = true;
	}
	if (Ready(CrossguardTags::State_NextAttack_Slash, bMelee))
	{
		Riders.WeaponFlat += SlashWeaponDamage;
	}
	if (Ready(CrossguardTags::State_NextAttack_MortalStrike, bMelee))
	{
		Riders.AttackAdd += MortalStrikeAttackDamage;
		Riders.bMortalStrike = true;
	}
	if (Ready(CrossguardTags::State_NextAttack_ShadeBlow, bMelee))
	{
		Riders.MagicAdd += ShadeBlowMagicDamage;
		Riders.OnHitSpecs.Add(UCrossguardSkillAbility::MakeStatusSpec(ASC, CrossguardTags::State_HealBlocked, ShadeBlowHealBlockSeconds));
	}
	if (Ready(CrossguardTags::State_NextAttack_Gouge, bMelee))
	{
		Riders.OnHitSpecs.Add(UCrossguardSkillAbility::MakeDamageOverTimeSpec(ASC, GougeDamagePerSecond, 0.0f, GougeSeconds));
		Riders.OnHitSpecs.Add(UCrossguardSkillAbility::MakeSlowSpec(ASC, GougeSlow, GougeSeconds));
	}
	if (Ready(CrossguardTags::State_NextAttack_PurgeShot, bRanged))
	{
		Riders.MagicAdd += PurgeShotMagicDamage;
		Riders.OnHitSpecs.Add(UCrossguardSkillAbility::MakeMuteSpec(ASC, PurgeShotMuteSeconds, 0.0f));
		Riders.bCancelSkills = true;
	}
	if (!UsedUp.IsEmpty())
	{
		ASC->RemoveActiveEffectsWithGrantedTags(UsedUp);
	}

	// Always-on: [Arcane Arrows] while active, [Splinter of Winter] while equipped and not Muted.
	if (Weapon.WeaponType == ECrossguardWeaponType::Bow && ASC->HasMatchingGameplayTag(CrossguardTags::State_ArcaneArrows))
	{
		Riders.MagicAdd += ArcaneArrowsMagicDamage;
	}
	if (Weapon.WeaponType == ECrossguardWeaponType::Dagger && ASC->HasMatchingGameplayTag(CrossguardTags::Passive_SplinterOfWinter)
		&& !ASC->HasMatchingGameplayTag(CrossguardTags::State_Muted))
	{
		Riders.MagicAdd += SplinterMagicDamage;
		Riders.OnHitSpecs.Add(UCrossguardSkillAbility::MakeSlowSpec(ASC, SplinterSlow, SplinterSlowSeconds));
	}
	return Riders;
}

void UCrossguardWeaponAttackAbility::ApplyRiders(const FCrossguardAttackRiders& Riders, UAbilitySystemComponent* TargetASC) const
{
	if (TargetASC == nullptr)
	{
		return;
	}
	for (const FGameplayEffectSpecHandle& OnHit : Riders.OnHitSpecs)
	{
		UCrossguardSkillAbility::ApplyOnHitSpec(OnHit, TargetASC);
	}
	if (Riders.bCancelSkills)
	{
		UCrossguardSkillAbility::CancelActiveSkills(TargetASC);
	}
	// Grounded ([Chop Down]): Flight ends and they fall. Crowd control.
	if (Riders.bGroundFlying && TargetASC->HasMatchingGameplayTag(CrossguardTags::State_Flying)
		&& !UCrossguardSkillAbility::IsCrowdControlImmune(TargetASC))
	{
		const FGameplayTagContainer FlightTag(CrossguardTags::Ability_Flight);
		TargetASC->CancelAbilities(&FlightTag);
	}
}

FGameplayEffectSpecHandle UCrossguardWeaponAttackAbility::MakeWeaponDamageSpec(const FCrossguardWeaponRow& Weapon, const FCrossguardWeaponItem& Item,
	const FCrossguardAttackRiders& Riders) const
{
	FGameplayEffectSpecHandle Spec = MakeOutgoingGameplayEffectSpec(UCrossguardDamageEffect::StaticClass(), GetAbilityLevel());
	if (!Spec.IsValid())
	{
		return Spec;
	}
	const float Value = UCrossguardItemLibrary::GetWeaponValue(Weapon, Item.Rarity) * Riders.WeaponMultiplier + Riders.WeaponFlat;
	float Attack = Riders.AttackAdd;
	float Magic = Riders.MagicAdd;
	switch (Weapon.Stat)
	{
	case ECrossguardWeaponStat::AttackDamage:
		Attack += Value;
		break;
	case ECrossguardWeaponStat::MagicDamage:
		Magic += Value;
		break;
	case ECrossguardWeaponStat::AttackResistance:
		break;
	}
	Spec.Data->SetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_Attack, Attack);
	Spec.Data->SetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_Magic, Magic);
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

AActor* UCrossguardWeaponAttackAbility::PerformHitscan(const FGameplayEffectSpecHandle& Spec) const
{
	AActor* Avatar = GetAvatarActorFromActorInfo();
	UWorld* World = Avatar != nullptr ? Avatar->GetWorld() : nullptr;
	if (World == nullptr)
	{
		return nullptr;
	}

	FVector EyeLocation;
	FRotator EyeRotation;
	Avatar->GetActorEyesViewPoint(EyeLocation, EyeRotation);
	const FVector End = EyeLocation + EyeRotation.Vector() * HitscanRange;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CrossguardHitscan), false, Avatar);

	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, EyeLocation, End, CROSSGUARD_TRACE_WEAPON, Params) && ApplyDamageTo(Hit.GetActor(), Spec))
	{
		return Hit.GetActor();
	}
	return nullptr;
}

void UCrossguardWeaponAttackAbility::SpawnProjectile(const FGameplayEffectSpecHandle& Spec, FName WeaponId, const FCrossguardAttackRiders& Riders) const
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
		Projectile->OnHitSpecs = Riders.OnHitSpecs;
		Projectile->bCancelSkillsOnHit = Riders.bCancelSkills;
		Projectile->FinishSpawning(SpawnTransform);
	}
}
