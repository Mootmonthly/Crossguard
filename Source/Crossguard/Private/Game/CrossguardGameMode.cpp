#include "Game/CrossguardGameMode.h"

#include "Character/CrossguardCharacter.h"
#include "Character/CrossguardMinion.h"
#include "AbilitySystem/CrossguardAttributeSet.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Crossguard.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Game/CrossguardGuildSubsystem.h"
#include "Game/CrossguardHubMarker.h"
#include "Game/CrossguardMenuGameMode.h"
#include "Game/CrossguardPartySubsystem.h"
#include "Game/CrossguardPlayerController.h"
#include "Game/CrossguardPlayerState.h"
#include "Game/CrossguardSaveSubsystem.h"
#include "Game/CrossguardTeamLibrary.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UI/CrossguardHUD.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "TimerManager.h"

const TCHAR* ACrossguardGameMode::PartyOption = TEXT("Party");
const TCHAR* ACrossguardGameMode::HubOption = TEXT("Hub");
const TCHAR* ACrossguardGameMode::DevPlayerIdOption = TEXT("DevPlayerId");

ACrossguardGameMode::ACrossguardGameMode()
{
	DefaultPawnClass = ACrossguardCharacter::StaticClass();
	PlayerStateClass = ACrossguardPlayerState::StaticClass();
	// Holds a raider's character when their connection drops (docs/persistence.md, "Reconnecting").
	PlayerControllerClass = ACrossguardPlayerController::StaticClass();
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
	if (IsHubMap(Options))
	{
		bIsRaid = false;
	}
}

bool ACrossguardGameMode::IsHubMap(const FString& Options) const
{
	return UGameplayStatics::HasOption(Options, HubOption) || ACrossguardHubMarker::IsHubWorld(GetWorld());
}

void ACrossguardGameMode::ReturnToMenu(APlayerController* Player, const FString& ReasonKey)
{
	if (Player == nullptr)
	{
		return;
	}
	// The server checks the map exists (same build as the client): a client travelling to a missing
	// map would fail and fall back to the default map anyway, with a travel error first.
	const FString Url = ACrossguardMenuGameMode::BuildMenuUrl(MenuMapName, ReasonKey);
	if (!Url.IsEmpty() && FPackageName::DoesPackageExist(MenuMapName))
	{
		Player->ClientTravel(Url, TRAVEL_Absolute);
		return;
	}
	Player->ClientReturnToMainMenuWithTextReason(FText::FromString(ACrossguardMenuGameMode::ReasonMessage(ReasonKey)));
}

APlayerController* ACrossguardGameMode::Login(UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal, const FString& Options,
	const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	TGuardValue<bool> LocalLogin(bLoggingInLocalPlayer, Cast<ULocalPlayer>(NewPlayer) != nullptr);
	return Super::Login(NewPlayer, InRemoteRole, Portal, Options, UniqueId, ErrorMessage);
}

