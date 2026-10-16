#pragma once

#include "CoreMinimal.h"
#include "CrossguardRules/Party.h"
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
// server) and only in memory. Docs: docs/parties.md.
UCLASS()
class CROSSGUARD_API UCrossguardPartySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// Inviting while in no party founds one with a fresh id.
	ECrossguardPartyResult Invite(ACrossguardPlayerState* Actor, ACrossguardPlayerState* Invitee);
	ECrossguardPartyResult Accept(ACrossguardPlayerState* Actor, const FString& PartyId);
	ECrossguardPartyResult Decline(ACrossguardPlayerState* Actor, const FString& PartyId);
	// Clears the actor's invite inbox (every party's invite, expired or not). NoInvite if it was empty.
	ECrossguardPartyResult DeclineAll(ACrossguardPlayerState* Actor);
	ECrossguardPartyResult Leave(ACrossguardPlayerState* Actor);
	ECrossguardPartyResult Kick(ACrossguardPlayerState* Actor, const FString& TargetId);
	ECrossguardPartyResult Promote(ACrossguardPlayerState* Actor, const FString& TargetId);
	// Leader (or a player in no party): sends the party to the raid (see PlanRaidTravel). Hub only.
	ECrossguardPartyResult StartRaid(ACrossguardPlayerState* Actor);

	// What StartRaid would do for Actor, without travelling: the mode and the URL (the match server
	// with ?MatchTicket=<ticket>; development: the server address or, dev listen mode only, the raid
	// map, plus ?Party=<id>). Asks matchmaking (UCrossguardMatchmakingSubsystem). A client travel's
	// URL is the shared one: StartRaid adds each traveller's ?DevPlayerId= (development builds). Checks the hub and the leader too: anything but None is StartRaid's
	// refusal.
	ECrossguardPartyResult PlanRaidTravel(ACrossguardPlayerState* Actor, ECrossguardRaidTravel& OutMode, FString& OutUrl) const;

	// Destination plus ?Party=<PartyId> (Crossguard::BuildRaidTravelUrl; ExtraOption e.g. "listen").
	// Empty if Destination is empty or the id is not URL-safe.
	static FString RaidTravelUrl(const FString& Destination, const FString& PartyId, const FString& ExtraOption = FString());

	// The party key of the party PlayerId is in on this server; empty if none.
	FString PartyKeyOf(const FString& PlayerId) const;

	// Rebuilds one player's party view (on joining the server, after changes).
	void RefreshPlayer(ACrossguardPlayerState* PlayerState);

	// The player left this server (logout from the hub): they leave their party, and the rest are
	// refreshed (a leader hands over).
	void PlayerLeftServer(ACrossguardPlayerState* PlayerState);
	// The server is going back to the hub after a raid (ACrossguardGameMode::ReturnToHub): every party
	// member not on this server (World's player states), or in Leaving (sent to the menu), leaves
	// their party as PlayerLeftServer would. Assumed: they left the server during the raid.
	void DropAbsentMembers(const UWorld* World, const TSet<FString>& Leaving);

	// Read-only view of every party (tests, a future backend).
	const Crossguard::FPartyRegistry& GetRegistry() const { return Registry; }

private:
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
};
