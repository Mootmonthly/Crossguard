#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

#include "CrossguardPlayerController.generated.h"

// The game's player controller (ACrossguardGameMode's PlayerControllerClass).
//
// Server: when the player's connection closes in the middle of a raid, the game mode may hold their
// character and player state for a reconnect (ACrossguardGameMode::HoldForReconnect,
// docs/persistence.md "Reconnecting"). This controller then neither destroys the pawn (the engine's
// PawnLeavingGame) nor its player state (CleanupPlayerState) as it goes.
UCLASS()
class CROSSGUARD_API ACrossguardPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	// Server: the game mode holds this player's character and player state for a reconnect.
	bool IsHeldForReconnect() const { return bHeldForReconnect; }

	// The connection closed (crash, quit, logout) while possessing a pawn.
	virtual void PawnLeavingGame() override;
	// Destroyed: the player state goes too, unless it is held for a reconnect.
	virtual void CleanupPlayerState() override;

private:
	bool bHeldForReconnect = false;
};
