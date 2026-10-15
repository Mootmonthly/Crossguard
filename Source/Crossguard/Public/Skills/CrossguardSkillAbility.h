#pragma once

#include "AbilitySystem/CrossguardGameplayAbility.h"
#include "CoreMinimal.h"

#include "CrossguardSkillAbility.generated.h"

class ACrossguardAICharacter;
class UAbilitySystemComponent;

// Base for skills implemented in C++ (docs/gas-architecture.md, Skills). Helpers for aiming and for
// building damage, healing and status effects from the caster. Values come from
// Data/Skills/Skills.json; each skill's constructor mirrors its row (the catalog's data validation
// flags cooldowns that drift).
UCLASS(Abstract)
class CROSSGUARD_API UCrossguardSkillAbility : public UCrossguardGameplayAbility
{
	GENERATED_BODY()

public:
	// Damage spec from the caster: Attack and/or Magic base damage and the bonus coefficient.
	FGameplayEffectSpecHandle MakeDamageSpec(float AttackDamage, float MagicDamage, float Coefficient = 1.0f) const;

	// Magical healing spec from the caster.
	FGameplayEffectSpecHandle MakeHealingSpec(float Healing, float Coefficient = 1.0f) const;

	// Applies a timed state tag (State.Blinded, State.ChopDownReady, ...) to Target. Crowd control
	// is skipped on a target immune to it.
	static void ApplyStatus(UAbilitySystemComponent* Source, UAbilitySystemComponent* Target, FGameplayTag State, float Seconds);

	// Slows Target by Fraction (0.5 = -50% Movement Speed) for Seconds, with State.Slowed. Crowd
	// control: skipped on a target immune to it.
	static void ApplySlow(UAbilitySystemComponent* Source, UAbilitySystemComponent* Target, float Fraction, float Seconds);

	// Specs for on-hit effects carried by attacks and projectiles; apply with ApplyOnHitSpec.
	static FGameplayEffectSpecHandle MakeStatusSpec(UAbilitySystemComponent* Source, FGameplayTag State, float Seconds);
	static FGameplayEffectSpecHandle MakeSlowSpec(UAbilitySystemComponent* Source, float Fraction, float Seconds);
	// Damage every second for Seconds (Attack and/or Magic per second).
	static FGameplayEffectSpecHandle MakeDamageOverTimeSpec(UAbilitySystemComponent* Source, float AttackPerSecond, float MagicPerSecond,
		float Seconds);
	// State.Muted for Seconds, with DamagePerSecond Magic Damage (0 for none). Also call
	// CancelActiveSkills on the target.
	static FGameplayEffectSpecHandle MakeMuteSpec(UAbilitySystemComponent* Source, float Seconds, float DamagePerSecond);

	// Applies Spec to Target unless it grants a crowd-control state Target is immune to.
	static void ApplyOnHitSpec(const FGameplayEffectSpecHandle& Spec, UAbilitySystemComponent* Target);

	// Crowd control (docs/design-decisions.md): Slow, Blind, knock-up, Silence (later Stun, Root).
	// Mute is not crowd control.
	static bool IsCrowdControl(const FGameplayTag& State);
	// [Immutable]: State.CCImmune, or the Immutable passive while not Muted.
	static bool IsCrowdControlImmune(const UAbilitySystemComponent* Target);

	// Timed stat change on Target: additive (Values are added) or multiplicative (Values multiply;
	// unnamed stats stay 1). Optional state tag for the duration.
	static FActiveGameplayEffectHandle ApplyTimedStats(UAbilitySystemComponent* Source, UAbilitySystemComponent* Target,
		const TMap<FName, float>& Values, float Seconds, bool bMultiply, FGameplayTag State = FGameplayTag());

	// Server. Cancels every skill ability running on ASC (death, Mute). Passives and basic attacks
	// are left alone.
	static void CancelActiveSkills(UAbilitySystemComponent* ASC);

	// The character's eyes: where aimed skills and projectiles start.
	void GetAim(FVector& OutLocation, FVector& OutDirection) const;

	// The first living actor with an ability system under the crosshair within Range (cm), not the
	// caster.
	AActor* FindAimedTarget(float Range) const;

	// AI characters (summons, monsters): whether to use this skill on Target now, judged on the class
	// default object (ACrossguardAICharacter::TryUseSkills; cooldowns and blocking tags are checked
	// on activation). Default: never. Skills an AI can use override it; Tools/ai_skills.py lists them
	// for the Mob Forge.
	virtual bool ShouldAIUse(const ACrossguardAICharacter& User, const AActor& Target) const { return false; }

protected:
	// Instant skills: commit (cooldown) and end. False if the commit failed (the ability has ended).
	bool CommitOrCancel(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo);

	// For a timer running on both sides (WaitDelay, root motion): ends the ability, replicating the
	// end only from the server, so a client never cuts the server's ability short.
	void EndOnTimer();
};
