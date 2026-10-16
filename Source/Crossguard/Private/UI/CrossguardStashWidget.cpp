#include "UI/CrossguardStashWidget.h"

#include "Character/CrossguardCharacter.h"
#include "Components/HorizontalBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Game/CrossguardPlayerState.h"
#include "Items/CrossguardStashComponent.h"
#include "UI/CrossguardItemDragOperation.h"
#include "UI/CrossguardItemGridWidget.h"
#include "UI/CrossguardUIButton.h"
#include "UI/CrossguardUIHelpers.h"

namespace CrossguardStashWidgetLocal
{
	// Stash squares are smaller than the inventory's so a 10 x 30 tab scrolls less.
	constexpr float StashSquareSize = 32.0f;
	constexpr float StashViewHeight = 600.0f;
	const FLinearColor Heading(1.0f, 0.85f, 0.5f);
	const FLinearColor ShownTabColor(0.25f, 0.35f, 0.55f, 1.0f);

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

UCrossguardStashWidget::UCrossguardStashWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Cheap: redraws only when what it shows changed (BuildSignature).
	RefreshInterval = 0.1f;
}

UCrossguardStashComponent* UCrossguardStashWidget::GetStash() const
{
	const ACrossguardPlayerState* PlayerState = GetCrossguardPlayerState();
	return PlayerState != nullptr ? PlayerState->GetStash() : nullptr;
}

UCrossguardInventoryComponent* UCrossguardStashWidget::GetInventory() const
{
	const ACrossguardCharacter* Character = GetCrossguardCharacter();
	return Character != nullptr ? Character->GetInventoryComponent() : nullptr;
}

void UCrossguardStashWidget::BuildTree()
{
	using namespace CrossguardStashWidgetLocal;
	BuildFrame(TEXT("Stash   (Esc to close)"), FVector2D(1060.0f, 860.0f));

	UTextBlock* Hint = MakeText(TEXT("Drag items between your inventory and the stash, or within the stash. Or click an item to select it, "
		"then click a square. Right click: a stash stack goes to your inventory, an inventory stack to the first free stash square; "
		"Skill Gems are learned. Between raids only."), 10, FLinearColor(0.7f, 0.7f, 0.7f));
	Hint->SetAutoWrapText(true);
	AddTo(Body, Hint);

	UHorizontalBox* Columns = MakeRow();
	AddTo(Body, Columns, 8.0f);

	// The inventory grids.
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
	SelectedText = MakeText(FString(), 11, FLinearColor(0.9f, 0.9f, 0.6f));
	SelectedText->SetAutoWrapText(true);
	AddTo(InventoryColumn, SelectedText, 8.0f);
	ActionRow = MakeRow();
	AddTo(InventoryColumn, ActionRow);

	const auto BindInventoryGrid = [this](UCrossguardItemGridWidget* Grid, ECrossguardInventoryContainer Container)
	{
		Grid->OnItemClicked = [this](const FCrossguardInventoryItem& Item) { ClickItem(Item, false); };
		Grid->OnItemRightClicked = [this](const FCrossguardInventoryItem& Item) { RightClickItem(Item, false); };
		Grid->OnSquareClicked = [this, Container](FIntPoint Position) { ClickSquare(false, Container, Position); };
		Grid->OnItemDropped = [this, Container](FIntPoint Position, const UCrossguardItemDragOperation& Drag)
		{
			if (!Drag.bFromLoot)
			{
				DropOnInventory(Container, Position, Drag.Item, IsFromStash(Drag));
			}
		};
	};
	BindInventoryGrid(MainGrid, ECrossguardInventoryContainer::Main);
	BindInventoryGrid(BackpackGrid, ECrossguardInventoryContainer::Backpack);
	BindInventoryGrid(QuiverGrid, ECrossguardInventoryContainer::Quiver);

	// The stash: tabs, buy, gold, the shown tab.
	UVerticalBox* StashColumn = MakeColumn();
	AddTo(Columns, StashColumn, 10.0f);
	AddTo(StashColumn, MakeText(TEXT("Stash"), 13, Heading));
	TabRow = MakeRow();
	AddTo(StashColumn, TabRow);
	UHorizontalBox* BuyRow = MakeRow();
	AddTo(StashColumn, BuyRow, 4.0f);
	BuyTabButton = MakeButton(TEXT("Buy tab"), [this]() { BuyTab(); });
	AddTo(BuyRow, BuyTabButton);
	GoldText = MakeText(FString(), 11);
	AddTo(BuyRow, GoldText, 8.0f);

	UScrollBox* StashScroll = Make<UScrollBox>();
	USizeBox* StashSize = Make<USizeBox>();
	StashSize->SetMaxDesiredHeight(StashViewHeight);
	StashSize->SetContent(StashScroll);
	AddTo(StashColumn, StashSize, 4.0f);
	StashGrid = Make<UCrossguardItemGridWidget>();
	StashGrid->SquareSize = StashSquareSize;
	StashScroll->AddChild(StashGrid);
	StashGrid->OnItemClicked = [this](const FCrossguardInventoryItem& Item) { ClickItem(Item, true); };
	StashGrid->OnItemRightClicked = [this](const FCrossguardInventoryItem& Item) { RightClickItem(Item, true); };
	StashGrid->OnSquareClicked = [this](FIntPoint Position) { ClickSquare(true, ECrossguardInventoryContainer::Main, Position); };
	StashGrid->OnItemDropped = [this](FIntPoint Position, const UCrossguardItemDragOperation& Drag)
	{
		if (!Drag.bFromLoot)
		{
			DropOnStash(Position, Drag.Item, IsFromStash(Drag));
		}
	};
}

