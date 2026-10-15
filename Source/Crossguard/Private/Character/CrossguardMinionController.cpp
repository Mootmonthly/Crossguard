#include "Character/CrossguardMinionController.h"

#include "AbilitySystem/CrossguardGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Character/CrossguardMinion.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/CrossguardTeamLibrary.h"
#include "Navigation/PathFollowingComponent.h"

namespace
{
	constexpr float RethinkSeconds = 0.5f;

	bool IsAliveTarget(const AActor* Actor)
	{
		const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor);
		return ASC != nullptr && !Actor->IsActorBeingDestroyed() && !ASC->HasMatchingGameplayTag(CrossguardTags::State_Dead);
	}
}

ACrossguardMinionController::ACrossguardMinionController()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ACrossguardMinionController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	RethinkCountdown = 0.0f;
}

AActor* ACrossguardMinionController::FindTarget(const ACrossguardMinion& Minion) const
{
	const ACrossguardCharacter* Summoner = Minion.GetSummoner();
	const AActor* Friend = Summoner != nullptr ? static_cast<const AActor*>(Summoner) : &Minion;
	const FVector Origin = Minion.GetActorLocation();

	AActor* BestPlayer = nullptr;
	AActor* BestOther = nullptr;
	float BestPlayerDistance = FMath::Square(AggroRange);
	float BestOtherDistance = BestPlayerDistance;
	for (TActorIterator<ACrossguardCharacter> It(Minion.GetWorld()); It; ++It)
	{
		ACrossguardCharacter* Candidate = *It;
		if (Candidate == &Minion || Candidate == Summoner || !IsAliveTarget(Candidate))
		{
			continue;
		}
		// Allies: the summoner's team, and the summoner's own minions (solo players have no team).
		const ACrossguardMinion* OtherMinion = Cast<ACrossguardMinion>(Candidate);
		const AActor* CandidateSide = OtherMinion != nullptr && OtherMinion->GetSummoner() != nullptr ? OtherMinion->GetSummoner() : Candidate;
		if (CandidateSide == Friend || UCrossguardTeamLibrary::AreAllies(Friend, CandidateSide))
		{
			continue;
		}
		const float Distance = FVector::DistSquared(Origin, Candidate->GetActorLocation());
		if (Candidate->IsPlayerControlled())
		{
			if (Distance < BestPlayerDistance)
			{
				BestPlayerDistance = Distance;
				BestPlayer = Candidate;
			}
		}
		else if (Distance < BestOtherDistance)
		{
			BestOtherDistance = Distance;
			BestOther = Candidate;
		}
	}
	return BestPlayer != nullptr ? BestPlayer : BestOther;
}

void ACrossguardMinionController::MoveToward(AActor* Goal, float Acceptance)
{
	if (Goal == MoveGoal.Get() && FMath::IsNearlyEqual(Acceptance, MoveAcceptance) && (bDirectMove || GetMoveStatus() != EPathFollowingStatus::Idle))
	{
		return;
	}
	MoveGoal = Goal;
	MoveAcceptance = Acceptance;
	bDirectMove = MoveToActor(Goal, Acceptance) == EPathFollowingRequestResult::Failed;
}

void ACrossguardMinionController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ACrossguardMinion* Minion = Cast<ACrossguardMinion>(GetPawn());
	if (Minion == nullptr || !HasAuthority())
	{
		return;
	}
	if (!Minion->IsAlive())
	{
		StopMovement();
		return;
	}

	RethinkCountdown -= DeltaSeconds;
	if (RethinkCountdown <= 0.0f)
	{
		RethinkCountdown = RethinkSeconds;
		Target = FindTarget(*Minion);
		const bool bMage = Minion->Role == ECrossguardMinionRole::Mage;
		if (AActor* Enemy = Target.Get())
		{
			MoveToward(Enemy, bMage ? Minion->KeepDistance : Minion->AttackRange * 0.5f);
		}
		else if (ACrossguardCharacter* Summoner = Minion->GetSummoner())
		{
			MoveToward(Summoner, FollowDistance);
		}
		else
		{
			StopMovement();
			MoveGoal.Reset();
			bDirectMove = false;
		}
	}

	if (AActor* Enemy = Target.Get())
	{
		if (IsAliveTarget(Enemy))
		{
			SetFocus(Enemy);
			// The Mage needs to see its target; the Warrior checks its reach itself.
			if (Minion->Role != ECrossguardMinionRole::Mage || LineOfSightTo(Enemy))
			{
				Minion->TryAttack(Enemy);
			}
		}
	}
	else
	{
		ClearFocus(EAIFocusPriority::Gameplay);
	}

	// No NavMesh path: walk straight at the goal.
	AActor* Goal = MoveGoal.Get();
	if (bDirectMove && Goal != nullptr)
	{
		const FVector ToGoal = Goal->GetActorLocation() - Minion->GetActorLocation();
		if (ToGoal.Size2D() > MoveAcceptance)
		{
			Minion->AddMovementInput(FVector(ToGoal.X, ToGoal.Y, 0.0f).GetSafeNormal());
		}
	}
}
