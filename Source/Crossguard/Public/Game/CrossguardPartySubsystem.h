#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "CrossguardRules/Party.h"
#include "Game/CrossguardMatchmaking.h"
#include "Game/CrossguardPartyComponent.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "CrossguardPartySubsystem.generated.h"

class ACrossguardPlayerState;

// How UCrossguardPartySubsystem would send a party to the raid (mirrors Crossguard::ERaidTravel).
UENUM(BlueprintType)
enum class ECrossguardRaidTravel : uint8
{
	// DEV-ONLY (listen server): the whole server loads the raid map (UWorld::ServerTravel).
	ServerTravel,
	// Each member's client travels to the match server matchmaking assigned (ClientTravel).
	ClientTravel,
	NoDestination,
	HostInParty,
};

// Server: every party (Crossguard::FPartyRegistry). Players act through their
// UCrossguardPartyComponent; after each change the subsystem puts every member on the party's team
// (ACrossguardGameMode::SetPlayerParty: party id -> ?Party= key -> team, so members are allies at
// once) and refreshes their views. Lives with the game instance (survives map travel on this
// server). Docs: docs/parties.md.
//
// Two modes:
// - In memory (default: development, PIE, tests): the registry here is the parties; gone when the
//   process stops.
// - Backend (UCrossguardNakamaSubsystem::IsBackendEnabled, docs/parties.md "On Nakama"): Nakama holds the
//   parties (Backend/nakama/modules/parties.go, the same rules) so they survive hub -> raid -> hub and are
//   the same on every hub shard. This server checks its own rules (hub only, a player state), sends the
//   request (crossguard_party) and returns None ("sent"); the answer updates the registry here, a mirror
//   of the parties of the players on this server, and a refusal comes back later through
//   OnPartyRefused. The mirror refreshes when a player arrives and every PartyRefreshSeconds. Players
//   need a Nakama login (a verified user id); others are refused (NoPlayer).
//
// The raid launch asks matchmaking (asynchronous with the backend: "Looking for a raid...", then the
// travel or a refusal).
UCLASS()
class CROSSGUARD_API UCrossguardPartySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// Backend mode: how often the parties of everyone on this server are read again (placeholder).
	static constexpr float PartyRefreshSeconds = 5.0f;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Parties live in Nakama (the backend is on).
	bool UsesBackend() const { return bBackend; }

	// Inviting while in no party founds one with a fresh id.
	ECrossguardPartyResult Invite(ACrossguardPlayerState* Actor, ACrossguardPlayerState* Invitee);
	ECrossguardPartyResult Accept(ACrossguardPlayerState* Actor, const FString& PartyId);
	ECrossguardPartyResult Decline(ACrossguardPlayerState* Actor, const FString& PartyId);
	// Clears the actor's invite inbox (every party's invite, expired or not). NoInvite if it was empty.
	ECrossguardPartyResult DeclineAll(ACrossguardPlayerState* Actor);
	ECrossguardPartyResult Leave(ACrossguardPlayerState* Actor);
	ECrossguardPartyResult Kick(ACrossguardPlayerState* Actor, const FString& TargetId);
	ECrossguardPartyResult Promote(ACrossguardPlayerState* Actor, const FString& TargetId);
	// Leader (or a player in no party): sends the party to the raid. Hub only. Asks matchmaking; an answer
	// that comes at once (development, no backend) travels before this returns and its refusal is the
	// result; the backend's answers later ("Looking for a raid..." on the party's HUD; None now, the
	// travel or a refusal through OnPartyRefused then). AlreadySearching while a search runs.
	ECrossguardPartyResult StartRaid(ACrossguardPlayerState* Actor);

	// What StartRaid would do for Actor, without travelling: the mode and Actor's URL (the match server
	// with ?MatchTicket=<their ticket>; development: the server address or, dev listen mode only, the
	// raid map, plus ?Party=<id>). Asks matchmaking; only an answer that comes at once is planned (tests,
	// development): a backend search is called off and NoRaidDestination returned. A client travel's
	// URL gets each traveller's ?DevPlayerId= in StartRaid (development builds). Checks the hub and the
	// leader too: anything but None is StartRaid's refusal.
	ECrossguardPartyResult PlanRaidTravel(ACrossguardPlayerState* Actor, ECrossguardRaidTravel& OutMode, FString& OutUrl) const;
	// The plan for a given matchmaking answer (PlanRaidTravel's second half).
	ECrossguardPartyResult PlanRaidTravelWith(ACrossguardPlayerState* Actor, const FCrossguardMatchAssignment& Assignment, ECrossguardRaidTravel& OutMode,
		FString& OutUrl) const;

	// Destination plus ?Party=<PartyId> (Crossguard::BuildRaidTravelUrl; ExtraOption e.g. "listen").
	// Empty if Destination is empty or the id is not URL-safe.
	static FString RaidTravelUrl(const FString& Destination, const FString& PartyId, const FString& ExtraOption = FString());

	// The party key of the party PlayerId is in on this server; empty if none.
	FString PartyKeyOf(const FString& PlayerId) const;

	// Rebuilds one player's party view (after changes).
	void RefreshPlayer(ACrossguardPlayerState* PlayerState);
	// The player is on this server (their party component's BeginPlay): their view; with the backend,
	// their party is read from Nakama first (a team back from a raid gets its party again).
	void PlayerArrived(ACrossguardPlayerState* PlayerState);

	// Backend mode: applies a crossguard_party / crossguard_party_get answer to the registry here and
	// brings every affected player on this server up to date. Returns the answer's "result" (an
	// EPartyError name; empty for a get, "<unreadable>" for garbage). Public for tests.
	FString ApplyBackendState(const FString& Body);
	// "None", "NotLeader", ... -> the result (unknown names: BackendUnavailable).
	static ECrossguardPartyResult ResultFromName(const FString& Name);

	// The player left this server (logout from the hub): they leave their party, and the rest are
	// refreshed (a leader hands over).
	void PlayerLeftServer(ACrossguardPlayerState* PlayerState);
	// The server is going back to the hub after a raid (ACrossguardGameMode::ReturnToHub): every party
	// member not on this server (World's player states), or in Leaving (sent to the menu), leaves
	// their party as PlayerLeftServer would. Assumed: they left the server during the raid.
	void DropAbsentMembers(const UWorld* World, const TSet<FString>& Leaving);

	// Read-only view of every party (tests; with the backend, the mirror).
	const Crossguard::FPartyRegistry& GetRegistry() const { return Registry; }

