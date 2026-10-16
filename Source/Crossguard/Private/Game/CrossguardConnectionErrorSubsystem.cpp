#include "Game/CrossguardConnectionErrorSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"

void UCrossguardConnectionErrorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (GEngine != nullptr)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UCrossguardConnectionErrorSubsystem::HandleNetworkFailure);
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &UCrossguardConnectionErrorSubsystem::HandleTravelFailure);
	}
}

void UCrossguardConnectionErrorSubsystem::Deinitialize()
{
	if (GEngine != nullptr)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}
	Super::Deinitialize();
}

FString UCrossguardConnectionErrorSubsystem::TakeLastError()
{
	FString Taken = MoveTemp(LastError);
	LastError.Reset();
	return Taken;
}

FString UCrossguardConnectionErrorSubsystem::MenuLine(const FString& Message)
{
	return Message.IsEmpty() ? FString() : FString::Printf(TEXT("Connection failed: %s"), *Message);
}

void UCrossguardConnectionErrorSubsystem::HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType,
	const FString& ErrorString)
{
	// Only the client side of a connection: a server's own drivers report their clients' troubles too.
	if (NetDriver != nullptr && NetDriver->IsServer())
	{
		return;
	}
	Remember(World, ErrorString.IsEmpty() ? FString(ENetworkFailure::ToString(FailureType)) : ErrorString);
}

void UCrossguardConnectionErrorSubsystem::HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString)
{
	Remember(World, ErrorString.IsEmpty() ? FString(ETravelFailure::ToString(FailureType)) : ErrorString);
}

void UCrossguardConnectionErrorSubsystem::Remember(const UWorld* World, const FString& Message)
{
	if (World != nullptr && World->GetGameInstance() != GetGameInstance())
	{
		return;
	}
	LastError = Message.Left(300);
}
