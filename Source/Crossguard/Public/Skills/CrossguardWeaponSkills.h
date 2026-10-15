#pragma once

#include "CoreMinimal.h"
#include "Skills/CrossguardSkillAbility.h"

#include "CrossguardWeaponSkills.generated.h"

// Base for "your next attack ..." skills: readies a State.NextAttack.* tag for ReadySeconds; the
// next qualifying basic attack uses it (UCrossguardWeaponAttackAbility, FCrossguardAttackRiders).
UCLASS(Abstract)
class CROSSGUARD_API UCrossguardNextAttackAbility : public UCrossguardSkillAbility
{
	GENERATED_BODY()

public:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

protected:
	FGameplayTag ReadyTag;
	// Assumption (as Chop Down): unused after this long, it is lost.
	float ReadySeconds = 10.0f;
};

// [Slash] (Active, 4 s, sword or dagger): next melee attack +7 weapon damage.
UCLASS()
class CROSSGUARD_API UCrossguardSlashAbility : public UCrossguardNextAttackAbility
{
	GENERATED_BODY()

public:
	UCrossguardSlashAbility();
};

// [Mortal Strike] (Active, 30 s, melee): next melee attack +20 Attack Damage; a kill with it gives
// the cooldown back.
UCLASS()
class CROSSGUARD_API UCrossguardMortalStrikeAbility : public UCrossguardNextAttackAbility
{
	GENERATED_BODY()

public:
	UCrossguardMortalStrikeAbility();
};

// [Shade Blow] (Active, 10 s, sword or axe): next melee attack +20 Magic Damage; the target cannot
// be healed for 5 s.
UCLASS()
class CROSSGUARD_API UCrossguardShadeBlowAbility : public UCrossguardNextAttackAbility
{
	GENERATED_BODY()

public:
	UCrossguardShadeBlowAbility();
};

// [Gouge] (Active, 7 s, dagger): next melee attack adds 25 Attack Damage over 5 s and -5% Movement
// Speed for 5 s.
UCLASS()
class CROSSGUARD_API UCrossguardGougeAbility : public UCrossguardNextAttackAbility
{
	GENERATED_BODY()

public:
	UCrossguardGougeAbility();
};

// [Purge Shot] (Active, 35 s, bow or crossbow): next shot +10 Magic Damage and Mutes for 3 s
// (skills stop, passives off).
UCLASS()
class CROSSGUARD_API UCrossguardPurgeShotAbility : public UCrossguardNextAttackAbility
{
	GENERATED_BODY()

public:
	UCrossguardPurgeShotAbility();
};

// [Quickshot] (Active, 25 s, bow): +5% Movement Speed and +25% Action Speed for 8 s.
UCLASS()
class CROSSGUARD_API UCrossguardQuickshotAbility : public UCrossguardSkillAbility
{
	GENERATED_BODY()

public:
	UCrossguardQuickshotAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};

// [Arcane Arrows] (Active, 25 s, bow): bow attacks deal +5 Magic Damage for 8 s. (The dropoff part
// waits for arrow damage dropoff.)
UCLASS()
class CROSSGUARD_API UCrossguardArcaneArrowsAbility : public UCrossguardSkillAbility
{
	GENERATED_BODY()

public:
	UCrossguardArcaneArrowsAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};

// [Cleave] (Art, 10 s, melee): 85% of the main-hand weapon's damage to everyone in a 3 m, 120° arc
// in front.
UCLASS()
class CROSSGUARD_API UCrossguardCleaveAbility : public UCrossguardSkillAbility
{
	GENERATED_BODY()

public:
	UCrossguardCleaveAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};

// [Triple Thrust] (Art, 12 s, spear): three quick thrusts (0.15 s apart), each the spear's damage +15
// Attack Damage, 2.5 m reach. Piercing (through blocking shields) waits for blocking.
UCLASS()
class CROSSGUARD_API UCrossguardTripleThrustAbility : public UCrossguardSkillAbility
{
	GENERATED_BODY()

public:
	UCrossguardTripleThrustAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	void Thrust();

	UFUNCTION()
	void OnThrustsDone();

	FTimerHandle ThrustTimer;
	int32 ThrustsLeft = 0;
};

// [Parting the Land] (Active, 60 s, two-handed melee): a strike along the ground 8 m ahead, 2 m wide:
// 65 Attack Damage and a knock-up to everyone in it, and a 15 s Wall on each side.
UCLASS()
class CROSSGUARD_API UCrossguardPartingTheLandAbility : public UCrossguardSkillAbility
{
	GENERATED_BODY()

public:
	UCrossguardPartingTheLandAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};

// [Hunter's Leap] (Active, 15 s): leap forward 0.5 m per point of Agility (5 m at 10 AGI).
UCLASS()
class CROSSGUARD_API UCrossguardHuntersLeapAbility : public UCrossguardSkillAbility
{
	GENERATED_BODY()

public:
	UCrossguardHuntersLeapAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

private:
	UFUNCTION()
	void OnLeapFinished();
};