FString ACrossguardGameMode::DevPlayerIdFor(const FString& Options) const
{
#if UE_BUILD_SHIPPING
	return FString();
#else
	// Development only: saves and reconnects need an id that outlives the session, and without Steam
	// there is none (docs/persistence.md, "Player id").
	FString Name;
	// 1. Asked for in the login URL: the client's word, so only where the server allows it.
	if (FParse::Param(FCommandLine::Get(), TEXT("AllowDevPlayerIds")) || GIsAutomationTesting)
	{
		Name = UGameplayStatics::ParseOption(Options, DevPlayerIdOption);
	}
	// 2. This process's own player: -CrossguardPlayerId=<name>, else in PIE the PIE instance, so the
	// same PIE window gets the same save every run.
	if (Name.IsEmpty() && bLoggingInLocalPlayer)
	{
		FParse::Value(FCommandLine::Get(), TEXT("CrossguardPlayerId="), Name);
		if (Name.IsEmpty())
		{
			const FWorldContext* Context = GEngine != nullptr ? GEngine->GetWorldContextFromWorld(GetWorld()) : nullptr;
			return Context != nullptr && Context->WorldType == EWorldType::PIE ? FString::Printf(TEXT("pie-%d"), Context->PIEInstance) : FString();
		}
	}
	// Letters, digits, '-' and '_' (it names the save file), 32 at most.
	FString Safe;
	for (const TCHAR Char : Name.Left(32))
	{
		if (FChar::IsAlnum(Char) || Char == TEXT('-') || Char == TEXT('_'))
		{
			Safe.AppendChar(Char);
		}
	}
	return Safe.IsEmpty() ? FString() : TEXT("dev-") + Safe;
#endif
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
	// The player id this login claims, settled before Super gives the player state its unique id
	// (which starts loading the save under it).
	ACrossguardPlayerState* NewState = NewPlayerController != nullptr ? NewPlayerController->GetPlayerState<ACrossguardPlayerState>() : nullptr;
	const bool bOnlineId = UCrossguardGuildSubsystem::IsOnlineId(UniqueId);
	const FString DevId = bOnlineId ? FString() : DevPlayerIdFor(Options);
	const FString LoginId = bOnlineId ? UniqueId.ToString() : DevId;
	if (!LoginId.IsEmpty())
	{
		if (bIsRaid && FinishedPlayerIds.Contains(LoginId))
		{
			// Extracted or dead for this raid (also while away): back to the menu / hub path.
			return TEXT("Your raid is over.");
		}
		if (FindOnlinePlayer(LoginId, NewState) != nullptr)
		{
			// Also a reconnect before the server has noticed the old connection drop (it times out).
			return TEXT("You are already in this match.");
		}
	}
	if (NewState != nullptr)
	{
		NewState->DevPlayerId = DevId;
	}
	if (bIsRaid && !LoginId.IsEmpty() && HeldPlayers.Contains(LoginId))
	{
		return RestoreHeldPlayer(NewPlayerController, LoginId, UniqueId, Options, Portal);
	}

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
			Party = Parties->PartyKeyOf(UCrossguardGuildSubsystem::IsOnlineId(UniqueId) ? UniqueId.ToString()
																						 : UCrossguardGuildSubsystem::PlayerIdOf(NewPlayerController->PlayerState));
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
	ACrossguardPlayerState* LeavingState = Exiting != nullptr ? Exiting->GetPlayerState<ACrossguardPlayerState>() : nullptr;
	// Held for a reconnect (HoldForReconnect): still in the raid. No last save and nothing forgotten:
	// the stored record stays the raid start's (no carried gear) until the raid ends for them.
	const bool bHeld = LeavingState != nullptr && !HeldIdOf(LeavingState).IsEmpty();

	// A last save while the player state is still here.
	UCrossguardSaveSubsystem* Saves = UCrossguardSaveSubsystem::Get(this);
	if (Saves != nullptr && LeavingState != nullptr && !bHeld)
	{
		Saves->PlayerLeaving(LeavingState);
	}
	// Extracted or dead for the raid: they cannot log in to it again.
	if (bIsRaid && LeavingState != nullptr && LeavingState->HasFinishedRaid())
	{
		FinishedPlayerIds.Add(UCrossguardGuildSubsystem::PlayerIdOf(LeavingState));
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
	if (PlayerKeys.RemoveAndCopyValue(TWeakObjectPtr<const AController>(Exiting), Key) && !bHeld)
	{
		Teams.Leave(std::string(TCHAR_TO_UTF8(*Key)));
	}
	if (bHeld)
	{
		// Nobody owns it while the player is away (the controller is going).
		LeavingState->SetOwner(nullptr);
	}
	Super::Logout(Exiting);
	// Whoever was watching the leaving player needs someone else.
	UpdateSpectators();
}

void ACrossguardGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// Reconnected: their own character, not a new pawn (RestartPlayer), save or raid.
	TWeakObjectPtr<ACrossguardCharacter> Waiting;
	if (NewPlayer != nullptr && Reconnecting.RemoveAndCopyValue(TWeakObjectPtr<APlayerController>(NewPlayer), Waiting))
	{
		ResumeHeldCharacter(NewPlayer, Waiting.Get());
		return;
	}

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
	if (PlayerState != nullptr && Player == nullptr && bIsRaid)
	{
		// Died while its player was away.
		if (const FString HeldId = HeldIdOf(PlayerState); !HeldId.IsEmpty())
		{
			ReleaseHeldPlayer(HeldId);
			return;
		}
	}
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

// --- Reconnecting ----------------------------------------------------------------------------------

bool ACrossguardGameMode::HoldForReconnect(APlayerController* Leaving)
{
	const UWorld* World = GetWorld();
	ACrossguardPlayerState* PlayerState = Leaving != nullptr ? Leaving->GetPlayerState<ACrossguardPlayerState>() : nullptr;
	ACrossguardCharacter* Character = Leaving != nullptr ? Cast<ACrossguardCharacter>(Leaving->GetPawn()) : nullptr;
	// Not when the map is closing, nor for a player whose raid here is over (dead for the raid,
	// extracted): their pawn goes as usual.
	if (!bIsRaid || World == nullptr || World->bIsTearingDown || PlayerState == nullptr || Character == nullptr || !PlayerState->IsInRaid()
		|| PlayerState->HasFinishedRaid() || Character->HasLeftRaid())
	{
		return false;
	}
	const FString PlayerId = UCrossguardGuildSubsystem::PlayerIdOf(PlayerState);
	if (PlayerId.IsEmpty() || HeldPlayers.Contains(PlayerId))
	{
		return false;
	}
	FCrossguardHeldPlayer& Held = HeldPlayers.Add(PlayerId);
	Held.PlayerState = PlayerState;
	Held.Character = Character;
	Held.TeamKey = PlayerKeys.FindRef(TWeakObjectPtr<const AController>(Leaving));

	// The character stays where it is, unpossessed: it can be hit, killed and looted. It stays this
	// player's (unpossessing clears the link, so it is set again): their team, a player kill for whoever
	// kills it, their save's watcher, and HandlePermanentDeath finds them.
	Leaving->UnPossess();
	Character->SetPlayerState(PlayerState);
	// Without a controller the movement component would freeze it (no falling, no knockback).
	if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
	{
		Movement->bRunPhysicsWithNoController = true;
	}
	// Teammates see "(disconnected)" (party panel, HUD team list).
	PlayerState->SetDisconnected(true);
	UE_LOG(LogCrossguard, Log, TEXT("%s (%s) disconnected mid-raid: character held for a reconnect"), *Character->GetName(), *PlayerId);
	return true;
}

ACrossguardPlayerState* ACrossguardGameMode::FindHeldPlayer(const FString& PlayerId) const
{
	const FCrossguardHeldPlayer* Held = HeldPlayers.Find(PlayerId);
	return Held != nullptr ? Held->PlayerState.Get() : nullptr;
}

FString ACrossguardGameMode::HeldIdOf(const ACrossguardPlayerState* PlayerState) const
{
	for (const TPair<FString, FCrossguardHeldPlayer>& Pair : HeldPlayers)
	{
		if (PlayerState != nullptr && Pair.Value.PlayerState.Get() == PlayerState)
		{
			return Pair.Key;
		}
	}
	return FString();
}

const ACrossguardPlayerState* ACrossguardGameMode::FindOnlinePlayer(const FString& PlayerId, const APlayerState* Except) const
{
	if (GameState == nullptr)
	{
		return nullptr;
	}
	for (const APlayerState* Other : GameState->PlayerArray)
	{
		const ACrossguardPlayerState* OtherState = Cast<ACrossguardPlayerState>(Other);
		if (OtherState != nullptr && OtherState != Except && UCrossguardGuildSubsystem::PlayerIdOf(OtherState) == PlayerId
			&& HeldIdOf(OtherState).IsEmpty())
		{
			return OtherState;
		}
	}
	return nullptr;
}

FString ACrossguardGameMode::RestoreHeldPlayer(APlayerController* Controller, const FString& PlayerId, const FUniqueNetIdRepl& UniqueId,
	const FString& Options, const FString& Portal)
{
	FCrossguardHeldPlayer Held;
	HeldPlayers.RemoveAndCopyValue(PlayerId, Held);
	ACrossguardPlayerState* HeldState = Held.PlayerState.Get();
	ACrossguardCharacter* HeldCharacter = Held.Character.Get();
	if (HeldState == nullptr || HeldCharacter == nullptr || Controller == nullptr)
	{
		// Nothing left to take back (should not happen: a death releases the player).
		return TEXT("Your raid is over.");
	}

	// The engine's own way (AGameMode::FindInactivePlayer): the held player state replaces the fresh one.
	// Matched by player id only (an online id, or a development id), never by name.
	APlayerState* Fresh = Controller->PlayerState;
	Controller->PlayerState = HeldState;
	HeldState->SetOwner(Controller);
	if (Fresh != nullptr && Fresh != HeldState)
	{
		Fresh->Destroy();
	}
	// Registers the player, sets the unique id again (the save subsystem already has this player state)
	// and the name.
	const FString Error = Super::InitNewPlayer(Controller, UniqueId, Options, Portal);
	if (!Error.IsEmpty())
	{
		// Refused after all: held again; the controller leaves without a player state.
		Controller->PlayerState = nullptr;
		HeldState->SetOwner(nullptr);
		HeldPlayers.Add(PlayerId, Held);
		return Error;
	}
	// Back: no longer shown as disconnected (SetDisconnected sends the player state at once).
	HeldState->SetDisconnected(false);
	HeldState->ForceNetUpdate();
	// Same team slot (the registry key is just a string).
	PlayerKeys.Add(TWeakObjectPtr<const AController>(Controller), Held.TeamKey);
	Reconnecting.Add(TWeakObjectPtr<APlayerController>(Controller), HeldCharacter);
	UE_LOG(LogCrossguard, Log, TEXT("%s reconnected to the raid"), *PlayerId);
	return FString();
}

void ACrossguardGameMode::ResumeHeldCharacter(APlayerController* Player, ACrossguardCharacter* Character)
{
	ACrossguardPlayerState* PlayerState = Player != nullptr ? Player->GetPlayerState<ACrossguardPlayerState>() : nullptr;
	if (PlayerState != nullptr)
	{
		// Connected again either way (RestoreHeldPlayer cleared it already; this covers any other path).
		PlayerState->SetDisconnected(false);
	}
	if (PlayerState == nullptr || Character == nullptr || Character->GetPlayerState() != PlayerState || !PlayerState->IsInRaid())
	{
		// The character is gone meanwhile: nothing to take back; they watch, like after a death.
		if (Player != nullptr)
		{
			Spectators.AddUnique(Player);
			UpdateSpectators();
		}
		return;
	}
	// Possession sets the attributes up again with full Health: the character keeps what it had (no
	// healing by reconnecting; a downed character stays at 0).
	UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent();
	const float Health = ASC != nullptr ? ASC->GetNumericAttribute(UCrossguardAttributeSet::GetHealthAttribute()) : 0.0f;
	Player->Possess(Character);
	if (ASC != nullptr)
	{
		ASC->SetNumericAttributeBase(UCrossguardAttributeSet::GetHealthAttribute(), Health);
	}
	Player->SetControlRotation(Character->GetActorRotation());
	Player->ClientSetRotation(Character->GetActorRotation());
}

void ACrossguardGameMode::ReleaseHeldPlayer(const FString& PlayerId)
{
	FCrossguardHeldPlayer Held;
	if (!HeldPlayers.RemoveAndCopyValue(PlayerId, Held))
	{
		return;
	}
	ACrossguardPlayerState* PlayerState = Held.PlayerState.Get();
	ACrossguardCharacter* Character = Held.Character.Get();
	// The body stays, lootable, but is no longer theirs: the failure's save takes no gear from it (it
	// keeps the raid start's record: none).
	if (Character != nullptr && Character->GetPlayerState() == PlayerState)
	{
		Character->SetPlayerState(nullptr);
	}
	FinishedPlayerIds.Add(PlayerId);
	if (!Held.TeamKey.IsEmpty())
	{
		Teams.Leave(std::string(TCHAR_TO_UTF8(*Held.TeamKey)));
	}
	if (PlayerState != nullptr)
	{
		// Gone for good (the player state is destroyed below): not "away" any more.
		PlayerState->SetDisconnected(false);
		PlayerState->EndRaid(false);
		if (UCrossguardSaveSubsystem* Saves = UCrossguardSaveSubsystem::Get(this))
		{
			Saves->PlayerLeaving(PlayerState);
		}
		PlayerState->Destroy();
	}
	UE_LOG(LogCrossguard, Log, TEXT("%s died for the raid while away: raid failed"), *PlayerId);
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
		ReturnToMenu(Player, ACrossguardMenuGameMode::RaidOverReason);
	}
}
