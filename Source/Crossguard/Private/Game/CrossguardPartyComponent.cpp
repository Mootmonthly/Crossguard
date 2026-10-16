#include "Game/CrossguardPartyComponent.h"

#include "Crossguard.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Game/CrossguardPartySubsystem.h"
#include "Game/CrossguardPlayerState.h"
#include "Net/UnrealNetwork.h"

namespace CrossguardPartyComponentLocal
{
	UCrossguardPartySubsystem* PartiesFor(const UActorComponent& Component)
	{
		const UWorld* World = Component.GetWorld();
		UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
		return GameInstance != nullptr ? GameInstance->GetSubsystem<UCrossguardPartySubsystem>() : nullptr;
	}
}

UCrossguardPartyComponent::UCrossguardPartyComponent()
{
	SetIsReplicatedByDefault(true);
}

void UCrossguardPartyComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// All of it for the owner only: the party id is the raid's ?Party= key, and the roster is the
	// members' business. Who is allied shows through the public team id.
	DOREPLIFETIME_CONDITION(UCrossguardPartyComponent, PartyId, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UCrossguardPartyComponent, LeaderId, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UCrossguardPartyComponent, bIsLeader, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UCrossguardPartyComponent, Members, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UCrossguardPartyComponent, Invites, COND_OwnerOnly);
}

void UCrossguardPartyComponent::BeginPlay()
{
	Super::BeginPlay();
	// Joining (or travelling to) a server: the party it already knows you in (with the backend: the
	// party Nakama has for you, e.g. back from a raid).
	if (GetOwner() != nullptr && GetOwner()->HasAuthority())
	{
		if (UCrossguardPartySubsystem* Subsystem = CrossguardPartyComponentLocal::PartiesFor(*this))
		{
			Subsystem->PlayerArrived(Cast<ACrossguardPlayerState>(GetOwner()));
		}
	}
}

void UCrossguardPartyComponent::SetView(const FString& InPartyId, const FString& InLeaderId, bool bInIsLeader,
	TArray<FCrossguardPartyMemberView>&& InMembers, TArray<FCrossguardPartyInviteView>&& InInvites)
{
	PartyId = InPartyId;
	LeaderId = InLeaderId;
	bIsLeader = bInIsLeader;
	Members = MoveTemp(InMembers);
	Invites = MoveTemp(InInvites);
	StampInviteExpiry();
	// Player states replicate rarely by default: send it now.
	if (AActor* OwningActor = GetOwner())
	{
		OwningActor->ForceNetUpdate();
	}
	OnPartyChanged.Broadcast();
}

void UCrossguardPartyComponent::Refuse(ECrossguardPartyResult Result)
{
	UE_LOG(LogCrossguard, Log, TEXT("Party request from %s refused: %s"), *GetNameSafe(GetOwner()),
		*StaticEnum<ECrossguardPartyResult>()->GetNameStringByValue(static_cast<int64>(Result)));
	ClientRefused(Result);
}

void UCrossguardPartyComponent::ClientRefused_Implementation(ECrossguardPartyResult Result)
{
	OnPartyRefused.Broadcast(Result);
}

void UCrossguardPartyComponent::OnRep_Party()
{
	// Until a party panel exists.
	UE_LOG(LogCrossguard, Display, TEXT("Party: %s, %d members%s, %d invites"), PartyId.IsEmpty() ? TEXT("none") : *PartyId, Members.Num(),
		bIsLeader ? TEXT(" (you lead)") : TEXT(""), Invites.Num());
	StampInviteExpiry();
	OnPartyChanged.Broadcast();
}

void UCrossguardPartyComponent::StampInviteExpiry()
{
	// Real time: the server's invite clock (FPlatformTime) ignores pause and time dilation too.
	const UWorld* World = GetWorld();
	const double Now = World != nullptr ? World->GetRealTimeSeconds() : 0.0;
	InviteExpiresAt.Reset();
	for (const FCrossguardPartyInviteView& Invite : Invites)
	{
		InviteExpiresAt.Add(Invite.PartyId, Now + FMath::Max(0.0f, Invite.SecondsLeft));
	}
}

float UCrossguardPartyComponent::GetInviteSecondsLeft(const FString& InPartyId) const
{
	const double* ExpiresAt = InviteExpiresAt.Find(InPartyId);
	const UWorld* World = GetWorld();
	if (ExpiresAt == nullptr || World == nullptr)
	{
		return 0.0f;
	}
	return static_cast<float>(FMath::Max(0.0, *ExpiresAt - World->GetRealTimeSeconds()));
}

// Every request: run it on the subsystem, refuse with the result if it failed.
#define CROSSGUARD_PARTY_REQUEST(Call) \
	if (UCrossguardPartySubsystem* Subsystem = CrossguardPartyComponentLocal::PartiesFor(*this)) \
	{ \
		const ECrossguardPartyResult Result = Subsystem->Call; \
		if (Result != ECrossguardPartyResult::None) \
		{ \
			Refuse(Result); \
		} \
	}

void UCrossguardPartyComponent::ServerInvite_Implementation(APlayerState* Invitee)
{
	CROSSGUARD_PARTY_REQUEST(Invite(Cast<ACrossguardPlayerState>(GetOwner()), Cast<ACrossguardPlayerState>(Invitee)))
}

void UCrossguardPartyComponent::ServerAccept_Implementation(const FString& InPartyId)
{
	CROSSGUARD_PARTY_REQUEST(Accept(Cast<ACrossguardPlayerState>(GetOwner()), InPartyId))
}

void UCrossguardPartyComponent::ServerDecline_Implementation(const FString& InPartyId)
{
	CROSSGUARD_PARTY_REQUEST(Decline(Cast<ACrossguardPlayerState>(GetOwner()), InPartyId))
}

void UCrossguardPartyComponent::ServerDeclineAll_Implementation()
{
	CROSSGUARD_PARTY_REQUEST(DeclineAll(Cast<ACrossguardPlayerState>(GetOwner())))
}

void UCrossguardPartyComponent::ServerLeave_Implementation()
{
	CROSSGUARD_PARTY_REQUEST(Leave(Cast<ACrossguardPlayerState>(GetOwner())))
}

void UCrossguardPartyComponent::ServerKick_Implementation(const FString& PlayerId)
{
	CROSSGUARD_PARTY_REQUEST(Kick(Cast<ACrossguardPlayerState>(GetOwner()), PlayerId))
}

void UCrossguardPartyComponent::ServerPromote_Implementation(const FString& PlayerId)
{
	CROSSGUARD_PARTY_REQUEST(Promote(Cast<ACrossguardPlayerState>(GetOwner()), PlayerId))
}

void UCrossguardPartyComponent::ServerStartRaid_Implementation()
{
	CROSSGUARD_PARTY_REQUEST(StartRaid(Cast<ACrossguardPlayerState>(GetOwner())))
}

#undef CROSSGUARD_PARTY_REQUEST
