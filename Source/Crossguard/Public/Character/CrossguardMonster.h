#pragma once

#include "Character/CrossguardAICharacter.h"
#include "CoreMinimal.h"

#include "CrossguardMonster.generated.h"

// A PvE monster, run by ACrossguardMonsterController: it guards the spot it starts at, attacks the
// nearest player or minion it can see within AggroRange, and gives up the chase beyond LeashRange
// from home (walks back, healed). Its body holds what it carried plus a roll of LootTableId; its
// killer gets KillXpSource XP. Monsters never target each other (a Mage monster's fireball splash can
// still hurt one). Death is final: no revive. Placeholder look and numbers.
UCLASS()
class CROSSGUARD_API ACrossguardMonster : public ACrossguardAICharacter
{
	GENERATED_BODY()

public:
	ACrossguardMonster();

	// A row of Data/Mobs/Mobs.json (the game mode's MobTable). Set: the monster takes that mob's
	// stats, loot and kill XP when the game starts, over everything below.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|Monster")
	FName MobId;

	// cm from the monster.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|Monster", meta = (ClampMin = "0"))
	float AggroRange = 1500.0f;

	// cm from home.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|Monster", meta = (ClampMin = "0"))
	float LeashRange = 3000.0f;

	FVector GetHomeLocation() const { return HomeLocation; }

	virtual void BeginPlay() override;
	virtual void OnDamaged(AActor* Attacker, float Damage) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	// Server. MobId's row from the game mode's MobTable, if any.
	void ApplyMobData();

	UFUNCTION()
	void OnRep_MobScale();

	FVector HomeLocation = FVector::ZeroVector;

	// The mob's size, replicated so every machine shows it.
	UPROPERTY(ReplicatedUsing = OnRep_MobScale)
	float MobScale = 1.0f;
};
