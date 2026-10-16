#include "UI/CrossguardVendorWidget.h"

#include "Character/CrossguardCharacter.h"
#include "Components/HorizontalBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Game/CrossguardPlayerState.h"
#include "Items/CrossguardVendorComponent.h"
#include "UI/CrossguardItemDragOperation.h"
#include "UI/CrossguardItemGridWidget.h"
#include "UI/CrossguardUIButton.h"
#include "UI/CrossguardUIHelpers.h"

namespace CrossguardVendorWidgetLocal
{
	const FLinearColor Heading(1.0f, 0.85f, 0.5f);
	const FLinearColor NotBought(0.55f, 0.55f, 0.55f);

	// Gemstones are the Trinket items (Data/Items/Items.json: ruby, sapphire, emerald).
	bool IsGemstone(const FCrossguardInventoryItem& Item)
	{
		return Item.Category == ECrossguardItemCategory::Trinket;
	}

	TArray<FCrossguardInventoryItem> ItemsIn(const UCrossguardInventoryComponent& Inventory, ECrossguardInventoryContainer Container)
	{
		TArray<FCrossguardInventoryItem> Result;
		for (const FCrossguardInventoryItem& Item : Inventory.GetItems())
		{
			if (Item.Container == Container)
			{
				Result.Add(Item);
			}
		}
		return Result;
	}
}

UCrossguardVendorWidget::UCrossguardVendorWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Cheap: redraws only when what it shows changed (BuildSignature).
	RefreshInterval = 0.1f;
}

UCrossguardVendorComponent* UCrossguardVendorWidget::GetVendor() const
{
	const ACrossguardPlayerState* PlayerState = GetCrossguardPlayerState();
	return PlayerState != nullptr ? PlayerState->GetVendor() : nullptr;
}

UCrossguardInventoryComponent* UCrossguardVendorWidget::GetInventory() const
{
	const ACrossguardCharacter* Character = GetCrossguardCharacter();
	return Character != nullptr ? Character->GetInventoryComponent() : nullptr;
}

void UCrossguardVendorWidget::BuildTree()
{
	using namespace CrossguardVendorWidgetLocal;
	BuildFrame(TEXT("Vendor   (Esc to close)"), FVector2D(1040.0f, 720.0f));

	VendorTitle = MakeText(FString::Printf(TEXT("Selling to %s"), *VendorName), 13, Heading);
	AddTo(Body, VendorTitle);
	UTextBlock* Hint = MakeText(TEXT("Drag a stack onto the sell box, or click Sell in the list. A whole stack sells at once, for banked gold. "
		"Between raids only."), 10, FLinearColor(0.7f, 0.7f, 0.7f));
	Hint->SetAutoWrapText(true);
	AddTo(Body, Hint);

	UHorizontalBox* Columns = MakeRow();
	AddTo(Body, Columns, 8.0f);

	// The inventory grids (drag sources) and the sell box.
	UVerticalBox* InventoryColumn = MakeColumn();
	AddTo(Columns, InventoryColumn, 10.0f);
	AddTo(InventoryColumn, MakeText(TEXT("Inventory"), 13, Heading));
	MainGrid = Make<UCrossguardItemGridWidget>();
	AddTo(InventoryColumn, MainGrid);
	UHorizontalBox* ExtraGrids = MakeRow();
	AddTo(InventoryColumn, ExtraGrids, 6.0f);
	UVerticalBox* BackpackColumn = MakeColumn();
	AddTo(ExtraGrids, BackpackColumn, 6.0f);
	BackpackLabel = MakeText(TEXT("Backpack"), 11);
	AddTo(BackpackColumn, BackpackLabel);
	BackpackGrid = Make<UCrossguardItemGridWidget>();
	AddTo(BackpackColumn, BackpackGrid);
	UVerticalBox* QuiverColumn = MakeColumn();
	AddTo(ExtraGrids, QuiverColumn, 6.0f);
	QuiverLabel = MakeText(TEXT("Quiver"), 11);
	AddTo(QuiverColumn, QuiverLabel);
	QuiverGrid = Make<UCrossguardItemGridWidget>();
	AddTo(QuiverColumn, QuiverGrid);
	// Moving items around is the inventory screen's job: here the grids only start drags to the sell box.

	USizeBox* SellBoxSize = Make<USizeBox>();
	SellBoxSize->SetWidthOverride(400.0f);
	SellBoxSize->SetHeightOverride(70.0f);
	SellBox = MakeButton(TEXT("Drop a stack here to sell it"), nullptr, 13);
	SellBox->SetColor(FLinearColor(0.2f, 0.15f, 0.05f, 1.0f));
	SellBox->OnDropped = [this](UDragDropOperation* Operation)
	{
		const UCrossguardItemDragOperation* Drag = Cast<UCrossguardItemDragOperation>(Operation);
		if (Drag == nullptr || Drag->bFromLoot)
		{
			return false;
		}
		DropToSell(Drag->Item);
		return true;
	};
	SellBoxSize->SetContent(SellBox);
	AddTo(InventoryColumn, SellBoxSize, 10.0f);

	// Gold, sell all, the list.
	UVerticalBox* SellColumn = MakeColumn();
	AddTo(Columns, SellColumn, 10.0f);
	GoldText = MakeText(FString(), 13);
	AddTo(SellColumn, GoldText);
	SellGemsButton = MakeButton(TEXT("Sell all gemstones"), [this]() { SellAllGemstones(); });
	AddTo(SellColumn, SellGemsButton, 4.0f);
	AddTo(SellColumn, MakeText(TEXT("Your stacks"), 12, Heading), 6.0f);
	UScrollBox* Scroll = Make<UScrollBox>();
	SellList = MakeColumn();
	Scroll->AddChild(SellList);
	USizeBox* ListSize = Make<USizeBox>();
	ListSize->SetWidthOverride(460.0f);
	ListSize->SetHeightOverride(480.0f);
	ListSize->SetContent(Scroll);
	AddTo(SellColumn, ListSize);
}

