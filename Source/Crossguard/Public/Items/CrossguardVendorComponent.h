#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Items/CrossguardInventoryComponent.h"

#include "CrossguardVendorComponent.generated.h"

UENUM(BlueprintType)
enum class ECrossguardItemSource : uint8
{
	// The character's inventory (on the pawn).
	Inventory,
	Stash,
};

// Server and owning client: Gold is the amount received.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCrossguardSold, int32, Gold);

// Owning client: show the vendor screen. Vendor: the vendor NPC used, or null.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCrossguardVendorOpened, AActor*, Vendor);

// Selling to NPC vendors for banked gold. Lives on the player state; the vendor NPC in the hub is
// only where the player opens it. Between raids only, server authoritative.
//
// A whole stack sells at once: SellValue + SellPerRarity x rarity level per item, from the item
// table; items with no sell value cannot be sold. Gear (weapons, armor, Backpacks, Quivers) sells
// for 5 gold per grid square per rarity step.
UCLASS(ClassGroup = (Crossguard), meta = (BlueprintSpawnableComponent))
class CROSSGUARD_API UCrossguardVendorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCrossguardVendorComponent();

	// Gold the stack would sell for; 0 if vendors do not buy it.
	UFUNCTION(BlueprintPure, Category = "Vendor")
	int32 GetSellPrice(const FCrossguardInventoryItem& Item) const;

	// Sells a whole stack from the inventory or the stash. Failures reach the owning client
	// through OnSellRejected.
	UFUNCTION(BlueprintCallable, Category = "Vendor")
	void RequestSell(ECrossguardItemSource Source, int32 ItemId);

	// Server. A vendor NPC (ACrossguardVendorNPC) was used: between raids the owning client is told to
	// show the vendor screen (OnVendorOpened there); in a raid it is refused (BetweenRaidsOnly, through
	// OnSellRejected). Returns whether it was allowed. Every sale is still checked by RequestSell.
	bool OpenVendorScreen(AActor* Vendor);

	UPROPERTY(BlueprintAssignable, Category = "Vendor")
	FCrossguardVendorOpened OnVendorOpened;

	UPROPERTY(BlueprintAssignable, Category = "Vendor")
	FCrossguardSold OnSold;

	UPROPERTY(BlueprintAssignable, Category = "Vendor")
	FCrossguardInventoryRejected OnSellRejected;

protected:
	UFUNCTION(Server, Reliable)
	void ServerSell(ECrossguardItemSource Source, int32 ItemId);

	UFUNCTION(Client, Reliable)
	void ClientSold(int32 Gold);

	UFUNCTION(Client, Reliable)
	void ClientSellRejected(const FCrossguardInventoryResult& Result);

	UFUNCTION(Client, Reliable)
	void ClientOpenVendor(AActor* Vendor);

private:
	FCrossguardInventoryResult Sell(ECrossguardItemSource Source, int32 ItemId, int32& OutGold);
	UCrossguardInventoryComponent* GetInventory() const;
};
