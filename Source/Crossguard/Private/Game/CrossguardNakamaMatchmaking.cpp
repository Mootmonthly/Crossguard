#include "Game/CrossguardNakamaMatchmaking.h"

#include "Crossguard.h"
#include "Dom/JsonObject.h"
#include "Game/CrossguardNakamaClient.h"
#include "Game/CrossguardNakamaSubsystem.h"
#include "HAL/PlatformTime.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace CrossguardNakamaMatchmakingLocal
{
	const TCHAR* RequestRpc = TEXT("crossguard_match_request");
	const TCHAR* StatusRpc = TEXT("crossguard_match_status");
	const TCHAR* CancelRpc = TEXT("crossguard_match_cancel");
	const TCHAR* HubFindRpc = TEXT("crossguard_hub_find");

	FCrossguardMatchAssignment FailedAssignment(const FString& Error)
	{
		FCrossguardMatchAssignment Assignment;
		Assignment.Status = ECrossguardMatchStatus::Failed;
		Assignment.Error = Error;
		return Assignment;
	}
}

FCrossguardNakamaMatchmaking::FCrossguardNakamaMatchmaking(UCrossguardNakamaSubsystem* InNakama)
	: Nakama(InNakama)
{
}

FString FCrossguardNakamaMatchmaking::BuildRequestPayload(const FCrossguardMatchRequest& Request, const FString& HubServerId)
{
	FString Users;
	for (const FString& PlayerId : Request.PlayerIds)
	{
		Users += (Users.IsEmpty() ? TEXT("") : TEXT(",")) + FCrossguardNakamaClient::Quote(PlayerId);
	}
	return FString::Printf(TEXT("{\"party_id\":%s,\"user_ids\":[%s],\"server_id\":%s}"), *FCrossguardNakamaClient::Quote(Request.PartyKey), *Users,
		*FCrossguardNakamaClient::Quote(HubServerId));
}

bool FCrossguardNakamaMatchmaking::ParseStatus(const FString& Body, FString& OutState, FString& OutRequestId, FCrossguardMatchAssignment& OutAssignment)
{
	TSharedPtr<FJsonObject> Object;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Object) || !Object.IsValid()
		|| !Object->TryGetStringField(TEXT("state"), OutState) || !Object->TryGetStringField(TEXT("request_id"), OutRequestId))
	{
		return false;
	}
	OutAssignment = FCrossguardMatchAssignment();
	if (OutState != TEXT("assigned"))
	{
		return true;
	}
	const TSharedPtr<FJsonObject>* Tickets = nullptr;
	if (!Object->TryGetStringField(TEXT("address"), OutAssignment.ServerAddress) || OutAssignment.ServerAddress.IsEmpty()
		|| !Object->TryGetObjectField(TEXT("tickets"), Tickets) || Tickets == nullptr || !Tickets->IsValid())
	{
		return false;
	}
	Object->TryGetStringField(TEXT("match_id"), OutAssignment.MatchId);
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Tickets)->Values)
	{
		FString Ticket;
		if (Pair.Value.IsValid() && Pair.Value->TryGetString(Ticket) && !Ticket.IsEmpty())
		{
			OutAssignment.PlayerTickets.Add(Pair.Key, Ticket);
		}
	}
	OutAssignment.Status = ECrossguardMatchStatus::Assigned;
	return true;
}

FString FCrossguardNakamaMatchmaking::ParseHubAddress(const FString& Body)
{
	TSharedPtr<FJsonObject> Object;
	FString Address;
	if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Object) && Object.IsValid())
	{
		Object->TryGetStringField(TEXT("address"), Address);
	}
	return Address;
}

