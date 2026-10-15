#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Items/CrossguardInventoryComponent.h"

#include "CrossguardGuildComponent.generated.h"

// Mirrors Crossguard::EGuildRank.
UENUM(BlueprintType)
enum class ECrossguardGuildRank : uint8
{
	Member,
	Officer,
	Leader,
};

// Mirrors Crossguard::EGuildError (same order), then the game's own refusals.
UENUM(BlueprintType)
enum class ECrossguardGuildResult : uint8
{
	None,
	InvalidPlayer,
	InvalidGuildId,
	GuildIdTaken,
	InvalidName,
	NameTaken,
	UnknownGuild,
	NotInGuild,
	AlreadyInGuild,
	TargetNotInGuild,
	NotPermitted,
	CannotTargetSelf,
	AlreadyRank,
	GuildFull,
	AlreadyInvited,
	NoInvite,
	InviteExpired,
	LeaderMustTransfer,
	InvalidAmount,
	NotEnoughGold,
	GoldOverflow,
	InvalidItem,
	UnknownItem,
	NoRoom,
	InvalidData,
	// The game's own:
	// The bank is used between raids only.
	OnlyBetweenRaids,
	// Depositing more banked gold than you have.
	NotEnoughBankedGold,
	// No such stack in your inventory (or it is in hand).
	NoSuchInventoryItem,
	// Redistributing to a member who is not on this server.
	RecipientOffline,
	// What you (or the recipient) would get does not fit in the inventory.
	NoRoomInInventory,
	// No player state or character to act for.
	NoPlayer,
};

USTRUCT(BlueprintType)
struct CROSSGUARD_API FCrossguardGuildMemberView
{
	GENERATED_BODY()

	// Opaque player id (a Steam id with Steam; see UCrossguardGuildSubsystem::PlayerIdOf).
	UPROPERTY(BlueprintReadOnly, Category = "Guild")
	FString PlayerId;

	// Their name when last seen online.
	UPROPERTY(BlueprintReadOnly, Category = "Guild")
	FString DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "Guild")
	ECrossguardGuildRank Rank = ECrossguardGuildRank::Member;

	UPROPERTY(BlueprintReadOnly, Category = "Guild")
	bool bOnline = false;
};

USTRUCT(BlueprintType)
struct CROSSGUARD_API FCrossguardGuildInviteView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Guild")
	FString GuildId;

	UPROPERTY(BlueprintReadOnly, Category = "Guild")
	FString GuildName;

	UPROPERTY(BlueprintReadOnly, Category = "Guild")
	FString InvitedBy;
};

USTRUCT(BlueprintType)
struct CROSSGUARD_API FCrossguardGuildBankItemView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Guild")
	int32 SlotId = 0;

	// The stack itself (affixes included); its Position / bRotated are its place in the bank grid.
	UPROPERTY(BlueprintReadOnly, Category = "Guild")
	FCrossguardInventoryItem Item;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCrossguardGuildChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCrossguardGuildRefused, ECrossguardGuildResult, Result);

