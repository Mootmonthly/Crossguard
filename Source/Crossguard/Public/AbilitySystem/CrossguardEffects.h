#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"

#include "CrossguardEffects.generated.h"

// Gameplay effects defined in C++ so they need no editor setup.

// SetByCaller names used by the native effects below.
namespace CrossguardSetByCaller
{
	// Cooldown duration in seconds (UCrossguardCooldownEffect).
	extern CROSSGUARD_API const FName Cooldown;
}

// Instant. Runs UCrossguardDamageExecution (SetByCaller.Damage.Attack / .Magic, SetByCaller.Coefficient).
UCLASS()
class CROSSGUARD_API UCrossguardDamageEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UCrossguardDamageEffect();
};

// Instant. Runs UCrossguardHealingExecution (SetByCaller.Healing.Physical / .Magical, SetByCaller.Coefficient).
UCLASS()
class CROSSGUARD_API UCrossguardHealingEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UCrossguardHealingEffect();
};

// Duration from SetByCaller "Cooldown". The default cooldown effect of every Crossguard ability;
// the ability adds its own cooldown tags to the spec.
UCLASS()
class CROSSGUARD_API UCrossguardCooldownEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UCrossguardCooldownEffect();
};

// Infinite. Holds the stat contributions of equipped items, one Add modifier per stat with a
// SetByCaller magnitude named after the attribute (e.g. "AttackResistance"). Set every name in
// GetStatNames (0 for unused) before applying.
UCLASS()
class CROSSGUARD_API UCrossguardEquipmentEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UCrossguardEquipmentEffect();

	static const TArray<FName>& GetStatNames();
};
