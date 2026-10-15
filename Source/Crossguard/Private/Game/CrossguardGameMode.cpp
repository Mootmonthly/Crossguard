#include "Game/CrossguardGameMode.h"

#include "Character/CrossguardCharacter.h"
#include "Crossguard.h"
#include "Game/CrossguardPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

const TCHAR* ACrossguardGameMode::PartyOption = TEXT("Party");

ACrossguardGameMode::ACrossguardGameMode()
{
	DefaultPawnClass = ACrossguardCharacter::StaticClass();
	PlayerStateClass = ACrossguardPlayerState::StaticClass();
}

std::string ACrossguardGameMode::PlayerKey(const FUniqueNetIdRepl& UniqueId, const AController* Controller)
{
	// Online id when there is one; otherwise the controller (local / PIE testing).
	const FString Key = UniqueId.IsValid() ? UniqueId.ToString() : GetPathNameSafe(Controller);
	return std::string(TCHAR_TO_UTF8(*Key));
}

std::string ACrossguardGameMode::PartyKey(const FString& Options)
{
	return std::string(TCHAR_TO_UTF8(*UGameplayStatics::ParseOption(Options, PartyOption)));
}

void ACrossguardGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	if (!ErrorMessage.IsEmpty())
	{
		return;
	}

	// Only the party cap can be checked here: without an online id there is no player key yet.
	const std::string Party = PartyKey(Options);
	if (!Party.empty() && UniqueId.IsValid() && Teams.CanJoin(PlayerKey(UniqueId, nullptr), Party) == Crossguard::ETeamJoinError::TeamFull)
	{
		ErrorMessage = TEXT("Your party's team is full.");
	}
}

FString ACrossguardGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options,
	const FString& Portal)
{
	const FString Error = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
	if (!Error.IsEmpty())
	{
		return Error;
	}

	// PreLogin's check can race with another login, so Join decides.
	const std::string Key = PlayerKey(UniqueId, NewPlayerController);
	const Crossguard::FTeamJoinResult Joined = Teams.Join(Key, PartyKey(Options));
	switch (Joined.Error)
	{
	case Crossguard::ETeamJoinError::None:
		break;
	case Crossguard::ETeamJoinError::TeamFull:
		return TEXT("Your party's team is full.");
	case Crossguard::ETeamJoinError::AlreadyInMatch:
		return TEXT("You are already in this match.");
	case Crossguard::ETeamJoinError::InvalidPlayer:
		return TEXT("Invalid player.");
	}

	PlayerKeys.Add(NewPlayerController, FString(UTF8_TO_TCHAR(Key.c_str())));
	if (ACrossguardPlayerState* PlayerState = NewPlayerController->GetPlayerState<ACrossguardPlayerState>())
	{
		PlayerState->SetTeamId(Joined.TeamId);
	}
	UE_LOG(LogCrossguard, Log, TEXT("%s joined team %d"), *GetNameSafe(NewPlayerController), Joined.TeamId);
	return FString();
}

void ACrossguardGameMode::Logout(AController* Exiting)
{
	FString Key;
	if (PlayerKeys.RemoveAndCopyValue(Exiting, Key))
	{
		Teams.Leave(std::string(TCHAR_TO_UTF8(*Key)));
	}
	Super::Logout(Exiting);
}
