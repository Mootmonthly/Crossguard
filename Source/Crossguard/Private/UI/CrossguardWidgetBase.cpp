#include "UI/CrossguardWidgetBase.h"

#include "Character/CrossguardCharacter.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Game/CrossguardPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Styling/CoreStyle.h"
#include "UI/CrossguardHUD.h"
#include "UI/CrossguardUIButton.h"

// --- UCrossguardWidgetBase ------------------------------------------------------------------------

void UCrossguardWidgetBase::EnsureTree()
{
	if (bTreeChecked)
	{
		return;
	}
	bTreeChecked = true;
	if (WidgetTree == nullptr)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
	}
	// A Widget Blueprint subclass brings its own tree: keep it.
	if (WidgetTree->RootWidget == nullptr)
	{
		BuildTree();
		bBuiltInCode = WidgetTree->RootWidget != nullptr;
	}
}

TSharedRef<SWidget> UCrossguardWidgetBase::RebuildWidget()
{
	EnsureTree();
	return Super::RebuildWidget();
}

void UCrossguardWidgetBase::Refresh()
{
	EnsureTree();
	SinceRefresh = 0.0f;
	if (bBuiltInCode)
	{
		NativeRefresh();
	}
	OnRefreshed();
}

void UCrossguardWidgetBase::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (RefreshInterval <= 0.0f)
	{
		return;
	}
	SinceRefresh += InDeltaTime;
	if (SinceRefresh >= RefreshInterval)
	{
		Refresh();
	}
}

ACrossguardCharacter* UCrossguardWidgetBase::GetCrossguardCharacter() const
{
	return Cast<ACrossguardCharacter>(GetOwningPlayerPawn());
}

ACrossguardPlayerState* UCrossguardWidgetBase::GetCrossguardPlayerState() const
{
	const APlayerController* Player = GetOwningPlayer();
	return Player != nullptr ? Player->GetPlayerState<ACrossguardPlayerState>() : nullptr;
}

UTextBlock* UCrossguardWidgetBase::MakeText(const FString& Text, int32 FontSize, const FLinearColor& Color)
{
	UTextBlock* Block = Make<UTextBlock>();
	Block->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), FontSize));
	Block->SetColorAndOpacity(FSlateColor(Color));
	Block->SetText(FText::FromString(Text));
	return Block;
}

UCrossguardUIButton* UCrossguardWidgetBase::MakeButton(const FString& Label, TFunction<void()> OnClicked, int32 FontSize)
{
	UCrossguardUIButton* Button = Make<UCrossguardUIButton>();
	Button->SetFontSize(FontSize);
	Button->SetLabel(Label);
	Button->OnClicked = MoveTemp(OnClicked);
	return Button;
}

UBorder* UCrossguardWidgetBase::MakePanel(const FLinearColor& Background, float PanelPadding)
{
	UBorder* Panel = Make<UBorder>();
	Panel->SetBrushColor(Background);
	Panel->SetPadding(FMargin(PanelPadding));
	return Panel;
}

UVerticalBox* UCrossguardWidgetBase::MakeColumn()
{
	return Make<UVerticalBox>();
}

UHorizontalBox* UCrossguardWidgetBase::MakeRow()
{
	return Make<UHorizontalBox>();
}

UEditableTextBox* UCrossguardWidgetBase::MakeTextField(const FString& Hint)
{
	UEditableTextBox* Field = Make<UEditableTextBox>();
	Field->SetHintText(FText::FromString(Hint));
	return Field;
}

void UCrossguardWidgetBase::AddTo(UPanelWidget* Parent, UWidget* Child, float Gap)
{
	if (Parent == nullptr || Child == nullptr)
	{
		return;
	}
	if (UVerticalBox* Column = Cast<UVerticalBox>(Parent))
	{
		Column->AddChildToVerticalBox(Child)->SetPadding(FMargin(0.0f, Gap * 0.5f));
	}
	else if (UHorizontalBox* Row = Cast<UHorizontalBox>(Parent))
	{
		UHorizontalBoxSlot* RowSlot = Row->AddChildToHorizontalBox(Child);
		RowSlot->SetPadding(FMargin(Gap * 0.5f, 0.0f));
		RowSlot->SetVerticalAlignment(VAlign_Top);
	}
	else
	{
		Parent->AddChild(Child);
	}
}

void UCrossguardWidgetBase::SetText(UTextBlock* Text, const FString& Value)
{
	if (Text != nullptr)
	{
		Text->SetText(FText::FromString(Value));
	}
}

// --- UCrossguardWindowWidget ----------------------------------------------------------------------

void UCrossguardWindowWidget::BuildFrame(const FString& Title, FVector2D Size)
{
	// Visible: the window itself takes the clicks that miss its buttons.
	SetVisibility(ESlateVisibility::Visible);

	UCanvasPanel* Root = Make<UCanvasPanel>();
	WidgetTree->RootWidget = Root;

	UBorder* Panel = MakePanel(FLinearColor(0.03f, 0.03f, 0.04f, 0.92f), 10.0f);
	UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Panel);
	PanelSlot->SetAnchors(FAnchors(0.5f, 0.5f));
	PanelSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	PanelSlot->SetPosition(FVector2D::ZeroVector);
	PanelSlot->SetSize(Size);

	UVerticalBox* Frame = MakeColumn();
	Panel->SetContent(Frame);

	UHorizontalBox* TitleRow = MakeRow();
	AddTo(Frame, TitleRow);
	UTextBlock* TitleText = MakeText(Title, 16, FLinearColor(1.0f, 0.85f, 0.5f));
	TitleRow->AddChildToHorizontalBox(TitleText)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	UCrossguardUIButton* Close = MakeButton(TEXT(" X "), [this]() { RequestClose(); }, 12);
	Close->SetColor(FLinearColor(0.35f, 0.08f, 0.08f, 1.0f));
	TitleRow->AddChildToHorizontalBox(Close);

	Body = MakeColumn();
	Frame->AddChildToVerticalBox(Body)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	StatusText = MakeText(FString(), 11, FLinearColor(1.0f, 0.6f, 0.3f));
	StatusText->SetAutoWrapText(true);
	AddTo(Frame, StatusText);
}

FReply UCrossguardWindowWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// Keep clicks in the window: a click that misses a button must not reach the game (attack).
	return FReply::Handled();
}

void UCrossguardWindowWidget::RequestClose()
{
	const APlayerController* Player = GetOwningPlayer();
	if (ACrossguardHUD* Hud = Player != nullptr ? Player->GetHUD<ACrossguardHUD>() : nullptr)
	{
		Hud->CloseWindow(this);
		return;
	}
	SetVisibility(ESlateVisibility::Collapsed);
	OnWindowClosed();
}

void UCrossguardWindowWidget::ShowStatus(const FString& Message, FLinearColor Color)
{
	if (StatusText != nullptr)
	{
		StatusText->SetText(FText::FromString(Message));
		StatusText->SetColorAndOpacity(FSlateColor(Color));
	}
}
