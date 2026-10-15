#pragma once

#include "AIController.h"
#include "CoreMinimal.h"

#include "CrossguardMonsterController.generated.h"

class ACrossguardMonster;

// Runs a PvE monster (ACrossguardMonster). Twice a second: keeps its target while it is alive,
// visible (not stealthed) and the monster is within LeashRange of home; otherwise picks the
// nearest living, visible player or minion within AggroRange that it has line of sight to. Too far
// from home: drops the target and walks home, ignoring everyone, and is healed on arrival.
// Warriors run up and hit; Mages stop KeepDistance away and cast Fireball. NavMesh paths, or
// straight lines without one.
UCLASS()
class CROSSGUARD_API ACrossguardMonsterController : public AAIController
{
	GENERATED_BODY()

public:
	ACrossguardMonsterController();

	virtual void Tick(float DeltaSeconds) override;

private:
	AActor* FindTarget(const ACrossguardMonster& Monster) const;
	bool IsValidTarget(const ACrossguardMonster& Monster, const AActor* Candidate) const;
	void MoveToGoal(AActor* GoalActor, const FVector& GoalLocation, float Acceptance);

	TWeakObjectPtr<AActor> Target;
	bool bReturningHome = false;
	float RethinkCountdown = 0.0f;

	// The current move: an actor, or a location when GoalActor is null.
	TWeakObjectPtr<AActor> GoalActor;
	FVector GoalLocation = FVector::ZeroVector;
	float GoalAcceptance = 0.0f;
	bool bHasGoal = false;
	// The NavMesh refused the move: steer straight at the goal each frame.
	bool bDirectMove = false;
};
