#pragma once

#include "AbilitySystem/CrossguardGameplayAbility.h"
#include "CoreMinimal.h"
#include "CrossguardRules/Guard.h"
#include "GameplayEffectTypes.h"

#include "CrossguardGuard.generated.h"

class UAbilitySystemComponent;

// An Attack Damage hit, for guards (block and parry).
struct CROSSGUARD_API FCrossguardHit
{
	AActor* Attacker = nullptr;
	// What hits: its Impact Power (CrossguardGuard::ImpactPowerOf for weapons; projectiles carry
	// their own). 0: not a weapon hit (an explosion), no guard stops it.
	int32 ImpactPower = 0;
	// Where it comes from: the attacker's eyes, or back along a projectile's flight.
	FVector Origin = FVector::ZeroVector;
	// [Triple Thrust]: passes through a guard that would stop it, for half.
	bool bPiercing = false;
};

// Block and parry (docs/design-decisions.md, Block and parry; rules: Crossguard::ResolveGuard).
// Every Attack Damage hit goes through ApplyHit: if the target holds a guard (State.Guarding) and
// the hit meets its surface (it comes from where the guard faces), the two Impact Powers decide:
// the loser takes 25% / 50% / 100% of the winner's hit at a difference of 1 / 2 / 3+, stunned
// 0.5 s / 1 s at 2 / 3+; a tie stops it all. A shield deals nothing back; a parry the defender
// does not lose readies a riposte (+50% Attack Damage Bonus on the next attack, +10% Action Speed
// for 3 s). Magic Damage is never stopped.
namespace CrossguardGuard
{
	CROSSGUARD_API const Crossguard::FGuardConfig& Config();

	// The Impact Power an actor attacks with: its main-hand weapon; an AI character's own;
	// natural attacks otherwise.
	CROSSGUARD_API int32 ImpactPowerOf(const AActor* Attacker);

	// Whether Actor can raise a guard: AI characters always; players with a shield in the off hand
	// (block) or a weapon that parries and an empty off hand (parry).
	CROSSGUARD_API bool CanGuard(const AActor* Actor);

	// Server. Applies Spec from Source to Target through Target's guard. Returns the share of the
	// hit's Attack Damage that went through: 0 when stopped (skip on-hit effects), 1 without a guard.
	CROSSGUARD_API float ApplyHit(UAbilitySystemComponent* Source, AActor* Target, const FGameplayEffectSpecHandle& Spec, const FCrossguardHit& Hit);

	// Server. Stuns Target for Seconds (crowd control: skipped when immune): no moving, attacking,
	// guarding or skills; running skills and the guard stop.
	CROSSGUARD_API void Stun(UAbilitySystemComponent* Source, UAbilitySystemComponent* Target, float Seconds);

	// Server. A melee attack is starting: AI characters in Attacker's reach may raise their guard.
	CROSSGUARD_API void NotifyMeleeAttack(AActor* Attacker, float Reach);
}

// Right click held with a shield (block) or a weapon that parries (parry); AI characters raise it
// themselves. While held: State.Guarding, half movement speed, no attacks; it faces where the
// player looks (the mouse moves it). Ends on release or on a stun. With the weapons changed under
// it, it stops nothing (it is held, but no longer a guard) until raised again.
UCLASS()
class CROSSGUARD_API UCrossguardGuardAbility : public UCrossguardGameplayAbility
{
	GENERATED_BODY()

public:
	UCrossguardGuardAbility();

	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	FActiveGameplayEffectHandle SlowHandle;
};
