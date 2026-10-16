#include "UI/CrossguardItemGridWidget.h"

#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "UI/CrossguardUIButton.h"
#include "UI/CrossguardUIHelpers.h"

UCrossguardItemGridWidget::UCrossguardItemGridWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Redrawn by its window through SetGrid.
	RefreshInterval = 0.0f;
}

void UCrossguardItemGridWidget::BuildTree()
{
	Frame = Make<USizeBox>();
	Canvas = Make<UCanvasPanel>();
	Frame->SetContent(Canvas);
	WidgetTree->RootWidget = Frame;
}

void UCrossguardItemGridWidget::SetGrid(FIntPoint Size, const TArray<FCrossguardInventoryItem>& Items, int32 SelectedItemId,
	const UCrossguardInventoryComponent* Inventory, const UCrossguardEquipmentComponent* Equipment)
{
	EnsureTree();
	ItemCellCount = 0;
	if (Canvas == nullptr || Frame == nullptr)
	{
		return;
	}
	Canvas->ClearChildren();
	Frame->SetWidthOverride(FMath::Max(Size.X, 1) * SquareSize);
	Frame->SetHeightOverride(FMath::Max(Size.Y, 1) * SquareSize);

	const auto Place = [this](UWidget* Widget, FIntPoint Position, FIntPoint Footprint, int32 ZOrder)
	{
		UCanvasPanelSlot* CellSlot = Canvas->AddChildToCanvas(Widget);
		CellSlot->SetPosition(FVector2D(Position.X * SquareSize + 1.0f, Position.Y * SquareSize + 1.0f));
		CellSlot->SetSize(FVector2D(Footprint.X * SquareSize - 2.0f, Footprint.Y * SquareSize - 2.0f));
		CellSlot->SetZOrder(ZOrder);
	};

	// Empty squares: clicking one moves the selected item there.
	for (int32 Y = 0; Y < Size.Y; ++Y)
	{
		for (int32 X = 0; X < Size.X; ++X)
		{
			UCrossguardUIButton* Square = Make<UCrossguardUIButton>();
			Square->SetColor(FLinearColor(0.08f, 0.08f, 0.09f, 0.9f));
			const FIntPoint Position(X, Y);
			Square->OnClicked = [this, Position]()
			{
				if (OnSquareClicked)
				{
					OnSquareClicked(Position);
				}
			};
			Place(Square, Position, FIntPoint(1, 1), 0);
		}
	}

	for (const FCrossguardInventoryItem& Item : Items)
	{
		FIntPoint Footprint(FMath::Max(Item.Size.X, 1), FMath::Max(Item.Size.Y, 1));
		if (Item.bRotated)
		{
			Footprint = FIntPoint(Footprint.Y, Footprint.X);
		}
		const FLinearColor Rarity = CrossguardUI::RarityColor(CrossguardUI::ItemRarity(Item));
		const bool bSelected = Item.Id == SelectedItemId;

		UCrossguardUIButton* Cell = Make<UCrossguardUIButton>();
		// Text past the cell's edge is cut off, not drawn over its neighbours; the tooltip has it all.
		Cell->SetClipping(EWidgetClipping::ClipToBounds);
		Cell->SetLabelPadding(FMargin(1.0f));
		Cell->SetFontSize(SquareSize >= 38.0f ? 11 : 10);
		const FString Name = CrossguardUI::ItemName(Item, Inventory, Equipment);
		const FString Count = Item.Count > 1 ? FString::Printf(TEXT("x%d"), Item.Count) : FString();
		if (Footprint.X < 2)
		{
			// One square wide: a full name cannot be read, so a short one with the count below it.
			const FString Short = CrossguardUI::ShortItemName(Name);
			Cell->SetLabel(Count.IsEmpty() ? Short : FString::Printf(TEXT("%s\n%s"), *Short, *Count));
		}
		else
		{
			// Wider items: the full name, wrapped; the count on its own line when there is room.
			Cell->SetLabelWrap(true);
			Cell->SetLabelPadding(FMargin(3.0f, 1.0f));
			const TCHAR* Separator = Footprint.Y > 1 ? TEXT("\n") : TEXT(" ");
			Cell->SetLabel(Count.IsEmpty() ? Name : FString::Printf(TEXT("%s%s%s"), *Name, Separator, *Count));
		}
		Cell->SetColor(bSelected ? FLinearColor(0.85f, 0.75f, 0.2f, 0.95f) : FLinearColor(Rarity.R * 0.35f, Rarity.G * 0.35f, Rarity.B * 0.35f, 0.95f));
		Cell->SetLabelColor(bSelected ? FLinearColor::Black : Rarity);
		Cell->SetToolTipText(FText::FromString(CrossguardUI::ItemLabel(Item, Inventory, Equipment)));
		const FCrossguardInventoryItem Copy = Item;
		Cell->OnClicked = [this, Copy]()
		{
			if (OnItemClicked)
			{
				OnItemClicked(Copy);
			}
		};
		Cell->OnRightClicked = [this, Copy]()
		{
			if (OnItemRightClicked)
			{
				OnItemRightClicked(Copy);
			}
		};
		Place(Cell, Item.Position, Footprint, 1);
		++ItemCellCount;
	}
}
