#include "UI/CrossguardAttributesWidget.h"

#include "Components/HorizontalBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "CrossguardRules/Progression.h"
#include "UI/CrossguardUIButton.h"
#include "UI/CrossguardUIHelpers.h"

namespace CrossguardAttributesWidgetLocal
{
	constexpr int32 AttributeCount = 3;
	const TCHAR* const AttributeNames[AttributeCount] = { TEXT("Strength"), TEXT("Agility"), TEXT("Intelligence") };

	int32& AttributeOf(FCrossguardAttributeAllocation& Allocation, int32 Attribute)
	{
		return Attribute == 0 ? Allocation.Strength : (Attribute == 1 ? Allocation.Agility : Allocation.Intelligence);
	}

	int32 AttributeOf(const FCrossguardAttributeAllocation& Allocation, int32 Attribute)
	{
		return Attribute == 0 ? Allocation.Strength : (Attribute == 1 ? Allocation.Agility : Allocation.Intelligence);
	}

	// Summed wide, like the rules (client numbers never wrap).
	int64 TotalOf(const FCrossguardAttributeAllocation& Allocation)
	{
		return static_cast<int64>(Allocation.Strength) + Allocation.Agility + Allocation.Intelligence;
	}

	bool SameAllocation(const FCrossguardAttributeAllocation& First, const FCrossguardAttributeAllocation& Second)
	{
		return First.Strength == Second.Strength && First.Agility == Second.Agility && First.Intelligence == Second.Intelligence;
	}

	const FLinearColor Refusal(1.0f, 0.55f, 0.3f);
	const FLinearColor Dim(0.7f, 0.7f, 0.7f);
}

UCrossguardAttributesWidget::UCrossguardAttributesWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	RefreshInterval = 0.2f;
}

void UCrossguardAttributesWidget::BuildTree()
{
	using namespace CrossguardAttributesWidgetLocal;
	BuildFrame(TEXT("Attributes   (K to close)"), FVector2D(620.0f, 420.0f));

	PointsText = MakeText(TEXT("No player state."), 14);
	AddTo(Body, PointsText);
	AddTo(Body, MakeText(TEXT("Spending unspent points is free. Lowering any attribute is a respec: a respec token, or banked gold. Between raids only."),
		10, Dim), 4.0f);

	// One row per attribute: name, current -> draft, - and +.
	for (int32 Attribute = 0; Attribute < AttributeCount; ++Attribute)
	{
		UHorizontalBox* Row = MakeRow();
		USizeBox* NameBox = Make<USizeBox>();
		NameBox->SetWidthOverride(320.0f);
		UTextBlock* Line = MakeText(AttributeNames[Attribute], 13);
		NameBox->SetContent(Line);
		AddTo(Row, NameBox);
		UCrossguardUIButton* Minus = MakeButton(TEXT("  -  "), [this, Attribute]() { AdjustDraft(Attribute, -1); }, 13);
		UCrossguardUIButton* Plus = MakeButton(TEXT("  +  "), [this, Attribute]() { AdjustDraft(Attribute, 1); }, 13);
		AddTo(Row, Minus, 4.0f);
		AddTo(Row, Plus, 4.0f);
		AddTo(Body, Row, 6.0f);
		AttributeTexts.Add(Line);
		MinusButtons.Add(Minus);
		PlusButtons.Add(Plus);
	}

	UHorizontalBox* DraftRow = MakeRow();
	AddTo(DraftRow, MakeButton(TEXT("Reset"), [this]() { ResetDraft(); }));
	AddTo(DraftRow, MakeButton(TEXT("Unassign all"), [this]() { ClearDraft(); }));
	AddTo(Body, DraftRow, 8.0f);

	UHorizontalBox* Requests = MakeRow();
	ApplyButton = MakeButton(TEXT("Apply (free)"), [this]() { Apply(); }, 12);
	TokenButton = MakeButton(TEXT("Respec with token"), [this]() { RespecWithToken(); }, 12);
	GoldButton = MakeButton(TEXT("Respec for gold"), [this]() { RespecWithGold(); }, 12);
	AddTo(Requests, ApplyButton);
	AddTo(Requests, TokenButton);
	AddTo(Requests, GoldButton);
	AddTo(Body, Requests, 10.0f);

	HintText = MakeText(FString(), 11, Dim);
	HintText->SetAutoWrapText(true);
	AddTo(Body, HintText, 4.0f);
}

int32 UCrossguardAttributesWidget::PointsAtLevel() const
{
	const ACrossguardPlayerState* Progression = GetCrossguardPlayerState();
	return Progression != nullptr ? Crossguard::AttributePointsAtLevel(Progression->GetProgressionConfig(), Progression->GetCharacterLevel()) : 0;
}

