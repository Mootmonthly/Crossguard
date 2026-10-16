#pragma once

#include "AbilitySystem/CrossguardGameplayAbility.h"
#include "CoreMinimal.h"
#include "Items/CrossguardItemTypes.h"

#include "CrossguardWeaponAttackAbility.generated.h"

class ACrossguardProjectile;
class UAbilitySystemComponent;

// What readied skills and always-on effects add to one attack (GatherRiders).
struct FCrossguardAttackRiders
{
	// Weapon base value × WeaponMultiplier + WeaponFlat, then AttackAdd / MagicAdd on top.
	float WeaponMultiplier = 1.0f;
	float WeaponFlat = 0.0f;
	float AttackAdd = 0.0f;
	float MagicAdd = 0.0f;
	// Extra Attack Damage Bonus for this hit (a parry's riposte: +0.5).
	float AttackBonusAdd = 0.0f;
	// Applied to each target hit (slows, damage over time, Mute, heal block).
	TArray<FGameplayEffectSpecHandle> OnHitSpecs;
	// [Chop Down]: ends Flight on targets hit.
	bool bGroundFlying = false;
	// [Purge Shot]: the Mute also stops running skills.
	bool bCancelSkills = false;
	// [Neckbite]: a Silence that lands also stops running skills.
	bool bSilence = false;
	// [Mortal Strike]: a kill removes its cooldown.
	bool bMortalStrike = false;
};
struct FCrossguardWeaponRow;
struct FCrossguardWeaponItem;

// The attack riders are gathered for: a weapon's, or an AI character's own attack.
struct FCrossguardRiderAttack
{
	bool bMelee = false;
	// Bow or crossbow.
	bool bRanged = false;
	// Unset: an unarmed AI's natural attack (a tiger's claws), which counts as every melee weapon for
	// next-attack skills but as no weapon for always-on effects (Splinter of Winter, Arcane Arrows).
	TOptional<ECrossguardWeaponType> WeaponType;
	// A thrown weapon (UCrossguardInventoryComponent's throw). Owner rulings: a thrown throwing knife is
	// a dagger attack, a thrown throwing axe an axe attack. Next-attack skills take it as a melee attack
	// with that weapon (Slash, Gouge, Chop Down, Shade Blow, Mortal Strike; not Purge Shot), and
	// [Splinter of Winter] rides on a knife. A parry's riposte stays readied for a weapon attack.
	bool bThrown = false;

	static FCrossguardRiderAttack ForWeapon(const FCrossguardWeaponRow& Weapon);
	// A throwable that counts as WeaponType (throwing knife: Dagger, throwing axe: Axe).
	static FCrossguardRiderAttack ForThrown(ECrossguardWeaponType WeaponType);
	// AI basic attacks (ACrossguardAICharacter): melee, with WeaponType if it wields one.
	static FCrossguardRiderAttack ForAIMelee(const TOptional<ECrossguardWeaponType>& WeaponType);
	// Ranged AI: a shot with its weapon (a bow when unset).
	static FCrossguardRiderAttack ForAIRanged(const TOptional<ECrossguardWeaponType>& WeaponType);
};

