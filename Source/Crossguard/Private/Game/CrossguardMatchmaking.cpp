#include "Game/CrossguardMatchmaking.h"

#include "Crossguard.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Game/CrossguardGameMode.h"
#include "Game/CrossguardMenuGameMode.h"
#include "Game/CrossguardNakamaMatchmaking.h"
#include "Game/CrossguardNakamaSubsystem.h"

const TCHAR* UCrossguardMatchmakingSubsystem::MatchTicketOption = TEXT("MatchTicket");

void FCrossguardNoMatchmaking::RequestMatch(const UWorld* HubWorld, const FCrossguardMatchRequest& Request, FMatchCallback OnDone)
{
	if (OnDone)
	{
		OnDone(FCrossguardMatchAssignment());
	}
}

void FCrossguardNoMatchmaking::FindHubServer(const FString& PlayerId, const FString& PartyKey, FHubCallback OnDone)
{
	if (OnDone)
	{
		OnDone(FString());
	}
}

void FCrossguardDevMatchmaking::RequestMatch(const UWorld* HubWorld, const FCrossguardMatchRequest& Request, FMatchCallback OnDone)
{
	FCrossguardMatchAssignment Assignment;
	const ACrossguardGameMode* GameMode = HubWorld != nullptr ? HubWorld->GetAuthGameMode<ACrossguardGameMode>() : nullptr;
	const FString Address = GameMode != nullptr ? GameMode->GetRaidServerAddress() : FString();
	if (!Address.IsEmpty())
	{
		Assignment.Status = ECrossguardMatchStatus::Assigned;
		Assignment.ServerAddress = Address;
		Assignment.bDevPartyOption = true;
	}
	if (OnDone)
	{
		OnDone(Assignment);
	}
}

void FCrossguardDevMatchmaking::FindHubServer(const FString& PlayerId, const FString& PartyKey, FHubCallback OnDone)
{
	if (OnDone)
	{
		OnDone(FString());
	}
}

void UCrossguardMatchmakingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UCrossguardNakamaSubsystem>();
	UCrossguardNakamaSubsystem* Nakama = GetGameInstance()->GetSubsystem<UCrossguardNakamaSubsystem>();
	if (Nakama != nullptr && Nakama->IsBackendEnabled())
	{
		SetMatchmaking(MakeShared<FCrossguardNakamaMatchmaking>(Nakama));
		UE_LOG(LogCrossguard, Log, TEXT("Matchmaking: %s"), *GetMatchmaking().Describe());
	}
}

ICrossguardMatchmaking& UCrossguardMatchmakingSubsystem::GetMatchmaking() const
{
	if (Matchmaking.IsValid())
	{
		return *Matchmaking;
	}
#if UE_BUILD_SHIPPING
	static FCrossguardNoMatchmaking Default;
#else
	static FCrossguardDevMatchmaking Default;
#endif
	return Default;
}

UCrossguardMatchmakingSubsystem* UCrossguardMatchmakingSubsystem::Get(const UWorld* World)
{
	const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
	return GameInstance != nullptr ? GameInstance->GetSubsystem<UCrossguardMatchmakingSubsystem>() : nullptr;
}

FString UCrossguardMatchmakingSubsystem::BuildMatchTravelUrl(const FString& Address, const FString& Ticket)
{
	const FString Destination = ACrossguardMenuGameMode::BuildJoinUrl(Address);
	if (Destination.IsEmpty() || Ticket.IsEmpty() || Ticket.Len() > 256)
	{
		return FString();
	}
	for (const TCHAR Char : Ticket)
	{
		const bool bAscii = Char < 128;
		if (!bAscii || (!FChar::IsAlnum(Char) && Char != TEXT('-') && Char != TEXT('_')))
		{
			return FString();
		}
	}
	return FString::Printf(TEXT("%s?%s=%s"), *Destination, MatchTicketOption, *Ticket);
}