bool UCrossguardAttributesWidget::DraftIsValid() const
{
	return Draft.Strength >= 0 && Draft.Agility >= 0 && Draft.Intelligence >= 0
		&& CrossguardAttributesWidgetLocal::TotalOf(Draft) <= PointsAtLevel();
}

bool UCrossguardAttributesWidget::DraftDiffers() const
{
	const ACrossguardPlayerState* Progression = GetCrossguardPlayerState();
	return Progression != nullptr && bHasDraft && !CrossguardAttributesWidgetLocal::SameAllocation(Draft, Progression->GetAttributeAllocation());
}

bool UCrossguardAttributesWidget::InRaid() const
{
	const ACrossguardPlayerState* Progression = GetCrossguardPlayerState();
	return Progression != nullptr && Progression->IsInRaid();
}

bool UCrossguardAttributesWidget::DraftNeedsRespec() const
{
	const ACrossguardPlayerState* Progression = GetCrossguardPlayerState();
	if (Progression == nullptr || !bHasDraft)
	{
		return false;
	}
	const FCrossguardAttributeAllocation& Current = Progression->GetAttributeAllocation();
	// Crossguard::RequiresRespec: any attribute lowered.
	return Draft.Strength < Current.Strength || Draft.Agility < Current.Agility || Draft.Intelligence < Current.Intelligence;
}

bool UCrossguardAttributesWidget::CanApply() const
{
	return DraftDiffers() && DraftIsValid() && !DraftNeedsRespec() && !InRaid();
}

bool UCrossguardAttributesWidget::CanRespecWithToken() const
{
	const ACrossguardPlayerState* Progression = GetCrossguardPlayerState();
	return Progression != nullptr && DraftNeedsRespec() && DraftIsValid() && !InRaid() && Progression->GetRespecTokens() > 0;
}

bool UCrossguardAttributesWidget::CanRespecWithGold() const
{
	const ACrossguardPlayerState* Progression = GetCrossguardPlayerState();
	return Progression != nullptr && DraftNeedsRespec() && DraftIsValid() && !InRaid() && Progression->GetGold() >= Progression->GetRespecGoldPrice();
}

void UCrossguardAttributesWidget::AdjustDraft(int32 Attribute, int32 Delta)
{
	using namespace CrossguardAttributesWidgetLocal;
	if (Attribute < 0 || Attribute >= AttributeCount)
	{
		return;
	}
	if (!bHasDraft)
	{
		ResetDraft();
	}
	int32& Value = AttributeOf(Draft, Attribute);
	if (Delta < 0)
	{
		Value = FMath::Max(0, Value + Delta);
	}
	else
	{
		const int64 Room = FMath::Max<int64>(0, PointsAtLevel() - TotalOf(Draft));
		Value += static_cast<int32>(FMath::Min<int64>(Delta, Room));
	}
	Redraw();
}

void UCrossguardAttributesWidget::ResetDraft()
{
	if (const ACrossguardPlayerState* Progression = GetCrossguardPlayerState())
	{
		Draft = Progression->GetAttributeAllocation();
		DraftBase = Draft;
		bHasDraft = true;
	}
	Redraw();
}

void UCrossguardAttributesWidget::ClearDraft()
{
	if (!bHasDraft)
	{
		ResetDraft();
	}
	Draft.Strength = 0;
	Draft.Agility = 0;
	Draft.Intelligence = 0;
	Redraw();
}

void UCrossguardAttributesWidget::Send(bool bPayWithGold)
{
	ACrossguardPlayerState* Progression = GetCrossguardPlayerState();
	if (Progression == nullptr || !bHasDraft)
	{
		return;
	}
	BindProgression();
	ShowStatus(TEXT("Sent; the server decides."), FLinearColor::White);
	Progression->RequestAttributeAllocation(Draft, bPayWithGold);
}

void UCrossguardAttributesWidget::Apply()
{
	if (CanApply())
	{
		Send(false);
	}
}

void UCrossguardAttributesWidget::RespecWithToken()
{
	if (CanRespecWithToken())
	{
		Send(false);
	}
}

void UCrossguardAttributesWidget::RespecWithGold()
{
	if (CanRespecWithGold())
	{
		Send(true);
	}
}

void UCrossguardAttributesWidget::OnWindowOpened()
{
	BindProgression();
	ShowStatus(FString(), FLinearColor::White);
	ResetDraft();
}

