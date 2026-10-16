#include "UI/CrossguardTradeWidget.h"

#include "Character/CrossguardCharacter.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "CrossguardRules/Trade.h"
#include "Game/CrossguardPlayerState.h"
#include "Items/CrossguardTradeComponent.h"
#include "UI/CrossguardUIButton.h"
#include "UI/CrossguardUIHelpers.h"

UCrossguardTradeWidget::UCrossguardTradeWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	RefreshInterval = 0.1f;
}

UCrossguardTradeComponent* UCrossguardTradeWidget::GetTrade() const
{
	const ACrossguardCharacter* Character = GetCrossguardCharacter();
	return Character != nullptr ? Character->GetTradeComponent() : nullptr;
}

void UCrossguardTradeWidget::BuildTree()
{
	BuildFrame(TEXT("Trade   (T to close)"), FVector2D(980.0f, 600.0f));

	StateText = MakeText(TEXT("No trade"), 13);
	StateText->SetAutoWrapText(true);
	AddTo(Body, StateText);
	Controls = MakeColumn();
	AddTo(Body, Controls);

	UHorizontalBox* Columns = MakeRow();
	AddTo(Body, Columns, 8.0f);
	const auto AddList = [this, Columns](const FString& Title) -> UVerticalBox*
	{
		UVerticalBox* Column = MakeColumn();
		AddTo(Column, MakeText(Title, 12, FLinearColor(1.0f, 0.85f, 0.5f)));
		UScrollBox* Scroll = Make<UScrollBox>();
		UVerticalBox* List = MakeColumn();
		Scroll->AddChild(List);
		USizeBox* Size = Make<USizeBox>();
		Size->SetWidthOverride(300.0f);
		Size->SetHeightOverride(360.0f);
		Size->SetContent(Scroll);
		AddTo(Column, Size);
		AddTo(Columns, Column, 8.0f);
		return List;
	};
	MyItemsList = AddList(TEXT("Your items (click to offer)"));
	MyOfferList = AddList(TEXT("Your offer (click to take back)"));
	TheirOfferList = AddList(TEXT("Their offer"));

	UHorizontalBox* GoldRow = MakeRow();
	AddTo(Body, GoldRow, 8.0f);
	AddTo(GoldRow, MakeText(TEXT("Banked gold to offer (between raids only):"), 11));
	GoldField = MakeTextField(TEXT("0"));
	USizeBox* GoldSize = Make<USizeBox>();
	GoldSize->SetWidthOverride(120.0f);
	GoldSize->SetContent(GoldField);
	AddTo(GoldRow, GoldSize);
	AddTo(GoldRow, MakeButton(TEXT("Set gold"), [this]()
	{
		if (UCrossguardTradeComponent* Trade = GetTrade())
		{
			Trade->RequestSetOffer(Trade->GetTradeView().MyItemIds, ReadGoldField());
		}
	}));
}

int32 UCrossguardTradeWidget::ReadGoldField() const
{
	return GoldField != nullptr ? FMath::Max(0, FCString::Atoi(*GoldField->GetText().ToString())) : 0;
}

void UCrossguardTradeWidget::AskNearestPlayer()
{
	ACrossguardCharacter* Character = GetCrossguardCharacter();
	UCrossguardTradeComponent* Trade = Character != nullptr ? Character->GetTradeComponent() : nullptr;
	if (Trade == nullptr)
	{
		return;
	}
	ACrossguardCharacter* Target = CrossguardUI::FindNearestPlayer(Character, static_cast<float>(Crossguard::FTradeConfig().MaxDistanceCm));
	if (Target == nullptr)
	{
		ShowStatus(TEXT("No player within 5 m."), FLinearColor(1.0f, 0.6f, 0.3f));
		return;
	}
	Trade->RequestTrade(Target);
	ShowStatus(FString::Printf(TEXT("Asked %s."), *CrossguardUI::PlayerNameOf(Target)), FLinearColor::White);
}

