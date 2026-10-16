#include "Game/CrossguardPartySubsystem.h"

#include "Crossguard.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Game/CrossguardGameMode.h"
#include "Game/CrossguardGuildSubsystem.h"
#include "Game/CrossguardPlayerState.h"
#include "Game/CrossguardSaveSubsystem.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/Guid.h"

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
	const Crossguard::EPartyError Error = Registry.Accept(CrossguardPartyLocal::StdOf(PartyId), CrossguardPartyLocal::StdOf(CrossguardPartyLocal::IdOf(Actor)), Now());
	if (Error == Crossguard::EPartyError::None)
	{
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
	const Crossguard::FPartyLeaveResult Left = Registry.Leave(CrossguardPartyLocal::StdOf(CrossguardPartyLocal::IdOf(Actor)));
	if (!Left.IsValid())
	{
		return ToResult(Left.Error);
	}
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
	const std::string ActorId = CrossguardPartyLocal::StdOf(CrossguardPartyLocal::IdOf(Actor));
	const Crossguard::FParty* Party = Registry.PartyOf(ActorId);
	const std::string PartyId = Party != nullptr ? Party->Id : std::string();
	const Crossguard::EPartyError Error = Registry.Kick(ActorId, CrossguardPartyLocal::StdOf(TargetId));
	if (Error == Crossguard::EPartyError::None)
	{
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
	const std::string PlayerId = CrossguardPartyLocal::StdOf(CrossguardPartyLocal::IdOf(PlayerState));
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

bool UCrossguardPartySubsystem::JoinReturningParty(ACrossguardPlayerState* PlayerState, const FString& PartyKey, bool bServerOwnPlayer)
{
	if (PlayerState == nullptr || PartyKey.IsEmpty() || !IsInHub(PlayerState))
	{
		return false;
	}
	const std::string Key = CrossguardPartyLocal::StdOf(PartyKey);
	const std::string PlayerId = CrossguardPartyLocal::StdOf(CrossguardPartyLocal::IdOf(PlayerState));
	if (const Crossguard::FParty* Current = Registry.PartyOf(PlayerId))
	{
		// Already in a party here (e.g. this hub kept it): no change.
		return Current->Id == Key;
	}
	Crossguard::EPartyError Error = Crossguard::EPartyError::UnknownParty;
	if (Registry.Find(Key) == nullptr)
	{
		// Only the hub's own player founds it: its own travel URL carried the key.
		if (bServerOwnPlayer)
		{
			Error = Registry.Create(Key, PlayerId);
			if (Error == Crossguard::EPartyError::None)
			{
				ReturningPartyKeys.Add(PartyKey);
			}
		}
	}
	else if (ReturningPartyKeys.Contains(PartyKey))
	{
		Error = Registry.Join(Key, PlayerId);
	}
	if (Error != Crossguard::EPartyError::None)
	{
		UE_LOG(LogCrossguard, Log, TEXT("%s did not rejoin party %s by key (error %d): team only"), *PlayerState->GetPlayerName(), *PartyKey,
			static_cast<int32>(Error));
		return false;
	}
	UE_LOG(LogCrossguard, Log, TEXT("%s is back in party %s (%s)"), *PlayerState->GetPlayerName(), *PartyKey,
		bServerOwnPlayer ? TEXT("leader, hosting the hub") : TEXT("joined by key"));
	DisplayNames.Add(CrossguardPartyLocal::IdOf(PlayerState), PlayerState->GetPlayerName());
	SyncParty(Key);
	return true;
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

ECrossguardPartyResult UCrossguardPartySubsystem::PlanRaidTravel(ACrossguardPlayerState* Actor, ECrossguardRaidTravel& OutMode, FString& OutUrl) const
{
	OutMode = ECrossguardRaidTravel::NoDestination;
	OutUrl.Reset();
	if (Actor == nullptr)
	{
		return ECrossguardPartyResult::NoPlayer;
	}
	if (!IsInHub(Actor))
	{
		return ECrossguardPartyResult::OnlyInHub;
	}
	const std::string ActorId = CrossguardPartyLocal::StdOf(CrossguardPartyLocal::IdOf(Actor));
	if (const Crossguard::EPartyError Error = Registry.CanStartRaid(ActorId); Error != Crossguard::EPartyError::None)
	{
		return ToResult(Error);
	}

	const UWorld* World = Actor->GetWorld();
	const ACrossguardGameMode* GameMode = World->GetAuthGameMode<ACrossguardGameMode>();
	const AGameStateBase* GameState = World->GetGameState();
	const TArray<ACrossguardPlayerState*> Travellers = TravellersFor(Actor);
	const bool bListenServer = World->GetNetMode() == NM_ListenServer;

	Crossguard::FRaidTravelSituation Situation;
	Situation.bWholeServerInParty = GameState != nullptr && GameState->PlayerArray.Num() == Travellers.Num();
	Situation.bHasRaidMap = !GameMode->GetRaidMapName().IsEmpty();
	Situation.bHasRaidServer = !GameMode->GetRaidServerAddress().IsEmpty();
	for (const ACrossguardPlayerState* Traveller : Travellers)
	{
		const APlayerController* Controller = Cast<APlayerController>(Traveller->GetOwner());
		Situation.bListenHostInParty |= bListenServer && Controller != nullptr && Controller->IsLocalController();
	}

	const Crossguard::FParty* Party = Registry.PartyOf(ActorId);
	const FString PartyId = Party != nullptr ? CrossguardPartyLocal::TextOf(Party->Id) : FString();
	OutMode = static_cast<ECrossguardRaidTravel>(static_cast<uint8>(Crossguard::PickRaidTravel(Situation)));
	switch (OutMode)
	{
	case ECrossguardRaidTravel::ServerTravel:
		// A listen server keeps listening on the raid map.
		OutUrl = RaidTravelUrl(GameMode->GetRaidMapName(), PartyId, bListenServer ? TEXT("listen") : TEXT(""));
		break;
	case ECrossguardRaidTravel::ClientTravel:
		OutUrl = RaidTravelUrl(GameMode->GetRaidServerAddress(), PartyId);
		break;
	case ECrossguardRaidTravel::NoDestination:
		return ECrossguardPartyResult::NoRaidDestination;
	case ECrossguardRaidTravel::HostInParty:
		return ECrossguardPartyResult::HostInParty;
	}
	return OutUrl.IsEmpty() ? ECrossguardPartyResult::TravelFailed : ECrossguardPartyResult::None;
}

ECrossguardPartyResult UCrossguardPartySubsystem::StartRaid(ACrossguardPlayerState* Actor)
{
	ECrossguardRaidTravel Mode = ECrossguardRaidTravel::NoDestination;
	FString Url;
	if (const ECrossguardPartyResult Planned = PlanRaidTravel(Actor, Mode, Url); Planned != ECrossguardPartyResult::None)
	{
		return Planned;
	}
	UWorld* World = Actor->GetWorld();
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
		// Seamless travel carries each player state's PartyKey (set when they joined the party) to
		// the raid map, where the game mode puts the party on one team again. Absolute: the hub's
		// own options (?Hub) must not carry over.
		return World->ServerTravel(Url, true) ? ECrossguardPartyResult::None : ECrossguardPartyResult::TravelFailed;
	}
	// Each member's client connects to the raid server with ?Party=<id> in its login URL.
	for (ACrossguardPlayerState* Traveller : Travellers)
	{
		if (APlayerController* Controller = Cast<APlayerController>(Traveller->GetOwner()))
		{
			Controller->ClientTravel(Url, TRAVEL_Absolute);
		}
	}
	return ECrossguardPartyResult::None;
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
	if (GameMode != nullptr && Controller != nullptr)
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
