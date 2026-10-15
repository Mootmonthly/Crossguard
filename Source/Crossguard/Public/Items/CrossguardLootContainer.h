#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Items/CrossguardInventoryComponent.h"

#include "CrossguardLootContainer.generated.h"

class USkeletalMesh;
class USkeletalMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCrossguardLootContentsChanged);

// A grid of items in the world that players open with the interact key and loot: a dead
// character's body (spawned on death with everything they carried), later treasure chests.
//
// Server authoritative. Players take and put items through their own
// UCrossguardInventoryComponent, which checks they are alive and within InteractRange. Contents
// replicate to everyone.
UCLASS()
class CROSSGUARD_API ACrossguardLootContainer : public AActor
{
	GENERATED_BODY()

public:
	ACrossguardLootContainer();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Server. Replaces the contents. The grid is Width squares wide and grows taller until every
	// item fits. Items must already have their Size set (they come from an inventory).
	void InitializeContents(const TArray<FCrossguardInventoryItem>& NewItems);

	UFUNCTION(BlueprintPure, Category = "Loot")
	const TArray<FCrossguardInventoryItem>& GetItems() const { return Items; }

	UFUNCTION(BlueprintPure, Category = "Loot")
	FIntPoint GetGridSize() const { return GridSize; }

	UFUNCTION(BlueprintPure, Category = "Loot")
	bool FindItem(int32 ItemId, FCrossguardInventoryItem& OutItem) const;

	// Server. Count 0 removes the item. False if there is no such item.
	bool SetItemCount(int32 ItemId, int32 Count);

	// Server. Puts the item in the first free spot (rotation allowed). False if it does not fit.
	bool AddItem(const FCrossguardInventoryItem& Item);

	// Server. Empties the container and returns its items.
	TArray<FCrossguardInventoryItem> TakeAllItems();

	// Server. A body: the mesh shown, as a ragdoll, once the dead character itself is gone (e.g.
	// the player left). While the character exists, its own ragdoll is the body.
	void SetBodyMesh(USkeletalMesh* Mesh, const FTransform& RelativeTransform);
	void ShowBodyMesh();

	// How close a player must be to open it and move items, in cm.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "0"))
	float InteractRange = 250.0f;

	// Minimum grid size; a body grows taller to fit everything.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "1"))
	int32 Width = 10;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "1"))
	int32 MinHeight = 5;

	UPROPERTY(BlueprintAssignable, Category = "Loot")
	FCrossguardLootContentsChanged OnContentsChanged;

protected:
	UFUNCTION()
	void OnRep_Contents();

	UFUNCTION()
	void OnRep_BodyMesh();

	UPROPERTY(VisibleAnywhere, Category = "Loot")
	TObjectPtr<USkeletalMeshComponent> BodyMeshComponent;

private:
	Crossguard::FInventoryGrid BuildGrid(FIntPoint Size) const;
	// Places Item in the first free spot of a grid of Size. Does not add it.
	bool FindPlacement(FCrossguardInventoryItem& Item, FIntPoint Size) const;

	UPROPERTY(ReplicatedUsing = OnRep_Contents)
	TArray<FCrossguardInventoryItem> Items;

	UPROPERTY(ReplicatedUsing = OnRep_Contents)
	FIntPoint GridSize = FIntPoint(10, 5);

	UPROPERTY(ReplicatedUsing = OnRep_BodyMesh)
	TObjectPtr<USkeletalMesh> BodyMesh;

	UPROPERTY(ReplicatedUsing = OnRep_BodyMesh)
	FTransform BodyMeshTransform;

	UPROPERTY(ReplicatedUsing = OnRep_BodyMesh)
	bool bShowBodyMesh = false;

	void ApplyBodyMesh();

	// Server only.
	int32 NextItemId = 1;
};
