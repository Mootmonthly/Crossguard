#pragma once

#include "Character/CrossguardCharacter.h"
#include "CoreMinimal.h"

#include "CrossguardMinion.generated.h"

class ACrossguardSpellProjectile;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class ECrossguardMinionRole : uint8
{
	// Runs up and hits with its weapon.
	Warrior,
	// Keeps its distance and casts Fireball.
	Mage,
};

// A summoned character ([Summon Skeletons]) on its summoner's team, run by
// ACrossguardMinionController. No loot, no body, no kill XP; it vanishes when it dies or its time
// runs out. Its kills count as its summoner's. Placeholder look: a cylinder until a Blueprint
// subclass sets a mesh.
UCLASS()
class CROSSGUARD_API ACrossguardMinion : public ACrossguardCharacter
{
	GENERATED_BODY()

public:
	ACrossguardMinion();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Minion")
	ECrossguardMinionRole Role = ECrossguardMinionRole::Warrior;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Minion", meta = (ClampMin = "1"))
	float MinionHealth = 40.0f;

	// Warrior: Attack Damage per hit, one hit per AttackInterval, within AttackRange (cm).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Minion", meta = (ClampMin = "0"))
	float AttackDamage = 10.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Minion", meta = (ClampMin = "0.1"))
	float AttackInterval = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Minion", meta = (ClampMin = "0"))
	float AttackRange = 200.0f;

	// Mage: seconds between fireballs, and the distance it keeps from its target (cm).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Minion", meta = (ClampMin = "0.1"))
	float FireballInterval = 8.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Minion", meta = (ClampMin = "0"))
	float KeepDistance = 800.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Minion")
	TSubclassOf<ACrossguardSpellProjectile> FireballClass;

	// Server. Set before FinishSpawning.
	void SetSummoner(ACrossguardCharacter* InSummoner) { Summoner = InSummoner; }
	ACrossguardCharacter* GetSummoner() const { return Summoner.Get(); }

	// Server. Hits Target if it is in range and the attack is ready (Warrior), or casts Fireball at
	// it if ready (Mage). False if nothing happened.
	bool TryAttack(AActor* Target);

	bool IsAlive() const;

	// Server. Gone at once (time ran out, or the summoner cast again).
	void Dismiss();

	// The summoner's team.
	virtual FGenericTeamId GetGenericTeamId() const override;

	virtual void HandleDeath() override;
	virtual void PossessedBy(AController* NewController) override;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Crossguard|Minion")
	TObjectPtr<UStaticMeshComponent> PlaceholderMesh;

private:
	TWeakObjectPtr<ACrossguardCharacter> Summoner;
	double NextAttackTime = 0.0;
};