int32 UCrossguardVendorWidget::GetInventoryCellCount() const
{
	return (MainGrid != nullptr ? MainGrid->GetItemCellCount() : 0) + (BackpackGrid != nullptr ? BackpackGrid->GetItemCellCount() : 0)
		+ (QuiverGrid != nullptr ? QuiverGrid->GetItemCellCount() : 0);
}

void UCrossguardVendorWidget::SetVendorName(const FString& Name)
{
	VendorName = Name.IsEmpty() ? FString(TEXT("Vendor")) : Name;
	SetText(VendorTitle, FString::Printf(TEXT("Selling to %s"), *VendorName));
}

FString UCrossguardVendorWidget::BuildSignature() const
{
	const UCrossguardInventoryComponent* Inventory = GetInventory();
	const ACrossguardPlayerState* PlayerState = GetCrossguardPlayerState();
	if (Inventory == nullptr || PlayerState == nullptr || GetVendor() == nullptr)
	{
		return TEXT("none");
	}
	FString Signature = FString::Printf(TEXT("%s|%d|%d;"), *GetNameSafe(GetCrossguardCharacter()), PlayerState->GetGold(), PlayerState->IsInRaid() ? 1 : 0);
	for (const FCrossguardInventoryItem& Item : Inventory->GetItems())
	{
		Signature += FString::Printf(TEXT("%d:%d:%d:%d,%d:%d;"), Item.Id, Item.Count, static_cast<int32>(Item.Container), Item.Position.X,
			Item.Position.Y, Item.bRotated ? 1 : 0);
	}
	Signature += FString::Printf(TEXT("|C%d,%d"), Inventory->GetEquippedContainer(ECrossguardInventoryContainer::Backpack).Id,
		Inventory->GetEquippedContainer(ECrossguardInventoryContainer::Quiver).Id);
	return Signature;
}

void UCrossguardVendorWidget::NativeRefresh()
{
	const FString Signature = BuildSignature();
	if (Signature == LastSignature)
	{
		return;
	}
	LastSignature = Signature;
	Redraw();
}

