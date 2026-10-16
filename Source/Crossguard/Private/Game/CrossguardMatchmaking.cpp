#include "Game/CrossguardMatchmaking.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Game/CrossguardGameMode.h"
#include "Game/CrossguardMenuGameMode.h"

const TCHAR* UCrossguardMatchmakingSubsystem::MatchTicketOption = TEXT("MatchTicket");

FCrossguardMatchAssignment FCrossguardNoMatchmaking::RequestMatch(const UWorld* HubWorld, const FCrossguardMatchRequest& Request) const
{
	return FCrossguardMatchAssignment();
}

FCrossguardMatchAssignment FCrossguardDevMatchmaking::RequestMatch(const UWorld* HubWorld, const FCrossguardMatchRequest& Request) const
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
	return Assignment;
}

const ICrossguardMatchmaking& UCrossguardMatchmakingSubsystem::GetMatchmaking() const
{
	if (Matchmaking.IsValid())
	{
		return *Matchmaking;
	}
#if UE_BUILD_SHIPPING
	static const FCrossguardNoMatchmaking Default;
#else
	static const FCrossguardDevMatchmaking Default;
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
