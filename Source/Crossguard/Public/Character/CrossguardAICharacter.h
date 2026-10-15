#pragma once

#include "Character/CrossguardCharacter.h"
#include "CoreMinimal.h"

#include "CrossguardAICharacter.generated.h"

class ACrossguardSpellProjectile;
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

	// Server. Hits Target if it is in range and the attack is ready (Warrior), or casts Fireball at
	// it if ready (Mage). False if nothing happened.
	bool TryAttack(AActor* Target);

	bool IsAlive() const;

	// Server. Back to full Health (monsters giving up the chase).
	void RestoreFullHealth();

	virtual void PossessedBy(AController* NewController) override;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Crossguard|AI")
	TObjectPtr<UStaticMeshComponent> PlaceholderMesh;

private:
	double NextAttackTime = 0.0;
};
