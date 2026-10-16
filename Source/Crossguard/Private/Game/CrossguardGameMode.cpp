#include "Game/CrossguardGameMode.h"

#include "Character/CrossguardCharacter.h"
#include "Character/CrossguardMinion.h"
#include "AbilitySystem/CrossguardAttributeSet.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Crossguard.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Game/CrossguardDevMode.h"
#include "Game/CrossguardGuildSubsystem.h"
#include "Game/CrossguardHubMarker.h"
#include "Game/CrossguardMatchmaking.h"
#include "Game/CrossguardMenuGameMode.h"
#include "Game/CrossguardNakamaSubsystem.h"
#include "Game/CrossguardPartySubsystem.h"
#include "Game/CrossguardPlayerController.h"
#include "Game/CrossguardPlayerState.h"
#include "Game/CrossguardSaveSubsystem.h"
#include "Game/CrossguardTeamLibrary.h"
#include "Items/CrossguardEquipmentComponent.h"
#include "Items/CrossguardInventoryComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UI/CrossguardHUD.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "TimerManager.h"

namespace CrossguardGameModeLocal
{
	// How often the countdown to the hub checks the time (the HUD line changes once a second).
	constexpr float HubReturnTickSeconds = 0.25f;
	const FLinearColor HubReturnColor(0.55f, 0.85f, 1.0f);
}

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
	// The engine's own cap (GameSession MaxPlayers, 16 by default) would refuse a 48-player raid.
	if (GameSession != nullptr && IsRunningDedicatedServer())
	{
		GameSession->MaxPlayers = GetServerCapacity();
	}
}

int32 ACrossguardGameMode::GetServerCapacity() const
{
	int32 Capacity = bIsRaid ? RaidCapacity : HubCapacity;
	FParse::Value(FCommandLine::Get(), bIsRaid ? TEXT("RaidCapacity=") : TEXT("HubCapacity="), Capacity);
	return FMath::Max(1, Capacity);
}

bool ACrossguardGameMode::RequiresMatchTicket() const
{
	const UCrossguardNakamaSubsystem* Nakama = UCrossguardNakamaSubsystem::Get(this);
	// DEV-ONLY (listen server): the whole-server launch brings no tickets.
	return bIsRaid && Nakama != nullptr && Nakama->IsBackendEnabled() && !Crossguard::IsDevListenMode(GetWorld());
}

