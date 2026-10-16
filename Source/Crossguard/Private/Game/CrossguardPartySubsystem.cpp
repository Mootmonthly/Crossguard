#include "Game/CrossguardPartySubsystem.h"

#include "Crossguard.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Game/CrossguardDevMode.h"
#include "Game/CrossguardGameMode.h"
#include "Dom/JsonObject.h"
#include "Game/CrossguardGuildSubsystem.h"
#include "Game/CrossguardMatchmaking.h"
#include "Game/CrossguardNakamaClient.h"
#include "Game/CrossguardNakamaSubsystem.h"
#include "Game/CrossguardPlayerController.h"
#include "Game/CrossguardPlayerState.h"
#include "Game/CrossguardSaveSubsystem.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Interaction/CrossguardRaidPortal.h"
#include "Misc/Guid.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

static_assert(static_cast<int>(ECrossguardPartyResult::InviteExpired) == static_cast<int>(Crossguard::EPartyError::InviteExpired),
	"ECrossguardPartyResult must mirror Crossguard::EPartyError");
static_assert(static_cast<int>(ECrossguardRaidTravel::HostInParty) == static_cast<int>(Crossguard::ERaidTravel::HostInParty),
	"ECrossguardRaidTravel must mirror Crossguard::ERaidTravel");

// Named, not anonymous: unity builds put this file next to the guild subsystem, whose anonymous
// helpers have the same jobs.
namespace CrossguardPartyLocal
{
	std::string StdOf(const FString& Value)
	{
		return std::string(TCHAR_TO_UTF8(*Value));
	}

	FString TextOf(const std::string& Value)
	{
		return FString(UTF8_TO_TCHAR(Value.c_str()));
	}

	FString IdOf(const APlayerState* PlayerState)
	{
		// The same ids guilds use (online id, else a per-session stand-in).
		return UCrossguardGuildSubsystem::PlayerIdOf(PlayerState);
	}

	const TCHAR* PartyRpc = TEXT("crossguard_party");
	const TCHAR* PartyGetRpc = TEXT("crossguard_party_get");
	const FLinearColor SearchColor(0.6f, 0.85f, 1.0f);

	// A raid launch's answer: whether it came before RequestMatch returned (then StartRaid returns it).
	struct FLaunchAnswer
	{
		bool bReturned = false;
		bool bAnswered = false;
		ECrossguardPartyResult Result = ECrossguardPartyResult::None;
	};
}

void UCrossguardPartySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UCrossguardNakamaSubsystem>();
	const UCrossguardNakamaSubsystem* Nakama = GetGameInstance()->GetSubsystem<UCrossguardNakamaSubsystem>();
	bBackend = Nakama != nullptr && Nakama->IsBackendEnabled();
	if (bBackend)
	{
		RefreshHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UCrossguardPartySubsystem::RefreshTick), PartyRefreshSeconds);
		UE_LOG(LogCrossguard, Log, TEXT("Parties: in Nakama (refreshed every %.0f s)"), PartyRefreshSeconds);
	}
}

void UCrossguardPartySubsystem::Deinitialize()
{
	if (RefreshHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(RefreshHandle);
		RefreshHandle.Reset();
	}
	Super::Deinitialize();
}

UWorld* UCrossguardPartySubsystem::GetServerWorld() const
{
	return GetGameInstance() != nullptr ? GetGameInstance()->GetWorld() : nullptr;
}

double UCrossguardPartySubsystem::Now()
{
	// Monotonic seconds: invites live minutes and parties never outlive the process.
	return FPlatformTime::Seconds();
}

ECrossguardPartyResult UCrossguardPartySubsystem::ToResult(Crossguard::EPartyError Error)
{
	return static_cast<ECrossguardPartyResult>(static_cast<uint8>(Error));
}

FString UCrossguardPartySubsystem::RaidTravelUrl(const FString& Destination, const FString& PartyId, const FString& ExtraOption)
{
	std::vector<std::string> Options;
	if (!ExtraOption.IsEmpty())
	{
		Options.push_back(CrossguardPartyLocal::StdOf(ExtraOption));
	}
	return CrossguardPartyLocal::TextOf(Crossguard::BuildRaidTravelUrl(CrossguardPartyLocal::StdOf(Destination), CrossguardPartyLocal::StdOf(PartyId), Options));
}

FString UCrossguardPartySubsystem::PartyKeyOf(const FString& PlayerId) const
{
	const Crossguard::FParty* Party = Registry.PartyOf(CrossguardPartyLocal::StdOf(PlayerId));
	return Party != nullptr ? CrossguardPartyLocal::TextOf(Party->Id) : FString();
}

bool UCrossguardPartySubsystem::IsInHub(const ACrossguardPlayerState* Actor) const
{
	// Ruled: the party is the raid team, so it only changes between raids (in the hub).
	const ACrossguardGameMode* GameMode = Actor != nullptr ? Actor->GetWorld()->GetAuthGameMode<ACrossguardGameMode>() : nullptr;
	return GameMode != nullptr && !GameMode->bIsRaid;
}

ACrossguardPlayerState* UCrossguardPartySubsystem::FindOnline(const UWorld* World, const FString& PlayerId) const
{
	const AGameStateBase* GameState = World != nullptr ? World->GetGameState() : nullptr;
	if (GameState == nullptr)
	{
		return nullptr;
	}
	for (APlayerState* PlayerState : GameState->PlayerArray)
	{
		if (CrossguardPartyLocal::IdOf(PlayerState) == PlayerId)
		{
			return Cast<ACrossguardPlayerState>(PlayerState);
		}
	}
	return nullptr;
}

// --- Membership -----------------------------------------------------------------------------------

