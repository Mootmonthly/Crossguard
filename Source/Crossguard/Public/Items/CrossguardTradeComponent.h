#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "CrossguardRules/Trade.h"
#include "Items/CrossguardInventoryComponent.h"

#include "CrossguardTradeComponent.generated.h"

UENUM(BlueprintType)
enum class ECrossguardTradeState : uint8
{
	None,
	// You asked; waiting for an answer.
	RequestSent,
	// Someone asked you (Partner); answer with RespondToTrade.
	RequestReceived,
	// The trade window is open.
	Open,
};

// Why a trade or request ended (shown to both).
UENUM(BlueprintType)
enum class ECrossguardTradeEnd : uint8
{
	Completed,
	Cancelled,
	Declined,
	RequestExpired,
	TooFar,
	// Someone died or left.
	PartnerGone,
	// The items no longer fit (or moved) when both accepted.
	NoRoom,
};

// What the owning client sees of its trade. Only the two traders receive it.
USTRUCT(BlueprintType)
struct CROSSGUARD_API FCrossguardTradeView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Trade")
	ECrossguardTradeState State = ECrossguardTradeState::None;

	UPROPERTY(BlueprintReadOnly, Category = "Trade")
	TObjectPtr<APawn> Partner;

	// Your offer: inventory item ids (whole stacks) and banked gold.
	UPROPERTY(BlueprintReadOnly, Category = "Trade")
	TArray<int32> MyItemIds;

	UPROPERTY(BlueprintReadOnly, Category = "Trade")
	int32 MyGold = 0;

	// Their offer, as copies of the items (with affixes) so you can inspect them.
	UPROPERTY(BlueprintReadOnly, Category = "Trade")
	TArray<FCrossguardInventoryItem> TheirItems;

	UPROPERTY(BlueprintReadOnly, Category = "Trade")
	int32 TheirGold = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Trade")
	bool bMyLocked = false;

	UPROPERTY(BlueprintReadOnly, Category = "Trade")
	bool bTheirLocked = false;

	UPROPERTY(BlueprintReadOnly, Category = "Trade")
	bool bMyAccepted = false;

	UPROPERTY(BlueprintReadOnly, Category = "Trade")
	bool bTheirAccepted = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCrossguardTradeChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCrossguardTradeEnded, ECrossguardTradeEnd, Reason);

// Player-to-player trading (docs/trading.md; rules: Crossguard::FTradeSession). Ask a nearby
// player, they accept, each puts items (and banked gold, between raids) on the table; changing an
// offer unlocks both; both lock, both accept, and the server swaps everything at once, or nothing
// if it no longer fits. Moving apart, dying or leaving cancels. Server-authoritative; clients call
// the Request functions.
UCLASS(ClassGroup = (Crossguard), meta = (BlueprintSpawnableComponent))
class CROSSGUARD_API UCrossguardTradeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCrossguardTradeComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintPure, Category = "Trade")
	const FCrossguardTradeView& GetTradeView() const { return View; }

	UFUNCTION(BlueprintCallable, Category = "Trade")
	void RequestTrade(APawn* Target);

	UFUNCTION(BlueprintCallable, Category = "Trade")
	void RespondToTrade(bool bAccept);

	// Replaces your offer.
	UFUNCTION(BlueprintCallable, Category = "Trade")
	void RequestSetOffer(const TArray<int32>& ItemIds, int32 Gold);

	UFUNCTION(BlueprintCallable, Category = "Trade")
	void RequestLock(bool bLocked);

	UFUNCTION(BlueprintCallable, Category = "Trade")
	void RequestAccept();

	// Cancels the trade or withdraws / declines the request.
	UFUNCTION(BlueprintCallable, Category = "Trade")
	void RequestCancel();

	UPROPERTY(BlueprintAssignable, Category = "Trade")
	FCrossguardTradeChanged OnTradeChanged;

	UPROPERTY(BlueprintAssignable, Category = "Trade")
	FCrossguardTradeEnded OnTradeEnded;

protected:
	UFUNCTION(Server, Reliable)
	void ServerRequestTrade(APawn* Target);

	UFUNCTION(Server, Reliable)
	void ServerRespondToTrade(bool bAccept);

	UFUNCTION(Server, Reliable)
	void ServerSetOffer(const TArray<int32>& ItemIds, int32 Gold);

	UFUNCTION(Server, Reliable)
	void ServerLock(bool bLocked);

	UFUNCTION(Server, Reliable)
	void ServerAccept();

	UFUNCTION(Server, Reliable)
	void ServerCancel();

	UFUNCTION(Client, Reliable)
	void ClientTradeEnded(ECrossguardTradeEnd Reason);

	UFUNCTION()
	void OnRep_View();

private:
	// Server: both sides end; each owner is told why.
	void EndTrade(ECrossguardTradeEnd Reason);
	void ResetLocal();
	// Server: rebuilds both views from the session (bLog: show it in a listen-server host's log).
	void RefreshViews(bool bLog = true);
	// Server, twice a second while asking or trading: distance, death, expiry.
	void CheckTrade();
	// Server: both accepted.
	void Complete();
	void StartChecks();
	void LogView() const;

	UCrossguardInventoryComponent* GetInventory() const;
	bool IsOwnerAlive() const;
	bool IsInRaid() const;
	int32 GetBankedGold() const;

	UPROPERTY(ReplicatedUsing = OnRep_View)
	FCrossguardTradeView View;

	// Server only.
	TWeakObjectPtr<UCrossguardTradeComponent> Partner;
	// 0: asked, 1: was asked.
	int32 Side = 0;
	TSharedPtr<Crossguard::FTradeSession> Session;
	// Server: the offered stacks as they were offered. If one changes (used up, topped up), both
	// sides unlock, so nobody accepts a deal that changed under them.
	TArray<FCrossguardInventoryItem> OfferedStacks;
	// True if every offered stack is still exactly as offered.
	bool OfferUnchanged() const;
	// Server: unlocks both sides and takes new snapshots.
	void ResetAfterChange();
	double RequestTime = 0.0;
	FTimerHandle CheckTimer;
};