void ACrossguardGameMode::RegisterWithBackend()
{
	UCrossguardNakamaSubsystem* Nakama = UCrossguardNakamaSubsystem::Get(this);
	UWorld* World = GetWorld();
	if (Nakama == nullptr || !Nakama->IsBackendEnabled() || World == nullptr || GetNetMode() != NM_DedicatedServer)
	{
		return;
	}
	TWeakObjectPtr<ACrossguardGameMode> WeakThis(this);
	Nakama->RegisterServer(bIsRaid ? TEXT("match") : TEXT("hub"), UCrossguardNakamaSubsystem::GetPublicAddress(World->URL.Port), GetServerCapacity(),
		[WeakThis]()
		{
			const ACrossguardGameMode* Self = WeakThis.Get();
			return Self != nullptr && Self->GameState != nullptr ? Self->GameState->PlayerArray.Num() : 0;
		});
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
	// Leaving: keeps no raid going and does not travel back to the hub with the server.
	SentToMenuIds.Add(UCrossguardGuildSubsystem::PlayerIdOf(Player->PlayerState));
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

FString ACrossguardGameMode::WithDevPlayerId(const FString& Url, const FString& DevPlayerId)
{
#if UE_BUILD_SHIPPING
	return Url;
#else
	if (Url.IsEmpty() || !DevPlayerId.StartsWith(TEXT("dev-")) || DevPlayerId.Len() <= 4)
	{
		return Url;
	}
	// Honoured where the next server allows it (-AllowDevPlayerIds).
	return FString::Printf(TEXT("%s?%s=%s"), *Url, DevPlayerIdOption, *DevPlayerId.RightChop(4));
#endif
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

void ACrossguardGameMode::PreLoginAsync(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId,
	const FOnPreLoginCompleteDelegate& OnComplete)
{
	UCrossguardNakamaSubsystem* Nakama = UCrossguardNakamaSubsystem::Get(this);
	const FString Token = UGameplayStatics::ParseOption(Options, UCrossguardNakamaSubsystem::TokenOption);
	if (Nakama == nullptr || !Nakama->IsServerConfigured() || Token.IsEmpty())
	{
		Super::PreLoginAsync(Options, Address, UniqueId, OnComplete);
		return;
	}
	// The HTTP timeout (UCrossguardNakamaSubsystem::RequestTimeoutSeconds) bounds the wait.
	TWeakObjectPtr<ACrossguardGameMode> WeakThis(this);
	Nakama->VerifySessionToken(Token, [WeakThis, Options, Address, UniqueId, OnComplete](bool bVerified, const FString& UserId, const FString& Error)
	{
		ACrossguardGameMode* Self = WeakThis.Get();
		if (!bVerified || Self == nullptr)
		{
			UE_LOG(LogCrossguard, Warning, TEXT("Login from %s refused: Nakama session not verified (%s)"), *Address, Self != nullptr ? *Error : TEXT("map changed"));
			OnComplete.ExecuteIfBound(TEXT("Your login could not be verified. Log in again."));
			return;
		}
		// A match server with the backend: the backend's ticket decides who comes in (and their team).
		const FString Ticket = UGameplayStatics::ParseOption(Options, UCrossguardMatchmakingSubsystem::MatchTicketOption);
		UCrossguardNakamaSubsystem* Link = UCrossguardNakamaSubsystem::Get(Self);
		if (Self->RequiresMatchTicket() && Link != nullptr)
		{
			if (Ticket.IsEmpty())
			{
				if (!Link->IsNakamaLoginRequired())
				{
					// DEV-ONLY: -AllowDevPlayerIds servers let a login without a ticket in (?Party= applies).
					Self->AGameModeBase::PreLoginAsync(Options, Address, UniqueId, OnComplete);
					return;
				}
				UE_LOG(LogCrossguard, Warning, TEXT("Login of %s to this match server refused: no match ticket"), *UserId);
				OnComplete.ExecuteIfBound(TEXT("No match ticket: start the raid from the hub."));
				return;
			}
			TWeakObjectPtr<ACrossguardGameMode> WeakSelf(Self);
			Link->VerifyMatchTicket(Ticket, UserId, [WeakSelf, Options, Address, UniqueId, OnComplete, UserId](bool bValid, const FString& /*PartyId*/, const FString& TicketError)
			{
				ACrossguardGameMode* Verified = WeakSelf.Get();
				if (!bValid || Verified == nullptr)
				{
					UE_LOG(LogCrossguard, Warning, TEXT("Login of %s to this match server refused: match ticket not valid (%s)"), *UserId,
						Verified != nullptr ? *TicketError : TEXT("map changed"));
					OnComplete.ExecuteIfBound(TEXT("Your match ticket is not valid for this raid (expired, or for another server)."));
					return;
				}
				Verified->AGameModeBase::PreLoginAsync(Options, Address, UniqueId, OnComplete);
			});
			return;
		}
		// Verified (kept for InitNewPlayer): the usual checks (PreLogin), then the engine goes on.
		Self->AGameModeBase::PreLoginAsync(Options, Address, UniqueId, OnComplete);
	});
}

FString ACrossguardGameMode::NakamaUserIdFor(const FString& Options, FString& OutError) const
{
	UCrossguardNakamaSubsystem* Nakama = UCrossguardNakamaSubsystem::Get(this);
	if (Nakama == nullptr)
	{
		return FString();
	}
	if (bLoggingInLocalPlayer)
	{
		// The server's own player: its own login, no verification needed.
		return Nakama->IsLoggedIn() ? Nakama->GetUserId() : FString();
	}
	const FString VerifiedId = Nakama->TakeVerifiedUserId(UGameplayStatics::ParseOption(Options, UCrossguardNakamaSubsystem::TokenOption));
	if (VerifiedId.IsEmpty() && Nakama->IsNakamaLoginRequired())
	{
		OutError = TEXT("Log in first: this server needs a verified account.");
	}
	return VerifiedId;
}

FString ACrossguardGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options,
	const FString& Portal)
{
	// The player id this login claims, settled before Super gives the player state its unique id
	// (which starts loading the save under it). A verified Nakama user id comes first.
	ACrossguardPlayerState* NewState = NewPlayerController != nullptr ? NewPlayerController->GetPlayerState<ACrossguardPlayerState>() : nullptr;
	FString NakamaError;
	const FString NakamaId = NakamaUserIdFor(Options, NakamaError);
	if (!NakamaError.IsEmpty())
	{
		return NakamaError;
	}
	const bool bOnlineId = UCrossguardGuildSubsystem::IsOnlineId(UniqueId);
	const FString DevId = (bOnlineId || !NakamaId.IsEmpty()) ? FString() : DevPlayerIdFor(Options);
	const FString LoginId = !NakamaId.IsEmpty() ? NakamaId : bOnlineId ? UniqueId.ToString() : DevId;
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
		NewState->NakamaUserId = NakamaId;
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
	FString TicketParty;
	UCrossguardNakamaSubsystem* TicketCheck = UCrossguardNakamaSubsystem::Get(this);
	if (RequiresMatchTicket() && TicketCheck != nullptr && !NakamaId.IsEmpty()
		&& TicketCheck->TakeVerifiedTicket(UGameplayStatics::ParseOption(Options, UCrossguardMatchmakingSubsystem::MatchTicketOption), NakamaId, TicketParty))
	{
		// The backend's ticket says the team; the client's ?Party= is not trusted here.
		Party = TicketParty;
	}
	else if (Party.IsEmpty())
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
	// A team arriving from a match server (?Party=<key>) gets the key's team at once; with the backend the
	// party itself comes back from Nakama when the player state gets its id (UCrossguardPartySubsystem::
	// PlayerArrived; docs/parties.md "Back from the raid").
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
	// The leaving player state is still listed until the controller is gone: count the raid next tick.
	if (bIsRaid && GetWorld() != nullptr && !GetWorld()->bIsTearingDown)
	{
		GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			UpdateHubReturn();
		}));
	}
}

void ACrossguardGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// Reconnected: their own character, not a new pawn (RestartPlayer), save or raid.
	TWeakObjectPtr<ACrossguardCharacter> Waiting;
	if (NewPlayer != nullptr && Reconnecting.RemoveAndCopyValue(TWeakObjectPtr<APlayerController>(NewPlayer), Waiting))
	{
		ResumeHeldCharacter(NewPlayer, Waiting.Get());
		UpdateHubReturn();
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
		// A raider joining during the countdown keeps the raid going.
		UpdateHubReturn();
	}
}

bool ACrossguardGameMode::PlayerCanRestart_Implementation(APlayerController* Player)
{
	const ACrossguardPlayerState* PlayerState = Player != nullptr ? Player->GetPlayerState<ACrossguardPlayerState>() : nullptr;
	// Back in the hub (bIsRaid off) everyone spawns: the hub's player states are new (seamless travel
	// copies no HasFinishedRaid, CopyProperties) and the hub never checks it.
	if (bIsRaid && PlayerState != nullptr && PlayerState->HasFinishedRaid())
	{
		return false;
	}
	return Super::PlayerCanRestart_Implementation(Player);
}

