#pragma once

#include "CoreMinimal.h"
#include "CrossguardRules/Teams.h"
#include "GameFramework/GameModeBase.h"

#include "CrossguardGameMode.generated.h"

// Match game mode. Assigns teams on login from the "Party" URL option: players joining with the
// same ?Party=<key> share a team (up to 8); players without one each get their own team.
// Matchmaking must put the party key in the travel URL.
UCLASS()
class CROSSGUARD_API ACrossguardGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACrossguardGameMode();

	// Refuses a login whose party's team is already full.
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options,
		const FString& Portal = TEXT("")) override;
	virtual void Logout(AController* Exiting) override;

	static const TCHAR* PartyOption;

private:
	static std::string PlayerKey(const FUniqueNetIdRepl& UniqueId, const AController* Controller);
	static std::string PartyKey(const FString& Options);

	Crossguard::FTeamRegistry Teams;
	// Controller -> registry key, so Logout can find the player.
	TMap<TWeakObjectPtr<const AController>, FString> PlayerKeys;
};
