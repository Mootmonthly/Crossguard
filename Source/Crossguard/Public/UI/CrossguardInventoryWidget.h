#pragma once

#include "CoreMinimal.h"
#include "Items/CrossguardInventoryComponent.h"
#include "UI/CrossguardWidgetBase.h"

#include "CrossguardInventoryWidget.generated.h"

class UCrossguardItemGridWidget;
class UScrollBox;
class UTextBlock;
class UVerticalBox;
class UHorizontalBox;

// The inventory screen (placeholder, docs/ui.md): equipment slots (two weapon sets, six armor
// slots, Backpack, Quiver), the inventory grid and the Backpack / Quiver grids, the selected item's
// actions, hotbar assignment, and, while a body or chest is open, its contents side by side.
// Click an item to select it, then click a square to move it there (or an item of the same kind to
// merge, or an equipment slot to equip it); right click uses, readies or equips. Everything is a
// request to UCrossguardInventoryComponent; the server checks it.
UCLASS()
class CROSSGUARD_API UCrossguardInventoryWidget : public UCrossguardWindowWidget
{
	GENERATED_BODY()

public:
	UCrossguardInventoryWidget(const FObjectInitializer& ObjectInitializer);

	// Item cells in the inventory, Backpack and Quiver grids after the last Refresh.
	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	int32 GetItemCellCount() const;

	// Item cells in the open body / chest after the last Refresh (0 when none is open).
	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	int32 GetLootCellCount() const;

	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	int32 GetSelectedItemId() const { return SelectedItemId; }

	// The clicks, callable directly (tests, gamepad later).
	void ClickItem(const FCrossguardInventoryItem& Item);
	void RightClickItem(const FCrossguardInventoryItem& Item);
	void ClickSquare(ECrossguardInventoryContainer Container, FIntPoint Position);
	// Drag and drop, callable directly. An item dropped with its top-left on Position of one of the
	// player's grids: from the open body / chest it is taken (wherever it fits); from the player's own
	// grids it moves there (onto the top-left of a stack of the same kind: merges), keeping its rotation.
	void DropItem(ECrossguardInventoryContainer Container, FIntPoint Position, const FCrossguardInventoryItem& Item, bool bFromLoot);
	// An item dropped on the open body / chest: put in (nothing for one dragged out of it).
	void DropOnLoot(const FCrossguardInventoryItem& Item, bool bFromLoot);
	// Takes every stack from the open body or chest (one request per stack).
	UFUNCTION(BlueprintCallable, Category = "Crossguard|UI")
	void TakeAll();

	virtual void OnWindowClosed() override;

protected:
	virtual void BuildTree() override;
	virtual void NativeRefresh() override;

private:
	UCrossguardInventoryComponent* GetInventory() const;
	FString BuildSignature() const;
	void Redraw();
	void RedrawEquipment();
	void RedrawActions();
	void Select(int32 ItemId);
	void MarkDirty() { LastSignature.Reset(); }

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> EquipmentList;

	UPROPERTY(Transient)
	TObjectPtr<UCrossguardItemGridWidget> MainGrid;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> BackpackLabel;

	UPROPERTY(Transient)
	TObjectPtr<UCrossguardItemGridWidget> BackpackGrid;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> QuiverLabel;

	UPROPERTY(Transient)
	TObjectPtr<UCrossguardItemGridWidget> QuiverGrid;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SelectedText;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> ActionList;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> LootColumn;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> LootTitle;

	UPROPERTY(Transient)
	TObjectPtr<UCrossguardItemGridWidget> LootGrid;

	int32 SelectedItemId = 0;
	// Rotation for the next move of the selected item.
	bool bPlaceRotated = false;
	FString LastSignature;
};