bool ACrossguardGameMode::GiveStarterKit(ACrossguardCharacter* Character) const
{
	UCrossguardEquipmentComponent* Equipment = Character != nullptr ? Character->GetEquipmentComponent() : nullptr;
	UCrossguardInventoryComponent* Inventory = Character != nullptr ? Character->GetInventoryComponent() : nullptr;
	if (Equipment == nullptr || Inventory == nullptr)
	{
		return false;
	}
	// Main hand first: a two-handed main hand then rejects the off hand, as in play.
	for (const TPair<ECrossguardEquipmentSlot, FName>& Weapon :
		{ TPair<ECrossguardEquipmentSlot, FName>(ECrossguardEquipmentSlot::MainHand, StarterMainHand),
			TPair<ECrossguardEquipmentSlot, FName>(ECrossguardEquipmentSlot::OffHand, StarterOffHand) })
	{
		if (Weapon.Value.IsNone() || !Equipment->GetItem(Weapon.Key).IsEmpty())
		{
			continue;
		}
		FCrossguardWeaponItem Item;
		Item.WeaponId = Weapon.Value;
		Item.Rarity = ECrossguardRarity::Common;
		const ECrossguardEquipError Error = Equipment->Equip(Weapon.Key, Item);
		if (Error != ECrossguardEquipError::None)
		{
			UE_LOG(LogCrossguard, Warning, TEXT("Starter kit: %s not equipped (%s)"), *Weapon.Value.ToString(),
				*StaticEnum<ECrossguardEquipError>()->GetNameStringByValue(static_cast<int64>(Error)));
		}
	}
	for (const TPair<FName, int32>& Starter : StarterItems)
	{
		const FCrossguardItemRow* Row = Inventory->ItemTable != nullptr
			? Inventory->ItemTable->FindRow<FCrossguardItemRow>(Starter.Key, TEXT("GiveStarterKit"), false)
			: nullptr;
		if (Row == nullptr || Starter.Value <= 0)
		{
			UE_LOG(LogCrossguard, Warning, TEXT("Starter kit: %s is not an item of the Item Table"), *Starter.Key.ToString());
			continue;
		}
		FCrossguardInventoryItem Item;
		Item.Category = Row->Category;
		Item.ItemId = Starter.Key;
		Item.Rarity = ECrossguardRarity::Common;
		Item.Count = Starter.Value;
		if (const int32 Left = Inventory->AddItem(Item); Left > 0)
		{
			UE_LOG(LogCrossguard, Warning, TEXT("Starter kit: %d of %s did not fit"), Left, *Starter.Key.ToString());
		}
	}
	// Consumables and throwables on the first free hotbar keys (3-7).
	int32 HotbarSlot = 0;
	for (const FCrossguardInventoryItem& Item : Inventory->GetItems())
	{
		const bool bHotbarItem = Item.Category == ECrossguardItemCategory::Consumable || Item.Category == ECrossguardItemCategory::Throwable;
		const bool bAssigned = Inventory->GetHotbar().ContainsByPredicate([&Item](const FCrossguardHotbarSlot& Slot)
		{
			return Slot.ItemId == Item.ItemId && Slot.Rarity == Item.Rarity;
		});
		while (HotbarSlot < Inventory->GetHotbar().Num() && !Inventory->GetHotbar()[HotbarSlot].ItemId.IsNone())
		{
			++HotbarSlot;
		}
		if (bHotbarItem && !bAssigned && HotbarSlot < Inventory->GetHotbar().Num())
		{
			Inventory->RequestAssignHotbarSlot(HotbarSlot++, Item.Id);
		}
	}
	UE_LOG(LogCrossguard, Log, TEXT("%s: a new character, starter kit given"), *Character->GetName());
	return true;
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
	// in the raid (ruled; docs/design-decisions.md, Extraction) until the server goes back to the hub.
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
	UpdateHubReturn();
}

void ACrossguardGameMode::StartPlay()
{
	Super::StartPlay();
	RegisterWithBackend();
	RaidStartSeconds = GetWorld()->GetTimeSeconds();
	// The raid's time limit; the development listen server has none (DEV-ONLY (listen server)).
	if (bIsRaid && MaxRaidDurationSeconds > 0.0f && !Crossguard::IsDevListenMode(GetWorld()))
	{
		GetWorldTimerManager().SetTimer(MatchTimeLimitTimer, this, &ACrossguardGameMode::CloseRaidForTime, MaxRaidDurationSeconds, false);
	}
}

// --- Match lifecycle -------------------------------------------------------------------------------

void ACrossguardGameMode::CloseRaidForTime()
{
	UWorld* World = GetWorld();
	if (!bIsRaid || bMatchOver || bHubReturnStarted || World == nullptr || GameState == nullptr)
	{
		return;
	}
	UE_LOG(LogCrossguard, Log, TEXT("The raid's time is up (%.0f s): it closes"), MaxRaidDurationSeconds);
	// Raiders away fail it as at a team's return; collected first (releasing changes the map).
	TArray<FString> AwayIds;
	HeldPlayers.GetKeys(AwayIds);
	for (const FString& AwayId : AwayIds)
	{
		ReleaseHeldPlayer(AwayId, true);
	}
	// Assumed: raiders still in it fail it as a death for the raid; their teams then go back.
	TArray<ACrossguardCharacter*> StillIn;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* Player = It->Get();
		const ACrossguardPlayerState* PlayerState = Player != nullptr ? Player->GetPlayerState<ACrossguardPlayerState>() : nullptr;
		if (ACrossguardCharacter* Character = Player != nullptr ? Cast<ACrossguardCharacter>(Player->GetPawn()) : nullptr;
			Character != nullptr && PlayerState != nullptr && PlayerState->IsInRaid())
		{
			StillIn.Add(Character);
		}
	}
	for (ACrossguardCharacter* Character : StillIn)
	{
		if (ACrossguardPlayerController* Raider = Character->GetController<ACrossguardPlayerController>())
		{
			Raider->ClientShowMessage(TEXT("The raid has closed."), CrossguardGameModeLocal::HubReturnColor);
		}
		HandlePermanentDeath(Character);
	}
	UpdateHubReturn();
	CheckMatchOver();
}

