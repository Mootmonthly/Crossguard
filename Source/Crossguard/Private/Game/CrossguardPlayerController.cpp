#include "Game/CrossguardPlayerController.h"

#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Game/CrossguardGameMode.h"
#include "Game/CrossguardSettingsSave.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"
#include "UI/CrossguardHUD.h"
#include "UI/CrossguardHudWidget.h"
#include "UI/CrossguardPartyWidget.h"

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

void ACrossguardPlayerController::ClientTravelToHub_Implementation(const FString& Url, float DelaySeconds)
{
	if (Url.IsEmpty())
	{
		return;
	}
	PendingHubUrl = Url;
	if (DelaySeconds <= 0.0f)
	{
		TravelToHubNow();
		return;
	}
	// The leader's hub is loading: the HUD says so, then the client connects.
	ClientShowMessage_Implementation(FString::Printf(TEXT("Joining your team's hub in %.0f s"), DelaySeconds), FLinearColor(0.55f, 0.85f, 1.0f));
	GetWorldTimerManager().SetTimer(HubTravelTimer, this, &ACrossguardPlayerController::TravelToHubNow, DelaySeconds, false);
}

void ACrossguardPlayerController::TravelToHubNow()
{
	UWorld* World = GetWorld();
	if (GEngine == nullptr || World == nullptr || PendingHubUrl.IsEmpty())
	{
		return;
	}
	// What the engine's ClientTravel does on the client (and the "open" command): a listen hub of our
	// own, or a connection to the hub. Absolute: the raid's options do not carry over.
	GEngine->SetClientTravel(World, *PendingHubUrl, TRAVEL_Absolute);
}

void ACrossguardPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (IsLocalPlayerController())
	{
		if (const UCrossguardSettingsSave* Settings = UCrossguardSettingsSave::LoadOrDefault())
		{
			ApplySettings(*Settings);
		}
	}
}

void ACrossguardPlayerController::ApplySettings(const UCrossguardSettingsSave& Settings)
{
	SettingsLookSensitivity = Settings.MouseSensitivity;
	SettingsFieldOfView = Settings.FieldOfView;
	ApplyFieldOfView();
	Settings.ApplyVolume();
}

void ACrossguardPlayerController::SetPawn(APawn* InPawn)
{
	Super::SetPawn(InPawn);
	ApplyFieldOfView();
}

void ACrossguardPlayerController::ApplyFieldOfView() const
{
	if (SettingsFieldOfView <= 0.0f || !IsLocalPlayerController())
	{
		return;
	}
	const APawn* ControlledPawn = GetPawn();
	// Not "Camera": APlayerController has an exec function of that name.
	if (UCameraComponent* PawnCamera = ControlledPawn != nullptr ? ControlledPawn->FindComponentByClass<UCameraComponent>() : nullptr)
	{
		PawnCamera->SetFieldOfView(SettingsFieldOfView);
	}
}

void ACrossguardPlayerController::AddYawInput(float Val)
{
	Super::AddYawInput(Val * SettingsLookSensitivity);
}

void ACrossguardPlayerController::AddPitchInput(float Val)
{
	Super::AddPitchInput(Val * SettingsLookSensitivity);
}

void ACrossguardPlayerController::ClientShowMessage_Implementation(const FString& Message, FLinearColor Color)
{
	const ACrossguardHUD* Hud = GetHUD<ACrossguardHUD>();
	if (UCrossguardHudWidget* HudWidget = Hud != nullptr ? Hud->GetHudWidget() : nullptr)
	{
		HudWidget->ShowMessage(Message, Color);
	}
}

void ACrossguardPlayerController::ClientOpenPartyPanel_Implementation()
{
	if (ACrossguardHUD* Hud = GetHUD<ACrossguardHUD>())
	{
		Hud->OpenWindow(Hud->GetPartyWidget());
	}
}
