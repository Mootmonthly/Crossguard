#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Game/CrossguardMatchmaking.h"
#include "Templates/SharedPointer.h"

class UCrossguardNakamaSubsystem;
struct FCrossguardNakamaResponse;

// Matchmaking on Nakama (Backend/nakama/modules/matchmaking.go; docs/parties.md "Raid launch"),
// installed by UCrossguardMatchmakingSubsystem when the backend is on.
//
// - RequestMatch: crossguard_match_request (party, players, this hub's server id), then
//   crossguard_match_status every PollSeconds until the backend assigns a match server, with one
//   ticket per player. Gives up after MaxSearchSeconds (crossguard_match_cancel; Failed). Players must be
//   Nakama users (a verified login).
// - CancelMatch: crossguard_match_cancel (a party that changed while searching).
// - FindHubServer: crossguard_hub_find (the party's hub first, else the hub with the most room).
class CROSSGUARD_API FCrossguardNakamaMatchmaking : public ICrossguardMatchmaking, public TSharedFromThis<FCrossguardNakamaMatchmaking>
{
public:
	// Placeholders.
	static constexpr float PollSeconds = 2.0f;
	static constexpr double MaxSearchSeconds = 300.0;

	explicit FCrossguardNakamaMatchmaking(UCrossguardNakamaSubsystem* InNakama);

	virtual void RequestMatch(const UWorld* HubWorld, const FCrossguardMatchRequest& Request, FMatchCallback OnDone) override;
	virtual void CancelMatch(const FString& SearchKey) override;
	virtual bool IsSearching(const FString& SearchKey) const override { return Searches.Contains(SearchKey); }
	virtual void FindHubServer(const FString& PlayerId, const FString& PartyKey, FHubCallback OnDone) override;
	virtual FString Describe() const override { return TEXT("Nakama (crossguard_match_request, tickets)"); }

	// --- Pure helpers (tested in CrossguardNakamaTests.cpp) ---

	// The crossguard_match_request payload.
	static FString BuildRequestPayload(const FCrossguardMatchRequest& Request, const FString& HubServerId);
	// Reads a crossguard_match_request / _status answer: state ("queued", "assigned", "cancelled"), the
	// request id, and when assigned the address, match id and every player's ticket. False if unreadable.
	static bool ParseStatus(const FString& Body, FString& OutState, FString& OutRequestId, FCrossguardMatchAssignment& OutAssignment);
	// Reads a crossguard_hub_find answer's address (empty if none).
	static FString ParseHubAddress(const FString& Body);

private:
	struct FSearch
	{
		FString RequestId;
		FMatchCallback OnDone;
		double StartedAt = 0.0;
		FTSTicker::FDelegateHandle PollHandle;
	};

	void HandleStatus(const FString& SearchKey, const FCrossguardNakamaResponse& Response);
	void Poll(const FString& SearchKey);
	// Ends the search and answers its callback.
	void Finish(const FString& SearchKey, const FCrossguardMatchAssignment& Assignment);
	void SendCancel(const FString& RequestId) const;

	TWeakObjectPtr<UCrossguardNakamaSubsystem> Nakama;
	TMap<FString, FSearch> Searches;
};
