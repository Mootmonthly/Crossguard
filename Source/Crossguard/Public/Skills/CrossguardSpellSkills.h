#pragma once

#include "CoreMinimal.h"
#include "Skills/CrossguardSkillAbility.h"

#include "CrossguardSpellSkills.generated.h"

class ACrossguardMinion;
enum class ECrossguardCombatRole : uint8;
class ACrossguardSpellProjectile;

// [Fireball] (Spell, 15 s): a fireball flies where you aim at 30 m/s. 50 (0.5) Magic Damage to what it
// hits, 15 to everyone else within 2 m (you too). If it hits nothing it explodes after 10 s.
UCLASS()
class CROSSGUARD_API UCrossguardFireballAbility : public UCrossguardSkillAbility
{
	GENERATED_BODY()

public:
	UCrossguardFireballAbility();

	// AI: a target it can see within 25 m.
	virtual bool ShouldAIUse(const ACrossguardAICharacter& User, const AActor& Target) const override;

	// Set a Blueprint subclass for the visual.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fireball")
	TSubclassOf<ACrossguardSpellProjectile> ProjectileClass;

	// Server. Launches a fireball from Location toward Direction with Caster's stats. Also used by the
	// Skeleton Mage.
	static void LaunchFireball(UAbilitySystemComponent* Caster, TSubclassOf<ACrossguardSpellProjectile> Class, const FVector& Location,
		const FVector& Direction);

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};

// [Heal] (Spell, 12 s): 20 Magical Healing to the character under your crosshair within 15 m, ally
// or enemy; yourself if nobody is there.
UCLASS()
class CROSSGUARD_API UCrossguardHealAbility : public UCrossguardSkillAbility
{
	GENERATED_BODY()

public:
	UCrossguardHealAbility();

	// AI: on itself, once down to 60% Health (Greater Heal too).
	virtual bool ShouldAIUse(const ACrossguardAICharacter& User, const AActor& Target) const override;
	virtual bool AIAimsAtSelf() const override { return true; }

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

protected:
	// Magical Healing ([Greater Heal] subclasses with 55).
	float HealAmount = 20.0f;
};

// [Mute] (Spell, 180 s): the character under your crosshair within 20 m is Muted for 10 s: no skills,
// passives off, 5 Magic Damage per second. Basic attacks still work. With nobody there it fails and
// no cooldown starts.
UCLASS()
class CROSSGUARD_API UCrossguardMuteAbility : public UCrossguardSkillAbility
{
	GENERATED_BODY()

public:
	UCrossguardMuteAbility();

	// AI: a target it can see in range, not already Muted.
	virtual bool ShouldAIUse(const ACrossguardAICharacter& User, const AActor& Target) const override;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};

// [Eldritch Shield] (Spell, 25 s): lose 10 HP (never below 1) and gain a barrier for 12 s that
// absorbs 45 Attack Damage. Magic Damage passes through; once used up, damage goes through.
UCLASS()
class CROSSGUARD_API UCrossguardEldritchShieldAbility : public UCrossguardSkillAbility
{
	GENERATED_BODY()

public:
	UCrossguardEldritchShieldAbility();

	// AI: once a target is within 10 m.
	virtual bool ShouldAIUse(const ACrossguardAICharacter& User, const AActor& Target) const override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	UFUNCTION()
	void OnShieldExpired();
};

// [Chop Down] (Active, 25 s, needs an axe): readies your next melee axe attack for 10 s. That attack
// deals 110% damage, slows everyone it hits by 50% for 2 s and Grounds flying targets (their Flight
// ends). The attack itself is UCrossguardWeaponAttackAbility.
UCLASS()
class CROSSGUARD_API UCrossguardChopDownAbility : public UCrossguardSkillAbility
{
	GENERATED_BODY()

public:
	UCrossguardChopDownAbility();

	// AI (Warriors): when about to hit, not already readied.
	virtual bool ShouldAIUse(const ACrossguardAICharacter& User, const AActor& Target) const override;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};

