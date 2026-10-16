#include "Game/CrossguardGameMode.h"

#include "Character/CrossguardCharacter.h"
#include "Character/CrossguardMinion.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Crossguard.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Game/CrossguardGuildSubsystem.h"
#include "Game/CrossguardPartySubsystem.h"
#include "Game/CrossguardPlayerState.h"
#include "Game/CrossguardSaveSubsystem.h"
#include "Game/CrossguardTeamLibrary.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UI/CrossguardHUD.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"

const TCHAR* ACrossguardGameMode::PartyOption = TEXT("Party");
const TCHAR* ACrossguardGameMode::HubOption = TEXT("Hub");

ACrossguardGameMode::ACrossguardGameMode()
{
	DefaultPawnClass = ACrossguardCharacter::StaticClass();
	PlayerStateClass = ACrossguardPlayerState::StaticClass();
	// Placeholder UI built in C++ (docs/ui.md); a Blueprint game mode can set its own HUD Class.
	HUDClass = ACrossguardHUD::StaticClass();
	// A hub party's ServerTravel to the raid map keeps the player states, and with them each
	// PartyKey (HandleSeamlessTravelPlayer puts the party on one team again).
	bUseSeamlessTravel = true;
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

void ACrossguardGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	if (UGameplayStatics::HasOption(Options, HubOption))
	{
		bIsRaid = false;
	}
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
	FString Party = UGameplayStatics::ParseOption(Options, PartyOption);
	if (Party.IsEmpty())
	{
		// No ?Party=: a party this server already knows them in (a non-seamless travel from the hub
		// drops the player states, and the server's URL options never reach a client's login).
		const UGameInstance* GameInstance = GetGameInstance();
		const UCrossguardPartySubsystem* Parties = GameInstance != nullptr ? GameInstance->GetSubsystem<UCrossguardPartySubsystem>() : nullptr;
		if (Parties != nullptr)
		{
			Party = Parties->PartyKeyOf(UniqueId.IsValid() ? UniqueId.ToString() : UCrossguardGuildSubsystem::PlayerIdOf(NewPlayerController->PlayerState));
		}
	}
	return JoinTeam(NewPlayerController, PlayerKey(UniqueId, NewPlayerController), Party);
}

void ACrossguardGameMode::SetPlayerParty(AController* Controller, const FString& Party)
{
	ACrossguardPlayerState* PlayerState = Controller != nullptr ? Controller->GetPlayerState<ACrossguardPlayerState>() : nullptr;
	if (PlayerState == nullptr || (PlayerState->PartyKey == Party && PlayerState->GetTeamId() >= 0))
	{
		return;
	}
	FString Key;
	if (PlayerKeys.RemoveAndCopyValue(TWeakObjectPtr<const AController>(Controller), Key))
	{
		Teams.Leave(std::string(TCHAR_TO_UTF8(*Key)));
	}
	else
	{
		Key = FString(UTF8_TO_TCHAR(PlayerKey(PlayerState->GetUniqueId(), Controller).c_str()));
	}
	const std::string RegistryKey(TCHAR_TO_UTF8(*Key));
	if (const FString Error = JoinTeam(Controller, RegistryKey, Party); !Error.IsEmpty())
	{
		UE_LOG(LogCrossguard, Warning, TEXT("%s could not join party %s's team (%s); on a team of their own"), *GetNameSafe(Controller), *Party, *Error);
		JoinTeam(Controller, RegistryKey, FString());
	}
}

FString ACrossguardGameMode::GetRaidMapName() const
{
	FString FromCommandLine;
	return FParse::Value(FCommandLine::Get(), TEXT("RaidMap="), FromCommandLine) ? FromCommandLine : RaidMapName;
}

FString ACrossguardGameMode::GetRaidServerAddress() const
{
	FString FromCommandLine;
	return FParse::Value(FCommandLine::Get(), TEXT("RaidServer="), FromCommandLine) ? FromCommandLine : RaidServerAddress;
}

FString ACrossguardGameMode::JoinTeam(AController* Controller, const std::string& Key, const FString& Party)
{
	const Crossguard::FTeamJoinResult Joined = Teams.Join(Key, std::string(TCHAR_TO_UTF8(*Party)));
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

	PlayerKeys.Add(TWeakObjectPtr<const AController>(Controller), FString(UTF8_TO_TCHAR(Key.c_str())));
	if (ACrossguardPlayerState* PlayerState = Controller->GetPlayerState<ACrossguardPlayerState>())
	{
		PlayerState->PartyKey = Party;
		PlayerState->SetTeamId(Joined.TeamId);
	}
	UE_LOG(LogCrossguard, Log, TEXT("%s joined team %d"), *GetNameSafe(Controller), Joined.TeamId);
	return FString();
}