void FCrossguardNakamaMatchmaking::RequestMatch(const UWorld* HubWorld, const FCrossguardMatchRequest& Request, FMatchCallback OnDone)
{
	using namespace CrossguardNakamaMatchmakingLocal;
	const FString SearchKey = Request.SearchKey();
	UCrossguardNakamaSubsystem* Link = Nakama.Get();
	FString Refusal;
	if (Link == nullptr || !Link->IsBackendEnabled())
	{
		Refusal = TEXT("the backend is off on this server");
	}
	else if (SearchKey.IsEmpty() || Searches.Contains(SearchKey))
	{
		Refusal = TEXT("already searching for this party");
	}
	for (const FString& PlayerId : Request.PlayerIds)
	{
		if (Refusal.IsEmpty() && !FCrossguardNakamaClient::IsUserId(PlayerId))
		{
			Refusal = FString::Printf(TEXT("%s has no Nakama login"), *PlayerId);
		}
	}
	if (!Refusal.IsEmpty())
	{
		if (OnDone)
		{
			OnDone(FailedAssignment(Refusal));
		}
		return;
	}
	FSearch& Search = Searches.Add(SearchKey);
	Search.OnDone = MoveTemp(OnDone);
	Search.StartedAt = FPlatformTime::Seconds();
	UE_LOG(LogCrossguard, Log, TEXT("Matchmaking: %s (%d player(s)) queued for a raid"), *SearchKey, Request.PlayerIds.Num());
	TWeakPtr<FCrossguardNakamaMatchmaking> WeakThis = AsShared();
	Link->CallServerRpc(RequestRpc, BuildRequestPayload(Request, Link->GetServerId()), [WeakThis, SearchKey](const FCrossguardNakamaResponse& Response)
	{
		if (const TSharedPtr<FCrossguardNakamaMatchmaking> Self = WeakThis.Pin())
		{
			Self->HandleStatus(SearchKey, Response);
		}
	});
}

void FCrossguardNakamaMatchmaking::HandleStatus(const FString& SearchKey, const FCrossguardNakamaResponse& Response)
{
	using namespace CrossguardNakamaMatchmakingLocal;
	FString State;
	FString RequestId;
	FCrossguardMatchAssignment Assignment;
	const bool bRead = Response.IsOk() && ParseStatus(Response.Body, State, RequestId, Assignment);
	FSearch* Search = Searches.Find(SearchKey);
	if (Search == nullptr)
	{
		// Called off while the answer was on its way: the backend must not keep it queued.
		if (bRead && State == TEXT("queued"))
		{
			SendCancel(RequestId);
		}
		return;
	}
	if (!bRead)
	{
		Finish(SearchKey, FailedAssignment(Response.IsOk() ? FString(TEXT("unexpected answer")) : Response.Describe()));
		return;
	}
	Search->RequestId = RequestId;
	if (State == TEXT("assigned"))
	{
		UE_LOG(LogCrossguard, Log, TEXT("Matchmaking: %s assigned to %s (match %s, %d ticket(s))"), *SearchKey, *Assignment.ServerAddress,
			*Assignment.MatchId, Assignment.PlayerTickets.Num());
		Finish(SearchKey, Assignment);
		return;
	}
	if (State != TEXT("queued"))
	{
		Finish(SearchKey, FailedAssignment(FString::Printf(TEXT("search %s"), *State)));
		return;
	}
	if (FPlatformTime::Seconds() - Search->StartedAt > MaxSearchSeconds)
	{
		SendCancel(RequestId);
		Finish(SearchKey, FailedAssignment(TEXT("no raid found in time")));
		return;
	}
	TWeakPtr<FCrossguardNakamaMatchmaking> WeakThis = AsShared();
	Search->PollHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis, SearchKey](float)
	{
		if (const TSharedPtr<FCrossguardNakamaMatchmaking> Self = WeakThis.Pin())
		{
			Self->Poll(SearchKey);
		}
		return false;
	}), PollSeconds);
}

