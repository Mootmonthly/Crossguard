#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"

#include "CrossguardPartyComponent.generated.h"

class APlayerState;

// Mirrors Crossguard::EPartyError (same order), then the game's own refusals.
UENUM(BlueprintType)
enum class ECrossguardPartyResult : uint8
{
	None,
	InvalidPlayer,
	InvalidPartyId,
	PartyIdTaken,
	UnknownParty,
	NotInParty,
	AlreadyInParty,
	TargetNotInParty,
	NotLeader,
	CannotTargetSelf,
	PartyFull,
	AlreadyInvited,
	NoInvite,
	InviteExpired,
	// The game's own:
	// Party changes and raid launches happen in the hub only (game mode bIsRaid off).
	OnlyInHub,
	// No player state to act for.
	NoPlayer,
	// No raid map (for a whole-server party) and no raid server address are set.
	NoRaidDestination,
	// The listen server's host is in the party, but only a whole-server party can travel with it.
	HostInParty,
	// The travel URL could not be built or the travel did not start.
	TravelFailed,
};

USTRUCT(BlueprintType)
struct CROSSGUARD_API FCrossguardPartyMemberView
{
	GENERATED_BODY()

	// Opaque player id (UCrossguardGuildSubsystem::PlayerIdOf): what kick and promote take.
	UPROPERTY(BlueprintReadOnly, Category = "Party")
	FString PlayerId;

	UPROPERTY(BlueprintReadOnly, Category = "Party")
	FString DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "Party")
	bool bLeader = false;
};

USTRUCT(BlueprintType)
struct CROSSGUARD_API FCrossguardPartyInviteView
{
	GENERATED_BODY()

	// What RequestAccept / RequestDecline take.
	UPROPERTY(BlueprintReadOnly, Category = "Party")
	FString PartyId;

	// The inviter's name.
	UPROPERTY(BlueprintReadOnly, Category = "Party")
	FString InvitedBy;

	// The inviter's player id (UCrossguardGuildSubsystem::PlayerIdOf).
	UPROPERTY(BlueprintReadOnly, Category = "Party")
	FString InvitedById;

	UPROPERTY(BlueprintReadOnly, Category = "Party")
	int32 MemberCount = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCrossguardPartyChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCrossguardPartyRefused, ECrossguardPartyResult, Result);

// A player's party, on their player state (docs/parties.md; rules: Crossguard::FPartyRegistry, run
// by UCrossguardPartySubsystem on the server). Everything here replicates to the owner only: a party
// member sees the party through their own component, everyone else sees nothing (allies show through
// the public team id). Calls go to the server; refusals come back through OnPartyRefused. Parties
// live in server memory.
//
// For a party panel: bind OnPartyChanged (fires on the server and the owning client after every
// change) and read the getters; act through the Request* functions.
UCLASS(ClassGroup = (Crossguard), meta = (BlueprintSpawnableComponent))
class CROSSGUARD_API UCrossguardPartyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCrossguardPartyComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;

	UFUNCTION(BlueprintPure, Category = "Party")
	bool IsInParty() const { return !PartyId.IsEmpty(); }

	UFUNCTION(BlueprintPure, Category = "Party")
	bool IsLeader() const { return IsInParty() && bIsLeader; }

	// Also the ?Party= key the party travels with. Empty when in no party.
	UFUNCTION(BlueprintPure, Category = "Party")
	const FString& GetPartyId() const { return PartyId; }

	UFUNCTION(BlueprintPure, Category = "Party")
	const FString& GetLeaderId() const { return LeaderId; }

	// In join order, the founder first; you included.
	UFUNCTION(BlueprintPure, Category = "Party")
	const TArray<FCrossguardPartyMemberView>& GetMembers() const { return Members; }

	// Your invite inbox: open invites to you, one per inviting party. Refreshed on every change to
	// them; one can expire while listed (accepting it is then refused with InviteExpired and it drops
	// off).
	UFUNCTION(BlueprintPure, Category = "Party")
	const TArray<FCrossguardPartyInviteView>& GetInvites() const { return Invites; }

	UPROPERTY(BlueprintAssignable, Category = "Party")
	FCrossguardPartyChanged OnPartyChanged;

	UPROPERTY(BlueprintAssignable, Category = "Party")
	FCrossguardPartyRefused OnPartyRefused;

	// Requests (run on the server). Inviting while in no party founds one, with you as leader.
	UFUNCTION(BlueprintCallable, Category = "Party")
	void RequestInvite(APlayerState* Invitee) { ServerInvite(Invitee); }

	UFUNCTION(BlueprintCallable, Category = "Party")
	void RequestAccept(const FString& InPartyId) { ServerAccept(InPartyId); }

	// Deletes one invite from your inbox (expired or not). Works in a raid too.
	UFUNCTION(BlueprintCallable, Category = "Party")
	void RequestDecline(const FString& InPartyId) { ServerDecline(InPartyId); }

	// Deletes every invite in your inbox. Works in a raid too.
	UFUNCTION(BlueprintCallable, Category = "Party")
	void RequestDeclineAll() { ServerDeclineAll(); }

	UFUNCTION(BlueprintCallable, Category = "Party")
	void RequestLeave() { ServerLeave(); }

	// Leader only. PlayerId from GetMembers().
	UFUNCTION(BlueprintCallable, Category = "Party")
	void RequestKick(const FString& PlayerId) { ServerKick(PlayerId); }

	// Leader only: hands the lead to PlayerId.
	UFUNCTION(BlueprintCallable, Category = "Party")
	void RequestPromote(const FString& PlayerId) { ServerPromote(PlayerId); }

	// Leader (or a player in no party, alone): sends the party to the raid.
	UFUNCTION(BlueprintCallable, Category = "Party")
	void RequestStartRaid() { ServerStartRaid(); }

	// Server: a refusal for the owner.
	void Refuse(ECrossguardPartyResult Result);

	// Server: the view, written by UCrossguardPartySubsystem::RefreshPlayer.
	void SetView(const FString& InPartyId, const FString& InLeaderId, bool bInIsLeader, TArray<FCrossguardPartyMemberView>&& InMembers,
		TArray<FCrossguardPartyInviteView>&& InInvites);

protected:
	UFUNCTION(Server, Reliable)
	void ServerInvite(APlayerState* Invitee);
	UFUNCTION(Server, Reliable)
	void ServerAccept(const FString& InPartyId);
	UFUNCTION(Server, Reliable)
	void ServerDecline(const FString& InPartyId);
	UFUNCTION(Server, Reliable)
	void ServerDeclineAll();
	UFUNCTION(Server, Reliable)
	void ServerLeave();
	UFUNCTION(Server, Reliable)
	void ServerKick(const FString& PlayerId);
	UFUNCTION(Server, Reliable)
	void ServerPromote(const FString& PlayerId);
	UFUNCTION(Server, Reliable)
	void ServerStartRaid();

	UFUNCTION(Client, Reliable)
	void ClientRefused(ECrossguardPartyResult Result);

	UFUNCTION()
	void OnRep_Party();

private:
	UPROPERTY(ReplicatedUsing = OnRep_Party)
	FString PartyId;

	UPROPERTY(ReplicatedUsing = OnRep_Party)
	FString LeaderId;

	UPROPERTY(ReplicatedUsing = OnRep_Party)
	bool bIsLeader = false;

	UPROPERTY(ReplicatedUsing = OnRep_Party)
	TArray<FCrossguardPartyMemberView> Members;

	UPROPERTY(ReplicatedUsing = OnRep_Party)
	TArray<FCrossguardPartyInviteView> Invites;
};
