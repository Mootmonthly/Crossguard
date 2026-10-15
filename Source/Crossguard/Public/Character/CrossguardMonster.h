#pragma once

#include "Character/CrossguardAICharacter.h"
#include "CoreMinimal.h"

#include "CrossguardMonster.generated.h"

// A PvE monster, run by ACrossguardMonsterController: it guards the spot it starts at, attacks the
// nearest player or minion it can see within AggroRange, and gives up the chase beyond LeashRange
// from home (walks back, healed). Its body holds what it carried plus a roll of LootTableId; its
// killer gets KillXpSource XP. Monsters never fight each other. Placeholder look and numbers.
UCLASS()
class CROSSGUARD_API ACrossguardMonster : public ACrossguardAICharacter
{
	GENERATED_BODY()

public:
	ACrossguardMonster();

	// cm from the monster.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|Monster", meta = (ClampMin = "0"))
	float AggroRange = 1500.0f;

	// cm from home.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|Monster", meta = (ClampMin = "0"))
	float LeashRange = 3000.0f;

	FVector GetHomeLocation() const { return HomeLocation; }

	virtual void BeginPlay() override;

private:
	FVector HomeLocation = FVector::ZeroVector;
};