void ACrossguardGameMode::CheckMatchOver()
{
	if (!bIsRaid || bMatchOver || bHubReturnStarted || GameState == nullptr)
	{
		return;
	}
	// DEV-ONLY (listen server): the development server travels back as a whole instead.
	if (Crossguard::IsDevListenMode(GetWorld()))
	{
		return;
	}
	// Nobody may still be playing here: in the raid, away, or done but waiting for their team.
	for (const APlayerState* Listed : GameState->PlayerArray)
	{
		const ACrossguardPlayerState* PlayerState = Cast<ACrossguardPlayerState>(Listed);
		if (PlayerState == nullptr || PlayerState->IsActorBeingDestroyed())
		{
			continue;
		}
		const FString PlayerId = UCrossguardGuildSubsystem::PlayerIdOf(PlayerState);
		if (!LeavingForHubIds.Contains(PlayerId) && !SentToMenuIds.Contains(PlayerId))
		{
			return;
		}
	}
	// And somebody played (an empty server waiting for its first team is not over).
	if (LeavingForHubIds.IsEmpty() && SentToMenuIds.IsEmpty() && FinishedPlayerIds.IsEmpty())
	{
		return;
	}
	bMatchOver = true;
	GetWorldTimerManager().ClearTimer(MatchTimeLimitTimer);
	UE_LOG(LogCrossguard, Log, TEXT("Match over: every team has left"));
	OnMatchOver.Broadcast();
	// The backend may hand this server to the next match (a server that exits deregisters instead).
	if (UCrossguardNakamaSubsystem* Nakama = UCrossguardNakamaSubsystem::Get(this))
	{
		Nakama->ReportMatchOver();
	}
	if (ShouldExitWhenMatchOver())
	{
		UE_LOG(LogCrossguard, Log, TEXT("Match server exits in %.0f s (one match per process)"), MatchOverExitDelaySeconds);
		GetWorldTimerManager().SetTimer(MatchOverExitTimer, this, &ACrossguardGameMode::ExitForMatchOver, FMath::Max(0.1f, MatchOverExitDelaySeconds), false);
	}
}

bool ACrossguardGameMode::ShouldExitWhenMatchOver() const
{
	return GetNetMode() == NM_DedicatedServer && !GIsEditor && !GIsAutomationTesting && !Crossguard::IsDevListenMode(GetWorld())
		&& !FParse::Param(FCommandLine::Get(), TEXT("KeepMatchServer"));
}

void ACrossguardGameMode::ExitForMatchOver()
{
	UE_LOG(LogCrossguard, Log, TEXT("Match over: the match server exits"));
	FPlatformMisc::RequestExit(false);
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
	// Watched again once no teammate is left to watch (UpdateSpectators).
	DeadBodies.Add(TWeakObjectPtr<APlayerController>(Player), TWeakObjectPtr<APawn>(Character));
	Spectators.AddUnique(Player);
	UpdateSpectators();
	UpdateHubReturn();
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
	// Leaving with their team (ReturnTeamToHub): a deliberate travel, not a disconnect.
	if (PlayerId.IsEmpty() || HeldPlayers.Contains(PlayerId) || LeavingForHubIds.Contains(PlayerId))
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

void ACrossguardGameMode::ReleaseHeldPlayer(const FString& PlayerId, bool bRaidOver)
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
	if (bRaidOver)
	{
		UE_LOG(LogCrossguard, Log, TEXT("%s was still away when the raid ended: raid failed"), *PlayerId);
	}
	else
	{
		UE_LOG(LogCrossguard, Log, TEXT("%s died for the raid while away: raid failed"), *PlayerId);
	}
	UpdateSpectators();
	UpdateHubReturn();
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
			// Extracted: the raid went well; they stay where they left until the server goes back to
			// the hub.
			continue;
		}
		if (bHubReturnStarted || HasTripBack())
		{
			// Ruled (owner): dead for the raid with nobody left to watch, they stay where they are
			// (watching their body) and leave with their team (UpdateHubReturn; development: the whole
			// server's return). Not to the menu on their own.
			APawn* Body = DeadBodies.FindRef(TWeakObjectPtr<APlayerController>(Player)).Get();
			const APlayerCameraManager* Camera = Player->PlayerCameraManager;
			if (Body != nullptr && Player->GetViewTarget() != Body && (Camera == nullptr || Camera->PendingViewTarget.Target.Get() != Body))
			{
				Player->SetViewTargetWithBlend(Body, 0.5f);
			}
			continue;
		}
		// Development mode with no hub to go back to: the main menu.
		if (Player->IsLocalController() && GetNetMode() == NM_ListenServer)
		{
			// DEV-ONLY (listen server): sending the listen-server host to the menu would end the match for everyone; it keeps
			// watching its body instead.
			continue;
		}
		ReturnToMenu(Player, ACrossguardMenuGameMode::RaidOverReason);
	}
}

// --- Back to the hub -------------------------------------------------------------------------------

FString ACrossguardGameMode::GetHubMapName() const
{
	FString FromCommandLine;
	return FParse::Value(FCommandLine::Get(), TEXT("HubMap="), FromCommandLine) ? FromCommandLine : HubMapName;
}

FString ACrossguardGameMode::BuildHubReturnUrl(const FString& HubMap, bool bListen)
{
	if (HubMap.IsEmpty())
	{
		return FString();
	}
	// ?Hub as well as the map's hub marker: the hub game mode turns bIsRaid off either way.
	return bListen ? ACrossguardMenuGameMode::BuildHostUrl(HubMap) : FString::Printf(TEXT("%s?%s"), *HubMap, HubOption);
}

FCrossguardHubDestination ACrossguardGameMode::GetHubDestination(const APlayerController* Player, const FString& BackendHubAddress) const
{
	FCrossguardHubDestination Destination;
	// 1. The backend's pick for the team (ReturnTeamToHub asks it: ICrossguardMatchmaking::FindHubServer).
	Destination.Address = BackendHubAddress;
	Destination.Source = TEXT("backend");
	// 2. The hub server this match server is configured with.
	if (Destination.Address.IsEmpty())
	{
		Destination.Address = GetHubServerAddress();
		Destination.Source = TEXT("-HubServer");
	}
	if (!Destination.Address.IsEmpty())
	{
		Destination.Kind = ECrossguardHubDestination::HubServer;
		return Destination;
	}
	// 3. DEV-ONLY (listen server): this match server is the development listen server; it travels back
	// to the hub map as a whole.
	if (Crossguard::IsDevListenMode(GetWorld()) && !GetHubMapName().IsEmpty())
	{
		Destination.Kind = ECrossguardHubDestination::DevServerTravel;
		Destination.Address = GetHubMapName();
		Destination.Source = TEXT("dev listen server");
		return Destination;
	}
	// 4. Nowhere: the main menu (a dedicated or Shipping server with no hub server known).
	Destination.Kind = ECrossguardHubDestination::Menu;
	Destination.Address.Reset();
	Destination.Source = TEXT("none");
	return Destination;
}

bool ACrossguardGameMode::HasTripBack() const
{
	// DEV-ONLY (listen server): development without a hub map or hub server keeps players where they
	// are. Elsewhere a team with nowhere to go is sent to the menu with a message.
	return !(Crossguard::IsDevListenMode(GetWorld()) && GetHubDestination(nullptr).Kind == ECrossguardHubDestination::Menu);
}