ECrossguardPartyResult UCrossguardPartySubsystem::Invite(ACrossguardPlayerState* Actor, ACrossguardPlayerState* Invitee)
{
	if (Actor == nullptr || Invitee == nullptr)
	{
		return ECrossguardPartyResult::NoPlayer;
	}
	if (!IsInHub(Actor))
	{
		return ECrossguardPartyResult::OnlyInHub;
	}
	if (bBackend)
	{
		return SendBackendOp(TEXT("invite"), Actor, CrossguardPartyLocal::IdOf(Invitee), FString());
	}
	const std::string ActorId = CrossguardPartyLocal::StdOf(CrossguardPartyLocal::IdOf(Actor));
	// Used only if the actor is in no party yet. Hex digits: safe in a travel URL.
	const std::string NewPartyId = CrossguardPartyLocal::StdOf(FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const Crossguard::EPartyError Error = Registry.Invite(ActorId, CrossguardPartyLocal::StdOf(CrossguardPartyLocal::IdOf(Invitee)), Now(), NewPartyId);
	if (Error == Crossguard::EPartyError::None)
	{
		DisplayNames.Add(CrossguardPartyLocal::IdOf(Actor), Actor->GetPlayerName());
		const std::string PartyId = Registry.PartyOf(ActorId)->Id;
		SyncParty(PartyId);
		RefreshPlayer(Invitee);
	}
	return ToResult(Error);
}

ECrossguardPartyResult UCrossguardPartySubsystem::Accept(ACrossguardPlayerState* Actor, const FString& PartyId)
{
	if (Actor == nullptr)
	{
		return ECrossguardPartyResult::NoPlayer;
	}
	if (!IsInHub(Actor))
	{
		return ECrossguardPartyResult::OnlyInHub;
	}
	if (bBackend)
	{
		return SendBackendOp(TEXT("accept"), Actor, FString(), PartyId);
	}
	const Crossguard::EPartyError Error = Registry.Accept(CrossguardPartyLocal::StdOf(PartyId), CrossguardPartyLocal::StdOf(CrossguardPartyLocal::IdOf(Actor)), Now());
	if (Error == Crossguard::EPartyError::None)
	{
		// A solo raid search of theirs, and the party's own, are moot now.
		CancelSearch(PartyId, CrossguardPartyLocal::IdOf(Actor));
		SyncParty(CrossguardPartyLocal::StdOf(PartyId));
	}
	else
	{
		// An expired or vanished invite drops off their list.
		RefreshPlayer(Actor);
	}
	return ToResult(Error);
}

ECrossguardPartyResult UCrossguardPartySubsystem::Decline(ACrossguardPlayerState* Actor, const FString& PartyId)
{
	// Allowed anywhere: it changes no party.
	if (Actor == nullptr)
	{
		return ECrossguardPartyResult::NoPlayer;
	}
	if (bBackend)
	{
		return SendBackendOp(TEXT("decline"), Actor, FString(), PartyId);
	}
	const Crossguard::EPartyError Error = Registry.Decline(CrossguardPartyLocal::StdOf(PartyId), CrossguardPartyLocal::StdOf(CrossguardPartyLocal::IdOf(Actor)));
	RefreshPlayer(Actor);
	return ToResult(Error);
}

ECrossguardPartyResult UCrossguardPartySubsystem::DeclineAll(ACrossguardPlayerState* Actor)
{
	// Allowed anywhere, like Decline.
	if (Actor == nullptr)
	{
		return ECrossguardPartyResult::NoPlayer;
	}
	if (bBackend)
	{
		return SendBackendOp(TEXT("decline_all"), Actor, FString(), FString());
	}
	const int Dropped = Registry.DeclineAll(CrossguardPartyLocal::StdOf(CrossguardPartyLocal::IdOf(Actor)));
	RefreshPlayer(Actor);
	return Dropped > 0 ? ECrossguardPartyResult::None : ECrossguardPartyResult::NoInvite;
}

ECrossguardPartyResult UCrossguardPartySubsystem::Leave(ACrossguardPlayerState* Actor)
{
	if (Actor == nullptr)
	{
		return ECrossguardPartyResult::NoPlayer;
	}
	if (!IsInHub(Actor))
	{
		return ECrossguardPartyResult::OnlyInHub;
	}
	if (bBackend)
	{
		return SendBackendOp(TEXT("leave"), Actor, FString(), FString());
	}
	const Crossguard::FPartyLeaveResult Left = Registry.Leave(CrossguardPartyLocal::StdOf(CrossguardPartyLocal::IdOf(Actor)));
	if (!Left.IsValid())
	{
		return ToResult(Left.Error);
	}
	CancelSearch(CrossguardPartyLocal::TextOf(Left.PartyId), CrossguardPartyLocal::IdOf(Actor));
	if (Left.bDisbanded)
	{
		UE_LOG(LogCrossguard, Log, TEXT("Party %s disbanded"), *CrossguardPartyLocal::TextOf(Left.PartyId));
	}
	SyncParty(Left.PartyId, Actor);
	return ECrossguardPartyResult::None;
}

ECrossguardPartyResult UCrossguardPartySubsystem::Kick(ACrossguardPlayerState* Actor, const FString& TargetId)
{
	if (Actor == nullptr)
	{
		return ECrossguardPartyResult::NoPlayer;
	}
	if (!IsInHub(Actor))
	{
		return ECrossguardPartyResult::OnlyInHub;
	}
	if (bBackend)
	{
		return SendBackendOp(TEXT("kick"), Actor, TargetId, FString());
	}
	const std::string ActorId = CrossguardPartyLocal::StdOf(CrossguardPartyLocal::IdOf(Actor));
	const Crossguard::FParty* Party = Registry.PartyOf(ActorId);
	const std::string PartyId = Party != nullptr ? Party->Id : std::string();
	const Crossguard::EPartyError Error = Registry.Kick(ActorId, CrossguardPartyLocal::StdOf(TargetId));
	if (Error == Crossguard::EPartyError::None)
	{
		CancelSearch(CrossguardPartyLocal::TextOf(PartyId), TargetId);
		SyncParty(PartyId, FindOnline(Actor->GetWorld(), TargetId));
	}
	return ToResult(Error);
}

ECrossguardPartyResult UCrossguardPartySubsystem::Promote(ACrossguardPlayerState* Actor, const FString& TargetId)
{
	// Allowed anywhere: the members (and so the team) stay the same.
	if (Actor == nullptr)
	{
		return ECrossguardPartyResult::NoPlayer;
	}
	if (bBackend)
	{
		return SendBackendOp(TEXT("promote"), Actor, TargetId, FString());
	}
	const std::string ActorId = CrossguardPartyLocal::StdOf(CrossguardPartyLocal::IdOf(Actor));
	const Crossguard::EPartyError Error = Registry.Promote(ActorId, CrossguardPartyLocal::StdOf(TargetId));
	if (Error == Crossguard::EPartyError::None)
	{
		const std::string PartyId = Registry.PartyOf(ActorId)->Id;
		SyncParty(PartyId);
	}
	return ToResult(Error);
}

void UCrossguardPartySubsystem::PlayerLeftServer(ACrossguardPlayerState* PlayerState)
{
	if (PlayerState == nullptr)
	{
		return;
	}
	const FString LeavingId = CrossguardPartyLocal::IdOf(PlayerState);
	if (bBackend)
	{
		// Off to a raid: still in the party (it is the raid team). Anything else (quit, menu): they leave
		// it in Nakama, as in memory; the answer updates the rest here.
		if (RaidTravellerIds.Remove(LeavingId) == 0 && IsInHub(PlayerState) && FCrossguardNakamaClient::IsUserId(LeavingId))
		{
			SendBackendOpFor(TEXT("logout"), LeavingId, FString(), FString());
		}
		return;
	}
	const std::string PlayerId = CrossguardPartyLocal::StdOf(LeavingId);
	// Their open invites go too.
	Registry.DeclineAll(PlayerId);
	const Crossguard::FPartyLeaveResult Left = Registry.Leave(PlayerId);
	if (Left.IsValid() && !Left.bDisbanded)
	{
		// The rest see them gone (and a new leader if it was them).
		SyncParty(Left.PartyId);
	}
}

void UCrossguardPartySubsystem::DropAbsentMembers(const UWorld* World, const TSet<FString>& Leaving)
{
	if (bBackend)
	{
		// DEV-ONLY (listen server) path; with the backend, members on no server leave on Nakama's side.
		return;
	}
	std::vector<std::string> Absent;
	for (const auto& Pair : Registry.All())
	{
		for (const std::string& Member : Pair.second.Members)
		{
			const FString MemberId = CrossguardPartyLocal::TextOf(Member);
			if (Leaving.Contains(MemberId) || FindOnline(World, MemberId) == nullptr)
			{
				Absent.push_back(Member);
			}
		}
	}
	// Collected first: leaving changes the registry.
	for (const std::string& PlayerId : Absent)
	{
		Registry.DeclineAll(PlayerId);
		const Crossguard::FPartyLeaveResult Left = Registry.Leave(PlayerId);
		UE_LOG(LogCrossguard, Log, TEXT("%s is not coming back to the hub: left their party"), *CrossguardPartyLocal::TextOf(PlayerId));
		if (Left.IsValid() && !Left.bDisbanded)
		{
			SyncParty(Left.PartyId);
		}
	}
}

// --- Raid launch ----------------------------------------------------------------------------------

TArray<ACrossguardPlayerState*> UCrossguardPartySubsystem::TravellersFor(ACrossguardPlayerState* Actor) const
{
	TArray<ACrossguardPlayerState*> Travellers;
	const Crossguard::FParty* Party = Actor != nullptr ? Registry.PartyOf(CrossguardPartyLocal::StdOf(CrossguardPartyLocal::IdOf(Actor))) : nullptr;
	if (Party == nullptr)
	{
		if (Actor != nullptr)
		{
			Travellers.Add(Actor);
		}
		return Travellers;
	}
	for (const std::string& Member : Party->Members)
	{
		if (ACrossguardPlayerState* Online = FindOnline(Actor->GetWorld(), CrossguardPartyLocal::TextOf(Member)))
		{
			Travellers.Add(Online);
		}
	}
	return Travellers;
}

ECrossguardPartyResult UCrossguardPartySubsystem::CheckRaidLaunch(ACrossguardPlayerState* Actor) const
{
	if (Actor == nullptr)
	{
		return ECrossguardPartyResult::NoPlayer;
	}
	if (!IsInHub(Actor))
	{
		return ECrossguardPartyResult::OnlyInHub;
	}
	return ToResult(Registry.CanStartRaid(CrossguardPartyLocal::StdOf(CrossguardPartyLocal::IdOf(Actor))));
}

FCrossguardMatchRequest UCrossguardPartySubsystem::MakeMatchRequest(ACrossguardPlayerState* Actor) const
{
	FCrossguardMatchRequest Request;
	const Crossguard::FParty* Party = Registry.PartyOf(CrossguardPartyLocal::StdOf(CrossguardPartyLocal::IdOf(Actor)));
	Request.PartyKey = Party != nullptr ? CrossguardPartyLocal::TextOf(Party->Id) : FString();
	for (const ACrossguardPlayerState* Traveller : TravellersFor(Actor))
	{
		Request.PlayerIds.Add(CrossguardPartyLocal::IdOf(Traveller));
	}
	return Request;
}

ECrossguardPartyResult UCrossguardPartySubsystem::PlanRaidTravel(ACrossguardPlayerState* Actor, ECrossguardRaidTravel& OutMode, FString& OutUrl) const
{
	OutMode = ECrossguardRaidTravel::NoDestination;
	OutUrl.Reset();
	if (const ECrossguardPartyResult Checked = CheckRaidLaunch(Actor); Checked != ECrossguardPartyResult::None)
	{
		return Checked;
	}
	UCrossguardMatchmakingSubsystem* Matchmaking = UCrossguardMatchmakingSubsystem::Get(Actor->GetWorld());
	FCrossguardMatchAssignment Assignment;
	if (Matchmaking != nullptr)
	{
		const FCrossguardMatchRequest Request = MakeMatchRequest(Actor);
		bool bAnswered = false;
		Matchmaking->GetMatchmaking().RequestMatch(Actor->GetWorld(), Request, [&bAnswered, &Assignment](const FCrossguardMatchAssignment& Answer)
		{
			bAnswered = true;
			Assignment = Answer;
		});
		if (!bAnswered)
		{
			// A backend search (it answers later): a plan only plans, so it is called off. The callback
			// above must not run after this returns: CancelMatch answers it now.
			Matchmaking->GetMatchmaking().CancelMatch(Request.SearchKey());
			UE_LOG(LogCrossguard, Log, TEXT("Raid plan for %s: matchmaking answers later (a plan only takes answers that come at once)"),
				*Actor->GetPlayerName());
			Assignment = FCrossguardMatchAssignment();
		}
	}
	return PlanRaidTravelWith(Actor, Assignment, OutMode, OutUrl);
}

ECrossguardPartyResult UCrossguardPartySubsystem::PlanRaidTravelWith(ACrossguardPlayerState* Actor, const FCrossguardMatchAssignment& Assignment,
	ECrossguardRaidTravel& OutMode, FString& OutUrl) const
{
	OutMode = ECrossguardRaidTravel::NoDestination;
	OutUrl.Reset();
	if (const ECrossguardPartyResult Checked = CheckRaidLaunch(Actor); Checked != ECrossguardPartyResult::None)
	{
		return Checked;
	}
	const std::string ActorId = CrossguardPartyLocal::StdOf(CrossguardPartyLocal::IdOf(Actor));
	const UWorld* World = Actor->GetWorld();
	const ACrossguardGameMode* GameMode = World->GetAuthGameMode<ACrossguardGameMode>();
	const AGameStateBase* GameState = World->GetGameState();
	const TArray<ACrossguardPlayerState*> Travellers = TravellersFor(Actor);
	const bool bListenServer = World->GetNetMode() == NM_ListenServer;
	const Crossguard::FParty* Party = Registry.PartyOf(ActorId);
	const FString PartyId = Party != nullptr ? CrossguardPartyLocal::TextOf(Party->Id) : FString();

	Crossguard::FRaidTravelSituation Situation;
	// DEV-ONLY (listen server): the whole-server launch.
	Situation.bDevListenMode = Crossguard::IsDevListenMode(World);
	Situation.bWholeServerInParty = GameState != nullptr && GameState->PlayerArray.Num() == Travellers.Num();
	Situation.bHasRaidMap = !GameMode->GetRaidMapName().IsEmpty();
	Situation.bHasRaidServer = Assignment.Status == ECrossguardMatchStatus::Assigned;
	for (const ACrossguardPlayerState* Traveller : Travellers)
	{
		const APlayerController* Controller = Cast<APlayerController>(Traveller->GetOwner());
		Situation.bListenHostInParty |= bListenServer && Controller != nullptr && Controller->IsLocalController();
	}

	OutMode = static_cast<ECrossguardRaidTravel>(static_cast<uint8>(Crossguard::PickRaidTravel(Situation)));
	switch (OutMode)
	{
	case ECrossguardRaidTravel::ServerTravel:
		// DEV-ONLY (listen server): the whole server loads the raid map; a listen server keeps listening.
		OutUrl = RaidTravelUrl(GameMode->GetRaidMapName(), PartyId, bListenServer ? TEXT("listen") : TEXT(""));
		break;
	case ECrossguardRaidTravel::ClientTravel:
		// The match server with the backend's ticket (the actor's; each traveller has their own); a
		// development assignment carries ?Party= instead.
		OutUrl = Assignment.bDevPartyOption ? RaidTravelUrl(Assignment.ServerAddress, PartyId)
											: UCrossguardMatchmakingSubsystem::BuildMatchTravelUrl(Assignment.ServerAddress,
												  Assignment.TicketFor(CrossguardPartyLocal::TextOf(ActorId)));
		break;
	case ECrossguardRaidTravel::NoDestination:
		return Assignment.Status == ECrossguardMatchStatus::Cancelled ? ECrossguardPartyResult::SearchCancelled : ECrossguardPartyResult::NoRaidDestination;
	case ECrossguardRaidTravel::HostInParty:
		// DEV-ONLY (listen server): the host cannot leave its own hub.
		return ECrossguardPartyResult::HostInParty;
	}
	return OutUrl.IsEmpty() ? ECrossguardPartyResult::TravelFailed : ECrossguardPartyResult::None;
}

ECrossguardPartyResult UCrossguardPartySubsystem::StartRaid(ACrossguardPlayerState* Actor)
{
	using namespace CrossguardPartyLocal;
	if (const ECrossguardPartyResult Checked = CheckRaidLaunch(Actor); Checked != ECrossguardPartyResult::None)
	{
		return Checked;
	}
	UCrossguardMatchmakingSubsystem* Matchmaking = UCrossguardMatchmakingSubsystem::Get(Actor->GetWorld());
	if (Matchmaking == nullptr)
	{
		return ECrossguardPartyResult::NoRaidDestination;
	}
	const FCrossguardMatchRequest Request = MakeMatchRequest(Actor);
	if (Matchmaking->GetMatchmaking().IsSearching(Request.SearchKey()))
	{
		return ECrossguardPartyResult::AlreadySearching;
	}
	const FString ActorId = IdOf(Actor);
	const TSharedRef<FLaunchAnswer> Answer = MakeShared<FLaunchAnswer>();
	TWeakObjectPtr<UCrossguardPartySubsystem> WeakThis(this);
	TWeakObjectPtr<ACrossguardPlayerState> WeakActor(Actor);
	Matchmaking->GetMatchmaking().RequestMatch(Actor->GetWorld(), Request, [WeakThis, WeakActor, ActorId, Answer](const FCrossguardMatchAssignment& Assignment)
	{
		UCrossguardPartySubsystem* Self = WeakThis.Get();
		Answer->Result = Self != nullptr ? Self->FinishRaidLaunch(WeakActor.Get(), ActorId, Assignment) : ECrossguardPartyResult::TravelFailed;
		Answer->bAnswered = true;
		if (Answer->bReturned && Self != nullptr && Answer->Result != ECrossguardPartyResult::None)
		{
			// The party window (OnPartyRefused) and a HUD line, as a refused portal launch gets.
			Self->RefuseLater(ActorId, Answer->Result);
			Self->ShowToPlayers({ActorId}, ACrossguardRaidPortal::RefusalMessage(Answer->Result));
		}
	});
	Answer->bReturned = true;
	if (Answer->bAnswered)
	{
		return Answer->Result;
	}
	UE_LOG(LogCrossguard, Log, TEXT("%s looks for a raid for %d player(s) (%s)"), *Actor->GetPlayerName(), Request.PlayerIds.Num(),
		*Matchmaking->GetMatchmaking().Describe());
	ShowToPlayers(Request.PlayerIds, TEXT("Looking for a raid..."));
	return ECrossguardPartyResult::None;
}

ECrossguardPartyResult UCrossguardPartySubsystem::FinishRaidLaunch(ACrossguardPlayerState* Actor, const FString& ActorId, const FCrossguardMatchAssignment& Assignment)
{
	if (Actor == nullptr || Actor->IsActorBeingDestroyed() || Actor->GetWorld() == nullptr)
	{
		UE_LOG(LogCrossguard, Warning, TEXT("Raid launch for %s answered after they left the server"), *ActorId);
		return ECrossguardPartyResult::NoPlayer;
	}
	UWorld* World = Actor->GetWorld();
	ECrossguardRaidTravel Mode = ECrossguardRaidTravel::NoDestination;
	FString Url;
	if (const ECrossguardPartyResult Planned = PlanRaidTravelWith(Actor, Assignment, Mode, Url); Planned != ECrossguardPartyResult::None)
	{
		if (Assignment.Status == ECrossguardMatchStatus::Failed)
		{
			UE_LOG(LogCrossguard, Warning, TEXT("Raid launch for %s: matchmaking failed: %s"), *Actor->GetPlayerName(), *Assignment.Error);
		}
		return Planned;
	}
	const TArray<ACrossguardPlayerState*> Travellers = TravellersFor(Actor);
	UE_LOG(LogCrossguard, Log, TEXT("%s starts the raid for %d player(s): %s %s"), *Actor->GetPlayerName(), Travellers.Num(),
		Mode == ECrossguardRaidTravel::ServerTravel ? TEXT("server travel to") : TEXT("client travel to"), *Url);

	// Every traveller's record is current before the map changes (the carried gear crosses a seamless
	// travel in the save subsystem's record, not on the pawn), not only what the watcher caught.
	if (UCrossguardSaveSubsystem* Saves = UCrossguardSaveSubsystem::Get(World))
	{
		for (ACrossguardPlayerState* Traveller : Travellers)
		{
			Saves->SaveNow(Traveller);
		}
		Saves->FlushPendingSaves();
	}

	if (Mode == ECrossguardRaidTravel::ServerTravel)
	{
		// DEV-ONLY (listen server): PlanRaidTravelWith only picks this in development mode.
		// Seamless travel carries each player state's PartyKey (set when they joined the party) to
		// the raid map, where the game mode puts the party on one team again. Absolute: the hub's
		// own options (?Hub) must not carry over.
		return World->ServerTravel(Url, true) ? ECrossguardPartyResult::None : ECrossguardPartyResult::TravelFailed;
	}
	// Each member's client connects to the match server with their own ticket (development: ?Party=<id>),
	// with their own development id in development builds. Their logout here is a travel: with the
	// backend they stay in the party (PlayerLeftServer).
	for (ACrossguardPlayerState* Traveller : Travellers)
	{
		const FString TravellerId = CrossguardPartyLocal::IdOf(Traveller);
		const FString TravellerUrl = Assignment.bDevPartyOption
			? Url
			: UCrossguardMatchmakingSubsystem::BuildMatchTravelUrl(Assignment.ServerAddress, Assignment.TicketFor(TravellerId));
		APlayerController* Controller = Cast<APlayerController>(Traveller->GetOwner());
		if (Controller == nullptr)
		{
			continue;
		}
		if (TravellerUrl.IsEmpty())
		{
			// Joined after the search started: no ticket for them.
			UE_LOG(LogCrossguard, Warning, TEXT("Raid launch: no match ticket for %s; they stay in the hub"), *TravellerId);
			if (ACrossguardPlayerController* CrossguardController = Cast<ACrossguardPlayerController>(Controller))
			{
				CrossguardController->ClientShowMessage(TEXT("Your party left for a raid without you (you joined during the search)."), FLinearColor::Yellow);
			}
			continue;
		}
		if (bBackend)
		{
			RaidTravellerIds.Add(TravellerId);
		}
		Controller->ClientTravel(ACrossguardGameMode::WithDevPlayerId(TravellerUrl, Traveller->DevPlayerId), TRAVEL_Absolute);
	}
	return ECrossguardPartyResult::None;
}

void UCrossguardPartySubsystem::CancelSearch(const FString& PartyKey, const FString& PlayerId)
{
	UCrossguardMatchmakingSubsystem* Matchmaking = UCrossguardMatchmakingSubsystem::Get(GetServerWorld());
	if (Matchmaking == nullptr)
	{
		return;
	}
	for (const FString& Key : {PartyKey, PlayerId})
	{
		if (!Key.IsEmpty() && Matchmaking->GetMatchmaking().IsSearching(Key))
		{
			UE_LOG(LogCrossguard, Log, TEXT("Raid search for %s called off: the party changed"), *Key);
			Matchmaking->GetMatchmaking().CancelMatch(Key);
		}
	}
}

void UCrossguardPartySubsystem::RefuseLater(const FString& ActorId, ECrossguardPartyResult Result)
{
	ACrossguardPlayerState* Actor = FindOnline(GetServerWorld(), ActorId);
	if (UCrossguardPartyComponent* View = Actor != nullptr ? Actor->GetParty() : nullptr)
	{
		View->Refuse(Result);
	}
}

void UCrossguardPartySubsystem::ShowToPlayers(const TArray<FString>& PlayerIds, const FString& Message) const
{
	const UWorld* World = GetServerWorld();
	for (const FString& PlayerId : PlayerIds)
	{
		const ACrossguardPlayerState* PlayerState = FindOnline(World, PlayerId);
		if (ACrossguardPlayerController* Controller = PlayerState != nullptr ? Cast<ACrossguardPlayerController>(PlayerState->GetOwner()) : nullptr)
		{
			Controller->ClientShowMessage(Message, CrossguardPartyLocal::SearchColor);
		}
	}
}

// --- Backend (Nakama) ------------------------------------------------------------------------------

ECrossguardPartyResult UCrossguardPartySubsystem::ResultFromName(const FString& Name)
{
	if (Name.IsEmpty() || Name.Contains(TEXT(":")))
	{
		return ECrossguardPartyResult::BackendUnavailable;
	}
	// The backend's names are EPartyError's, the same as ECrossguardPartyResult's first entries.
	const UEnum* Enum = StaticEnum<ECrossguardPartyResult>();
	int64 Value = Enum->GetValueByNameString(Name);
	if (Value == INDEX_NONE)
	{
		Value = Enum->GetValueByNameString(TEXT("ECrossguardPartyResult::") + Name);
	}
	return Value == INDEX_NONE ? ECrossguardPartyResult::BackendUnavailable : static_cast<ECrossguardPartyResult>(Value);
}

ECrossguardPartyResult UCrossguardPartySubsystem::SendBackendOp(const TCHAR* Op, ACrossguardPlayerState* Actor, const FString& TargetId, const FString& PartyId)
{
	const FString ActorId = CrossguardPartyLocal::IdOf(Actor);
	// Nakama knows players by their Nakama user id (a verified login).
	if (!FCrossguardNakamaClient::IsUserId(ActorId) || (!TargetId.IsEmpty() && !FCrossguardNakamaClient::IsUserId(TargetId)))
	{
		UE_LOG(LogCrossguard, Warning, TEXT("Party %s by %s: parties on this server need a Nakama login (%s / %s)"), Op, *Actor->GetPlayerName(), *ActorId,
			*TargetId);
		return ECrossguardPartyResult::NoPlayer;
	}
	SendBackendOpFor(Op, ActorId, TargetId, PartyId);
	return ECrossguardPartyResult::None;
}

void UCrossguardPartySubsystem::SendBackendOpFor(const TCHAR* Op, const FString& ActorId, const FString& TargetId, const FString& PartyId)
{
	UCrossguardNakamaSubsystem* Nakama = GetGameInstance() != nullptr ? GetGameInstance()->GetSubsystem<UCrossguardNakamaSubsystem>() : nullptr;
	if (Nakama == nullptr)
	{
		return;
	}
	const FString Payload = FString::Printf(TEXT("{\"op\":%s,\"actor_id\":%s,\"target_id\":%s,\"party_id\":%s,\"server_id\":%s}"),
		*FCrossguardNakamaClient::Quote(Op), *FCrossguardNakamaClient::Quote(ActorId), *FCrossguardNakamaClient::Quote(TargetId),
		*FCrossguardNakamaClient::Quote(PartyId), *FCrossguardNakamaClient::Quote(Nakama->GetServerId()));
	const FString OpName = Op;
	TWeakObjectPtr<UCrossguardPartySubsystem> WeakThis(this);
	Nakama->CallServerRpc(CrossguardPartyLocal::PartyRpc, Payload, [WeakThis, OpName, ActorId, TargetId](const FCrossguardNakamaResponse& Response)
	{
		UCrossguardPartySubsystem* Self = WeakThis.Get();
		if (Self == nullptr)
		{
			return;
		}
		if (!Response.IsOk())
		{
			UE_LOG(LogCrossguard, Warning, TEXT("Party %s by %s: Nakama did not take it (%s)"), *OpName, *ActorId, *Response.Describe());
			Self->RefuseLater(ActorId, ECrossguardPartyResult::BackendUnavailable);
			return;
		}
		const ECrossguardPartyResult Result = ResultFromName(Self->ApplyBackendState(Response.Body));
		if (Result != ECrossguardPartyResult::None)
		{
			Self->RefuseLater(ActorId, Result);
			return;
		}
		// A membership change calls off the raid searches it touches.
		if (OpName == TEXT("accept") || OpName == TEXT("leave") || OpName == TEXT("kick") || OpName == TEXT("logout"))
		{
			TSharedPtr<FJsonObject> Object;
			FString PartyId;
			if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response.Body), Object) && Object.IsValid())
			{
				Object->TryGetStringField(TEXT("party_id"), PartyId);
			}
			Self->CancelSearch(PartyId, OpName == TEXT("kick") ? TargetId : ActorId);
		}
	});
}

