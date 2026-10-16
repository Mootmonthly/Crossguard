#pragma once

#include "CoreMinimal.h"
#include "Items/CrossguardInventoryComponent.h"
#include "UI/CrossguardWidgetBase.h"

#include "CrossguardStashWidget.generated.h"

class UCrossguardItemDragOperation;
class UCrossguardItemGridWidget;
class UCrossguardStashComponent;
class UCrossguardUIButton;
class UHorizontalBox;
class UTextBlock;
class UVerticalBox;

// The stash screen (placeholder, docs/ui.md): the character's inventory grids beside one stash tab
// (10 x 30), the tab buttons, "Buy tab" (3000 banked gold, greyed when it cannot be bought), and the
// selected item's actions (take, store, learn a Skill Gem). Items drag both ways: inventory to a
// stash square, stash to the inventory (it goes wherever it fits, stacking first), and within the
// stash. Opened by F on a stash chest in the hub (ACrossguardStashChest) or the OpenStash console
// command. Everything is a request to UCrossguardStashComponent / UCrossguardInventoryComponent; the
// server checks it (between raids only).
UCLASS()
class CROSSGUARD_API UCrossguardStashWidget : public UCrossguardWindowWidget
{
	GENERATED_BODY()

public:
	UCrossguardStashWidget(const FObjectInitializer& ObjectInitializer);

	// The stash tab shown (0-based).
	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	int32 GetShownTab() const { return ShownTab; }

	UFUNCTION(BlueprintCallable, Category = "Crossguard|UI")
	void ShowTab(int32 Tab);

	// Tab buttons after the last Refresh (one per tab the stash has).
	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	int32 GetTabButtonCount() const { return TabButtonCount; }

	// Squares of the shown tab's grid (10 x 30) after the last Refresh.
	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	FIntPoint GetShownTabSize() const { return ShownTabSize; }

	// Item cells in the shown tab after the last Refresh.
	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	int32 GetStashCellCount() const;

	// Item cells in the inventory, Backpack and Quiver grids after the last Refresh.
	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	int32 GetInventoryCellCount() const;

	// "Buy tab": active only when a tab can be bought now (gold, cap, between raids). A hint; the
	// server decides.
	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	bool IsBuyTabActive() const;

	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	FString GetBuyTabLabel() const;

	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	FString GetGoldLine() const { return GoldLine; }

	// The buy-tab button's click.
	UFUNCTION(BlueprintCallable, Category = "Crossguard|UI")
	void BuyTab();

	// Drag and drop, callable directly (tests, gamepad later). An item dropped with its top-left on
	// Position of the shown stash tab: from the inventory it is stored there, from the stash it moves
	// there (onto the top-left of a stack of the same kind: merges). Keeps the item's rotation.
	void DropOnStash(FIntPoint Position, const FCrossguardInventoryItem& Item, bool bFromStash);
	// An item dropped on one of the inventory grids: from the stash it is taken (wherever it fits; the
	// server has no "take to this square"), from the inventory it moves there.
	void DropOnInventory(ECrossguardInventoryContainer Container, FIntPoint Position, const FCrossguardInventoryItem& Item, bool bFromStash);
	// Right click: on a stash Skill Gem learns it, on another stash stack takes it; on an inventory
	// Skill Gem learns it, on another inventory stack stores it in the first free stash spot.
	void RightClickItem(const FCrossguardInventoryItem& Item, bool bInStash);
	// Click: selects (again: deselects). With an item selected, clicking a square moves it there.
	void ClickItem(const FCrossguardInventoryItem& Item, bool bInStash);

	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	int32 GetSelectedItemId() const { return SelectedItemId; }

	virtual void OnWindowOpened() override;
	virtual void OnWindowClosed() override;

protected:
	virtual void BuildTree() override;
	virtual void NativeRefresh() override;

private:
	UCrossguardStashComponent* GetStash() const;
	UCrossguardInventoryComponent* GetInventory() const;
	FString BuildSignature() const;
	void Redraw();
	void RedrawTabs();
	void RedrawActions();
	void ClickSquare(bool bStashSquare, ECrossguardInventoryContainer Container, FIntPoint Position);
	void Select(int32 ItemId, bool bInStash);
	// Whether a drag started on this window's stash grid (else one of its inventory grids).
	bool IsFromStash(const UCrossguardItemDragOperation& Drag) const;
	void MarkDirty() { LastSignature.Reset(); }

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
	TObjectPtr<UCrossguardItemGridWidget> StashGrid;

	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> TabRow;

	UPROPERTY(Transient)
	TObjectPtr<UCrossguardUIButton> BuyTabButton;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> GoldText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SelectedText;

	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> ActionRow;

	int32 ShownTab = 0;
	int32 TabButtonCount = 0;
	FIntPoint ShownTabSize = FIntPoint::ZeroValue;
	int32 SelectedItemId = 0;
	// The selection is a stash stack (else an inventory one): ids of the two are separate.
	bool bSelectedInStash = false;
	FString GoldLine;
	FString LastSignature;
};
