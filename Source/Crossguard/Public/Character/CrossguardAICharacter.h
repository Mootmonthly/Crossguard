#pragma once

#include "Character/CrossguardCharacter.h"
#include "CoreMinimal.h"
#include "GameplayAbilitySpecHandle.h"
#include "Skills/CrossguardSkillTypes.h"

#include "CrossguardAICharacter.generated.h"

class ACrossguardSpellProjectile;
class UCrossguardSkillCatalog;
class UCrossguardSkillGrant;
class UStaticMeshComponent;

// How an AI character fights.
UENUM(BlueprintType)
enum class ECrossguardCombatRole : uint8
{
	// Runs up and hits.
	Warrior,
	// Keeps its distance and casts Fireball.
	Mage,
};

// A character run by AI (summoned minions, monsters): fixed Health instead of level-based
// attributes, a simple attack, and a placeholder look (a cylinder until a Blueprint subclass sets a
// mesh). Its controller decides whom to attack.
//
// Skills: SkillIds are granted on possession. Passives run on their own; active skills are used by
// TryUseSkills as soon as they are ready and the target is in their range (each skill's
// ShouldAIUse). An AI's skills come with it: weapon, armor, form and casting-weapon requirements do
// not apply. Silence and Mute stop its skills (and a Mage's Fireball) like anyone's. Its basic
// attack carries next-attack skills (Slash, Mortal Strike, Neckbite, ...) like a weapon attack:
// with no weapon (claws) it counts as every melee weapon.
UCLASS(Abstract)
class CROSSGUARD_API ACrossguardAICharacter : public ACrossguardCharacter
{
	GENERATED_BODY()

public:
	ACrossguardAICharacter();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|AI")
	ECrossguardCombatRole CombatRole = ECrossguardCombatRole::Warrior;

	// Max HP (Max HP Bonus still applies), kept when buffs change the attributes.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|AI", meta = (ClampMin = "1"))
	float FixedHealth = 40.0f;

	// Warrior: Attack Damage per hit, one hit per AttackInterval, within AttackRange (cm).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|AI", meta = (ClampMin = "0"))
	float AttackDamage = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|AI", meta = (ClampMin = "0.1"))
	float AttackInterval = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|AI", meta = (ClampMin = "0"))
	float AttackRange = 200.0f;

	// Mage: seconds between fireballs, and the distance it keeps from its target (cm).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|AI", meta = (ClampMin = "0.1"))
	float FireballInterval = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|AI", meta = (ClampMin = "0"))
	float KeepDistance = 800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|AI")
	TSubclassOf<ACrossguardSpellProjectile> FireballClass;

	// The weapon it fights with, for skills that care (Sword Mastery, Shade Blow). The damage is
	// still AttackDamage. Off: natural attacks (claws, fists).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|AI")
	bool bWieldsWeapon = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|AI", meta = (EditCondition = "bWieldsWeapon"))
	ECrossguardWeaponType WeaponType = ECrossguardWeaponType::Sword;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|AI", meta = (EditCondition = "bWieldsWeapon"))
	bool bTwoHandedWeapon = false;

	// Skill ids (Data/Skills/Skills.json), granted on possession.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|AI")
	TArray<FName> SkillIds;

	// Where SkillIds are looked up (data passives need it). Unset: native skills only.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|AI")
	TObjectPtr<UCrossguardSkillCatalog> SkillCatalog;

	// Server. Replaces the granted skills with SkillIds.
	void GrantSkills();

	// Server. Uses one ready skill whose range Target is in, facing Target first. Checked a few
	// times a second. False if none was used.
	bool TryUseSkills(AActor* Target);

	// Whether its weapon satisfies a weapon requirement (data passives' Wielding conditions).
	bool WieldsAnyOf(const TArray<ECrossguardWeaponType>& AnyOf) const;

	// Server. Hits Target if it is in range and the attack is ready (Warrior), or casts Fireball at
	// it if ready (Mage). False if nothing happened.
	bool TryAttack(AActor* Target);

	bool IsAlive() const;

	// Server. Back to full Health (monsters giving up the chase).
	void RestoreFullHealth();

	virtual void PossessedBy(AController* NewController) override;
	// Monsters and minions stay dead (a revive would duplicate a monster's loot).
	virtual bool CanBeRevived() const override { return false; }

protected:
	UPROPERTY(VisibleAnywhere, Category = "Crossguard|AI")
	TObjectPtr<UStaticMeshComponent> PlaceholderMesh;

private:
	void ClearSkills();

	double NextAttackTime = 0.0;
	double NextSkillCheckTime = 0.0;

	TArray<FGameplayAbilitySpecHandle> GrantedSkills;

	// Source objects of data passives; the ability specs only hold them weakly.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UCrossguardSkillGrant>> SkillGrants;
};
