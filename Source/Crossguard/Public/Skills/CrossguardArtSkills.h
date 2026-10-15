#pragma once

#include "CoreMinimal.h"
#include "Skills/CrossguardSkillAbility.h"

#include "CrossguardArtSkills.generated.h"

// [Shooting Star] (Art, 60 s, needs a sword): dash 10 m forward in 0.3 s through characters, 50
// Attack Damage to everyone in the path (once each). Everyone within 30 m looking at you (within 45°
// of facing you, with line of sight) is Blinded for 2 s, allies included.
UCLASS()
class CROSSGUARD_API UCrossguardShootingStarAbility : public UCrossguardSkillAbility
{
	GENERATED_BODY()

public:
	UCrossguardShootingStarAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	// Blinds everyone facing the caster.
	void ApplyBlind() const;
	// Damages everyone between DashStart and where the dash ended.
	void DamagePath() const;

	UFUNCTION()
	void OnDashFinished();

	FVector DashStart = FVector::ZeroVector;
	bool bIgnoringPawns = false;
	TEnumAsByte<ECollisionResponse> PreviousPawnResponse = ECR_Block;
};

// [Dragonification] (Art, 180 s): a small dragon for 45 s. +50% Max HP, +25% Attack and Magic
// Resistance, and the Flare Breath and Flight skills. Weapons and other skills cannot be used.
// Visuals: a Blueprint subclass reacts to State.Dragonified.
UCLASS()
class CROSSGUARD_API UCrossguardDragonificationAbility : public UCrossguardSkillAbility
{
	GENERATED_BODY()

public:
	UCrossguardDragonificationAbility();

	// The skills granted for the duration. Set Blueprint subclasses for visuals.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dragonification")
	TSubclassOf<UCrossguardSkillAbility> FlareBreathClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dragonification")
	TSubclassOf<UCrossguardSkillAbility> FlightClass;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	UFUNCTION()
	void OnFormExpired();

	FActiveGameplayEffectHandle StatsHandle;
	TArray<FGameplayAbilitySpecHandle> GrantedSkills;
};

// [Flare Breath] (Spell, 45 s): a 3 s gout of flame, 6 m in a 60° cone where you aim. Everyone
// caught takes 40 (0.5) Magic Damage the first time, then 25 (0.5) per second while inside.
// Usable in dragon form.
UCLASS()
class CROSSGUARD_API UCrossguardFlareBreathAbility : public UCrossguardSkillAbility
{
	GENERATED_BODY()

public:
	UCrossguardFlareBreathAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	void BreathTick();

	UFUNCTION()
	void OnBreathFinished();

	FTimerHandle TickTimer;
	TSet<TWeakObjectPtr<AActor>> Caught;
};

// [Flight] (Active, 60 s): fly for 20 s. Space rises, Left Ctrl descends; you fall when it ends.
// [Chop Down] ends it early (Grounded). Usable in dragon form.
UCLASS()
class CROSSGUARD_API UCrossguardFlightAbility : public UCrossguardSkillAbility
{
	GENERATED_BODY()

public:
	UCrossguardFlightAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	UFUNCTION()
	void OnFlightExpired();
};