bool UCrossguardStashWidget::IsFromStash(const UCrossguardItemDragOperation& Drag) const
{
	// The grid that starts a drag is the operation's outer (UCrossguardItemGridWidget::StartDrag).
	return StashGrid != nullptr && Drag.GetOuter() == StashGrid;
}

int32 UCrossguardStashWidget::GetStashCellCount() const
{
	return StashGrid != nullptr ? StashGrid->GetItemCellCount() : 0;
}

int32 UCrossguardStashWidget::GetInventoryCellCount() const
{
	return (MainGrid != nullptr ? MainGrid->GetItemCellCount() : 0) + (BackpackGrid != nullptr ? BackpackGrid->GetItemCellCount() : 0)
		+ (QuiverGrid != nullptr ? QuiverGrid->GetItemCellCount() : 0);
}

bool UCrossguardStashWidget::IsBuyTabActive() const
{
	return BuyTabButton != nullptr && BuyTabButton->IsActive();
}

FString UCrossguardStashWidget::GetBuyTabLabel() const
{
	return BuyTabButton != nullptr ? BuyTabButton->GetLabel() : FString();
}

void UCrossguardStashWidget::ShowTab(int32 Tab)
{
	const UCrossguardStashComponent* Stash = GetStash();
	ShownTab = Stash != nullptr ? FMath::Clamp(Tab, 0, FMath::Max(Stash->GetTabCount() - 1, 0)) : 0;
	// Redrawn on the next refresh, not now: this runs inside a tab button's click.
	MarkDirty();
}

FString UCrossguardStashWidget::BuildSignature() const
{
	const ACrossguardCharacter* Character = GetCrossguardCharacter();
	const UCrossguardInventoryComponent* Inventory = GetInventory();
	const ACrossguardPlayerState* PlayerState = GetCrossguardPlayerState();
	const UCrossguardStashComponent* Stash = GetStash();
	if (Inventory == nullptr || Stash == nullptr || PlayerState == nullptr)
	{
		return TEXT("none");
	}
	FString Signature = FString::Printf(TEXT("%s|%d|%d|%d|%d|%d|%d;"), *GetNameSafe(Character), ShownTab, SelectedItemId, bSelectedInStash ? 1 : 0,
		Stash->GetTabCount(), PlayerState->GetGold(), PlayerState->IsInRaid() ? 1 : 0);
	const auto AddItem = [&Signature](const FCrossguardInventoryItem& Item)
	{
		Signature += FString::Printf(TEXT("%d:%d:%d:%d,%d:%d;"), Item.Id, Item.Count, static_cast<int32>(Item.Container), Item.Position.X,
			Item.Position.Y, Item.bRotated ? 1 : 0);
	};
	for (const FCrossguardInventoryItem& Item : Inventory->GetItems())
	{
		AddItem(Item);
	}
	Signature += TEXT("|C");
	AddItem(Inventory->GetEquippedContainer(ECrossguardInventoryContainer::Backpack));
	AddItem(Inventory->GetEquippedContainer(ECrossguardInventoryContainer::Quiver));
	Signature += TEXT("|S");
	if (Stash->GetTabs().IsValidIndex(ShownTab))
	{
		for (const FCrossguardInventoryItem& Item : Stash->GetTabs()[ShownTab].Items)
		{
			AddItem(Item);
		}
	}
	return Signature;
}