void UCrossguardVendorWidget::Redraw()
{
	using namespace CrossguardVendorWidgetLocal;
	SellList->ClearChildren();
	SellRowLabels.Reset();
	GemstonesValue = 0;
	const ACrossguardCharacter* Character = GetCrossguardCharacter();
	const UCrossguardInventoryComponent* Inventory = GetInventory();
	const UCrossguardEquipmentComponent* Equipment = Character != nullptr ? Character->GetEquipmentComponent() : nullptr;
	const UCrossguardVendorComponent* Vendor = GetVendor();
	const ACrossguardPlayerState* PlayerState = GetCrossguardPlayerState();
	if (Inventory == nullptr || Vendor == nullptr || PlayerState == nullptr)
	{
		for (UCrossguardItemGridWidget* Grid : { MainGrid.Get(), BackpackGrid.Get(), QuiverGrid.Get() })
		{
			Grid->SetGrid(FIntPoint::ZeroValue, {}, 0, nullptr, nullptr);
		}
		GoldLine = TEXT("No character (dead for the raid or spectating).");
		SetText(GoldText, GoldLine);
		SellGemsButton->SetActive(false);
		return;
	}

	MainGrid->SetGrid(Inventory->GetContainerSize(ECrossguardInventoryContainer::Main), ItemsIn(*Inventory, ECrossguardInventoryContainer::Main), 0,
		Inventory, Equipment);
	const FIntPoint BackpackSize = Inventory->GetContainerSize(ECrossguardInventoryContainer::Backpack);
	const FIntPoint QuiverSize = Inventory->GetContainerSize(ECrossguardInventoryContainer::Quiver);
	BackpackGrid->SetGrid(BackpackSize, ItemsIn(*Inventory, ECrossguardInventoryContainer::Backpack), 0, Inventory, Equipment);
	QuiverGrid->SetGrid(QuiverSize, ItemsIn(*Inventory, ECrossguardInventoryContainer::Quiver), 0, Inventory, Equipment);
	const ESlateVisibility BackpackShown = BackpackSize.X > 0 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	const ESlateVisibility QuiverShown = QuiverSize.X > 0 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	BackpackLabel->SetVisibility(BackpackShown);
	BackpackGrid->SetVisibility(BackpackShown);
	QuiverLabel->SetVisibility(QuiverShown);
	QuiverGrid->SetVisibility(QuiverShown);

	GoldLine = FString::Printf(TEXT("Banked gold: %d"), PlayerState->GetGold());
	SetText(GoldText, GoldLine);

	// One row per stack: what it is, what it sells for, Sell (greyed when vendors do not buy it).
	for (const FCrossguardInventoryItem& Item : Inventory->GetItems())
	{
		const int32 Price = Vendor->GetSellPrice(Item);
		const FString Label = FString::Printf(TEXT("%s: %s"), *CrossguardUI::ItemLabel(Item, Inventory, Equipment),
			Price > 0 ? *FString::Printf(TEXT("%d gold"), Price) : TEXT("not bought"));
		SellRowLabels.Add(Label);
		if (IsGemstone(Item) && Price > 0)
		{
			GemstonesValue += Price;
		}

		UHorizontalBox* Row = MakeRow();
		const int32 Id = Item.Id;
		UCrossguardUIButton* Sell = MakeButton(TEXT("Sell"), [this, Id]() { SellItem(Id); }, 10);
		Sell->SetActive(Price > 0);
		AddTo(Row, Sell);
		AddTo(Row, MakeText(Label, 11, Price > 0 ? CrossguardUI::RarityColor(CrossguardUI::ItemRarity(Item)) : NotBought), 6.0f);
		AddTo(SellList, Row, 2.0f);
	}

	SellGemsButton->SetLabel(GemstonesValue > 0 ? FString::Printf(TEXT("Sell all gemstones: %d gold"), GemstonesValue)
												: FString(TEXT("Sell all gemstones (none carried)")));
	SellGemsButton->SetActive(GemstonesValue > 0 && !PlayerState->IsInRaid());
}

void UCrossguardVendorWidget::SellItem(int32 InventoryItemId)
{
	if (UCrossguardVendorComponent* Vendor = GetVendor())
	{
		Vendor->RequestSell(ECrossguardItemSource::Inventory, InventoryItemId);
	}
	MarkDirty();
}

void UCrossguardVendorWidget::SellAllGemstones()
{
	UCrossguardVendorComponent* Vendor = GetVendor();
	const UCrossguardInventoryComponent* Inventory = GetInventory();
	if (Vendor == nullptr || Inventory == nullptr)
	{
		return;
	}
	// The ids first: on a listen server each sale changes the inventory at once.
	TArray<int32> Ids;
	for (const FCrossguardInventoryItem& Item : Inventory->GetItems())
	{
		if (CrossguardVendorWidgetLocal::IsGemstone(Item) && Vendor->GetSellPrice(Item) > 0)
		{
			Ids.Add(Item.Id);
		}
	}
	for (const int32 Id : Ids)
	{
		Vendor->RequestSell(ECrossguardItemSource::Inventory, Id);
	}
	MarkDirty();
}

void UCrossguardVendorWidget::DropToSell(const FCrossguardInventoryItem& Item)
{
	SellItem(Item.Id);
}

void UCrossguardVendorWidget::OnWindowOpened()
{
	ShowStatus(FString(), FLinearColor::White);
	MarkDirty();
}