void UCrossguardPartySubsystem::FetchFromBackend(const TArray<FString>& PlayerIds)
{
	UCrossguardNakamaSubsystem* Nakama = GetGameInstance() != nullptr ? GetGameInstance()->GetSubsystem<UCrossguardNakamaSubsystem>() : nullptr;
	if (Nakama == nullptr || PlayerIds.IsEmpty())
	{
		return;
	}
	FString Users;
	for (const FString& PlayerId : PlayerIds)
	{
		Users += (Users.IsEmpty() ? TEXT("") : TEXT(",")) + FCrossguardNakamaClient::Quote(PlayerId);
	}
	TWeakObjectPtr<UCrossguardPartySubsystem> WeakThis(this);
	Nakama->CallServerRpc(CrossguardPartyLocal::PartyGetRpc, FString::Printf(TEXT("{\"user_ids\":[%s]}"), *Users),
		[WeakThis](const FCrossguardNakamaResponse& Response)
		{
			UCrossguardPartySubsystem* Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			if (!Response.IsOk())
			{
				UE_LOG(LogCrossguard, Warning, TEXT("Parties: could not read them from Nakama (%s)"), *Response.Describe());
				return;
			}
			Self->ApplyBackendState(Response.Body);
		});
}

bool UCrossguardPartySubsystem::RefreshTick(float DeltaSeconds)
{
	const UWorld* World = GetServerWorld();
	const AGameStateBase* GameState = World != nullptr ? World->GetGameState() : nullptr;
	if (GameState == nullptr || World->GetNetMode() == NM_Client)
	{
		return true;
	}
	TArray<FString> Ids;
	for (const APlayerState* PlayerState : GameState->PlayerArray)
	{
		const FString PlayerId = CrossguardPartyLocal::IdOf(PlayerState);
		if (FCrossguardNakamaClient::IsUserId(PlayerId))
		{
			Ids.AddUnique(PlayerId);
		}
	}
	// The backend takes 256 at a time; a hub holds fewer.
	if (Ids.Num() > 256)
	{
		Ids.SetNum(256);
	}
	FetchFromBackend(Ids);
	return true;
}

