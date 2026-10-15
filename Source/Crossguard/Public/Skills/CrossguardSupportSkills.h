#pragma once

#include "AbilitySystem/CrossguardPassiveAbility.h"
#include "CoreMinimal.h"
#include "Skills/CrossguardSpellSkills.h"

#include "CrossguardSupportSkills.generated.h"

class ACrossguardCharacter;
class ACrossguardLightOrb;
class ACrossguardWall;
struct FOnAttributeChangeData;

// [Greater Heal] (Spell, 20 s): as Heal, 55 Magical Healing.
UCLASS()
class CROSSGUARD_API UCrossguardGreaterHealAbility : public UCrossguardHealAbility
{
	GENERATED_BODY()

public:
	UCrossguardGreaterHealAbility();
};

// [Benediction] (Spell, 120 s): the character under the crosshair within 15 m (ally or enemy) is
// fully healed. Aimed at a body (a dead character within its revive window) it revives them at full
// HP with their unlooted gear back. Nobody there: yourself.
UCLASS()
class CROSSGUARD_API UCrossguardBenedictionAbility : public UCrossguardSkillAbility
{
	GENERATED_BODY()

public:
	UCrossguardBenedictionAbility();

	// AI: on itself, once down to 35% Health (it revives no bodies).
	virtual bool ShouldAIUse(const ACrossguardAICharacter& User, const AActor& Target) const override;
	virtual bool AIAimsAtSelf() const override { return true; }

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

private:
	// The revivable body closest to the crosshair (within 10°) and Range.
	ACrossguardCharacter* FindAimedBody(float Range) const;
};

// [Unleash] (Active, 90 s): Strength, Agility and Intelligence doubled for 15 s.
UCLASS()
class CROSSGUARD_API UCrossguardUnleashAbility : public UCrossguardSkillAbility
{
	GENERATED_BODY()

public:
	UCrossguardUnleashAbility();

	// AI: once a target is within 10 m.
	virtual bool ShouldAIUse(const ACrossguardAICharacter& User, const AActor& Target) const override;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};

// [Light] (Spell, 30 s): a ball of light over your head for 120 s. Casting again replaces it.
UCLASS()
class CROSSGUARD_API UCrossguardLightAbility : public UCrossguardSkillAbility
{
	GENERATED_BODY()

public:
	UCrossguardLightAbility();

	// AI: once a target is within 30 m (it lights the fight).
	virtual bool ShouldAIUse(const ACrossguardAICharacter& User, const AActor& Target) const override;

	// Set a Blueprint subclass for the look.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Light")
	TSubclassOf<ACrossguardLightOrb> OrbClass;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

private:
	TWeakObjectPtr<ACrossguardLightOrb> Orb;
};

// [Wall] (Spell, 10 s): an earthen wall 3 m in front of you, facing you: 4 m wide, 3 m tall,
// 0.5 m thick. Indestructible, lasts 15 s.
UCLASS()
class CROSSGUARD_API UCrossguardWallAbility : public UCrossguardSkillAbility
{
	GENERATED_BODY()

public:
	UCrossguardWallAbility();

	// AI (Mage and Ranged): a wall between it and a target closing in within 6 m.
	virtual bool ShouldAIUse(const ACrossguardAICharacter& User, const AActor& Target) const override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wall")
	TSubclassOf<ACrossguardWall> WallClass;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};

// [Stealth] (Active, 20 s): invisible to everyone outside your party for 12 s (State.Stealthed;
// ACrossguardCharacter hides the body on enemy screens, minions ignore you). Attacking, casting or
// taking damage ends it early. When it ends (not by death or Mute): +10% Movement Speed and +25%
// Attack Bonus for 3 s.
UCLASS()
class CROSSGUARD_API UCrossguardStealthAbility : public UCrossguardSkillAbility
{
	GENERATED_BODY()

public:
	UCrossguardStealthAbility();

	// AI: to sneak up on a target within 15 m but beyond twice its reach.
	virtual bool ShouldAIUse(const ACrossguardAICharacter& User, const AActor& Target) const override;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	UFUNCTION()
	void OnStealthExpired();
	void OnOtherAbilityActivated(UGameplayAbility* Ability);
	void OnHealthChanged(const FOnAttributeChangeData& Data);
	// Ends stealth now, from the server.
	void BreakStealth();

	FActiveGameplayEffectHandle StealthHandle;
	FDelegateHandle ActivatedHandle;
	FDelegateHandle HealthHandle;
};

// [Immutable] (Active, 60 s): while equipped (and not Muted) you are immune to crowd control.
// Activate: you and party members within 10 m are immune for 5 s (State.CCImmune).
UCLASS()
class CROSSGUARD_API UCrossguardImmutableAbility : public UCrossguardSkillAbility
{
	GENERATED_BODY()

public:
	UCrossguardImmutableAbility();

	// AI: once a target is within 10 m.
	virtual bool ShouldAIUse(const ACrossguardAICharacter& User, const AActor& Target) const override;

	virtual void OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override;
	virtual void OnRemoveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};

// [Splinter of Winter] (Passive): dagger attacks deal +5 Magic Damage and slow 10% for 3 s (applied
// by UCrossguardWeaponAttackAbility while Passive.SplinterOfWinter is on). Dagger Mastery comes with
// it as its own skill.
UCLASS()
class CROSSGUARD_API UCrossguardSplinterOfWinterAbility : public UCrossguardPassiveAbility
{
	GENERATED_BODY()

public:
	UCrossguardSplinterOfWinterAbility();
};
