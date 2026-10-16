#pragma once

#include "CoreMinimal.h"
#include "Items/CrossguardInventoryComponent.h"
#include "UI/CrossguardWidgetBase.h"

#include "CrossguardItemGridWidget.generated.h"

class UCanvasPanel;
class USizeBox;

// One item grid (the inventory, Backpack, Quiver or a body / chest): a square per grid square and
// a cell per item over its footprint, coloured by rarity, with its name and stack count. Clicks go
// to the callbacks; the window that owns it decides what they request.
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
	float SquareSize = 34.0f;

	TFunction<void(const FCrossguardInventoryItem&)> OnItemClicked;
	TFunction<void(const FCrossguardInventoryItem&)> OnItemRightClicked;
	TFunction<void(FIntPoint)> OnSquareClicked;

protected:
	virtual void BuildTree() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<USizeBox> Frame;

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> Canvas;

	int32 ItemCellCount = 0;
};