// Base for summoning skills (Spell: Minion): casting again first dismisses the minions still
// standing, and so does unequipping the skill. See ACrossguardMinion and ACrossguardMinionController.
UCLASS(Abstract)
class CROSSGUARD_API UCrossguardSummonAbility : public UCrossguardSkillAbility
{
	GENERATED_BODY()

public:
	UCrossguardSummonAbility();

	// AI: once a target is within 20 m.
	virtual bool ShouldAIUse(const ACrossguardAICharacter& User, const AActor& Target) const override;

	virtual void OnRemoveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override;

protected:
	// Server. Dismisses the previous minions.
	void DismissMinions();

	// Server. Spawns one minion at Location for Seconds; Setup fills it in before it finishes
	// spawning. Null if there was no room.
	ACrossguardMinion* SpawnMinion(TSubclassOf<ACrossguardMinion> Class, const FVector& Location, float Seconds,
		TFunctionRef<void(ACrossguardMinion&)> Setup, float Scale = 1.0f);

private:
	TArray<TWeakObjectPtr<ACrossguardMinion>> Minions;
};

// [Summon Skeletons] (Spell, 30 s): 5 Skeleton Warriors (40 HP, 10 Attack Damage hits) and 1
// Skeleton Mage (30 HP, Fireball every 8 s) appear around you for 60 s.
UCLASS()
class CROSSGUARD_API UCrossguardSummonSkeletonsAbility : public UCrossguardSummonAbility
{
	GENERATED_BODY()

public:
	UCrossguardSummonSkeletonsAbility();

	// Set Blueprint subclasses for the skeleton meshes.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Summon")
	TSubclassOf<ACrossguardMinion> WarriorClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Summon")
	TSubclassOf<ACrossguardMinion> MageClass;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};

// Summons one companion with skills ([Summon Tiger], [Summon Greater Demon]) in front of you. It
// uses its active skills as soon as they are ready and its target is in their range.
UCLASS(Abstract)
class CROSSGUARD_API UCrossguardSummonCompanionAbility : public UCrossguardSummonAbility
{
	GENERATED_BODY()

public:
	// Set a Blueprint subclass for the mesh.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Summon")
	TSubclassOf<ACrossguardMinion> CompanionClass;

	UCrossguardSummonCompanionAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

protected:

	float Health = 100.0f;
	float AttackDamage = 10.0f;
	float AttackInterval = 1.0f;
	// cm, centre to centre, at player size: the companion's reach is this times Scale.
	float AttackRange = 200.0f;
	float Seconds = 60.0f;
	float Scale = 1.0f;
	bool bWieldsWeapon = false;
	ECrossguardWeaponType WeaponType = ECrossguardWeaponType::Sword;
	bool bTwoHandedWeapon = false;
	// Block and parry: natural attacks 3.
	int32 ImpactPower = 3;
	TArray<FName> Skills;
};

// [Summon Tiger] (Spell, 185 s): a tiger for 180 s. 100 HP, 20 Attack Damage claws, 1.3 a second; Hunter's Leap,
// Neckbite, Slash. Needs a Pet Gem (Tiger) in the inventory (not used up; AI summoners need none).
UCLASS()
class CROSSGUARD_API UCrossguardSummonTigerAbility : public UCrossguardSummonCompanionAbility
{
	GENERATED_BODY()

public:
	UCrossguardSummonTigerAbility();

	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
};

// [Summon Greater Demon] (Spell, 240 s): a demon 3x the player's size for 60 s. 220 HP, a two-handed sword
// dealing 40 Attack Damage; Mortal Strike, Sword Mastery, Eldritch Shield, Shade Blow.
UCLASS()
class CROSSGUARD_API UCrossguardSummonGreaterDemonAbility : public UCrossguardSummonCompanionAbility
{
	GENERATED_BODY()

public:
	UCrossguardSummonGreaterDemonAbility();
};
