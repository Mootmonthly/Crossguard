#pragma once

#include "CoreMinimal.h"
#include "Items/CrossguardInventoryComponent.h"
#include "UI/CrossguardWidgetBase.h"

#include "CrossguardItemGridWidget.generated.h"

class UCanvasPanel;
class UCrossguardItemDragOperation;
class USizeBox;

// One item grid (the inventory, Backpack, Quiver or a body / chest): a square per grid square and
// a cell per item over its footprint, coloured by rarity, with its name and stack count (a short name
// in one-square-wide cells; the tooltip has the full name, count and rarity). Clicks go to the
// callbacks; the window that owns it decides what they request. Item cells can be dragged
// (UCrossguardItemDragOperation) and anything dropped on the grid comes back through OnItemDropped
// with the square the dragged item's top-left square landed on.
UCLASS()
class CROSSGUARD_API UCrossguardItemGridWidget : public UCrossguardWidgetBase
{
	GENERATED_BODY()

public:
	UCrossguardItemGridWidget(const FObjectInitializer& ObjectInitializer);

	// Redraws the grid: Size in squares, Items placed by their Position / bRotated / Size.
	void SetGrid(FIntPoint Size, const TArray<FCrossguardInventoryItem>& Items, int32 SelectedItemId,
		const UCrossguardInventoryComponent* Inventory, const UCrossguardEquipmentComponent* Equipment);

	// Item cells drawn by the last SetGrid.
	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	int32 GetItemCellCount() const { return ItemCellCount; }

	// Pixels per grid square.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crossguard|UI")
	float SquareSize = 40.0f;

	// Drags started from this grid say they come from the open body / chest.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crossguard|UI")
	bool bLootGrid = false;

	TFunction<void(const FCrossguardInventoryItem&)> OnItemClicked;
	TFunction<void(const FCrossguardInventoryItem&)> OnItemRightClicked;
	TFunction<void(FIntPoint)> OnSquareClicked;
	// A dragged item was dropped on this grid: the square its top-left would take, and the drag.
	TFunction<void(FIntPoint, const UCrossguardItemDragOperation&)> OnItemDropped;

protected:
	virtual void BuildTree() override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

private:
	UDragDropOperation* StartDrag(const FCrossguardInventoryItem& Item, FIntPoint Footprint, const FString& Label, const FLinearColor& Color,
		const FLinearColor& LabelColor, const FGeometry& CellGeometry, const FPointerEvent& MouseEvent);

private:
	UPROPERTY(Transient)
	TObjectPtr<USizeBox> Frame;

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> Canvas;

	int32 ItemCellCount = 0;
};
