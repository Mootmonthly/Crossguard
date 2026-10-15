#include "Character/CrossguardMonsterController.h"

#include "AbilitySystem/CrossguardGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Character/CrossguardMinion.h"
#include "Character/CrossguardMonster.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Navigation/PathFollowingComponent.h"

namespace
{
	constexpr float MonsterRethinkSeconds = 0.5f;
	// Close enough to home to stop walking back.
	constexpr float MonsterHomeAcceptance = 150.0f;
}

ACrossguardMonsterController::ACrossguardMonsterController()
{
	PrimaryActorTick.bCanEverTick = true;
}

bool ACrossguardMonsterController::IsValidTarget(const ACrossguardMonster& Monster, const AActor* Candidate) const
{
	const ACrossguardCharacter* Character = Cast<ACrossguardCharacter>(Candidate);
	const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Candidate);
	// Players and minions; never another monster. Alive and not stealthed.
	return Character != nullptr && Character != &Monster && !Character->IsA<ACrossguardMonster>()
		&& (Character->IsPlayerControlled() || Character->IsA<ACrossguardMinion>()) && !Character->IsActorBeingDestroyed()
		&& ASC != nullptr && !ASC->HasMatchingGameplayTag(CrossguardTags::State_Dead) && !ASC->HasMatchingGameplayTag(CrossguardTags::State_Stealthed);
}

AActor* ACrossguardMonsterController::FindTarget(const ACrossguardMonster& Monster) const
{
	AActor* Best = nullptr;
	float BestDistance = FMath::Square(Monster.AggroRange);
	for (TActorIterator<ACrossguardCharacter> It(Monster.GetWorld()); It; ++It)
	{
		ACrossguardCharacter* Candidate = *It;
		if (!IsValidTarget(Monster, Candidate))
		{
			continue;
		}
		const float Distance = FVector::DistSquared(Monster.GetActorLocation(), Candidate->GetActorLocation());
		if (Distance < BestDistance && FVector::Dist(Monster.GetHomeLocation(), Candidate->GetActorLocation()) <= Monster.LeashRange
			&& LineOfSightTo(Candidate))
		{
			BestDistance = Distance;
			Best = Candidate;
		}
	}
	return Best;
}

void ACrossguardMonsterController::NotifyAttacked(AActor* Attacker)
{
	const ACrossguardMonster* Monster = Cast<ACrossguardMonster>(GetPawn());
	if (Monster == nullptr || !Monster->IsAlive() || !IsValidTarget(*Monster, Attacker))
	{
		return;
	}
	if (bReturningHome || !IsValidTarget(*Monster, Target.Get()))
	{
		Target = Attacker;
		bReturningHome = false;
		RethinkCountdown = 0.0f;
	}
}

void ACrossguardMonsterController::MoveToGoal(AActor* InGoalActor, const FVector& InGoalLocation, float Acceptance)
{
	const bool bSameGoal = bHasGoal && GoalActor.Get() == InGoalActor && (InGoalActor != nullptr || GoalLocation.Equals(InGoalLocation, 1.0f))
		&& FMath::IsNearlyEqual(Acceptance, GoalAcceptance);
	if (bSameGoal && (bDirectMove || GetMoveStatus() != EPathFollowingStatus::Idle))
	{
		return;
	}
	bHasGoal = true;
	GoalActor = InGoalActor;
	GoalLocation = InGoalLocation;
	GoalAcceptance = Acceptance;
	const EPathFollowingRequestResult::Type Result = InGoalActor != nullptr ? MoveToActor(InGoalActor, Acceptance)
		: MoveToLocation(InGoalLocation, Acceptance);
	bDirectMove = Result == EPathFollowingRequestResult::Failed;
}

void ACrossguardMonsterController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ACrossguardMonster* Monster = Cast<ACrossguardMonster>(GetPawn());
	if (Monster == nullptr || !HasAuthority())
	{
		return;
	}
	if (!Monster->IsAlive())
	{
		StopMovement();
		bHasGoal = false;
		return;
	}

	const FVector Home = Monster->GetHomeLocation();
	RethinkCountdown -= DeltaSeconds;
	if (RethinkCountdown <= 0.0f)
	{
		RethinkCountdown = MonsterRethinkSeconds;
		const float FromHome = FVector::Dist(Monster->GetActorLocation(), Home);
		const bool bTargetValid = IsValidTarget(*Monster, Target.Get());
		if (bReturningHome || FromHome > Monster->LeashRange || !bTargetValid)
		{
			// Lost the target or pulled too far: another enemy in aggro range first, else home.
			AActor* Next = FromHome > Monster->LeashRange || !bTargetValid ? FindTarget(*Monster) : Target.Get();
			if (Next != nullptr)
			{
				Target = Next;
				bReturningHome = false;
			}
			else if (FromHome > Monster->LeashRange || bReturningHome)
			{
				Target.Reset();
				bReturningHome = FromHome > MonsterHomeAcceptance;
				// Home with nobody around: a full reset.
				if (!bReturningHome)
				{
					Monster->RestoreFullHealth();
				}
			}
			else
			{
				Target.Reset();
			}
		}

		if (bReturningHome)
		{
			MoveToGoal(nullptr, Home, MonsterHomeAcceptance * 0.5f);
		}
		else if (AActor* Enemy = Target.Get())
		{
			const bool bMage = Monster->CombatRole == ECrossguardCombatRole::Mage;
			MoveToGoal(Enemy, FVector::ZeroVector, bMage ? Monster->KeepDistance : Monster->AttackRange * 0.5f);
		}
		else if (FromHome > MonsterHomeAcceptance)
		{
			MoveToGoal(nullptr, Home, MonsterHomeAcceptance * 0.5f);
		}
		else
		{
			StopMovement();
			bHasGoal = false;
			bDirectMove = false;
		}
	}

	if (AActor* Enemy = Target.Get(); Enemy != nullptr && !bReturningHome && IsValidTarget(*Monster, Enemy))
	{
		// Focus aims its eyes (aimed skills).
		SetFocus(Enemy);
		Monster->TryUseSkills(Enemy);
		// The Mage needs to see its target; the Warrior checks its reach itself.
		if (Monster->CombatRole != ECrossguardCombatRole::Mage || LineOfSightTo(Enemy))
		{
			Monster->TryAttack(Enemy);
		}
	}
	else
	{
		ClearFocus(EAIFocusPriority::Gameplay);
	}

	// No NavMesh path: walk straight at the goal.
	if (bDirectMove && bHasGoal)
	{
		const FVector Goal = GoalActor.IsValid() ? GoalActor->GetActorLocation() : GoalLocation;
		const FVector ToGoal = Goal - Monster->GetActorLocation();
		if (ToGoal.Size2D() > GoalAcceptance)
		{
			Monster->AddMovementInput(FVector(ToGoal.X, ToGoal.Y, 0.0f).GetSafeNormal());
		}
	}
}