void UCrossguardTradeWidget::ToggleOffered(int32 ItemId)
{
	UCrossguardTradeComponent* Trade = GetTrade();
	if (Trade == nullptr)
	{
		return;
	}
	const FCrossguardTradeView& View = Trade->GetTradeView();
	TArray<int32> Ids = View.MyItemIds;
	if (Ids.Remove(ItemId) == 0)
	{
		Ids.Add(ItemId);
	}
	Trade->RequestSetOffer(Ids, View.MyGold);
}

FString UCrossguardTradeWidget::BuildSignature() const
{
	const ACrossguardCharacter* Character = GetCrossguardCharacter();
	const UCrossguardTradeComponent* Trade = Character != nullptr ? Character->GetTradeComponent() : nullptr;
	const UCrossguardInventoryComponent* Inventory = Character != nullptr ? Character->GetInventoryComponent() : nullptr;
	if (Trade == nullptr || Inventory == nullptr)
	{
		return TEXT("none");
	}
	const FCrossguardTradeView& View = Trade->GetTradeView();
	FString Signature = FString::Printf(TEXT("%s|%d|%s|%d|%d|%d%d%d%d;"), *GetNameSafe(Character), static_cast<int32>(View.State),
		*GetNameSafe(View.Partner), View.MyGold, View.TheirGold, View.bMyLocked ? 1 : 0, View.bTheirLocked ? 1 : 0,
		View.bMyAccepted ? 1 : 0, View.bTheirAccepted ? 1 : 0);
	for (const int32 Id : View.MyItemIds)
	{
		Signature += FString::Printf(TEXT("%d,"), Id);
	}
	Signature += TEXT("|T");
	for (const FCrossguardInventoryItem& Item : View.TheirItems)
	{
		Signature += FString::Printf(TEXT("%d:%d,"), Item.Id, Item.Count);
	}
	Signature += TEXT("|I");
	for (const FCrossguardInventoryItem& Item : Inventory->GetItems())
	{
		Signature += FString::Printf(TEXT("%d:%d,"), Item.Id, Item.Count);
	}
	return Signature;
}

void UCrossguardTradeWidget::NativeRefresh()
{
	const FString Signature = BuildSignature();
	if (Signature == LastSignature)
	{
		return;
	}
	LastSignature = Signature;
	Redraw();
}