void UCrossguardPartySubsystem::PlayerArrived(ACrossguardPlayerState* PlayerState)
{
	RefreshPlayer(PlayerState);
	const FString PlayerId = PlayerState != nullptr ? CrossguardPartyLocal::IdOf(PlayerState) : FString();
	if (bBackend && FCrossguardNakamaClient::IsUserId(PlayerId))
	{
		FetchFromBackend({PlayerId});
	}
}

FString UCrossguardPartySubsystem::ApplyBackendState(const FString& Body)
{
	using namespace CrossguardPartyLocal;
	TSharedPtr<FJsonObject> Object;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Object) || !Object.IsValid())
	{
		UE_LOG(LogCrossguard, Warning, TEXT("Parties: unreadable answer from Nakama: %s"), *Body.Left(200));
		return TEXT("<unreadable>");
	}
	FString Result;
	Object->TryGetStringField(TEXT("result"), Result);
	double NowMs = 0.0;
	Object->TryGetNumberField(TEXT("now_ms"), NowMs);
	const double Clock = Now();

	// Who to bring up to date afterwards: the players in the answer, and everyone in a party it touches
	// (before and after).
	TSet<FString> Touched;
	const auto TouchParty = [this, &Touched](const std::string& PartyId)
	{
		if (const Crossguard::FParty* Party = Registry.Find(PartyId))
		{
			for (const std::string& Member : Party->Members)
			{
				Touched.Add(TextOf(Member));
			}
		}
	};

	bool bDisbanded = false;
	FString AnswerPartyId;
	Object->TryGetBoolField(TEXT("disbanded"), bDisbanded);
	Object->TryGetStringField(TEXT("party_id"), AnswerPartyId);
	if (bDisbanded && !AnswerPartyId.IsEmpty())
	{
		TouchParty(StdOf(AnswerPartyId));
		Registry.RemoveParty(StdOf(AnswerPartyId));
	}

	const TSharedPtr<FJsonObject>* Parties = nullptr;
	if (Object->TryGetObjectField(TEXT("parties"), Parties) && Parties != nullptr && Parties->IsValid())
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Parties)->Values)
		{
			const TSharedPtr<FJsonObject>* PartyObject = nullptr;
			if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(PartyObject) || PartyObject == nullptr || !PartyObject->IsValid())
			{
				continue;
			}
			FString LeaderId;
			(*PartyObject)->TryGetStringField(TEXT("leader_id"), LeaderId);
			std::vector<std::string> Members;
			const TArray<TSharedPtr<FJsonValue>>* MemberValues = nullptr;
			if ((*PartyObject)->TryGetArrayField(TEXT("members"), MemberValues) && MemberValues != nullptr)
			{
				for (const TSharedPtr<FJsonValue>& Member : *MemberValues)
				{
					FString MemberId;
					if (Member.IsValid() && Member->TryGetString(MemberId) && !MemberId.IsEmpty())
					{
						Members.push_back(StdOf(MemberId));
					}
				}
			}
			TouchParty(StdOf(Pair.Key));
			Registry.PutParty(StdOf(Pair.Key), Members, StdOf(LeaderId));
			TouchParty(StdOf(Pair.Key));
		}
	}

	const TSharedPtr<FJsonObject>* Players = nullptr;
	if (Object->TryGetObjectField(TEXT("players"), Players) && Players != nullptr && Players->IsValid())
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Players)->Values)
		{
			const TSharedPtr<FJsonObject>* PlayerObject = nullptr;
			if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(PlayerObject) || PlayerObject == nullptr || !PlayerObject->IsValid())
			{
				continue;
			}
			const std::string PlayerId = StdOf(Pair.Key);
			Touched.Add(Pair.Key);
			FString PartyId;
			(*PlayerObject)->TryGetStringField(TEXT("party_id"), PartyId);
			if (PartyId.IsEmpty())
			{
				// In no party (left from another hub, dropped while away): the party here forgets them.
				if (const Crossguard::FParty* Old = Registry.PartyOf(PlayerId))
				{
					TouchParty(Old->Id);
				}
				Registry.DropMember(PlayerId);
			}
			// Their inbox is the backend's.
			Registry.DeclineAll(PlayerId);
			const TArray<TSharedPtr<FJsonValue>>* InviteValues = nullptr;
			if ((*PlayerObject)->TryGetArrayField(TEXT("invites"), InviteValues) && InviteValues != nullptr)
			{
				for (const TSharedPtr<FJsonValue>& InviteValue : *InviteValues)
				{
					const TSharedPtr<FJsonObject>* InviteObject = nullptr;
					if (!InviteValue.IsValid() || !InviteValue->TryGetObject(InviteObject) || InviteObject == nullptr || !InviteObject->IsValid())
					{
						continue;
					}
					FString InvitingParty;
					FString InvitedBy;
					double ExpiresAtMs = 0.0;
					(*InviteObject)->TryGetStringField(TEXT("party_id"), InvitingParty);
					(*InviteObject)->TryGetStringField(TEXT("invited_by"), InvitedBy);
					(*InviteObject)->TryGetNumberField(TEXT("expires_at_ms"), ExpiresAtMs);
					// The backend's clock -> this process's monotonic one.
					Crossguard::FPartyInvite Invite;
					Invite.PlayerId = PlayerId;
					Invite.InvitedBy = StdOf(InvitedBy);
					Invite.ExpiresAt = Clock + FMath::Max(0.0, (ExpiresAtMs - NowMs) / 1000.0);
					Registry.PutInvite(StdOf(InvitingParty), Invite);
				}
			}
		}
	}

	const UWorld* World = GetServerWorld();
	for (const FString& PlayerId : Touched)
	{
		if (ACrossguardPlayerState* Online = FindOnline(World, PlayerId))
		{
			SyncPlayer(Online);
		}
	}
	return Result;
}

