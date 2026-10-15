#pragma once

#include "CoreMinimal.h"
#include "Skills/CrossguardSkillAbility.h"

#include "CrossguardSpellSkills.generated.h"

class ACrossguardMinion;
enum class ECrossguardMinionRole : uint8;
class ACrossguardSpellProjectile;

// [Fireball] (Spell, 15 s): a fireball flies where you aim at 30 m/s. 50 (0.5) Magic Damage to what it
// hits, 15 to everyone else within 2 m (you too). If it hits nothing it explodes after 10 s.
UCLASS()
class CROSSGUARD_API UCrossguardFireballAbility : public UCrossguardSkillAbility
{
	GENERATED_BODY()

public:
	UCrossguardFireballAbility();

	// Set a Blueprint subclass for the visual.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fireball")
	TSubclassOf<ACrossguardSpellProjectile> ProjectileClass;

	// Server. Launches a fireball from Location toward Direction with Caster's stats. Also used by the
	// Skeleton Mage.
	static void LaunchFireball(UAbilitySystemComponent* Caster, TSubclassOf<ACrossguardSpellProjectile> Class, const FVector& Location,
		const FVector& Direction);

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};

// [Heal] (Spell, 12 s): 20 Magical Healing to the character under your crosshair within 15 m, ally
// or enemy; yourself if nobody is there.
UCLASS()
class CROSSGUARD_API UCrossguardHealAbility : public UCrossguardSkillAbility
{
	GENERATED_BODY()

public:
	UCrossguardHealAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

protected:
	// Magical Healing ([Greater Heal] subclasses with 55).
	float HealAmount = 20.0f;
};

// [Mute] (Spell, 180 s): the character under your crosshair within 20 m is Muted for 10 s: no skills,
// passives off, 5 Magic Damage per second. Basic attacks still work. With nobody there it fails and
// no cooldown starts.
UCLASS()
class CROSSGUARD_API UCrossguardMuteAbility : public UCrossguardSkillAbility
{
	GENERATED_BODY()

public:
	UCrossguardMuteAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};

// [Eldritch Shield] (Spell, 25 s): lose 10 HP (never below 1) and gain a barrier for 12 s that
// absorbs 45 Attack Damage. Magic Damage passes through; once used up, damage goes through.
UCLASS()
class CROSSGUARD_API UCrossguardEldritchShieldAbility : public UCrossguardSkillAbility
{
	GENERATED_BODY()

public:
	UCrossguardEldritchShieldAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	UFUNCTION()
	void OnShieldExpired();
};

// [Chop Down] (Active, 25 s, needs an axe): readies your next melee axe attack for 10 s. That attack
// deals 110% damage, slows everyone it hits by 50% for 2 s and Grounds flying targets (their Flight
// ends). The attack itself is UCrossguardWeaponAttackAbility.
UCLASS()
class CROSSGUARD_API UCrossguardChopDownAbility : public UCrossguardSkillAbility
{
	GENERATED_BODY()

public:
	UCrossguardChopDownAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};

// [Summon Skeletons] (Spell, 30 s): 5 Skeleton Warriors (40 HP, 10 Attack Damage hits) and 1
// Skeleton Mage (30 HP, Fireball every 8 s) appear around you for 60 s. Casting again first
// dismisses the ones still standing. See ACrossguardMinion and ACrossguardMinionController.
UCLASS()
class CROSSGUARD_API UCrossguardSummonSkeletonsAbility : public UCrossguardSkillAbility
{
	GENERATED_BODY()

public:
	UCrossguardSummonSkeletonsAbility();

	// Set Blueprint subclasses for the skeleton meshes.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Summon")
	TSubclassOf<ACrossguardMinion> WarriorClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Summon")
	TSubclassOf<ACrossguardMinion> MageClass;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void OnRemoveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override;

private:
	void Summon(TSubclassOf<ACrossguardMinion> Class, ECrossguardMinionRole Role, float Health, const FVector& Location);

	TArray<TWeakObjectPtr<ACrossguardMinion>> Minions;
};
