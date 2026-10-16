#include "UI/CrossguardUIButton.h"

#include "Blueprint/DragDropOperation.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Styling/CoreStyle.h"

UCrossguardUIButton::UCrossguardUIButton(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Redrawn by whoever owns it, not on a timer.
	RefreshInterval = 0.0f;
}

void UCrossguardUIButton::BuildTree()
{
	// Visible: the button itself is what the mouse hits; its border and label are not.
	SetVisibility(ESlateVisibility::Visible);
	Background = Make<UBorder>();
	Background->SetPadding(FMargin(5.0f, 2.0f));
	Background->SetHorizontalAlignment(HAlign_Center);
	Background->SetVerticalAlignment(VAlign_Center);
	Background->SetVisibility(ESlateVisibility::HitTestInvisible);
	LabelText = Make<UTextBlock>();
	LabelText->SetJustification(ETextJustify::Center);
	Background->SetContent(LabelText);
	WidgetTree->RootWidget = Background;
	ApplyLook();
}

void UCrossguardUIButton::SetLabel(const FString& InLabel)
{
	Label = InLabel;
	EnsureTree();
	if (LabelText != nullptr)
	{
		LabelText->SetText(FText::FromString(Label));
	}
}

void UCrossguardUIButton::SetFontSize(int32 Size)
{
	FontSize = Size;
	EnsureTree();
	ApplyLook();
}

void UCrossguardUIButton::SetLabelPadding(const FMargin& InPadding)
{
	EnsureTree();
	if (Background != nullptr)
	{
		Background->SetPadding(InPadding);
	}
}

void UCrossguardUIButton::SetLabelWrap(bool bWrap)
{
	EnsureTree();
	if (LabelText != nullptr)
	{
		LabelText->SetAutoWrapText(bWrap);
	}
}

void UCrossguardUIButton::SetColor(const FLinearColor& InColor)
{
	Color = InColor;
	EnsureTree();
	ApplyLook();
}

void UCrossguardUIButton::SetLabelColor(const FLinearColor& InColor)
{
	LabelColor = InColor;
	EnsureTree();
	ApplyLook();
}

void UCrossguardUIButton::SetActive(bool bInActive)
{
	bActive = bInActive;
	EnsureTree();
	ApplyLook();
}

void UCrossguardUIButton::ApplyLook()
{
	if (Background == nullptr || LabelText == nullptr)
	{
		return;
	}
	FLinearColor Shown = Color;
	if (bHovered && bActive)
	{
		Shown = FLinearColor(FMath::Min(Color.R + 0.15f, 1.0f), FMath::Min(Color.G + 0.15f, 1.0f), FMath::Min(Color.B + 0.15f, 1.0f), Color.A);
	}
	Background->SetBrushColor(Shown);
	LabelText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), FontSize));
	LabelText->SetColorAndOpacity(FSlateColor(bActive ? LabelColor : FLinearColor(0.45f, 0.45f, 0.45f, 1.0f)));
}

void UCrossguardUIButton::Click()
{
	if (bActive && OnClicked)
	{
		OnClicked();
	}
}

FReply UCrossguardUIButton::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bActive)
	{
		if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && OnDragStart)
		{
			// Draggable: Slate tells us (NativeOnDragDetected) once the mouse moves; a release without
			// moving is the click (NativeOnMouseButtonUp).
			return UWidgetBlueprintLibrary::DetectDragIfPressed(InMouseEvent, this, EKeys::LeftMouseButton).NativeReply;
		}
		// Copies: a callback may rebuild the window, which can drop this button.
		if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && OnClicked)
		{
			const TFunction<void()> Callback = OnClicked;
			Callback();
		}
		else if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton && OnRightClicked)
		{
			const TFunction<void()> Callback = OnRightClicked;
			Callback();
		}
	}
	return FReply::Handled();
}

FReply UCrossguardUIButton::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bActive && OnDragStart && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && OnClicked)
	{
		const TFunction<void()> Callback = OnClicked;
		Callback();
	}
	return FReply::Handled();
}

void UCrossguardUIButton::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation)
{
	OutOperation = bActive && OnDragStart ? OnDragStart(InGeometry, InMouseEvent) : nullptr;
}

bool UCrossguardUIButton::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	bHovered = false;
	ApplyLook();
	if (bActive && OnDropped)
	{
		const TFunction<bool(UDragDropOperation*)> Callback = OnDropped;
		return Callback(InOperation);
	}
	// Not taken here: the panel behind (an item grid) may take it.
	return false;
}

void UCrossguardUIButton::NativeOnDragEnter(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	Super::NativeOnDragEnter(InGeometry, InDragDropEvent, InOperation);
	// The hover highlight while something is dragged over it (no mouse-enter events during a drag).
	bHovered = true;
	ApplyLook();
}

void UCrossguardUIButton::NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	Super::NativeOnDragLeave(InDragDropEvent, InOperation);
	bHovered = false;
	ApplyLook();
}

void UCrossguardUIButton::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
	bHovered = true;
	ApplyLook();
	if (bActive && OnHovered)
	{
		const TFunction<void()> Callback = OnHovered;
		Callback();
	}
}

void UCrossguardUIButton::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	bHovered = false;
	ApplyLook();
}