// --- Views and teams ------------------------------------------------------------------------------

void UCrossguardPartySubsystem::SyncParty(const std::string& PartyId, ACrossguardPlayerState* Extra)
{
	UWorld* World = GetGameInstance() != nullptr ? GetGameInstance()->GetWorld() : nullptr;
	const AGameStateBase* GameState = World != nullptr ? World->GetGameState() : nullptr;
	if (GameState != nullptr && !PartyId.empty())
	{
		for (APlayerState* PlayerState : GameState->PlayerArray)
		{
			ACrossguardPlayerState* Player = Cast<ACrossguardPlayerState>(PlayerState);
			const Crossguard::FParty* Party = Player != nullptr ? Registry.PartyOf(CrossguardPartyLocal::StdOf(CrossguardPartyLocal::IdOf(Player))) : nullptr;
			if (Party != nullptr && Party->Id == PartyId && Player != Extra)
			{
				SyncPlayer(Player);
			}
		}
	}
	if (Extra != nullptr)
	{
		SyncPlayer(Extra);
	}
}

void UCrossguardPartySubsystem::SyncPlayer(ACrossguardPlayerState* PlayerState)
{
	if (PlayerState == nullptr)
	{
		return;
	}
	ACrossguardGameMode* GameMode = PlayerState->GetWorld()->GetAuthGameMode<ACrossguardGameMode>();
	AController* Controller = Cast<AController>(PlayerState->GetOwner());
	// A raid never changes teams (the backend's answers may arrive during one): the view only.
	if (GameMode != nullptr && Controller != nullptr && !GameMode->bIsRaid)
	{
		// Party id = ?Party= key = team: members are allies at once; out of a party, a team of one.
		GameMode->SetPlayerParty(Controller, PartyKeyOf(CrossguardPartyLocal::IdOf(PlayerState)));
	}
	RefreshPlayer(PlayerState);
}

