#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Templates/Function.h"

#include "CrossguardMatchmaking.generated.h"

class UWorld;

// Matchmaking: the backend service's part in getting players onto servers (docs/hub-and-menu.md,
// docs/parties.md "Raid launch"). Owner's architecture: raids run on dedicated match servers many
// teams join; the hub on dedicated hub servers; the backend (Nakama) places players on both.
//
// Implementations: FCrossguardNakamaMatchmaking (Game/CrossguardNakamaMatchmaking.h; installed when the
// backend is on, UCrossguardNakamaSubsystem::IsBackendEnabled), FCrossguardDevMatchmaking (development
// builds: a fake assignment from the hub game mode's RaidServerAddress, -RaidServer=), and
// FCrossguardNoMatchmaking ("not available"). Only the server asks. Answers are asynchronous: the
// development and stub implementations answer at once (before RequestMatch returns), Nakama when its
// queue has placed the party.

// A party (or a solo player) asking for a raid.
struct FCrossguardMatchRequest
{
	// The party's key (empty: solo).
	FString PartyKey;
	// Every traveller's player id (UCrossguardGuildSubsystem::PlayerIdOf).
	TArray<FString> PlayerIds;

	// What CancelMatch / IsSearching take: the party key, or the solo player's id.
	FString SearchKey() const { return !PartyKey.IsEmpty() ? PartyKey : (PlayerIds.Num() > 0 ? PlayerIds[0] : FString()); }
};

enum class ECrossguardMatchStatus : uint8
{
	// ServerAddress (and the tickets) hold the match server to travel to.
	Assigned,
	// No matchmaking here (no backend, or none configured).
	NotAvailable,
	// The backend could not place the party (no answer, refused, waited too long); Error says why.
	Failed,
	// The search was called off (CancelMatch: the party changed).
	Cancelled,
};

struct FCrossguardMatchAssignment
{
	ECrossguardMatchStatus Status = ECrossguardMatchStatus::NotAvailable;
	// The match server ("203.0.113.5:7777").
	FString ServerAddress;
	// Player id -> the backend's match ticket for them: each client logs in with ?MatchTicket=<ticket>
	// and the match server checks it with the backend, which also says the player's party (their team).
	// Letters, digits, '-' and '_'.
	TMap<FString, FString> PlayerTickets;
	// The backend's match id (logs).
	FString MatchId;
	// Development assignment (no tickets): the client logs in with ?Party=<key> instead, as the
	// development flow always did.
	bool bDevPartyOption = false;
	// Failed: why (logs).
	FString Error;

	FString TicketFor(const FString& PlayerId) const
	{
		const FString* Ticket = PlayerTickets.Find(PlayerId);
		return Ticket != nullptr ? *Ticket : FString();
	}
};

class CROSSGUARD_API ICrossguardMatchmaking
{
public:
	using FMatchCallback = TFunction<void(const FCrossguardMatchAssignment& Assignment)>;
	using FHubCallback = TFunction<void(const FString& HubAddress)>;

	virtual ~ICrossguardMatchmaking() = default;

	// Asks for a match server for the request. HubWorld: the hub asking (the development implementation
	// reads its game mode's settings). OnDone runs exactly once, on the game thread: before this returns
	// (development, no backend) or later (the backend's queue). A second request with the same search
	// key while one runs is answered Failed.
	virtual void RequestMatch(const UWorld* HubWorld, const FCrossguardMatchRequest& Request, FMatchCallback OnDone) = 0;
	// Calls off a running search (its OnDone gets Cancelled). Nothing if none runs.
	virtual void CancelMatch(const FString& SearchKey) {}
	virtual bool IsSearching(const FString& SearchKey) const { return false; }

	// The hub server a player leaving a match goes to (the backend's pick: their party's hub, else one
	// with room); empty if none is known. OnDone runs once on the game thread (at once without a
	// backend). Checked before the match server's own HubServerAddress.
	virtual void FindHubServer(const FString& PlayerId, const FString& PartyKey, FHubCallback OnDone) = 0;

	// For logs: "no backend", "development (-RaidServer=)", "Nakama".
	virtual FString Describe() const = 0;
};

// The stub: nothing is available.
class CROSSGUARD_API FCrossguardNoMatchmaking : public ICrossguardMatchmaking
{
public:
	virtual void RequestMatch(const UWorld* HubWorld, const FCrossguardMatchRequest& Request, FMatchCallback OnDone) override;
	virtual void FindHubServer(const FString& PlayerId, const FString& PartyKey, FHubCallback OnDone) override;
	virtual FString Describe() const override { return TEXT("no backend"); }
};

// Development builds only (never Shipping): the "assignment" is the hub game mode's RaidServerAddress
// (-RaidServer=<address>), with ?Party=<key> instead of a ticket. No hub assignment (the match
// server's HubServerAddress / -HubServer= applies). Answers at once.
class CROSSGUARD_API FCrossguardDevMatchmaking : public ICrossguardMatchmaking
{
public:
	virtual void RequestMatch(const UWorld* HubWorld, const FCrossguardMatchRequest& Request, FMatchCallback OnDone) override;
	virtual void FindHubServer(const FString& PlayerId, const FString& PartyKey, FHubCallback OnDone) override;
	virtual FString Describe() const override { return TEXT("development (-RaidServer=)"); }
};

// Holds the matchmaking this game instance uses (lives with the game instance: survives map travel).
UCLASS()
class CROSSGUARD_API UCrossguardMatchmakingSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// With the backend on (UCrossguardNakamaSubsystem::IsBackendEnabled), installs Nakama's matchmaking.
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// The one set (the backend, tests), else FCrossguardDevMatchmaking in development builds, else
	// FCrossguardNoMatchmaking.
	ICrossguardMatchmaking& GetMatchmaking() const;
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
