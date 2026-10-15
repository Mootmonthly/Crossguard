#pragma once

#include "AIController.h"
#include "CoreMinimal.h"

#include "CrossguardMonsterController.generated.h"

class ACrossguardMonster;

// Runs a PvE monster (ACrossguardMonster). Twice a second: keeps its target while it is alive,
// visible (not stealthed) and the monster is within LeashRange of home; otherwise picks the
// nearest living, visible player or minion within AggroRange that it has line of sight to (and that
// stands within LeashRange of home). Too far from home: it retargets such an enemy if there is one;
// otherwise it walks home, still retargeting anyone who comes into aggro on the way, and fully
// resets (full Health) only on reaching home. Damage from any range turns it on the attacker.
// Warriors run up and hit; Mages stop KeepDistance away and cast Fireball. NavMesh paths, or
// straight lines without one.
UCLASS()
class CROSSGUARD_API ACrossguardMonsterController : public AAIController
{
	GENERATED_BODY()

public:
	ACrossguardMonsterController();

	virtual void Tick(float DeltaSeconds) override;

	// Hit by Attacker, from any range: with no target (or walking home), it turns on them.
	void NotifyAttacked(AActor* Attacker);

private:
	// The nearest valid target within AggroRange in line of sight, standing within LeashRange of
	// home (no point chasing someone the leash would pull it back from).
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
