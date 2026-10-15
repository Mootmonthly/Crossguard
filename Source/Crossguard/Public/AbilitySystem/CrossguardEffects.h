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
	// Effect duration in seconds (UCrossguardHealOverTimeEffect).
	extern CROSSGUARD_API const FName Duration;
	// HP restored each tick (UCrossguardHealOverTimeEffect).
	extern CROSSGUARD_API const FName HealPerTick;
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

// Heals SetByCaller "HealPerTick" every TickSeconds for SetByCaller "Duration" seconds. Flat: no
// healing bonus applies. Used by potions and bandages; MakeSpec spreads a total over the duration.
UCLASS()
class CROSSGUARD_API UCrossguardHealOverTimeEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UCrossguardHealOverTimeEffect();

	static constexpr float TickSeconds = 0.5f;

	// Sets Duration and HealPerTick so that TotalHealing lands over Seconds in equal ticks.
	static void SetHealing(FGameplayEffectSpec& Spec, float TotalHealing, float Seconds);
};

// Infinite. Stat bonuses of a data-driven passive skill (UCrossguardDataPassiveAbility): one Add
// modifier per attribute in GetStatNames, SetByCaller named after the attribute. Set every name
// (0 for unused) before applying.
UCLASS()
class CROSSGUARD_API UCrossguardSkillStatsEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UCrossguardSkillStatsEffect();

	static const TArray<FName>& GetStatNames();
};

// Infinite. Like UCrossguardSkillStatsEffect, but each modifier multiplies (1 for unused).
UCLASS()
class CROSSGUARD_API UCrossguardSkillMultiplierEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UCrossguardSkillMultiplierEffect();
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
