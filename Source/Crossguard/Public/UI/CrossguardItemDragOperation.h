#pragma once

#include "Blueprint/DragDropOperation.h"
#include "CoreMinimal.h"
#include "Items/CrossguardInventoryComponent.h"

#include "CrossguardItemDragOperation.generated.h"

// An item being dragged from an item grid (UCrossguardItemGridWidget): a copy of the stack, where
// it came from, and which of its squares the mouse grabbed, so a drop places its top-left square
// where the drag visual shows it.
UCLASS()
class CROSSGUARD_API UCrossguardItemDragOperation : public UDragDropOperation
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "Crossguard|UI")
	FCrossguardInventoryItem Item;

	// From the open body or chest (else from the player's own grids).
	UPROPERTY(BlueprintReadOnly, Category = "Crossguard|UI")
	bool bFromLoot = false;

	// The grabbed square, in squares from the item's top-left (its drawn footprint, rotation applied).
	UPROPERTY(BlueprintReadOnly, Category = "Crossguard|UI")
	FIntPoint GrabOffset = FIntPoint::ZeroValue;
};
