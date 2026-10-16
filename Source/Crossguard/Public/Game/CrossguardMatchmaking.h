#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "CrossguardMatchmaking.generated.h"

class UWorld;

// Matchmaking: the backend service's part in getting players onto servers (docs/hub-and-menu.md,
// docs/parties.md "Raid launch"). Owner's architecture: raids run on dedicated match servers many
// teams join; the hub on dedicated hub servers; the backend (being chosen) places players on both.
//
// The backend module does not exist yet: the default is FCrossguardNoMatchmaking ("not available"),
// and in development builds FCrossguardDevMatchmaking, which fakes an assignment from the hub game
// mode's RaidServerAddress (-RaidServer=). Only the server asks.

// A party (or a solo player) asking for a raid.
struct FCrossguardMatchRequest
{
	// The party's key (empty: solo).
	FString PartyKey;
	// Every traveller's player id (UCrossguardGuildSubsystem::PlayerIdOf).
	TArray<FString> PlayerIds;
};

enum class ECrossguardMatchStatus : uint8
{
	// ServerAddress (and Ticket) hold the match server to travel to.
	Assigned,
	// No matchmaking here (no backend yet, or none configured).
	NotAvailable,
};

struct FCrossguardMatchAssignment
{
	ECrossguardMatchStatus Status = ECrossguardMatchStatus::NotAvailable;
	// The match server ("203.0.113.5:7777").
	FString ServerAddress;
	// The backend's match ticket / session token: the client logs in with ?MatchTicket=<ticket> and
	// the match server checks it with the backend (which says the player's team). Letters, digits,
	// '-' and '_'.
	FString Ticket;
	// Development assignment (no ticket): the client logs in with ?Party=<key> instead, as the
	// development flow always did.
	bool bDevPartyOption = false;
};

class CROSSGUARD_API ICrossguardMatchmaking
{
public:
	virtual ~ICrossguardMatchmaking() = default;

	// Asks for a match server for the request. HubWorld: the hub asking (the development
	// implementation reads its game mode's settings).
	// Assumed: synchronous for now (the development one answers at once); the backend's will answer
	// later (a queue), which turns this into a callback.
	virtual FCrossguardMatchAssignment RequestMatch(const UWorld* HubWorld, const FCrossguardMatchRequest& Request) const = 0;

	// The hub server a player leaving a match goes to (the backend's pick: their party's hub, one with
	// room); empty if none is known. Checked before the match server's own HubServerAddress.
	virtual FString FindHubServer(const FString& PlayerId, const FString& PartyKey) const = 0;

	// For logs: "no backend", "development (-RaidServer=)".
	virtual FString Describe() const = 0;
};

// The backend stub: nothing is available until the backend module exists.
class CROSSGUARD_API FCrossguardNoMatchmaking : public ICrossguardMatchmaking
{
public:
	virtual FCrossguardMatchAssignment RequestMatch(const UWorld* HubWorld, const FCrossguardMatchRequest& Request) const override;
	virtual FString FindHubServer(const FString& PlayerId, const FString& PartyKey) const override { return FString(); }
	virtual FString Describe() const override { return TEXT("no backend"); }
};

// Development builds only (never Shipping): the "assignment" is the hub game mode's RaidServerAddress
// (-RaidServer=<address>), with ?Party=<key> instead of a ticket. No hub assignment (the match
// server's HubServerAddress / -HubServer= applies).
class CROSSGUARD_API FCrossguardDevMatchmaking : public ICrossguardMatchmaking
{
public:
	virtual FCrossguardMatchAssignment RequestMatch(const UWorld* HubWorld, const FCrossguardMatchRequest& Request) const override;
	virtual FString FindHubServer(const FString& PlayerId, const FString& PartyKey) const override { return FString(); }
	virtual FString Describe() const override { return TEXT("development (-RaidServer=)"); }
};

// Holds the matchmaking this game instance uses (lives with the game instance: survives map travel).
UCLASS()
class CROSSGUARD_API UCrossguardMatchmakingSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// The one set (the backend module, tests), else FCrossguardDevMatchmaking in development builds,
	// else FCrossguardNoMatchmaking.
	const ICrossguardMatchmaking& GetMatchmaking() const;
	// Null: back to the default.
	void SetMatchmaking(TSharedPtr<ICrossguardMatchmaking> NewMatchmaking) { Matchmaking = MoveTemp(NewMatchmaking); }

	// The subsystem of World's game instance; null without one.
	static UCrossguardMatchmakingSubsystem* Get(const UWorld* World);

	// Login option carrying the match ticket to a match server (?MatchTicket=<ticket>).
	static const TCHAR* MatchTicketOption;

	// "<Address>?MatchTicket=<Ticket>"; empty if Address is empty or holds URL separators, or the
	// ticket is empty, longer than 256 or not letters, digits, '-' and '_'.
	static FString BuildMatchTravelUrl(const FString& Address, const FString& Ticket);

private:
	TSharedPtr<ICrossguardMatchmaking> Matchmaking;
};
