#include "Game/CrossguardPlayerController.h"

#include "Engine/World.h"
#include "Game/CrossguardGameMode.h"

void ACrossguardPlayerController::PawnLeavingGame()
{
	UWorld* World = GetWorld();
	ACrossguardGameMode* GameMode = World != nullptr ? World->GetAuthGameMode<ACrossguardGameMode>() : nullptr;
	if (GameMode != nullptr && GameMode->HoldForReconnect(this))
	{
		// The game mode unpossessed the pawn and keeps it, with the player state, in the raid.
		bHeldForReconnect = true;
		return;
	}
	Super::PawnLeavingGame();
}

void ACrossguardPlayerController::CleanupPlayerState()
{
	if (bHeldForReconnect)
	{
		// The game mode keeps the player state for the reconnect; only this controller lets go of it.
		PlayerState = nullptr;
		return;
	}
	Super::CleanupPlayerState();
}