// Basic attack with the weapon in Slot (left click: main hand; right click when dual wielding:
// off hand, see UCrossguardOffHandAttackAbility). Each hand keeps its own attack interval. Damage = the weapon's AD or MD at its rarity, through the
// damage formula. The ability stays active for the attack interval (weapon speed after Action
// Speed), which is what limits the attack rate.
//
// Delivery by weapon:
// - MeleeSwing: sphere sweep in front of the character; hits every actor with an ability system.
// - Hitscan: line trace from the eyes (wand).
// - Projectile: spawns ProjectileClasses[weapon id], or DefaultProjectileClass (bow, staff).
//
// Weapons with an AmmoId (bows: "arrow") use up one per attack from the inventory, Quiver first,
// and cannot attack without it. Characters without an inventory (AI) shoot freely.
//
// Next-attack skills: while their State.NextAttack.* tag (State.ChopDownReady) is on the attacker,
// the next qualifying attack carries them and uses them up, hit or miss. Several stack.
// - Melee, with the skill's own weapon (Slash: sword or dagger; Shade Blow: sword or axe; Gouge:
//   dagger; Chop Down: axe; Mortal Strike: any): [Slash] +7 weapon damage, [Mortal Strike] +20 Attack Damage (a kill refreshes its
//   cooldown), [Shade Blow] +20 Magic Damage and no healing 5 s, [Gouge] 25 Attack Damage over 5 s
//   and -5% speed, [Chop Down] (axe) 110%, -50% speed 2 s, ends Flight, [Neckbite] (AI only for
//   now) +10 Magic Damage and a 2 s Silence (running skills stop).
// - Any weapon attack (not a throw): a parry's riposte, +50% Attack Damage Bonus.
// Guards: every hit goes through CrossguardGuard::ApplyHit (block and parry by Impact Power); a hit a
// guard stops carries no on-hit effects.
// - Bow / crossbow: [Purge Shot] +10 Magic Damage and a 3 s Mute.
// Always on: [Arcane Arrows] +5 Magic Damage on bow attacks while active; [Splinter of Winter]
// +5 Magic Damage and -10% speed 3 s on dagger attacks.
// A thrown throwing knife (axe) is a melee dagger (axe) attack for next-attack skills: it uses up
// every readied skill a melee attack with that weapon would, riposte excepted, and a knife carries
// [Splinter of Winter] (FCrossguardRiderAttack::ForThrown). Bombs (explosive bottles) carry none.
//
// Hit detection runs on the server. Placeholder until animations and weapon collision exist.
UCLASS()
class CROSSGUARD_API UCrossguardWeaponAttackAbility : public UCrossguardGameplayAbility
{
	GENERATED_BODY()

public:
	UCrossguardWeaponAttackAbility();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Attack")
	ECrossguardEquipmentSlot Slot = ECrossguardEquipmentSlot::MainHand;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Attack", meta = (ClampMin = "0"))
	float MeleeRange = 220.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Attack", meta = (ClampMin = "0"))
	float MeleeRadius = 60.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Attack", meta = (ClampMin = "0"))
	float HitscanRange = 5000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Attack")
	TSubclassOf<ACrossguardProjectile> DefaultProjectileClass;

	// Weapon id -> projectile (e.g. "bow" -> fast arrow, "staff_two_handed" -> slower bolt).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Attack")
	TMap<FName, TSubclassOf<ACrossguardProjectile>> ProjectileClasses;

	// Seconds trimmed from the attack recovery on the owning client so a held attack button is not
	// rejected by a server that still sees the previous attack active.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Attack", meta = (ClampMin = "0"))
	float ClientRecoveryTolerance = 0.05f;

	// Fails without an attacking weapon in Slot, so the client never predicts a doomed attack.
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	// Server. Collects (and uses up) what applies to this attack. Shared with AI basic attacks.
	static FCrossguardAttackRiders GatherRiders(const FCrossguardRiderAttack& Attack, UAbilitySystemComponent* ASC);
	// Server. On-hit parts of Riders for one target hit.
	static void ApplyRiders(const FCrossguardAttackRiders& Riders, UAbilitySystemComponent* TargetASC);
	// Server. After a melee attack: [Mortal Strike] gives its cooldown back if a target hit is dead.
	static void FinishMeleeHits(const FCrossguardAttackRiders& Riders, UAbilitySystemComponent* ASC, const TArray<AActor*>& Hit);

private:
	FGameplayEffectSpecHandle MakeWeaponDamageSpec(const FCrossguardWeaponRow& Weapon, const FCrossguardWeaponItem& Item,
		const FCrossguardAttackRiders& Riders) const;
	// Returns the actors damaged.
	TArray<AActor*> PerformMeleeSwing(const FGameplayEffectSpecHandle& Spec) const;
	// Returns the actor damaged, if any.
	AActor* PerformHitscan(const FGameplayEffectSpecHandle& Spec) const;
	void SpawnProjectile(const FGameplayEffectSpecHandle& Spec, FName WeaponId, const FCrossguardAttackRiders& Riders) const;
	// False if Target has no ability system (or is the attacker).
	bool ApplyDamageTo(AActor* Target, const FGameplayEffectSpecHandle& Spec) const;

	UFUNCTION()
	void OnAttackRecovered();
};

// Off-hand basic attack (dual wielding).
UCLASS()
class CROSSGUARD_API UCrossguardOffHandAttackAbility : public UCrossguardWeaponAttackAbility
{
	GENERATED_BODY()

public:
	UCrossguardOffHandAttackAbility();
};
