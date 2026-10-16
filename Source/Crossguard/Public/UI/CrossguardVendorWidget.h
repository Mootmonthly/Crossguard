#pragma once

#include "CoreMinimal.h"
#include "Items/CrossguardInventoryComponent.h"
#include "UI/CrossguardWidgetBase.h"

#include "CrossguardVendorWidget.generated.h"

class UCrossguardItemGridWidget;
class UCrossguardUIButton;
class UCrossguardVendorComponent;
class UTextBlock;
class UVerticalBox;

// The vendor screen (placeholder, docs/ui.md): the character's inventory grids, a box to drop items
// on to sell them, the sell list (every inventory stack with what it sells for and a Sell button,
// greyed for what vendors do not buy), "Sell all gemstones" and the banked gold. A whole stack sells
// at once. Vendors only buy (selling to players is not ruled, so there is no buy list). Opened by F
// on a vendor NPC in the hub (ACrossguardVendorNPC) or the OpenVendor console command. Every sale is a
// UCrossguardVendorComponent::RequestSell; the server prices and checks it (between raids only).
UCLASS()
class CROSSGUARD_API UCrossguardVendorWidget : public UCrossguardWindowWidget
{
	GENERATED_BODY()

public:
	UCrossguardVendorWidget(const FObjectInitializer& ObjectInitializer);

	// Sell list rows after the last Refresh (one per inventory stack), and their text
	// ("Ruby x3 (Common): 150 gold").
	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	int32 GetSellRowCount() const { return SellRowLabels.Num(); }

	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	FString GetSellRowLabel(int32 Index) const { return SellRowLabels.IsValidIndex(Index) ? SellRowLabels[Index] : FString(); }

	// Gold every gemstone stack in the inventory sells for (the "Sell all gemstones" button's sum).
	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	int32 GetGemstonesValue() const { return GemstonesValue; }

	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	FString GetGoldLine() const { return GoldLine; }

	// Item cells in the inventory, Backpack and Quiver grids after the last Refresh.
	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	int32 GetInventoryCellCount() const;

	// The Sell button of an inventory stack, callable directly (tests, gamepad later).
	UFUNCTION(BlueprintCallable, Category = "Crossguard|UI")
	void SellItem(int32 InventoryItemId);

	// Sells every gemstone (Trinket) stack in the inventory, one request per stack.
	UFUNCTION(BlueprintCallable, Category = "Crossguard|UI")
	void SellAllGemstones();

	// An item dragged onto the sell box (from the inventory grids).
	void DropToSell(const FCrossguardInventoryItem& Item);

	// Shown in the title ("Vendor" by default; the NPC's name when opened by one).
	void SetVendorName(const FString& Name);

	virtual void OnWindowOpened() override;

protected:
	virtual void BuildTree() override;
	virtual void NativeRefresh() override;

private:
	UCrossguardVendorComponent* GetVendor() const;
	UCrossguardInventoryComponent* GetInventory() const;
	FString BuildSignature() const;
	void Redraw();
	void MarkDirty() { LastSignature.Reset(); }

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> VendorTitle;

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
	TObjectPtr<UCrossguardUIButton> SellBox;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> GoldText;

	UPROPERTY(Transient)
	TObjectPtr<UCrossguardUIButton> SellGemsButton;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> SellList;

	FString VendorName = TEXT("Vendor");
	TArray<FString> SellRowLabels;
	int32 GemstonesValue = 0;
	FString GoldLine;
	FString LastSignature;
};
