#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Items/CrossguardInventoryComponent.h"

#include "CrossguardStashComponent.generated.h"

USTRUCT(BlueprintType)
struct CROSSGUARD_API FCrossguardStashTab
{
	GENERATED_BODY()

	// Container and Position on each item locate it in this tab (Container is unused).
	UPROPERTY(BlueprintReadOnly, Category = "Stash")
	TArray<FCrossguardInventoryItem> Items;
};

// A character's stash: between-raid storage in tabs of 10 x 30 squares. Starts with one tab; more
// are bought with in-game currency (GrantStashTab, once a purchase exists). Lives on the player
// state, so it survives death and travel. Not saved yet.
//
// Server authoritative, between raids only. Items move between the stash and the character's
// inventory (UCrossguardInventoryComponent on the pawn). Skill Gems can be learned from here.
//
// Not built yet: shared stash tabs (subscription; needs accounts and a backend).
UCLASS(ClassGroup = (Crossguard), meta = (BlueprintSpawnableComponent))
class CROSSGUARD_API UCrossguardStashComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCrossguardStashComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;

	UFUNCTION(BlueprintPure, Category = "Stash")
	int32 GetTabCount() const { return Tabs.Num(); }

	UFUNCTION(BlueprintPure, Category = "Stash")
	const TArray<FCrossguardStashTab>& GetTabs() const { return Tabs; }

	// Width x height of every tab.
	UFUNCTION(BlueprintPure, Category = "Stash")
	FIntPoint GetTabSize() const;

	// Server. Adds an empty tab (after the purchase is paid for).
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Stash")
	void GrantStashTab();

	// Server. Seamless travel: copies the tabs from the previous player state's stash.
	void CopyFrom(const UCrossguardStashComponent& Other);

	// Client requests: run on the server (clients forward them). Failures reach the owning client
	// through OnStashRejected.

	// Moves or rotates a stash item, possibly to another tab. Dropping a stack on the top-left
	// square of the same item (same rarity) merges as many as fit.
	UFUNCTION(BlueprintCallable, Category = "Stash")
	void RequestMoveInStash(int32 StashItemId, int32 Tab, FIntPoint Position, bool bRotated);

	// Moves an inventory stack into the stash at a position.
	UFUNCTION(BlueprintCallable, Category = "Stash")
	void RequestStore(int32 InventoryItemId, int32 Tab, FIntPoint Position, bool bRotated);

	// Moves an inventory stack into the first free spot of any tab.
	UFUNCTION(BlueprintCallable, Category = "Stash")
	void RequestStoreAnywhere(int32 InventoryItemId);

	// Moves a stash stack into the inventory (stacking first). Whatever does not fit stays.
	UFUNCTION(BlueprintCallable, Category = "Stash")
	void RequestRetrieve(int32 StashItemId);

	// Right click on a Skill Gem in the stash: learns its skill and consumes the gem.
	UFUNCTION(BlueprintCallable, Category = "Stash")
	void RequestUseStashItem(int32 StashItemId);

	UPROPERTY(BlueprintAssignable, Category = "Stash")
	FCrossguardInventoryChanged OnStashChanged;

	UPROPERTY(BlueprintAssignable, Category = "Stash")
	FCrossguardInventoryRejected OnStashRejected;

protected:
	UFUNCTION(Server, Reliable)
	void ServerMoveInStash(int32 StashItemId, int32 Tab, FIntPoint Position, bool bRotated);

	UFUNCTION(Server, Reliable)
	void ServerStore(int32 InventoryItemId, int32 Tab, FIntPoint Position, bool bRotated, bool bAnywhere);

	UFUNCTION(Server, Reliable)
	void ServerRetrieve(int32 StashItemId);

	UFUNCTION(Server, Reliable)
	void ServerUseStashItem(int32 StashItemId);

	UFUNCTION(Client, Reliable)
	void ClientStashRejected(const FCrossguardInventoryResult& Result);

	UFUNCTION()
	void OnRep_Tabs();

private:
	FCrossguardInventoryResult MoveInStash(int32 StashItemId, int32 Tab, FIntPoint Position, bool bRotated);
	FCrossguardInventoryResult Store(int32 InventoryItemId, int32 Tab, FIntPoint Position, bool bRotated, bool bAnywhere);
	FCrossguardInventoryResult Retrieve(int32 StashItemId);
	FCrossguardInventoryResult UseStashItem(int32 StashItemId);
	void Finish(const FCrossguardInventoryResult& Result);

	// Between raids, with a pawn that has an inventory. Null (and OutError set) otherwise.
	UCrossguardInventoryComponent* GetUsableInventory(ECrossguardInventoryError& OutError) const;
	bool IsServer() const;
	Crossguard::FInventoryGrid BuildGrid(int32 Tab, int32 IgnoreId = 0) const;
	// Tab and index of a stash item; false if there is none.
	bool Locate(int32 StashItemId, int32& OutTab, int32& OutIndex) const;
	// Places a copy of Item at the spot, or anywhere in any tab when bAnywhere. Assigns a stash id.
	bool Insert(FCrossguardInventoryItem Item, int32 Tab, FIntPoint Position, bool bRotated, bool bAnywhere);

	UPROPERTY(ReplicatedUsing = OnRep_Tabs)
	TArray<FCrossguardStashTab> Tabs;

	// Server only. Unique across all tabs.
	int32 NextItemId = 1;
};
