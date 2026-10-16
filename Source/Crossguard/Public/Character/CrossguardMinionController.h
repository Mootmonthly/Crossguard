#pragma once

#include "AIController.h"
#include "CoreMinimal.h"

#include "CrossguardMinionController.generated.h"

class ACrossguardMinion;

// Runs a summoned minion ([Summon Skeletons]). Twice a second it picks a target: the nearest
// living enemy player within AggroRange, else the nearest living enemy non-player (monsters, other
// summoners' minions), else nobody and it follows its summoner. Warriors run up and hit; Mages
// stop KeepDistance away and cast Fireball. Moves on the NavMesh; with no NavMesh it walks
// straight at its goal.
UCLASS()
class CROSSGUARD_API ACrossguardMinionController : public AAIController
{
	GENERATED_BODY()

public:
	ACrossguardMinionController();

	// cm. Assumption: how far a minion notices enemies.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Minion", meta = (ClampMin = "0"))
	float AggroRange = 3000.0f;

	// cm. How close it stays to its summoner with nothing to fight.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Minion", meta = (ClampMin = "0"))
	float FollowDistance = 300.0f;

	virtual void Tick(float DeltaSeconds) override;

	// Whom it is fighting now (null while it follows its summoner).
	UFUNCTION(BlueprintPure, Category = "Crossguard|AI")
	AActor* GetCurrentTarget() const { return Target.Get(); }

protected:
	virtual void OnPossess(APawn* InPawn) override;

private:
	AActor* FindTarget(const ACrossguardMinion& Minion) const;
	// Starts moving toward Goal, stopping Acceptance cm away.
	void MoveToward(AActor* Goal, float Acceptance);

	TWeakObjectPtr<AActor> Target;
	TWeakObjectPtr<AActor> MoveGoal;
	float MoveAcceptance = 0.0f;
	// The NavMesh refused the move: steer straight at the goal each frame.
	bool bDirectMove = false;
	float RethinkCountdown = 0.0f;
};