float ACrossguardGameMode::GetHubReturnSecondsLeft() const
{
	const UWorld* World = GetWorld();
	return bHubReturnPending && World != nullptr ? static_cast<float>(FMath::Max(0.0, HubReturnAt - World->GetTimeSeconds())) : 0.0f;
}

void ACrossguardGameMode::UpdateHubReturn()
{
	UWorld* World = GetWorld();
	if (!bIsRaid || bHubReturnStarted || World == nullptr || World->bIsTearingDown || GameState == nullptr || !HasTripBack())
	{
		return;
	}
	// A dedicated match server whose last players left is done (one match per process).
	CheckMatchOver();
	// DEV-ONLY (listen server): the whole server goes back together once nobody raids.
	const bool bWholeServerReturn = GetHubDestination(nullptr).Kind == ECrossguardHubDestination::DevServerTravel;
	// Who is still in the raid, server-wide and per team: connected raiders (alive or downed, also a
	// reconnecting one) and raiders away (held). And whether anyone connected is left to bring back.
	// Players leaving with their team (ReturnTeamToHub) count for nothing here.
	struct FRaidCount
	{
		int32 Live = 0;
		int32 Away = 0;
		int32 Connected = 0;
		bool bHasServerPlayer = false;
	};
	FRaidCount All;
	TMap<int32, FRaidCount> ByTeam;
	for (const APlayerState* Listed : GameState->PlayerArray)
	{
		const ACrossguardPlayerState* PlayerState = Cast<ACrossguardPlayerState>(Listed);
		if (PlayerState == nullptr || PlayerState->IsActorBeingDestroyed())
		{
			continue;
		}
		const FString PlayerId = UCrossguardGuildSubsystem::PlayerIdOf(PlayerState);
		if (LeavingForHubIds.Contains(PlayerId))
		{
			continue;
		}
		const int32 TeamId = PlayerState->GetTeamId();
		// Unassigned (should not happen): counted server-wide only.
		FRaidCount Unused;
		FRaidCount& Team = TeamId >= 0 ? ByTeam.FindOrAdd(TeamId) : Unused;
		const bool bAway = !HeldIdOf(PlayerState).IsEmpty();
		if (!bAway && PlayerState->GetOwner() != nullptr && !SentToMenuIds.Contains(PlayerId))
		{
			++All.Connected;
			++Team.Connected;
			Team.bHasServerPlayer |= IsServerOwnPlayer(Cast<APlayerController>(PlayerState->GetOwner()));
		}
		if (PlayerState->IsInRaid() && bAway)
		{
			++All.Away;
			++Team.Away;
		}
		else if (PlayerState->IsInRaid())
		{
			++All.Live;
			++Team.Live;
		}
	}

	if (All.Live > 0 || All.Connected == 0 || !bWholeServerReturn)
	{
		// Someone is still raiding (a reconnect, a late joiner), or nobody is here to bring back (a
		// lone raider away waits for their reconnect as before).
		if (bHubReturnPending)
		{
			bHubReturnPending = false;
			GetWorldTimerManager().ClearTimer(HubReturnTimer);
			UE_LOG(LogCrossguard, Log, TEXT("Return to the hub called off: %d player(s) in the raid, %d here"), All.Live, All.Connected);
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				ACrossguardPlayerController* Player = Cast<ACrossguardPlayerController>(It->Get());
				if (Player != nullptr && !LeavingForHubIds.Contains(UCrossguardGuildSubsystem::PlayerIdOf(Player->PlayerState)))
				{
					Player->ClientShowMessage(TEXT("Return to the hub called off: a raider is back."), CrossguardGameModeLocal::HubReturnColor);
				}
			}
		}
	}
	else
	{
		// DEV-ONLY (listen server): nobody left in the raid: the whole server goes back together, teams
		// counting down included.
		TArray<int32> Counting;
		TeamHubReturns.GetKeys(Counting);
		for (const int32 TeamId : Counting)
		{
			CancelTeamHubReturn(TeamId, false);
		}

		// The short countdown. Only raiders away: their grace (placeholders).
		const bool bWaitsForHeld = All.Away > 0;
		const double Delay = FMath::Max(0.0f, bWaitsForHeld ? HeldPlayerReturnGraceSeconds : HubReturnCountdownSeconds);
		const double At = World->GetTimeSeconds() + Delay;
		if (bHubReturnPending)
		{
			// Already counting: a raider away who died meanwhile can only bring the travel closer.
			HubReturnAt = FMath::Min(HubReturnAt, At);
			bHubReturnWaitsForHeld = bWaitsForHeld;
			return;
		}
		bHubReturnPending = true;
		bHubReturnWaitsForHeld = bWaitsForHeld;
		HubReturnAt = At;
		HubReturnShownSeconds = -1;
		UE_LOG(LogCrossguard, Log, TEXT("Raid over: returning to the hub in %.0f s (%d disconnected raider(s) still away)"), Delay, All.Away);
		GetWorldTimerManager().SetTimer(HubReturnTimer, this, &ACrossguardGameMode::HubReturnTick, CrossguardGameModeLocal::HubReturnTickSeconds, true);
		HubReturnTick();
		return;
	}

	// Each team that has finished goes back on its own while others raid (ruled, owner). On a dedicated
	// match server this is the only way back.
	TArray<int32> Counting;
	TeamHubReturns.GetKeys(Counting);
	for (const int32 TeamId : Counting)
	{
		const FRaidCount* Team = ByTeam.Find(TeamId);
		if (Team == nullptr || Team->Connected == 0)
		{
			// Nobody of the team is here any more.
			CancelTeamHubReturn(TeamId, false);
		}
		else if (Team->Live > 0)
		{
			// A teammate reconnected.
			CancelTeamHubReturn(TeamId, true);
		}
		else if (Team->bHasServerPlayer && All.Live > 0)
		{
			// DEV-ONLY (listen server): the host's team was the last; another raider is back.
			CancelTeamHubReturn(TeamId, true);
		}
	}
	for (const TPair<int32, FRaidCount>& Pair : ByTeam)
	{
		const int32 TeamId = Pair.Key;
		const FRaidCount& Team = Pair.Value;
		if (Team.Live > 0 || Team.Connected == 0)
		{
			// Still raiding, or nobody to bring back (a lone teammate away waits for their reconnect).
			TeamsWaitingForServer.Remove(TeamId);
			continue;
		}
		if (bWholeServerReturn || (Team.bHasServerPlayer && All.Live > 0))
		{
			// DEV-ONLY (listen server): the development whole-server return takes everyone together, and
			// the server's own player cannot leave while others raid (Assumed: that would close the raid).
			if (!TeamsWaitingForServer.Contains(TeamId))
			{
				TeamsWaitingForServer.Add(TeamId);
				UE_LOG(LogCrossguard, Log, TEXT("Team %d finished: waiting for the other teams (development listen server)"), TeamId);
				TellTeam(TeamId, TEXT("Your team is done: back to the hub when every team is done (development server)."));
			}
			continue;
		}
		const bool bWaitsForHeld = Team.Away > 0;
		const double Delay = FMath::Max(0.0f, bWaitsForHeld ? HeldPlayerReturnGraceSeconds : HubReturnCountdownSeconds);
		const double At = World->GetTimeSeconds() + Delay;
		if (FCrossguardTeamHubCountdown* Pending = TeamHubReturns.Find(TeamId))
		{
			// Already counting: a teammate away who died meanwhile can only bring it closer.
			Pending->At = FMath::Min(Pending->At, At);
			Pending->bWaitsForHeld = bWaitsForHeld;
			continue;
		}
		FCrossguardTeamHubCountdown& Countdown = TeamHubReturns.Add(TeamId);
		Countdown.At = At;
		Countdown.bWaitsForHeld = bWaitsForHeld;
		UE_LOG(LogCrossguard, Log, TEXT("Team %d finished: returning to the hub in %.0f s while others raid (%d teammate(s) away)"), TeamId, Delay,
			Team.Away);
	}
	if (!TeamHubReturns.IsEmpty() && !GetWorldTimerManager().IsTimerActive(TeamHubReturnTimer))
	{
		GetWorldTimerManager().SetTimer(TeamHubReturnTimer, this, &ACrossguardGameMode::TeamHubReturnTick, CrossguardGameModeLocal::HubReturnTickSeconds,
			true);
	}
	if (!TeamHubReturns.IsEmpty())
	{
		TeamHubReturnTick();
	}
}

