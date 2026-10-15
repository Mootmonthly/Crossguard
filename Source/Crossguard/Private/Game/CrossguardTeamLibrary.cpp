#include "Game/CrossguardTeamLibrary.h"

#include "Game/CrossguardPlayerState.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"

int32 UCrossguardTeamLibrary::GetTeamId(const AActor* Actor)
{
	const ACrossguardPlayerState* PlayerState = Cast<ACrossguardPlayerState>(Actor);
	if (PlayerState == nullptr)
	{
		if (const APawn* Pawn = Cast<APawn>(Actor))
		{
			PlayerState = Pawn->GetPlayerState<ACrossguardPlayerState>();
		}
		else if (const AController* Controller = Cast<AController>(Actor))
		{
			PlayerState = Controller->GetPlayerState<ACrossguardPlayerState>();
		}
	}
	return PlayerState != nullptr ? PlayerState->GetTeamId() : -1;
}

bool UCrossguardTeamLibrary::AreAllies(const AActor* A, const AActor* B)
{
	const int32 TeamA = GetTeamId(A);
	return TeamA >= 0 && TeamA == GetTeamId(B);
}
