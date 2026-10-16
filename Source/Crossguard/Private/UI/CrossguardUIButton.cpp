#include "UI/CrossguardUIButton.h"

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
