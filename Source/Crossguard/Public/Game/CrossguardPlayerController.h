#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

#include "CrossguardPlayerController.generated.h"

class UCrossguardSettingsSave;

// The game's player controller (ACrossguardGameMode's PlayerControllerClass).
//
// Server: when the player's connection closes in the middle of a raid, the game mode may hold their
// character and player state for a reconnect (ACrossguardGameMode::HoldForReconnect,
// docs/persistence.md "Reconnecting"). This controller then neither destroys the pawn (the engine's
// PawnLeavingGame) nor its player state (CleanupPlayerState) as it goes.
//
// Local player: applies the saved settings (UCrossguardSettingsSave: mouse sensitivity, field of
// view, master volume), shows server messages on the HUD (ClientShowMessage), opens the party
// window when asked (ClientOpenPartyPanel) and travels back to the hub with its team when told
// (ClientTravelToHub).
UCLASS()
class CROSSGUARD_API ACrossguardPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	// A new pawn gets the field of view setting (its first camera component).
	virtual void SetPawn(APawn* InPawn) override;
	// Mouse look scaled by the sensitivity setting.
	virtual void AddYawInput(float Val) override;
	virtual void AddPitchInput(float Val) override;

	// Local player: takes these settings now (sensitivity, the pawn's field of view, master volume).
	void ApplySettings(const UCrossguardSettingsSave& Settings);

	// Server -> owning client: a line on the HUD (e.g. the raid portal's refusal).
	UFUNCTION(Client, Reliable)
	void ClientShowMessage(const FString& Message, FLinearColor Color);

	// Server -> owning client: opens the party window (the hub's party board, ACrossguardPartyBoard).
	UFUNCTION(Client, Reliable)
	void ClientOpenPartyPanel();

	// Server -> owning client: the player's team finished the raid early and goes back to the hub
	// (ACrossguardGameMode::ReturnTeamToHub): after DelaySeconds (a member waiting for the leader's hub
	// to load) the client travels to Url (the hub server, or the leader's address; the leader's own
	// "<hub map>?listen?Hub?Party=<key>"). The HUD counts down meanwhile.
	UFUNCTION(Client, Reliable)
	void ClientTravelToHub(const FString& Url, float DelaySeconds);

	// Server: the game mode holds this player's character and player state for a reconnect.
	bool IsHeldForReconnect() const { return bHeldForReconnect; }

	// The connection closed (crash, quit, logout) while possessing a pawn.
	virtual void PawnLeavingGame() override;
	// Destroyed: the player state goes too, unless it is held for a reconnect.
	virtual void CleanupPlayerState() override;

private:
	void ApplyFieldOfView() const;
	// Local player: the travel ClientTravelToHub asked for.
	void TravelToHubNow();

	FString PendingHubUrl;
	FTimerHandle HubTravelTimer;

	bool bHeldForReconnect = false;
	float SettingsLookSensitivity = 1.0f;
	// Degrees; 0 until settings are applied (the camera keeps its own).
	float SettingsFieldOfView = 0.0f;
};