void UCrossguardTradeWidget::Redraw()
{
	Controls->ClearChildren();
	MyItemsList->ClearChildren();
	MyOfferList->ClearChildren();
	TheirOfferList->ClearChildren();
	MyItemRows = 0;
	MyOfferRows = 0;
	TheirOfferRows = 0;

	ACrossguardCharacter* Character = GetCrossguardCharacter();
	UCrossguardTradeComponent* Trade = Character != nullptr ? Character->GetTradeComponent() : nullptr;
	const UCrossguardInventoryComponent* Inventory = Character != nullptr ? Character->GetInventoryComponent() : nullptr;
	const UCrossguardEquipmentComponent* Equipment = Character != nullptr ? Character->GetEquipmentComponent() : nullptr;
	if (Trade == nullptr || Inventory == nullptr)
	{
		StateLine = TEXT("No character.");
		SetText(StateText, StateLine);
		return;
	}
	const FCrossguardTradeView& View = Trade->GetTradeView();
	const FString Partner = CrossguardUI::PlayerNameOf(View.Partner);
	UHorizontalBox* Buttons = MakeRow();
	AddTo(Controls, Buttons);
	// Each callback finds the trade component again when clicked.
	const auto AddControl = [this, Buttons](const FString& Label, bool bActive, TFunction<void(UCrossguardTradeComponent&)> OnClick)
	{
		UCrossguardUIButton* Button = MakeButton(Label, [this, Callback = MoveTemp(OnClick)]()
		{
			if (UCrossguardTradeComponent* Target = GetTrade())
			{
				Callback(*Target);
			}
		}, 12);
		Button->SetActive(bActive);
		AddTo(Buttons, Button, 6.0f);
	};

	switch (View.State)
	{
	case ECrossguardTradeState::None:
		StateLine = TEXT("No trade. Stand within 5 m of a player and ask.");
		AddControl(TEXT("Ask nearest player"), true, [this](UCrossguardTradeComponent&) { AskNearestPlayer(); });
		break;
	case ECrossguardTradeState::RequestSent:
		StateLine = FString::Printf(TEXT("Waiting for %s to answer (30 s)..."), *Partner);
		AddControl(TEXT("Withdraw request"), true, [](UCrossguardTradeComponent& Target) { Target.RequestCancel(); });
		break;
	case ECrossguardTradeState::RequestReceived:
		StateLine = FString::Printf(TEXT("%s wants to trade."), *Partner);
		AddControl(TEXT("Accept"), true, [](UCrossguardTradeComponent& Target) { Target.RespondToTrade(true); });
		AddControl(TEXT("Decline"), true, [](UCrossguardTradeComponent& Target) { Target.RespondToTrade(false); });
		break;
	case ECrossguardTradeState::Open:
	{
		const auto Side = [](bool bLocked, bool bAccepted)
		{
			return bAccepted ? TEXT("accepted") : bLocked ? TEXT("locked") : TEXT("changing");
		};
		StateLine = FString::Printf(TEXT("Trading with %s. You: %s. %s: %s. Offer %d / %d stacks. Changing an offer unlocks both sides."),
			*Partner, Side(View.bMyLocked, View.bMyAccepted), *Partner, Side(View.bTheirLocked, View.bTheirAccepted), View.MyItemIds.Num(),
			Crossguard::FTradeConfig().MaxOfferedStacks);
		const bool bLocked = View.bMyLocked;
		AddControl(bLocked ? TEXT("Unlock") : TEXT("Lock offer"), true, [bLocked](UCrossguardTradeComponent& Target) { Target.RequestLock(!bLocked); });
		AddControl(TEXT("Accept trade"), View.bMyLocked && View.bTheirLocked && !View.bMyAccepted,
			[](UCrossguardTradeComponent& Target) { Target.RequestAccept(); });
		AddControl(TEXT("Cancel trade"), true, [](UCrossguardTradeComponent& Target) { Target.RequestCancel(); });
		break;
	}
	}
	SetText(StateText, StateLine);

	if (View.State != ECrossguardTradeState::Open)
	{
		return;
	}

	// Your stacks: not offered on the left, offered in the middle (whole stacks; not the item in hand).
	for (const FCrossguardInventoryItem& Item : Inventory->GetItems())
	{
		const bool bOffered = View.MyItemIds.Contains(Item.Id);
		const bool bInHand = Item.Id == Inventory->GetReadiedItemId();
		UCrossguardUIButton* Row = MakeButton(CrossguardUI::ItemLabel(Item, Inventory, Equipment) + (bInHand ? TEXT(" [in hand]") : TEXT("")), nullptr, 10);
		const int32 Id = Item.Id;
		Row->OnClicked = [this, Id]() { ToggleOffered(Id); };
		Row->SetLabelColor(CrossguardUI::RarityColor(CrossguardUI::ItemRarity(Item)));
		// Changing the offer only works unlocked; the server would refuse it anyway.
		Row->SetActive(!View.bMyLocked && !bInHand);
		AddTo(bOffered ? MyOfferList.Get() : MyItemsList.Get(), Row);
		++(bOffered ? MyOfferRows : MyItemRows);
	}
	AddTo(MyOfferList, MakeText(FString::Printf(TEXT("Gold: %d"), View.MyGold), 11, FLinearColor(1.0f, 0.85f, 0.3f)), 6.0f);

	for (const FCrossguardInventoryItem& Item : View.TheirItems)
	{
		UTextBlock* Line = MakeText(CrossguardUI::ItemLabel(Item, Inventory, Equipment), 10, CrossguardUI::RarityColor(CrossguardUI::ItemRarity(Item)));
		AddTo(TheirOfferList, Line);
		++TheirOfferRows;
	}
	AddTo(TheirOfferList, MakeText(FString::Printf(TEXT("Gold: %d"), View.TheirGold), 11, FLinearColor(1.0f, 0.85f, 0.3f)), 6.0f);
}