void FCrossguardNakamaMatchmaking::Poll(const FString& SearchKey)
{
	FSearch* Search = Searches.Find(SearchKey);
	UCrossguardNakamaSubsystem* Link = Nakama.Get();
	if (Search == nullptr)
	{
		return;
	}
	Search->PollHandle.Reset();
	if (Link == nullptr)
	{
		Finish(SearchKey, CrossguardNakamaMatchmakingLocal::FailedAssignment(TEXT("the backend link is gone")));
		return;
	}
	TWeakPtr<FCrossguardNakamaMatchmaking> WeakThis = AsShared();
	Link->CallServerRpc(CrossguardNakamaMatchmakingLocal::StatusRpc,
		FString::Printf(TEXT("{\"request_id\":%s}"), *FCrossguardNakamaClient::Quote(Search->RequestId)),
		[WeakThis, SearchKey](const FCrossguardNakamaResponse& Response)
		{
			if (const TSharedPtr<FCrossguardNakamaMatchmaking> Self = WeakThis.Pin())
			{
				Self->HandleStatus(SearchKey, Response);
			}
		});
}

void FCrossguardNakamaMatchmaking::SendCancel(const FString& RequestId) const
{
	UCrossguardNakamaSubsystem* Link = Nakama.Get();
	if (Link == nullptr || RequestId.IsEmpty())
	{
		return;
	}
	Link->CallServerRpc(CrossguardNakamaMatchmakingLocal::CancelRpc, FString::Printf(TEXT("{\"request_id\":%s}"), *FCrossguardNakamaClient::Quote(RequestId)),
		[](const FCrossguardNakamaResponse& Response) {});
}

void FCrossguardNakamaMatchmaking::CancelMatch(const FString& SearchKey)
{
	FSearch* Search = Searches.Find(SearchKey);
	if (Search == nullptr)
	{
		return;
	}
	// No request id yet: the request's answer sees the search gone and cancels it then.
	SendCancel(Search->RequestId);
	FCrossguardMatchAssignment Cancelled;
	Cancelled.Status = ECrossguardMatchStatus::Cancelled;
	Finish(SearchKey, Cancelled);
}

void FCrossguardNakamaMatchmaking::Finish(const FString& SearchKey, const FCrossguardMatchAssignment& Assignment)
{
	FSearch Search;
	if (!Searches.RemoveAndCopyValue(SearchKey, Search))
	{
		return;
	}
	if (Search.PollHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(Search.PollHandle);
	}
	if (Assignment.Status == ECrossguardMatchStatus::Failed)
	{
		UE_LOG(LogCrossguard, Warning, TEXT("Matchmaking: %s not placed: %s"), *SearchKey, *Assignment.Error);
	}
	if (Search.OnDone)
	{
		Search.OnDone(Assignment);
	}
}

void FCrossguardNakamaMatchmaking::FindHubServer(const FString& PlayerId, const FString& PartyKey, FHubCallback OnDone)
{
	UCrossguardNakamaSubsystem* Link = Nakama.Get();
	if (Link == nullptr || !Link->IsBackendEnabled())
	{
		if (OnDone)
		{
			OnDone(FString());
		}
		return;
	}
	const FString UserId = FCrossguardNakamaClient::IsUserId(PlayerId) ? PlayerId : FString();
	Link->CallServerRpc(CrossguardNakamaMatchmakingLocal::HubFindRpc,
		FString::Printf(TEXT("{\"user_id\":%s,\"party_id\":%s}"), *FCrossguardNakamaClient::Quote(UserId), *FCrossguardNakamaClient::Quote(PartyKey)),
		[OnDone = MoveTemp(OnDone)](const FCrossguardNakamaResponse& Response)
		{
			const FString Address = Response.IsOk() ? ParseHubAddress(Response.Body) : FString();
			if (Address.IsEmpty())
			{
				UE_LOG(LogCrossguard, Warning, TEXT("Matchmaking: no hub server from the backend (%s)"), *Response.Describe());
			}
			if (OnDone)
			{
				OnDone(Address);
			}
		});
}
