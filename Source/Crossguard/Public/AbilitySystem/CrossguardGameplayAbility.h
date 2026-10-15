#pragma once

#include "Abilities/GameplayAbility.h"
#include "AbilitySystem/CrossguardAbilitySystemComponent.h"
#include "CoreMinimal.h"

#include "CrossguardGameplayAbility.generated.h"

// Base class for every Crossguard skill.
//
// Cooldowns use UCrossguardCooldownEffect by default: this class fills its duration with
// BaseCooldownSeconds after CDR and grants the CooldownTags below, so one effect serves every skill.
// Weapon requirements: activation fails unless the owner's equipment satisfies RequiredWeapons.
UCLASS(Abstract)
class CROSSGUARD_API UCrossguardGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UCrossguardGameplayAbility();

	// Id in Data/Skills/Skills.json.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard")
	FName SkillId;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard")
	ECrossguardSkillCategory SkillCategory = ECrossguardSkillCategory::Active;

	// Before CDR. 0 means no cooldown.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Cooldown", meta = (ClampMin = "0"))
	float BaseCooldownSeconds = 0.0f;

	// Tags granted while on cooldown. Use a unique tag per skill, e.g. Cooldown.Skill.Fireball.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Cooldown")
	FGameplayTagContainer CooldownTags;

	// Any one of these must be equipped (e.g. Cleave: AnyMelee; Shade Blow: Sword, Axe). Empty: none.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard")
	TArray<ECrossguardWeaponType> RequiredWeapons;

	// Before cast time reduction. 0 means instant.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Cast", meta = (ClampMin = "0"))
	float BaseCastTimeSeconds = 0.0f;

	// Cooldown after CDR for the owning character.
	UFUNCTION(BlueprintPure, Category = "Crossguard")
	float GetReducedCooldown() const;

	// Cast time after reduction for the owning character. Use for cast bars and wait tasks.
	UFUNCTION(BlueprintPure, Category = "Crossguard")
	float GetReducedCastTime() const;

	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual const FGameplayTagContainer* GetCooldownTags() const override;
	virtual void ApplyCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) const override;

protected:
	// Null when the ability has no actor info (e.g. called on the class default object).
	const UCrossguardAbilitySystemComponent* GetCrossguardAbilitySystemComponent() const;

	float ReducedCooldownFor(const UCrossguardAbilitySystemComponent* ASC) const;

private:
	// Parent cooldown tags plus CooldownTags. Rebuilt on each GetCooldownTags call.
	UPROPERTY(Transient)
	FGameplayTagContainer CombinedCooldownTags;
};