void ACrossguardGameMode::HubReturnTick()
{
	if (!bHubReturnPending)
	{
		GetWorldTimerManager().ClearTimer(HubReturnTimer);
		return;
	}
	const float SecondsLeft = GetHubReturnSecondsLeft();
	if (SecondsLeft <= KINDA_SMALL_NUMBER)
	{
		ReturnToHub();
		return;
	}
	const int32 Shown = FMath::CeilToInt(SecondsLeft);
	if (Shown == HubReturnShownSeconds)
	{
		return;
	}
	HubReturnShownSeconds = Shown;
	const FString Message = FString::Printf(TEXT("Returning to the hub in %d s"), Shown);
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		ACrossguardPlayerController* Player = Cast<ACrossguardPlayerController>(It->Get());
		// Not those already leaving with their team.
		if (Player != nullptr && !LeavingForHubIds.Contains(UCrossguardGuildSubsystem::PlayerIdOf(Player->PlayerState)))
		{
			Player->ClientShowMessage(Message, CrossguardGameModeLocal::HubReturnColor);
		}
	}
}

// DEV-ONLY (listen server): the whole server's return.
void ACrossguardGameMode::ReturnToHub()
{
	UWorld* World = GetWorld();
	if (!bIsRaid || bHubReturnStarted || World == nullptr || GameState == nullptr)
	{
		return;
	}
	if (!Crossguard::IsDevListenMode(World))
	{
		// A dedicated match server never travels as a whole: its teams leave on their own.
		UE_LOG(LogCrossguard, Error, TEXT("Whole-server return to the hub refused: development listen mode only (-DevListenHub, PIE)"));
		return;
	}
	bHubReturnStarted = true;
	bHubReturnPending = false;
	GetWorldTimerManager().ClearTimer(HubReturnTimer);
	// The whole server goes: no team leaves on its own any more.
	TeamHubReturns.Reset();
	GetWorldTimerManager().ClearTimer(TeamHubReturnTimer);

	// 1. Raiders still away: the raid ends without them, a failed extraction (Assumed rule,
	// docs/design-decisions.md "Reconnecting"), released the way a death while away is: raid XP lost,
	// saved, team slot freed, player state gone. Their spectators stay (UpdateSpectators).
	TArray<FString> AwayIds;
	HeldPlayers.GetKeys(AwayIds);
	for (const FString& AwayId : AwayIds)
	{
		ReleaseHeldPlayer(AwayId, true);
	}

	// 2. Players not coming along (sent to the menu, released, gone) leave their parties, as logging
	// out of the hub does; the travellers' parties stay (the subsystem lives with the game instance).
	if (UCrossguardPartySubsystem* Parties = GetGameInstance() != nullptr ? GetGameInstance()->GetSubsystem<UCrossguardPartySubsystem>() : nullptr)
	{
		// Teams that went back early are on a hub server now.
		Parties->DropAbsentMembers(World, SentToMenuIds.Union(LeavingForHubIds));
	}

	// 3. Every traveller's record is current before the map changes: extracted players' gear carried
	// out (EndRaid saved it; no pawn now, so the record keeps it), the dead's none. The hub applies it
	// to the new player state and pawn (UCrossguardSaveSubsystem::PlayerJoined / PlayerReady).
	if (UCrossguardSaveSubsystem* Saves = UCrossguardSaveSubsystem::Get(this))
	{
		for (APlayerState* Listed : GameState->PlayerArray)
		{
			if (ACrossguardPlayerState* PlayerState = Cast<ACrossguardPlayerState>(Listed))
			{
				Saves->SaveNow(PlayerState);
			}
		}
		Saves->FlushPendingSaves();
	}

	// 4. The whole server goes back, seamlessly (player states, party keys and controllers come along),
	// absolute like the raid launch. A listen server keeps listening.
	const FString HubMap = GetHubMapName();
	HubReturnUrl = BuildHubReturnUrl(HubMap, GetNetMode() == NM_ListenServer);
	const bool bPlanOnly = bHubReturnPlanOnly || (GIsAutomationTesting && !bHubReturnTravelInTests);
	if (bPlanOnly)
	{
		UE_LOG(LogCrossguard, Log, TEXT("Return to the hub planned (not travelling): server travel to %s"), *HubReturnUrl);
		return;
	}
	if (HubReturnUrl.IsEmpty() || !FPackageName::DoesPackageExist(HubMap))
	{
		UE_LOG(LogCrossguard, Error, TEXT("Cannot return to the hub: map '%s' does not exist (Hub Map Name, -HubMap=)"), *HubMap);
		return;
	}
	UE_LOG(LogCrossguard, Log, TEXT("Raid over: server travel back to the hub, %s"), *HubReturnUrl);
	if (!World->ServerTravel(HubReturnUrl, true))
	{
		UE_LOG(LogCrossguard, Error, TEXT("Return to the hub: server travel to %s refused"), *HubReturnUrl);
	}
}