void UCrossguardPartySubsystem::RefreshPlayer(ACrossguardPlayerState* PlayerState)
{
	UCrossguardPartyComponent* View = PlayerState != nullptr ? PlayerState->GetParty() : nullptr;
	if (View == nullptr)
	{
		return;
	}
	const FString PlayerId = CrossguardPartyLocal::IdOf(PlayerState);
	DisplayNames.Add(PlayerId, PlayerState->GetPlayerName());
	const auto NameOf = [this, PlayerState](const std::string& NamedId)
	{
		const FString Key = CrossguardPartyLocal::TextOf(NamedId);
		if (const ACrossguardPlayerState* Online = FindOnline(PlayerState->GetWorld(), Key))
		{
			return Online->GetPlayerName();
		}
		const FString* Seen = DisplayNames.Find(Key);
		return Seen != nullptr ? *Seen : Key;
	};

	const std::string Id = CrossguardPartyLocal::StdOf(PlayerId);
	const Crossguard::FParty* Party = Registry.PartyOf(Id);
	TArray<FCrossguardPartyMemberView> Members;
	if (Party != nullptr)
	{
		for (const std::string& Member : Party->Members)
		{
			FCrossguardPartyMemberView& Out = Members.AddDefaulted_GetRef();
			Out.PlayerId = CrossguardPartyLocal::TextOf(Member);
			Out.DisplayName = NameOf(Member);
			Out.bLeader = Member == Party->LeaderId;
		}
	}
	TArray<FCrossguardPartyInviteView> Invites;
	const double Clock = Now();
	for (const Crossguard::FParty* Inviting : Registry.InvitesFor(Id, Clock))
	{
		FCrossguardPartyInviteView& Out = Invites.AddDefaulted_GetRef();
		Out.PartyId = CrossguardPartyLocal::TextOf(Inviting->Id);
		Out.MemberCount = static_cast<int32>(Inviting->Members.size());
		for (const Crossguard::FPartyInvite& Pending : Inviting->Invites)
		{
			if (Pending.PlayerId == Id)
			{
				Out.InvitedBy = NameOf(Pending.InvitedBy);
				Out.InvitedById = CrossguardPartyLocal::TextOf(Pending.InvitedBy);
				Out.SecondsLeft = static_cast<float>(FMath::Max(0.0, Pending.ExpiresAt - Clock));
			}
		}
	}
	View->SetView(Party != nullptr ? CrossguardPartyLocal::TextOf(Party->Id) : FString(), Party != nullptr ? CrossguardPartyLocal::TextOf(Party->LeaderId) : FString(),
		Party != nullptr && Party->LeaderId == Id, MoveTemp(Members), MoveTemp(Invites));
}
