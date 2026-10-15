#include "Character/CrossguardMinion.h"

#include "Character/CrossguardMinionController.h"
#include "Net/UnrealNetwork.h"

ACrossguardMinion::ACrossguardMinion()
{
	AIControllerClass = ACrossguardMinionController::StaticClass();
	bGrantsKillXp = false;
	// No body: it vanishes.
	BodyContainerClass = nullptr;
}

void ACrossguardMinion::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACrossguardMinion, Summoner);
}

FGenericTeamId ACrossguardMinion::GetGenericTeamId() const
{
	const ACrossguardCharacter* SummonerCharacter = Summoner.Get();
	return SummonerCharacter != nullptr ? SummonerCharacter->GetGenericTeamId() : FGenericTeamId::NoTeam;
}

void ACrossguardMinion::Dismiss()
{
	if (HasAuthority() && !IsActorBeingDestroyed())
	{
		Destroy();
	}
}

void ACrossguardMinion::HandleDeath()
{
	// No body, no loot, no revive: it collapses and vanishes.
	if (!HasAuthority())
	{
		return;
	}
	// Its controller sees it is dead and stops; destroying the pawn destroys the controller too.
	SetLifeSpan(3.0f);
}