// --- A team going back early -----------------------------------------------------------------------

FString ACrossguardGameMode::GetHubServerAddress() const
{
	FString FromCommandLine;
	return FParse::Value(FCommandLine::Get(), TEXT("HubServer="), FromCommandLine) ? FromCommandLine : HubServerAddress;
}

FString ACrossguardGameMode::BuildTeamHubJoinUrl(const FString& Address, const FString& Party)
{
	const FString Destination = ACrossguardMenuGameMode::BuildJoinUrl(Address);
	return Destination.IsEmpty() ? FString() : UCrossguardPartySubsystem::RaidTravelUrl(Destination, Party);
}

bool ACrossguardGameMode::IsServerOwnPlayer(const APlayerController* Player)
{
	// DEV-ONLY (listen server): a dedicated server has none. A remote player's controller has a
	// UNetConnection instead.
	return Player != nullptr && Cast<ULocalPlayer>(Player->Player) != nullptr;
}

float ACrossguardGameMode::GetTeamHubReturnSecondsLeft(int32 TeamId) const
{
	const FCrossguardTeamHubCountdown* Pending = TeamHubReturns.Find(TeamId);
	const UWorld* World = GetWorld();
	return Pending != nullptr && World != nullptr ? static_cast<float>(FMath::Max(0.0, Pending->At - World->GetTimeSeconds())) : 0.0f;
}

void ACrossguardGameMode::TellTeam(int32 TeamId, const FString& Message) const
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		ACrossguardPlayerController* Player = Cast<ACrossguardPlayerController>(It->Get());
		const ACrossguardPlayerState* PlayerState = Player != nullptr ? Player->GetPlayerState<ACrossguardPlayerState>() : nullptr;
		if (PlayerState != nullptr && PlayerState->GetTeamId() == TeamId)
		{
			Player->ClientShowMessage(Message, CrossguardGameModeLocal::HubReturnColor);
		}
	}
}

void ACrossguardGameMode::CancelTeamHubReturn(int32 TeamId, bool bTell)
{
	if (TeamHubReturns.Remove(TeamId) == 0)
	{
		return;
	}
	if (bTell)
	{
		UE_LOG(LogCrossguard, Log, TEXT("Team %d's return to the hub called off: a teammate is back in the raid"), TeamId);
		TellTeam(TeamId, TEXT("Return to the hub called off: a teammate is back."));
	}
	if (TeamHubReturns.IsEmpty())
	{
		GetWorldTimerManager().ClearTimer(TeamHubReturnTimer);
	}
}

void ACrossguardGameMode::TeamHubReturnTick()
{
	const UWorld* World = GetWorld();
	if (World == nullptr || TeamHubReturns.IsEmpty())
	{
		GetWorldTimerManager().ClearTimer(TeamHubReturnTimer);
		return;
	}
	// Collected first: a team's return changes the map (and can count the raid again).
	TArray<int32> Due;
	for (TPair<int32, FCrossguardTeamHubCountdown>& Pair : TeamHubReturns)
	{
		const float SecondsLeft = static_cast<float>(FMath::Max(0.0, Pair.Value.At - World->GetTimeSeconds()));
		if (SecondsLeft <= KINDA_SMALL_NUMBER)
		{
			Due.Add(Pair.Key);
			continue;
		}
		const int32 Shown = FMath::CeilToInt(SecondsLeft);
		if (Shown != Pair.Value.ShownSeconds)
		{
			Pair.Value.ShownSeconds = Shown;
			// That team only: the others play on.
			TellTeam(Pair.Key, FString::Printf(TEXT("Returning to the hub in %d s"), Shown));
		}
	}
	for (const int32 TeamId : Due)
	{
		ReturnTeamToHub(TeamId);
	}
	if (TeamHubReturns.IsEmpty())
	{
		GetWorldTimerManager().ClearTimer(TeamHubReturnTimer);
	}
}