void UCrossguardAttributesWidget::BindProgression()
{
	ACrossguardPlayerState* Progression = GetCrossguardPlayerState();
	if (Progression == BoundProgression.Get())
	{
		return;
	}
	if (ACrossguardPlayerState* Old = BoundProgression.Get())
	{
		Old->OnProgressionRejected.RemoveDynamic(this, &UCrossguardAttributesWidget::HandleProgressionRejected);
	}
	if (Progression != nullptr)
	{
		Progression->OnProgressionRejected.AddUniqueDynamic(this, &UCrossguardAttributesWidget::HandleProgressionRejected);
	}
	BoundProgression = Progression;
}

void UCrossguardAttributesWidget::HandleProgressionRejected(ECrossguardProgressionError Error)
{
	ShowStatus(FString::Printf(TEXT("Refused: %s"), *CrossguardUI::UIEnumName(Error)), CrossguardAttributesWidgetLocal::Refusal);
}

void UCrossguardAttributesWidget::NativeRefresh()
{
	BindProgression();
	const ACrossguardPlayerState* Progression = GetCrossguardPlayerState();
	// The server changed the allocation (applied, a save loaded): the draft starts over from it.
	if (Progression != nullptr && (!bHasDraft || !CrossguardAttributesWidgetLocal::SameAllocation(DraftBase, Progression->GetAttributeAllocation())))
	{
		if (bHasDraft)
		{
			ShowStatus(TEXT("Applied."), FLinearColor(0.5f, 1.0f, 0.5f));
		}
		ResetDraft();
		return;
	}
	Redraw();
}

void UCrossguardAttributesWidget::Redraw()
{
	using namespace CrossguardAttributesWidgetLocal;
	const ACrossguardPlayerState* Progression = GetCrossguardPlayerState();
	if (Progression == nullptr || PointsText == nullptr)
	{
		PointsLine = TEXT("No player state.");
		SetText(PointsText, PointsLine);
		return;
	}
	const FCrossguardAttributeAllocation& Current = Progression->GetAttributeAllocation();
	const int32 Points = PointsAtLevel();
	PointsLine = FString::Printf(TEXT("Level %d: %d points, %lld unspent"), Progression->GetCharacterLevel(), Points, Points - TotalOf(Current));
	if (DraftDiffers())
	{
		PointsLine += FString::Printf(TEXT("  (draft: %lld unspent)"), Points - TotalOf(Draft));
	}
	SetText(PointsText, PointsLine);

	const int64 Room = Points - TotalOf(Draft);
	for (int32 Attribute = 0; Attribute < AttributeCount && Attribute < AttributeTexts.Num(); ++Attribute)
	{
		const int32 Have = AttributeOf(Current, Attribute);
		const int32 Planned = AttributeOf(Draft, Attribute);
		FString Line = FString::Printf(TEXT("%s: %d"), AttributeNames[Attribute], Have);
		if (Planned != Have)
		{
			Line += FString::Printf(TEXT("  ->  %d (%s%d)"), Planned, Planned > Have ? TEXT("+") : TEXT(""), Planned - Have);
		}
		SetText(AttributeTexts[Attribute], Line);
		AttributeTexts[Attribute]->SetColorAndOpacity(FSlateColor(Planned < Have ? Refusal : (Planned > Have ? FLinearColor(0.5f, 1.0f, 0.5f) : FLinearColor::White)));
		MinusButtons[Attribute]->SetActive(Planned > 0);
		PlusButtons[Attribute]->SetActive(Room > 0);
	}

	const bool bRespec = DraftNeedsRespec();
	ApplyButton->SetActive(CanApply());
	TokenButton->SetLabel(FString::Printf(TEXT("Respec with token (%d left)"), Progression->GetRespecTokens()));
	TokenButton->SetActive(CanRespecWithToken());
	GoldButton->SetLabel(FString::Printf(TEXT("Respec for %d gold (you have %d)"), Progression->GetRespecGoldPrice(), Progression->GetGold()));
	GoldButton->SetActive(CanRespecWithGold());

	FString Hint;
	if (InRaid())
	{
		Hint = TEXT("In a raid: attributes change between raids only.");
	}
	else if (!DraftDiffers())
	{
		Hint = TEXT("Use - and + to plan, then Apply (or respec when you lower an attribute).");
	}
	else if (!DraftIsValid())
	{
		Hint = TEXT("More points than your level has.");
	}
	else if (bRespec)
	{
		Hint = TEXT("This lowers an attribute: a respec. Pay with a token or with gold.");
	}
	else
	{
		Hint = TEXT("Only unspent points are added: Apply is free.");
	}
	SetText(HintText, Hint);
}
