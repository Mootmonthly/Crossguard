#include "Game/CrossguardGameMode.h"

#include "Character/CrossguardCharacter.h"
#include "Character/CrossguardMinion.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Crossguard.h"
#include "Engine/World.h"
#include "Game/CrossguardPlayerState.h"
#include "Game/CrossguardTeamLibrary.h"
#include "GameFramework/GameStateBase.h"
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
	const FString Party = UGameplayStatics::ParseOption(Options, PartyOption);
	return JoinTeam(NewPlayerController, PlayerKey(UniqueId, NewPlayerController), Party);
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
	if (bIsRaid && PlayerState != nullptr)
	{
		PlayerState->BeginRaid();
	}
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
	const int32 Xp = PlayerState->EndRaid(true);
	UE_LOG(LogCrossguard, Log, TEXT("%s extracted: %d XP banked, %d gold banked in total"), *Character->GetName(), Xp, PlayerState->GetGold());
	OnPlayerExtracted.Broadcast(Character);
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
	PlayerState->EndRaid(false);
	UE_LOG(LogCrossguard, Log, TEXT("%s is dead for the raid"), *Character->GetName());

	// The body stays in the world; the player only watches from now on.
	Player->UnPossess();
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
		if (Player->IsLocalController() && GetNetMode() == NM_ListenServer)
		{
			// Sending the listen-server host to the menu would end the match for everyone; it keeps
			// watching its body instead.
			continue;
		}
		Player->ClientReturnToMainMenuWithTextReason(FText::FromString(TEXT("Your raid is over.")));
	}
}