void ACrossguardGameMode::HandleSeamlessTravelPlayer(AController*& C)
{
	Super::HandleSeamlessTravelPlayer(C);

	const ACrossguardPlayerState* PlayerState = C != nullptr ? C->GetPlayerState<ACrossguardPlayerState>() : nullptr;
	if (PlayerState == nullptr)
	{
		return;
	}
	const FString Error = JoinTeam(C, PlayerKey(PlayerState->GetUniqueId(), C), PlayerState->PartyKey);
	if (!Error.IsEmpty())
	{
		UE_LOG(LogCrossguard, Warning, TEXT("%s could not rejoin a team after travel: %s"), *GetNameSafe(C), *Error);
	}
}

void ACrossguardGameMode::Logout(AController* Exiting)
{
	// A last save while the player state is still here.
	UCrossguardSaveSubsystem* Saves = UCrossguardSaveSubsystem::Get(this);
	if (ACrossguardPlayerState* LeavingState = Saves != nullptr && Exiting != nullptr ? Exiting->GetPlayerState<ACrossguardPlayerState>() : nullptr)
	{
		Saves->PlayerLeaving(LeavingState);
	}

	// In the hub, leaving the server leaves the party (the rest get a new leader if need be). In a
	// raid the party stays: it is the team.
	const UGameInstance* GameInstance = GetGameInstance();
	UCrossguardPartySubsystem* Parties = GameInstance != nullptr ? GameInstance->GetSubsystem<UCrossguardPartySubsystem>() : nullptr;
	if (!bIsRaid && Parties != nullptr && Exiting != nullptr)
	{
		Parties->PlayerLeftServer(Exiting->GetPlayerState<ACrossguardPlayerState>());
	}

	FString Key;
	if (PlayerKeys.RemoveAndCopyValue(TWeakObjectPtr<const AController>(Exiting), Key))
	{
		Teams.Leave(std::string(TCHAR_TO_UTF8(*Key)));
	}
	Super::Logout(Exiting);
	// Whoever was watching the leaving player needs someone else.
	UpdateSpectators();
}

void ACrossguardGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);

	ACrossguardPlayerState* PlayerState = NewPlayer != nullptr ? NewPlayer->GetPlayerState<ACrossguardPlayerState>() : nullptr;
	// The pawn is possessed: the loaded save goes onto it (gear carried in) before a raid begins.
	UCrossguardSaveSubsystem* Saves = UCrossguardSaveSubsystem::Get(this);
	if (Saves != nullptr && PlayerState != nullptr)
	{
		Saves->PlayerReady(PlayerState);
	}
	if (bIsRaid && PlayerState != nullptr)
	{
		PlayerState->BeginRaid();
	}
}

bool ACrossguardGameMode::PlayerCanRestart_Implementation(APlayerController* Player)
{
	const ACrossguardPlayerState* PlayerState = Player != nullptr ? Player->GetPlayerState<ACrossguardPlayerState>() : nullptr;
	if (bIsRaid && PlayerState != nullptr && PlayerState->HasFinishedRaid())
	{
		return false;
	}
	return Super::PlayerCanRestart_Implementation(Player);
}

TArray<FCrossguardInventoryItem> ACrossguardGameMode::RollLoot(FName TableId)
{
	const int64 Seed = (static_cast<int64>(FMath::Rand()) << 31) ^ static_cast<int64>(FMath::Rand()) ^ static_cast<int64>(FPlatformTime::Cycles64());
	return UCrossguardLootLibrary::RollLoot(Loot, TableId, Seed);
}

void ACrossguardGameMode::HandleKill(AActor* Victim, AActor* Killer)
{
	// A minion's kills are its summoner's.
	if (const ACrossguardMinion* Minion = Cast<ACrossguardMinion>(Killer))
	{
		Killer = Minion->GetSummoner();
	}
	if (const ACrossguardCharacter* VictimCharacter = Cast<ACrossguardCharacter>(Victim); VictimCharacter != nullptr && !VictimCharacter->bGrantsKillXp)
	{
		return;
	}
	const APawn* KillerPawn = Cast<APawn>(Killer);
	ACrossguardPlayerState* KillerState = KillerPawn != nullptr ? KillerPawn->GetPlayerState<ACrossguardPlayerState>() : nullptr;
	if (KillerState == nullptr || Victim == nullptr || Killer == Victim || UCrossguardTeamLibrary::AreAllies(Killer, Victim))
	{
		return;
	}

	const APawn* VictimPawn = Cast<APawn>(Victim);
	if (VictimPawn != nullptr && VictimPawn->IsPlayerControlled())
	{
		KillerState->AwardXp(ECrossguardXpSource::PlayerKill);
	}
	else if (const ACrossguardCharacter* Monster = Cast<ACrossguardCharacter>(Victim))
	{
		KillerState->AwardXp(Monster->KillXpSource);
	}
}

