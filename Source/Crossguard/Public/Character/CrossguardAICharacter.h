#pragma once

#include "Character/CrossguardCharacter.h"
#include "CoreMinimal.h"
#include "GameplayAbilitySpecHandle.h"
#include "Skills/CrossguardSkillTypes.h"

#include "CrossguardAICharacter.generated.h"

class ACrossguardProjectile;
struct FCrossguardAttackRiders;
struct FGameplayEffectSpecHandle;
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
	// Keeps its distance and shoots arrows (Attack Damage) with a bow.
	Ranged,
};

// A character run by AI (summoned minions, monsters): fixed Health instead of level-based
// attributes, a simple attack, and a placeholder look (a cylinder until a Blueprint subclass sets a
// mesh). Its controller decides whom to attack.
//
// Skills: SkillIds are granted on possession (bundles with their included skills). Passives run on
// their own; active skills are used by TryUseSkills as soon as they are ready and the target is in
// their range (each skill's ShouldAIUse), aimed straight at the target. An AI's skills come with it: weapon, armor, form and casting-weapon requirements do
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

	// Warrior: Attack Damage per hit, one hit per AttackInterval, within AttackRange (cm). Ranged: the
	// same per arrow, shooting at targets within AttackRange that it can see.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|AI", meta = (ClampMin = "0"))
	float AttackDamage = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|AI", meta = (ClampMin = "0.1"))
	float AttackInterval = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|AI", meta = (ClampMin = "0"))
	float AttackRange = 200.0f;

	// Mage: seconds between fireballs. Mage and Ranged: the distance it keeps from its target (cm).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|AI", meta = (ClampMin = "0.1"))
	float FireballInterval = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|AI", meta = (ClampMin = "0"))
	float KeepDistance = 800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|AI")
	TSubclassOf<ACrossguardSpellProjectile> FireballClass;

	// Ranged: what it shoots.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|AI")
	TSubclassOf<ACrossguardProjectile> ArrowClass;

	// Mage and Ranged stand off at KeepDistance and need line of sight to attack.
	bool KeepsDistance() const { return CombatRole != ECrossguardCombatRole::Warrior; }

	// The weapon it fights with, for skills that care (Sword Mastery, Shade Blow). The damage is
	// still AttackDamage. Off: natural attacks (claws, fists); a Ranged character then holds a
	// two-handed Bow.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|AI")
	bool bWieldsWeapon = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|AI", meta = (EditCondition = "bWieldsWeapon"))
	ECrossguardWeaponType WeaponType = ECrossguardWeaponType::Sword;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|AI", meta = (EditCondition = "bWieldsWeapon"))
	bool bTwoHandedWeapon = false;

	// Block and parry: what its attacks and its guard weigh (its weapon's; 3 for natural attacks).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|AI", meta = (ClampMin = "0"))
	int32 ImpactPower = 3;

	// Server. Raises its guard toward Attacker for Seconds (it reacts to swings: CrossguardGuard).
	void RaiseGuard(AActor* Attacker, float Seconds);

	// Where its raised guard faces (toward the swing it reacted to).
	FVector GetGuardDirection() const { return GuardDirection.IsNearlyZero() ? GetActorForwardVector() : GuardDirection; }

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

	// What it holds: its weapon, else a Bow if Ranged, unset for natural attacks.
	TOptional<ECrossguardWeaponType> GetWieldedWeapon() const;

	// Who its last skill was aimed at (itself for heals); UCrossguardSkillAbility::GetAim aims there.
	const AActor* GetSkillAimTarget() const { return SkillAimTarget.Get(); }

	// Server. Hits Target if it is in range and the attack is ready (Warrior), casts Fireball at it
	// if ready (Mage), or shoots an arrow at it if it is in range and ready (Ranged). False if nothing
	// happened.
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
	// Ends its Stealth (it attacked).
	void BreakOwnStealth();
	// Seconds between attacks after Action Speed.
	float ActionSpeedInterval(float Seconds) const;
	void ShootArrow(AActor* Target, const FGameplayEffectSpecHandle& Spec, const FCrossguardAttackRiders& Riders);

	double NextAttackTime = 0.0;
	double NextSkillCheckTime = 0.0;
	FTimerHandle GuardTimer;
	FVector GuardDirection = FVector::ZeroVector;
	TWeakObjectPtr<AActor> SkillAimTarget;

	TArray<FGameplayAbilitySpecHandle> GrantedSkills;

	// Source objects of data passives; the ability specs only hold them weakly.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UCrossguardSkillGrant>> SkillGrants;
};