void UCrossguardStashWidget::NativeRefresh()
{
	const FString Signature = BuildSignature();
	if (Signature == LastSignature)
	{
		return;
	}
	LastSignature = Signature;
	Redraw();
}

void UCrossguardStashWidget::Redraw()
{
	const ACrossguardCharacter* Character = GetCrossguardCharacter();
	const UCrossguardInventoryComponent* Inventory = GetInventory();
	const UCrossguardEquipmentComponent* Equipment = Character != nullptr ? Character->GetEquipmentComponent() : nullptr;
	const UCrossguardStashComponent* Stash = GetStash();
	if (Inventory == nullptr || Stash == nullptr)
	{
		SetText(SelectedText, TEXT("No character or no stash (dead for the raid or spectating)."));
		for (UCrossguardItemGridWidget* Grid : { MainGrid.Get(), BackpackGrid.Get(), QuiverGrid.Get(), StashGrid.Get() })
		{
			Grid->SetGrid(FIntPoint::ZeroValue, {}, 0, nullptr, nullptr);
		}
		ShownTabSize = FIntPoint::ZeroValue;
		TabRow->ClearChildren();
		TabButtonCount = 0;
		ActionRow->ClearChildren();
		return;
	}

	// A tab may be gone (a save loaded with fewer); the selection may have moved or left.
	ShownTab = FMath::Clamp(ShownTab, 0, FMath::Max(Stash->GetTabCount() - 1, 0));
	FCrossguardInventoryItem Selected;
	if (SelectedItemId != 0
		&& !(bSelectedInStash ? Stash->FindStashItem(SelectedItemId, Selected) : Inventory->FindItem(SelectedItemId, Selected)))
	{
		SelectedItemId = 0;
	}
	const int32 InventorySelection = bSelectedInStash ? 0 : SelectedItemId;
	const int32 StashSelection = bSelectedInStash ? SelectedItemId : 0;

	using CrossguardStashWidgetLocal::ItemsIn;
	MainGrid->SetGrid(Inventory->GetContainerSize(ECrossguardInventoryContainer::Main), ItemsIn(*Inventory, ECrossguardInventoryContainer::Main),
		InventorySelection, Inventory, Equipment);
	const FIntPoint BackpackSize = Inventory->GetContainerSize(ECrossguardInventoryContainer::Backpack);
	const FIntPoint QuiverSize = Inventory->GetContainerSize(ECrossguardInventoryContainer::Quiver);
	BackpackGrid->SetGrid(BackpackSize, ItemsIn(*Inventory, ECrossguardInventoryContainer::Backpack), InventorySelection, Inventory, Equipment);
	QuiverGrid->SetGrid(QuiverSize, ItemsIn(*Inventory, ECrossguardInventoryContainer::Quiver), InventorySelection, Inventory, Equipment);
	const ESlateVisibility BackpackShown = BackpackSize.X > 0 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	const ESlateVisibility QuiverShown = QuiverSize.X > 0 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	BackpackLabel->SetVisibility(BackpackShown);
	BackpackGrid->SetVisibility(BackpackShown);
	QuiverLabel->SetVisibility(QuiverShown);
	QuiverGrid->SetVisibility(QuiverShown);

	ShownTabSize = Stash->GetTabSize();
	const TArray<FCrossguardInventoryItem> NoItems;
	const TArray<FCrossguardInventoryItem>& TabItems = Stash->GetTabs().IsValidIndex(ShownTab) ? Stash->GetTabs()[ShownTab].Items : NoItems;
	StashGrid->SetGrid(ShownTabSize, TabItems, StashSelection, Inventory, Equipment);

	RedrawTabs();
	RedrawActions();
}