void ACrossguardGameMode::HandleExtraction(ACrossguardCharacter* Character)
{
	ACrossguardPlayerState* PlayerState = Character != nullptr ? Character->GetPlayerState<ACrossguardPlayerState>() : nullptr;
	const UAbilitySystemComponent* ASC = Character != nullptr ? Character->GetAbilitySystemComponent() : nullptr;
	if (PlayerState == nullptr || !PlayerState->IsInRaid() || ASC == nullptr || ASC->HasMatchingGameplayTag(CrossguardTags::State_Dead))
	{
		return;
	}
	// Saves the gear carried out while the pawn still holds it.
	const int32 Xp = PlayerState->EndRaid(true);
	UE_LOG(LogCrossguard, Log, TEXT("%s extracted: %d XP banked, %d gold banked in total"), *Character->GetName(), Xp, PlayerState->GetGold());
	OnPlayerExtracted.Broadcast(Character);

	// The character leaves the raid: nothing of it stays to loot, pick up, trade, open doors or keep a
	// portal alive, and nothing it would touch reaches the save (the record now holds the gear carried
	// out). Unpossessed first, like a death, so the controller is not left waiting to respawn; the pawn
	// goes next tick (this can run inside its own overlap event). The player watches a teammate still
	// in the raid (ruled; docs/design-decisions.md, Extraction). The trip to the hub does not exist yet.
	Character->MarkLeftRaid();
	Character->SetActorHiddenInGame(true);
	if (APlayerController* Player = Character->GetController<APlayerController>())
	{
		Player->UnPossess();
		ExtractedPlayers.AddUnique(Player);
		Spectators.AddUnique(Player);
	}
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(Character, [Character]()
	{
		Character->Destroy();
	}));
	UpdateSpectators();
}

void ACrossguardGameMode::StartPlay()
{
	Super::StartPlay();
	RaidStartSeconds = GetWorld()->GetTimeSeconds();
}

float ACrossguardGameMode::GetRaidElapsedSeconds() const
{
	return bIsRaid ? GetWorld()->GetTimeSeconds() - RaidStartSeconds : 0.0f;
}

void ACrossguardGameMode::HandlePermanentDeath(ACrossguardCharacter* Character)
{
	ACrossguardPlayerState* PlayerState = Character != nullptr ? Character->GetPlayerState<ACrossguardPlayerState>() : nullptr;
	APlayerController* Player = Character != nullptr ? Character->GetController<APlayerController>() : nullptr;
	if (PlayerState == nullptr || Player == nullptr || !bIsRaid || !PlayerState->IsInRaid())
	{
		// AI, a hub map, or a player who already extracted: no raid to end.
		return;
	}
	// The body stays in the world; the player only watches from now on. Unpossessed before the raid
	// ends, so the failure's save takes no gear from the body (it keeps the raid start's: none).
	Player->UnPossess();
	PlayerState->EndRaid(false);
	UE_LOG(LogCrossguard, Log, TEXT("%s is dead for the raid"), *Character->GetName());
	Spectators.AddUnique(Player);
	UpdateSpectators();
}

APawn* ACrossguardGameMode::FindSpectateTarget(const APlayerController* Player) const
{
	const int32 Team = UCrossguardTeamLibrary::GetTeamId(Player);
	if (Team < 0 || GameState == nullptr)
	{
		return nullptr;
	}
	for (APlayerState* Other : GameState->PlayerArray)
	{
		const ACrossguardPlayerState* Teammate = Cast<ACrossguardPlayerState>(Other);
		// Downed teammates are still in the raid and can still be watched.
		if (Teammate != nullptr && Teammate->GetOwner() != Player && Teammate->GetTeamId() == Team && Teammate->IsInRaid()
			&& Teammate->GetPawn() != nullptr)
		{
			return Teammate->GetPawn();
		}
	}
	return nullptr;
}

void ACrossguardGameMode::UpdateSpectators()
{
	for (int32 Index = Spectators.Num() - 1; Index >= 0; --Index)
	{
		APlayerController* Player = Spectators[Index].Get();
		if (Player == nullptr)
		{
			Spectators.RemoveAt(Index);
			continue;
		}
		const APawn* WatchedPawn = Cast<APawn>(Player->GetViewTarget());
		const ACrossguardPlayerState* Watched = WatchedPawn != nullptr ? WatchedPawn->GetPlayerState<ACrossguardPlayerState>() : nullptr;
		if (Watched != nullptr && Watched->IsInRaid() && Watched->GetOwner() != Player)
		{
			continue;
		}
		if (APawn* Target = FindSpectateTarget(Player))
		{
			// Also tells a remote client's camera.
			Player->SetViewTargetWithBlend(Target, 0.5f);
			continue;
		}
		// Solo, or no teammate left in the raid.
		Spectators.RemoveAt(Index);
		if (ExtractedPlayers.Contains(Player))
		{
			// Extracted: the raid went well; they stay where they left until the hub trip exists.
			continue;
		}
		if (Player->IsLocalController() && GetNetMode() == NM_ListenServer)
		{
			// Sending the listen-server host to the menu would end the match for everyone; it keeps
			// watching its body instead.
			continue;
		}
		Player->ClientReturnToMainMenuWithTextReason(FText::FromString(TEXT("Your raid is over.")));
	}
}
