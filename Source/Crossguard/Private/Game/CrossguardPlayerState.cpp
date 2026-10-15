#include "Game/CrossguardPlayerState.h"

#include "Net/UnrealNetwork.h"

void ACrossguardPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Everyone knows everyone's team (nameplates, party skills).
	DOREPLIFETIME(ACrossguardPlayerState, TeamId);
}

void ACrossguardPlayerState::SetTeamId(int32 NewTeamId)
{
	if (HasAuthority() && TeamId != NewTeamId)
	{
		TeamId = NewTeamId;
		OnTeamChanged.Broadcast(TeamId);
	}
}

void ACrossguardPlayerState::OnRep_TeamId()
{
	OnTeamChanged.Broadcast(TeamId);
}

FGenericTeamId ACrossguardPlayerState::GetGenericTeamId() const
{
	return TeamId >= 0 && TeamId < FGenericTeamId::NoTeam.GetId() ? FGenericTeamId(static_cast<uint8>(TeamId)) : FGenericTeamId::NoTeam;
}