private:
	// Raid launch: the checks (hub, leader), the request, and the end of an answered search.
	ECrossguardPartyResult CheckRaidLaunch(ACrossguardPlayerState* Actor) const;
	FCrossguardMatchRequest MakeMatchRequest(ACrossguardPlayerState* Actor) const;
	ECrossguardPartyResult FinishRaidLaunch(const FString& ActorId, const FCrossguardMatchAssignment& Assignment);
	// A running raid search for the party (or the solo player) is called off: its membership changed.
	void CancelSearch(const FString& PartyKey, const FString& PlayerId);
	// A refusal that comes after the request returned (an asynchronous answer).
	void RefuseLater(const FString& ActorId, ECrossguardPartyResult Result);
	void ShowToPlayers(const TArray<FString>& PlayerIds, const FString& Message) const;

	// Backend mode.
	ECrossguardPartyResult SendBackendOp(const TCHAR* Op, ACrossguardPlayerState* Actor, const FString& TargetId, const FString& PartyId);
	void SendBackendOpFor(const TCHAR* Op, const FString& ActorId, const FString& TargetId, const FString& PartyId);
	void FetchFromBackend(const TArray<FString>& PlayerIds);
	bool RefreshTick(float DeltaSeconds);
	UWorld* GetServerWorld() const;

	static ECrossguardPartyResult ToResult(Crossguard::EPartyError Error);
	static double Now();
	bool IsInHub(const ACrossguardPlayerState* Actor) const;
	// Every online member of the party (and Extra, e.g. someone who just left) gets their team and
	// view brought up to date.
	void SyncParty(const std::string& PartyId, ACrossguardPlayerState* Extra = nullptr);
	// The player's team follows their party (none: a team of their own), then their view.
	void SyncPlayer(ACrossguardPlayerState* PlayerState);
	ACrossguardPlayerState* FindOnline(const UWorld* World, const FString& PlayerId) const;
	// Server: the players of the actor's party on this server (the actor alone when in no party).
	TArray<ACrossguardPlayerState*> TravellersFor(ACrossguardPlayerState* Actor) const;

	Crossguard::FPartyRegistry Registry;
	// Last seen names, for rosters and invites.
	TMap<FString, FString> DisplayNames;
	bool bBackend = false;
	// Backend mode: players leaving this hub for a raid (their logout is a travel: they stay in the party).
	TSet<FString> RaidTravellerIds;
	FTSTicker::FDelegateHandle RefreshHandle;
};
