#include "UI/CrossguardItemGridWidget.h"

#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "UI/CrossguardItemDragOperation.h"
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
		const FString CellLabel = Cell->GetLabel();
		const FLinearColor CellColor(Rarity.R * 0.35f, Rarity.G * 0.35f, Rarity.B * 0.35f, 0.95f);
		Cell->OnDragStart = [this, Copy, Footprint, CellLabel, CellColor, Rarity](const FGeometry& Geometry, const FPointerEvent& MouseEvent)
		{
			return StartDrag(Copy, Footprint, CellLabel, CellColor, Rarity, Geometry, MouseEvent);
		};
		Place(Cell, Item.Position, Footprint, 1);
		++ItemCellCount;
	}
}

UDragDropOperation* UCrossguardItemGridWidget::StartDrag(const FCrossguardInventoryItem& Item, FIntPoint Footprint, const FString& Label,
	const FLinearColor& Color, const FLinearColor& LabelColor, const FGeometry& CellGeometry, const FPointerEvent& MouseEvent)
{
	UCrossguardItemDragOperation* Operation = NewObject<UCrossguardItemDragOperation>(this);
	Operation->Item = Item;
	Operation->bFromLoot = bLootGrid;
	// Which of the item's squares was grabbed: a drop puts the top-left square that many squares up
	// and left of the square under the mouse.
	const FVector2D Local = CellGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	Operation->GrabOffset = FIntPoint(FMath::Clamp(FMath::FloorToInt(Local.X / SquareSize), 0, Footprint.X - 1),
		FMath::Clamp(FMath::FloorToInt(Local.Y / SquareSize), 0, Footprint.Y - 1));
	// The visual: the cell again, at its size, following the mouse from the grabbed point.
	USizeBox* Visual = Make<USizeBox>();
	Visual->SetWidthOverride(Footprint.X * SquareSize - 2.0f);
	Visual->SetHeightOverride(Footprint.Y * SquareSize - 2.0f);
	UCrossguardUIButton* Ghost = Make<UCrossguardUIButton>();
	Ghost->SetLabel(Label);
	Ghost->SetFontSize(SquareSize >= 38.0f ? 11 : 10);
	Ghost->SetLabelPadding(FMargin(1.0f));
	Ghost->SetLabelWrap(Footprint.X >= 2);
	Ghost->SetColor(FLinearColor(Color.R, Color.G, Color.B, 0.8f));
	Ghost->SetLabelColor(LabelColor);
	Ghost->SetVisibility(ESlateVisibility::HitTestInvisible);
	Visual->SetContent(Ghost);
	Operation->DefaultDragVisual = Visual;
	Operation->Pivot = EDragPivot::MouseDown;
	return Operation;
}

bool UCrossguardItemGridWidget::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	const UCrossguardItemDragOperation* Drag = Cast<UCrossguardItemDragOperation>(InOperation);
	if (Drag == nullptr || !OnItemDropped || Canvas == nullptr)
	{
		return false;
	}
	// The square under the mouse, then back by the grabbed offset: where the item's top-left goes.
	const FVector2D Local = Canvas->GetCachedGeometry().AbsoluteToLocal(InDragDropEvent.GetScreenSpacePosition());
	const FIntPoint Under(FMath::FloorToInt(Local.X / SquareSize), FMath::FloorToInt(Local.Y / SquareSize));
	const FIntPoint Position(FMath::Max(Under.X - Drag->GrabOffset.X, 0), FMath::Max(Under.Y - Drag->GrabOffset.Y, 0));
	// A copy: the callback redraws this grid, which rebuilds the cells.
	const TFunction<void(FIntPoint, const UCrossguardItemDragOperation&)> Callback = OnItemDropped;
	Callback(Position, *Drag);
	return true;
}
