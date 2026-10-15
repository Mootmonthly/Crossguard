#pragma once

#include "AbilitySystem/CrossguardGameplayAbility.h"
#include "CoreMinimal.h"

#include "CrossguardSkillAbility.generated.h"

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

	// Applies a timed state tag (State.Blinded, State.ChopDownReady, ...) to Target.
	static void ApplyStatus(UAbilitySystemComponent* Source, UAbilitySystemComponent* Target, FGameplayTag State, float Seconds);

	// Slows Target by Fraction (0.5 = -50% Movement Speed) for Seconds, with State.Slowed.
	static void ApplySlow(UAbilitySystemComponent* Source, UAbilitySystemComponent* Target, float Fraction, float Seconds);

	// The character's eyes: where aimed skills and projectiles start.
	void GetAim(FVector& OutLocation, FVector& OutDirection) const;

	// The first actor with an ability system under the crosshair within Range (cm), not the caster.
	AActor* FindAimedTarget(float Range) const;

protected:
	// Instant skills: commit (cooldown) and end. False if the commit failed (the ability has ended).
	bool CommitOrCancel(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo);
};
