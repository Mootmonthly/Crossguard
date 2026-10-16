#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "CrossguardConnectionErrorSubsystem.generated.h"

class UNetDriver;
class UWorld;

// Remembers why this game instance's last connection or travel failed (the server's refusal, e.g.
// "No match ticket: start the raid from the hub.", or a lost connection), so the main menu the engine
// falls back to can show it (UCrossguardMainMenuWidget). Client side; nothing is sent anywhere.
UCLASS()
class CROSSGUARD_API UCrossguardConnectionErrorSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// The last failure's message ("" if none since the last take), cleared.
	FString TakeLastError();

	// The menu's line for a failure message: "Connection failed: <message>" ("" for none).
	static FString MenuLine(const FString& Message);

private:
	void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);
	// Keeps Message when World belongs to this game instance (one process can run several in PIE).
	void Remember(const UWorld* World, const FString& Message);

	FString LastError;
	FDelegateHandle NetworkFailureHandle;
	FDelegateHandle TravelFailureHandle;
};