void ACrossguardGameMode::ReturnTeamToHub(int32 TeamId)
{
	TeamHubReturns.Remove(TeamId);
	UWorld* World = GetWorld();
	if (!bIsRaid || bHubReturnStarted || World == nullptr || GameState == nullptr)
	{
		return;
	}
	if (GetHubDestination(nullptr).Kind == ECrossguardHubDestination::DevServerTravel)
	{
		// DEV-ONLY (listen server): the team goes with the whole server (UpdateHubReturn waits for it).
		UE_LOG(LogCrossguard, Log, TEXT("Team %d waits for the whole server's return (development listen server)"), TeamId);
		return;
	}

	// 1. Who goes: the team's members here. Teammates still away fail the raid, as at the whole server's
	// return (ReturnToHub).
	TArray<ACrossguardPlayerState*> Members;
	TArray<FString> AwayIds;
	for (APlayerState* Listed : GameState->PlayerArray)
	{
		ACrossguardPlayerState* PlayerState = Cast<ACrossguardPlayerState>(Listed);
		if (PlayerState == nullptr || PlayerState->IsActorBeingDestroyed() || PlayerState->GetTeamId() != TeamId)
		{
			continue;
		}
		if (const FString HeldId = HeldIdOf(PlayerState); !HeldId.IsEmpty())
		{
			AwayIds.Add(HeldId);
			continue;
		}
		const FString PlayerId = UCrossguardGuildSubsystem::PlayerIdOf(PlayerState);
		if (PlayerState->GetOwner() == nullptr || SentToMenuIds.Contains(PlayerId) || LeavingForHubIds.Contains(PlayerId))
		{
			continue;
		}
		if (PlayerState->IsInRaid())
		{
			// UpdateHubReturn calls the team's return off before this can happen.
			UE_LOG(LogCrossguard, Warning, TEXT("Team %d cannot leave: %s is still in the raid"), TeamId, *PlayerId);
			return;
		}
		Members.Add(PlayerState);
	}
	if (Members.IsEmpty())
	{
		return;
	}
	// Marked first: their connections closing (and the releases below) are a deliberate travel, not a
	// disconnect (HoldForReconnect), and they no longer count for this raid (UpdateHubReturn).
	for (const ACrossguardPlayerState* Member : Members)
	{
		LeavingForHubIds.Add(UCrossguardGuildSubsystem::PlayerIdOf(Member));
	}
	TeamsWaitingForServer.Remove(TeamId);
	for (const FString& AwayId : AwayIds)
	{
		ReleaseHeldPlayer(AwayId, true);
	}

	// 2. Every member's record is current before they go (extracted: the gear carried out; the dead:
	// none). Their pawns are gone already (extracted) or stay as lootable bodies (dead), as before.
	if (UCrossguardSaveSubsystem* Saves = UCrossguardSaveSubsystem::Get(this))
	{
		for (ACrossguardPlayerState* Member : Members)
		{
			Saves->SaveNow(Member);
		}
		Saves->FlushPendingSaves();
	}

	// 3. The backend's hub for the team (its party's hub first; asked once, for the first member). Without
	// a backend the answer comes at once (none) and the rest runs now.
	const FString TeamParty = Members[0]->PartyKey;
	TArray<TWeakObjectPtr<ACrossguardPlayerState>> WeakMembers;
	for (ACrossguardPlayerState* Member : Members)
	{
		WeakMembers.Add(Member);
	}
	UCrossguardMatchmakingSubsystem* Matchmaking = UCrossguardMatchmakingSubsystem::Get(World);
	if (Matchmaking == nullptr)
	{
		FinishTeamHubReturn(TeamId, WeakMembers, TeamParty, FString());
		return;
	}
	TWeakObjectPtr<ACrossguardGameMode> WeakThis(this);
	Matchmaking->GetMatchmaking().FindHubServer(UCrossguardGuildSubsystem::PlayerIdOf(Members[0]), TeamParty,
		[WeakThis, TeamId, WeakMembers, TeamParty](const FString& HubAddress)
		{
			if (ACrossguardGameMode* Self = WeakThis.Get())
			{
				Self->FinishTeamHubReturn(TeamId, WeakMembers, TeamParty, HubAddress);
			}
		});
}

void ACrossguardGameMode::FinishTeamHubReturn(int32 TeamId, const TArray<TWeakObjectPtr<ACrossguardPlayerState>>& Members, const FString& TeamParty,
	const FString& BackendHubAddress)
{
	// The plan: each member's hub destination (the backend's hub server, else HubServerAddress), with the
	// team's party key; none: the main menu with a message.
	TArray<FCrossguardTeamHubTravel> Plan;
	for (const TWeakObjectPtr<ACrossguardPlayerState>& WeakMember : Members)
	{
		ACrossguardPlayerState* Member = WeakMember.Get();
		if (Member == nullptr)
		{
			continue;
		}
		FCrossguardTeamHubTravel& Travel = Plan.AddDefaulted_GetRef();
		Travel.PlayerId = UCrossguardGuildSubsystem::PlayerIdOf(Member);
		Travel.Player = Cast<APlayerController>(Member->GetOwner());
		const FCrossguardHubDestination Destination = GetHubDestination(Travel.Player.Get(), BackendHubAddress);
		if (Destination.Kind == ECrossguardHubDestination::HubServer)
		{
			Travel.Url = BuildTeamHubJoinUrl(Destination.Address, TeamParty);
		}
		if (Travel.Url.IsEmpty())
		{
			Travel.Mode = ECrossguardTeamHubTravel::Menu;
		}
		else
		{
			Travel.Mode = ECrossguardTeamHubTravel::HubServer;
			// Development ids travel along (honoured where the hub allows them: -AllowDevPlayerIds).
			Travel.Url = WithDevPlayerId(Travel.Url, Member->DevPlayerId);
		}
		UE_LOG(LogCrossguard, Log, TEXT("Team %d back to the hub: %s %s %s (%s)"), TeamId, *Travel.PlayerId,
			Travel.Mode == ECrossguardTeamHubTravel::HubServer ? TEXT("travels to the hub server") : TEXT("goes to the menu (no hub server)"),
			*Travel.Url, *Destination.Source);
	}

	TeamHubPlans.Add(TeamId, Plan);
	// The last team leaving ends the match (not in development listen mode).
	CheckMatchOver();

	// 4. Go (each client leaves; the match server stays for the teams still raiding).
	const bool bPlanOnly = bHubReturnPlanOnly || (GIsAutomationTesting && !bHubReturnTravelInTests);
	if (bPlanOnly)
	{
		UE_LOG(LogCrossguard, Log, TEXT("Team %d return to the hub planned (not travelling)"), TeamId);
		return;
	}
	for (const FCrossguardTeamHubTravel& Travel : Plan)
	{
		APlayerController* Player = Travel.Player.Get();
		if (Player == nullptr)
		{
			continue;
		}
		if (Travel.Mode == ECrossguardTeamHubTravel::Menu)
		{
			ReturnToMenu(Player, ACrossguardMenuGameMode::NoHubReason);
			continue;
		}
		if (ACrossguardPlayerController* CrossguardPlayer = Cast<ACrossguardPlayerController>(Player))
		{
			CrossguardPlayer->ClientShowMessage(TEXT("Travelling to the hub..."), CrossguardGameModeLocal::HubReturnColor);
		}
		// Absolute: the match's options do not carry over.
		Player->ClientTravel(Travel.Url, TRAVEL_Absolute);
	}
}
