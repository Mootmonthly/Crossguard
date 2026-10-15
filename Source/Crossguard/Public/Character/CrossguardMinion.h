#pragma once

#include "Character/CrossguardAICharacter.h"
#include "CoreMinimal.h"

#include "CrossguardMinion.generated.h"

// A summoned character ([Summon Skeletons]) on its summoner's team, run by
// ACrossguardMinionController. No loot, no body, no kill XP; it vanishes when it dies or its time
// runs out, or when its summoner leaves the game. Its kills count as its summoner's.
UCLASS()
class CROSSGUARD_API ACrossguardMinion : public ACrossguardAICharacter
{
	GENERATED_BODY()

public:
	ACrossguardMinion();

	// Server. Set before FinishSpawning.
	void SetSummoner(ACrossguardCharacter* InSummoner) { Summoner = InSummoner; }
	ACrossguardCharacter* GetSummoner() const { return Summoner.Get(); }

	// Server. Gone at once (time ran out, or the summoner cast again).
	void Dismiss();

	// The summoner's team.
	virtual FGenericTeamId GetGenericTeamId() const override;

	virtual void HandleDeath() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	// Replicated so clients can tell whose side it is on (nameplates, ally checks).
	UPROPERTY(Replicated)
	TWeakObjectPtr<ACrossguardCharacter> Summoner;
};