// A player's guild, on their player state (docs/guild-rules.md; rules: Crossguard::FGuildRegistry,
// run by UCrossguardGuildSubsystem on the server). Everyone sees your guild name and rank; only you
// see the roster, your invites and the bank. Calls go to the server; refusals come back through
// OnGuildRefused. Guilds live in server memory until a backend saves them.
UCLASS(ClassGroup = (Crossguard), meta = (BlueprintSpawnableComponent))
class CROSSGUARD_API UCrossguardGuildComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCrossguardGuildComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;

	UFUNCTION(BlueprintPure, Category = "Guild")
	bool IsInGuild() const { return !GuildId.IsEmpty(); }

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Guild")
	FString GuildId;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Guild")
	FString GuildName;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Guild")
	ECrossguardGuildRank Rank = ECrossguardGuildRank::Member;

	// Owner only from here.
	UPROPERTY(ReplicatedUsing = OnRep_Guild, BlueprintReadOnly, Category = "Guild")
	TArray<FCrossguardGuildMemberView> Members;

	UPROPERTY(ReplicatedUsing = OnRep_Guild, BlueprintReadOnly, Category = "Guild")
	TArray<FCrossguardGuildInviteView> Invites;

	UPROPERTY(ReplicatedUsing = OnRep_Guild, BlueprintReadOnly, Category = "Guild")
	int64 BankGold = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Guild, BlueprintReadOnly, Category = "Guild")
	TArray<FCrossguardGuildBankItemView> BankItems;

	UPROPERTY(BlueprintAssignable, Category = "Guild")
	FCrossguardGuildChanged OnGuildChanged;

	UPROPERTY(BlueprintAssignable, Category = "Guild")
	FCrossguardGuildRefused OnGuildRefused;

	// Requests (run on the server).
	UFUNCTION(BlueprintCallable, Category = "Guild")
	void RequestCreate(const FString& Name) { ServerCreate(Name); }

	UFUNCTION(BlueprintCallable, Category = "Guild")
	void RequestInvite(APlayerState* Invitee) { ServerInvite(Invitee); }

	UFUNCTION(BlueprintCallable, Category = "Guild")
	void RequestAcceptInvite(const FString& InGuildId) { ServerAcceptInvite(InGuildId); }

	UFUNCTION(BlueprintCallable, Category = "Guild")
	void RequestDeclineInvite(const FString& InGuildId) { ServerDeclineInvite(InGuildId); }

	UFUNCTION(BlueprintCallable, Category = "Guild")
	void RequestKick(const FString& PlayerId) { ServerKick(PlayerId); }

	UFUNCTION(BlueprintCallable, Category = "Guild")
	void RequestSetRank(const FString& PlayerId, ECrossguardGuildRank NewRank) { ServerSetRank(PlayerId, NewRank); }

	UFUNCTION(BlueprintCallable, Category = "Guild")
	void RequestLeave() { ServerLeave(); }

	// Banked gold in (positive) or out (negative), between raids.
	UFUNCTION(BlueprintCallable, Category = "Guild|Bank")
	void RequestMoveGold(int32 Amount) { ServerMoveGold(Amount); }

	UFUNCTION(BlueprintCallable, Category = "Guild|Bank")
	void RequestDepositItem(int32 InventoryItemId) { ServerDepositItem(InventoryItemId); }

	UFUNCTION(BlueprintCallable, Category = "Guild|Bank")
	void RequestWithdrawItem(int32 SlotId) { ServerWithdrawItem(SlotId); }

	// Officers: bank gold or an item to a member on this server.
	UFUNCTION(BlueprintCallable, Category = "Guild|Bank")
	void RequestGiveGold(const FString& PlayerId, int32 Amount) { ServerGiveGold(PlayerId, Amount); }

	UFUNCTION(BlueprintCallable, Category = "Guild|Bank")
	void RequestGiveItem(const FString& PlayerId, int32 SlotId) { ServerGiveItem(PlayerId, SlotId); }

	// Server: a refusal for the owner.
	void Refuse(ECrossguardGuildResult Result);

protected:
	UFUNCTION(Server, Reliable)
	void ServerCreate(const FString& Name);
	UFUNCTION(Server, Reliable)
	void ServerInvite(APlayerState* Invitee);
	UFUNCTION(Server, Reliable)
	void ServerAcceptInvite(const FString& InGuildId);
	UFUNCTION(Server, Reliable)
	void ServerDeclineInvite(const FString& InGuildId);
	UFUNCTION(Server, Reliable)
	void ServerKick(const FString& PlayerId);
	UFUNCTION(Server, Reliable)
	void ServerSetRank(const FString& PlayerId, ECrossguardGuildRank NewRank);
	UFUNCTION(Server, Reliable)
	void ServerLeave();
	UFUNCTION(Server, Reliable)
	void ServerMoveGold(int32 Amount);
	UFUNCTION(Server, Reliable)
	void ServerDepositItem(int32 InventoryItemId);
	UFUNCTION(Server, Reliable)
	void ServerWithdrawItem(int32 SlotId);
	UFUNCTION(Server, Reliable)
	void ServerGiveGold(const FString& PlayerId, int32 Amount);
	UFUNCTION(Server, Reliable)
	void ServerGiveItem(const FString& PlayerId, int32 SlotId);

	UFUNCTION(Client, Reliable)
	void ClientRefused(ECrossguardGuildResult Result);

	UFUNCTION()
	void OnRep_Guild();
};