void UCrossguardStashWidget::RedrawTabs()
{
	TabRow->ClearChildren();
	TabButtonCount = 0;
	const UCrossguardStashComponent* Stash = GetStash();
	const ACrossguardPlayerState* PlayerState = GetCrossguardPlayerState();
	if (Stash == nullptr || PlayerState == nullptr)
	{
		return;
	}
	for (int32 Tab = 0; Tab < Stash->GetTabCount(); ++Tab)
	{
		UCrossguardUIButton* TabButton = MakeButton(FString::Printf(TEXT("Tab %d"), Tab + 1), [this, Tab]() { ShowTab(Tab); });
		if (Tab == ShownTab)
		{
			TabButton->SetColor(CrossguardStashWidgetLocal::ShownTabColor);
		}
		AddTo(TabRow, TabButton);
		++TabButtonCount;
	}

	// Buy tab: the same checks as the server (Crossguard::CanBuyStashTab), as a hint.
	const Crossguard::FInventoryConfig Config;
	const int32 Bought = Stash->GetPurchasedTabCount();
	const int32 Gold = PlayerState->GetGold();
	const bool bAtCap = Bought >= Config.MaxPurchasedStashTabs;
	if (bAtCap)
	{
		BuyTabButton->SetLabel(FString::Printf(TEXT("No more tabs (%d / %d bought)"), Bought, Config.MaxPurchasedStashTabs));
		BuyTabButton->SetToolTipText(FText::FromString(TEXT("Every tab a character can buy is bought")));
	}
	else
	{
		BuyTabButton->SetLabel(FString::Printf(TEXT("Buy tab: %d gold (%d / %d bought)"), Config.StashTabPrice, Bought, Config.MaxPurchasedStashTabs));
		const FString Why = PlayerState->IsInRaid() ? FString(TEXT("Between raids only"))
			: Gold < Config.StashTabPrice ? FString::Printf(TEXT("Not enough banked gold (%d more needed)"), Config.StashTabPrice - Gold)
										  : FString(TEXT("Adds an empty tab"));
		BuyTabButton->SetToolTipText(FText::FromString(Why));
	}
	BuyTabButton->SetActive(!bAtCap && Gold >= Config.StashTabPrice && !PlayerState->IsInRaid());
	GoldLine = FString::Printf(TEXT("Banked gold: %d"), Gold);
	SetText(GoldText, GoldLine);
}

void UCrossguardStashWidget::RedrawActions()
{
	ActionRow->ClearChildren();
	const ACrossguardCharacter* Character = GetCrossguardCharacter();
	const UCrossguardInventoryComponent* Inventory = GetInventory();
	const UCrossguardStashComponent* Stash = GetStash();
	FCrossguardInventoryItem Selected;
	if (Inventory == nullptr || Stash == nullptr || SelectedItemId == 0
		|| !(bSelectedInStash ? Stash->FindStashItem(SelectedItemId, Selected) : Inventory->FindItem(SelectedItemId, Selected)))
	{
		SetText(SelectedText, TEXT("Nothing selected."));
		return;
	}
	SetText(SelectedText, FString::Printf(TEXT("Selected (%s): %s"), bSelectedInStash ? TEXT("stash") : TEXT("inventory"),
		*CrossguardUI::ItemLabel(Selected, Inventory, Character != nullptr ? Character->GetEquipmentComponent() : nullptr)));

	const int32 Id = Selected.Id;
	const auto AddAction = [this](const FString& Label, TFunction<void()> OnClick)
	{
		AddTo(ActionRow, MakeButton(Label, [this, Callback = MoveTemp(OnClick)]()
		{
			Callback();
			MarkDirty();
		}, 10));
	};
	if (bSelectedInStash)
	{
		AddAction(TEXT("Take to inventory"), [this, Id]()
		{
			if (UCrossguardStashComponent* Target = GetStash())
			{
				Target->RequestRetrieve(Id);
			}
			SelectedItemId = 0;
		});
	}
	else
	{
		AddAction(TEXT("Store (first free square)"), [this, Id]()
		{
			if (UCrossguardStashComponent* Target = GetStash())
			{
				Target->RequestStoreAnywhere(Id);
			}
			SelectedItemId = 0;
		});
	}
	if (Selected.Category == ECrossguardItemCategory::SkillGem)
	{
		const bool bInStash = bSelectedInStash;
		AddAction(TEXT("Learn skill"), [this, Id, bInStash]()
		{
			if (bInStash)
			{
				if (UCrossguardStashComponent* Target = GetStash())
				{
					Target->RequestUseStashItem(Id);
				}
			}
			else if (UCrossguardInventoryComponent* Target = GetInventory())
			{
				Target->RequestUseItem(Id);
			}
		});
	}
	AddAction(TEXT("Deselect"), [this]() { SelectedItemId = 0; });
}

void UCrossguardStashWidget::Select(int32 ItemId, bool bInStash)
{
	SelectedItemId = ItemId;
	bSelectedInStash = bInStash;
	MarkDirty();
}

void UCrossguardStashWidget::ClickItem(const FCrossguardInventoryItem& Item, bool bInStash)
{
	if (SelectedItemId == Item.Id && bSelectedInStash == bInStash)
	{
		Select(0, false);
		return;
	}
	if (SelectedItemId != 0)
	{
		// Onto another item: as dropping the selection on that item's top-left square (merges a stack
		// of the same kind; anything else is refused by the server as an overlap).
		FCrossguardInventoryItem Selected;
		const UCrossguardStashComponent* Stash = GetStash();
		const UCrossguardInventoryComponent* Inventory = GetInventory();
		const bool bFound = bSelectedInStash ? Stash != nullptr && Stash->FindStashItem(SelectedItemId, Selected)
											 : Inventory != nullptr && Inventory->FindItem(SelectedItemId, Selected);
		if (bFound && Selected.CanStackWith(Item))
		{
			if (bInStash)
			{
				DropOnStash(Item.Position, Selected, bSelectedInStash);
			}
			else
			{
				DropOnInventory(Item.Container, Item.Position, Selected, bSelectedInStash);
			}
			return;
		}
	}
	Select(Item.Id, bInStash);
}

void UCrossguardStashWidget::RightClickItem(const FCrossguardInventoryItem& Item, bool bInStash)
{
	const bool bGem = Item.Category == ECrossguardItemCategory::SkillGem;
	if (bInStash)
	{
		if (UCrossguardStashComponent* Stash = GetStash())
		{
			if (bGem)
			{
				Stash->RequestUseStashItem(Item.Id);
			}
			else
			{
				Stash->RequestRetrieve(Item.Id);
			}
		}
	}
	else if (bGem)
	{
		if (UCrossguardInventoryComponent* Inventory = GetInventory())
		{
			Inventory->RequestUseItem(Item.Id);
		}
	}
	else if (UCrossguardStashComponent* Stash = GetStash())
	{
		Stash->RequestStoreAnywhere(Item.Id);
	}
	Select(0, false);
}

void UCrossguardStashWidget::ClickSquare(bool bStashSquare, ECrossguardInventoryContainer Container, FIntPoint Position)
{
	if (SelectedItemId == 0)
	{
		return;
	}
	FCrossguardInventoryItem Selected;
	const UCrossguardStashComponent* Stash = GetStash();
	const UCrossguardInventoryComponent* Inventory = GetInventory();
	const bool bFound = bSelectedInStash ? Stash != nullptr && Stash->FindStashItem(SelectedItemId, Selected)
										 : Inventory != nullptr && Inventory->FindItem(SelectedItemId, Selected);
	if (!bFound)
	{
		Select(0, false);
		return;
	}
	if (bStashSquare)
	{
		DropOnStash(Position, Selected, bSelectedInStash);
	}
	else
	{
		DropOnInventory(Container, Position, Selected, bSelectedInStash);
	}
}

void UCrossguardStashWidget::DropOnStash(FIntPoint Position, const FCrossguardInventoryItem& Item, bool bFromStash)
{
	if (UCrossguardStashComponent* Stash = GetStash())
	{
		if (bFromStash)
		{
			Stash->RequestMoveInStash(Item.Id, ShownTab, Position, Item.bRotated);
		}
		else
		{
			Stash->RequestStore(Item.Id, ShownTab, Position, Item.bRotated);
		}
	}
	Select(0, false);
}

void UCrossguardStashWidget::DropOnInventory(ECrossguardInventoryContainer Container, FIntPoint Position, const FCrossguardInventoryItem& Item, bool bFromStash)
{
	if (bFromStash)
	{
		if (UCrossguardStashComponent* Stash = GetStash())
		{
			Stash->RequestRetrieve(Item.Id);
		}
	}
	else if (UCrossguardInventoryComponent* Inventory = GetInventory())
	{
		Inventory->RequestMoveItem(Item.Id, Container, Position, Item.bRotated);
	}
	Select(0, false);
}

void UCrossguardStashWidget::BuyTab()
{
	if (UCrossguardStashComponent* Stash = GetStash())
	{
		Stash->RequestBuyStashTab();
	}
	MarkDirty();
}

void UCrossguardStashWidget::OnWindowOpened()
{
	ShowStatus(FString(), FLinearColor::White);
	MarkDirty();
}

void UCrossguardStashWidget::OnWindowClosed()
{
	SelectedItemId = 0;
	bSelectedInStash = false;
	MarkDirty();
}
