#include "UI/CrossguardInventoryWidget.h"

#include "Character/CrossguardCharacter.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/DataTable.h"
#include "Items/CrossguardEquipmentComponent.h"
#include "Items/CrossguardLootContainer.h"
#include "UI/CrossguardItemDragOperation.h"
#include "UI/CrossguardItemGridWidget.h"
#include "UI/CrossguardUIButton.h"
#include "UI/CrossguardUIHelpers.h"

namespace CrossguardInventoryWidgetLocal
{
	constexpr int32 ArmorSlotCount = 6;
	constexpr int32 HotbarKeyCount = 5;

	// A shield goes in the off hand.
	bool IsShield(const FCrossguardWeaponItem& Weapon, const UCrossguardEquipmentComponent* Equipment)
	{
		const UDataTable* Table = Equipment != nullptr ? Equipment->WeaponTable.Get() : nullptr;
		const FCrossguardWeaponRow* Row = Table != nullptr ? Table->FindRow<FCrossguardWeaponRow>(Weapon.WeaponId, TEXT("UI"), false) : nullptr;
		return Row != nullptr && Row->Stat == ECrossguardWeaponStat::AttackResistance;
	}

	FString DescribeItem(const FCrossguardInventoryItem& Item, const UCrossguardEquipmentComponent* Equipment)
	{
		TArray<FString> Affixes;
		const TArray<FCrossguardAffix>& List = Item.Category == ECrossguardItemCategory::Weapon ? Item.Weapon.Affixes : Item.Armor.Affixes;
		if (Item.Category == ECrossguardItemCategory::Weapon || Item.Category == ECrossguardItemCategory::Armor)
		{
			const UDataTable* Table = Equipment != nullptr ? Equipment->AffixTable.Get() : nullptr;
			for (const FCrossguardAffix& Affix : List)
			{
				const FCrossguardAffixRow* Row = Table != nullptr ? Table->FindRow<FCrossguardAffixRow>(Affix.AffixId, TEXT("UI"), false) : nullptr;
				const FString AffixName = Row != nullptr && !Row->DisplayName.IsEmpty() ? Row->DisplayName.ToString() : Affix.AffixId.ToString();
				Affixes.Add(FString::Printf(TEXT("%s %s%g"), *AffixName, Affix.Value >= 0.0f ? TEXT("+") : TEXT(""), Affix.Value));
			}
		}
		FString Text = FString::Printf(TEXT("%s, %dx%d"), *CrossguardUI::UIEnumName(Item.Category), Item.Size.X, Item.Size.Y);
		if (Item.MaxStack > 1)
		{
			Text += FString::Printf(TEXT(", stack %d / %d"), Item.Count, Item.MaxStack);
		}
		if (!Affixes.IsEmpty())
		{
			Text += TEXT("\n") + FString::Join(Affixes, TEXT(", "));
		}
		return Text;
	}
}

UCrossguardInventoryWidget::UCrossguardInventoryWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Cheap: redraws only when what it shows changed (BuildSignature).
	RefreshInterval = 0.1f;
}

UCrossguardInventoryComponent* UCrossguardInventoryWidget::GetInventory() const
{
	const ACrossguardCharacter* Character = GetCrossguardCharacter();
	return Character != nullptr ? Character->GetInventoryComponent() : nullptr;
}

void UCrossguardInventoryWidget::BuildTree()
{
	BuildFrame(TEXT("Inventory   (I / Tab to close)"), FVector2D(1240.0f, 700.0f));

	UTextBlock* Hint = MakeText(TEXT("Drag an item to a square, onto a stack of the same kind to merge, onto an equipment slot to equip it, "
		"or between your inventory and an open body or chest. Or click it to select it, then click where it goes. "
		"Right click: equip / take in hand / learn. With nothing selected, click a worn item to take it off."), 10,
		FLinearColor(0.7f, 0.7f, 0.7f));
	Hint->SetAutoWrapText(true);
	AddTo(Body, Hint);

	UHorizontalBox* Columns = MakeRow();
	AddTo(Body, Columns, 8.0f);

	// Equipment.
	UVerticalBox* EquipmentColumn = MakeColumn();
	USizeBox* EquipmentSize = Make<USizeBox>();
	EquipmentSize->SetWidthOverride(290.0f);
	EquipmentSize->SetContent(EquipmentColumn);
	AddTo(Columns, EquipmentSize, 8.0f);
	AddTo(EquipmentColumn, MakeText(TEXT("Equipment"), 13, FLinearColor(1.0f, 0.85f, 0.5f)));
	EquipmentList = MakeColumn();
	AddTo(EquipmentColumn, EquipmentList);

	// Inventory grids, selection and actions.
	UVerticalBox* GridColumn = MakeColumn();
	AddTo(Columns, GridColumn, 8.0f);
	AddTo(GridColumn, MakeText(TEXT("Inventory"), 13, FLinearColor(1.0f, 0.85f, 0.5f)));
	MainGrid = Make<UCrossguardItemGridWidget>();
	AddTo(GridColumn, MainGrid);
	UHorizontalBox* ExtraGrids = MakeRow();
	AddTo(GridColumn, ExtraGrids, 6.0f);
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
	AddTo(GridColumn, SelectedText, 6.0f);
	ActionList = MakeColumn();
	AddTo(GridColumn, ActionList);

	const auto BindGrid = [this](UCrossguardItemGridWidget* Grid, ECrossguardInventoryContainer Container)
	{
		Grid->OnItemClicked = [this](const FCrossguardInventoryItem& Item) { ClickItem(Item); };
		Grid->OnItemRightClicked = [this](const FCrossguardInventoryItem& Item) { RightClickItem(Item); };
		Grid->OnSquareClicked = [this, Container](FIntPoint Position) { ClickSquare(Container, Position); };
		Grid->OnItemDropped = [this, Container](FIntPoint Position, const UCrossguardItemDragOperation& Drag)
		{
			DropItem(Container, Position, Drag.Item, Drag.bFromLoot);
		};
	};
	BindGrid(MainGrid, ECrossguardInventoryContainer::Main);
	BindGrid(BackpackGrid, ECrossguardInventoryContainer::Backpack);
	BindGrid(QuiverGrid, ECrossguardInventoryContainer::Quiver);

	// The open body or chest.
	LootColumn = MakeColumn();
	AddTo(Columns, LootColumn, 8.0f);
	LootTitle = MakeText(TEXT("Loot"), 13, FLinearColor(1.0f, 0.85f, 0.5f));
	AddTo(LootColumn, LootTitle);
	UHorizontalBox* LootButtons = MakeRow();
	AddTo(LootColumn, LootButtons);
	AddTo(LootButtons, MakeButton(TEXT("Take all"), [this]() { TakeAll(); }));
	AddTo(LootButtons, MakeButton(TEXT("Close it"), [this]()
	{
		if (UCrossguardInventoryComponent* Inventory = GetInventory())
		{
			Inventory->RequestCloseContainer();
		}
	}));
	UScrollBox* LootScroll = Make<UScrollBox>();
	USizeBox* LootSize = Make<USizeBox>();
	LootSize->SetMaxDesiredHeight(520.0f);
	LootSize->SetContent(LootScroll);
	AddTo(LootColumn, LootSize);
	LootGrid = Make<UCrossguardItemGridWidget>();
	LootGrid->SquareSize = 36.0f;
	LootScroll->AddChild(LootGrid);
	// Taking is the only thing to do with a loot cell: either click takes the stack.
	LootGrid->OnItemClicked = [this](const FCrossguardInventoryItem& Item)
	{
		if (UCrossguardInventoryComponent* Inventory = GetInventory())
		{
			Inventory->RequestTakeFromContainer(Item.Id);
		}
	};
	LootGrid->OnItemRightClicked = LootGrid->OnItemClicked;
	LootGrid->bLootGrid = true;
	LootGrid->OnItemDropped = [this](FIntPoint, const UCrossguardItemDragOperation& Drag) { DropOnLoot(Drag.Item, Drag.bFromLoot); };
}

int32 UCrossguardInventoryWidget::GetItemCellCount() const
{
	return (MainGrid != nullptr ? MainGrid->GetItemCellCount() : 0) + (BackpackGrid != nullptr ? BackpackGrid->GetItemCellCount() : 0)
		+ (QuiverGrid != nullptr ? QuiverGrid->GetItemCellCount() : 0);
}

int32 UCrossguardInventoryWidget::GetLootCellCount() const
{
	const UCrossguardInventoryComponent* Inventory = GetInventory();
	return LootGrid != nullptr && Inventory != nullptr && Inventory->GetOpenContainer() != nullptr ? LootGrid->GetItemCellCount() : 0;
}

FString UCrossguardInventoryWidget::BuildSignature() const
{
	const ACrossguardCharacter* Character = GetCrossguardCharacter();
	const UCrossguardInventoryComponent* Inventory = Character != nullptr ? Character->GetInventoryComponent() : nullptr;
	const UCrossguardEquipmentComponent* Equipment = Character != nullptr ? Character->GetEquipmentComponent() : nullptr;
	if (Inventory == nullptr || Equipment == nullptr)
	{
		return TEXT("none");
	}
	FString Signature = FString::Printf(TEXT("%s|%d|%d|%d|%d|%d;"), *GetNameSafe(Character), SelectedItemId, bPlaceRotated ? 1 : 0,
		Inventory->GetReadiedItemId(), Equipment->GetActiveWeaponSet(), Equipment->AreWeaponsReady() ? 1 : 0);
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
	Signature += TEXT("|E");
	for (int32 Set = 0; Set < 2; ++Set)
	{
		Signature += Equipment->GetWeaponInSet(Set, ECrossguardEquipmentSlot::MainHand).WeaponId.ToString() + TEXT(",")
			+ Equipment->GetWeaponInSet(Set, ECrossguardEquipmentSlot::OffHand).WeaponId.ToString() + TEXT(",");
	}
	for (int32 Index = 0; Index < CrossguardInventoryWidgetLocal::ArmorSlotCount; ++Index)
	{
		Signature += Equipment->GetArmorItem(static_cast<ECrossguardArmorSlot>(Index)).ArmorId.ToString() + TEXT(",");
	}
	Signature += TEXT("|H");
	for (const FCrossguardHotbarSlot& HotbarSlot : Inventory->GetHotbar())
	{
		Signature += HotbarSlot.ItemId.ToString() + TEXT(",");
	}
	Signature += FString::Printf(TEXT("|L%s;"), *GetNameSafe(Inventory->GetOpenContainer()));
	for (const FCrossguardInventoryItem& Item : Inventory->GetOpenContainerItems())
	{
		AddItem(Item);
	}
	return Signature;
}

void UCrossguardInventoryWidget::NativeRefresh()
{
	const FString Signature = BuildSignature();
	if (Signature == LastSignature)
	{
		return;
	}
	LastSignature = Signature;
	Redraw();
}

void UCrossguardInventoryWidget::Redraw()
{
	ACrossguardCharacter* Character = GetCrossguardCharacter();
	UCrossguardInventoryComponent* Inventory = Character != nullptr ? Character->GetInventoryComponent() : nullptr;
	const UCrossguardEquipmentComponent* Equipment = Character != nullptr ? Character->GetEquipmentComponent() : nullptr;
	if (Inventory == nullptr)
	{
		SetText(SelectedText, TEXT("No character (dead for the raid or spectating)."));
		MainGrid->SetGrid(FIntPoint::ZeroValue, {}, 0, nullptr, nullptr);
		BackpackGrid->SetGrid(FIntPoint::ZeroValue, {}, 0, nullptr, nullptr);
		QuiverGrid->SetGrid(FIntPoint::ZeroValue, {}, 0, nullptr, nullptr);
		LootGrid->SetGrid(FIntPoint::ZeroValue, {}, 0, nullptr, nullptr);
		EquipmentList->ClearChildren();
		ActionList->ClearChildren();
		LootColumn->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	// The selection may have left the inventory (used up, dropped, traded).
	FCrossguardInventoryItem Selected;
	if (SelectedItemId != 0 && !Inventory->FindItem(SelectedItemId, Selected))
	{
		SelectedItemId = 0;
	}

	const auto ItemsIn = [Inventory](ECrossguardInventoryContainer Container)
	{
		TArray<FCrossguardInventoryItem> Result;
		for (const FCrossguardInventoryItem& Item : Inventory->GetItems())
		{
			if (Item.Container == Container)
			{
				Result.Add(Item);
			}
		}
		return Result;
	};
	MainGrid->SetGrid(Inventory->GetContainerSize(ECrossguardInventoryContainer::Main), ItemsIn(ECrossguardInventoryContainer::Main),
		SelectedItemId, Inventory, Equipment);
	const FIntPoint BackpackSize = Inventory->GetContainerSize(ECrossguardInventoryContainer::Backpack);
	const FIntPoint QuiverSize = Inventory->GetContainerSize(ECrossguardInventoryContainer::Quiver);
	BackpackGrid->SetGrid(BackpackSize, ItemsIn(ECrossguardInventoryContainer::Backpack), SelectedItemId, Inventory, Equipment);
	QuiverGrid->SetGrid(QuiverSize, ItemsIn(ECrossguardInventoryContainer::Quiver), SelectedItemId, Inventory, Equipment);
	const ESlateVisibility BackpackShown = BackpackSize.X > 0 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	const ESlateVisibility QuiverShown = QuiverSize.X > 0 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	BackpackLabel->SetVisibility(BackpackShown);
	BackpackGrid->SetVisibility(BackpackShown);
	QuiverLabel->SetVisibility(QuiverShown);
	QuiverGrid->SetVisibility(QuiverShown);

	// Loot side by side while a body or chest is open.
	const ACrossguardLootContainer* Open = Inventory->GetOpenContainer();
	LootColumn->SetVisibility(Open != nullptr ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (Open != nullptr)
	{
		SetText(LootTitle, Open->LootTableId.IsNone() ? TEXT("Body (click a stack to take it)") : TEXT("Chest (click a stack to take it)"));
		LootGrid->SetGrid(Open->GetGridSize(), Inventory->GetOpenContainerItems(), 0, Inventory, Equipment);
	}
	else
	{
		LootGrid->SetGrid(FIntPoint::ZeroValue, {}, 0, Inventory, Equipment);
	}

	RedrawEquipment();
	RedrawActions();
}

void UCrossguardInventoryWidget::RedrawEquipment()
{
	EquipmentList->ClearChildren();
	ACrossguardCharacter* Character = GetCrossguardCharacter();
	UCrossguardInventoryComponent* Inventory = Character != nullptr ? Character->GetInventoryComponent() : nullptr;
	const UCrossguardEquipmentComponent* Equipment = Character != nullptr ? Character->GetEquipmentComponent() : nullptr;
	if (Inventory == nullptr || Equipment == nullptr)
	{
		return;
	}
	FCrossguardInventoryItem Selected;
	const bool bHasSelected = SelectedItemId != 0 && Inventory->FindItem(SelectedItemId, Selected);
	// Callbacks look the inventory up again when clicked (the character may have changed since).
	// OnDrop: an item of the slot's kind dragged onto it (one from the open body is taken instead).
	const auto AddSlot = [this](const FString& Label, ECrossguardRarity Rarity, bool bEmpty, TFunction<void(UCrossguardInventoryComponent&)> OnClick,
		TFunction<void(UCrossguardInventoryComponent&, const FCrossguardInventoryItem&)> OnDrop)
	{
		UCrossguardUIButton* Button = MakeButton(Label, [this, Callback = MoveTemp(OnClick)]()
		{
			if (UCrossguardInventoryComponent* Target = GetInventory())
			{
				Callback(*Target);
			}
			MarkDirty();
		}, 10);
		Button->OnDropped = [this, Callback = MoveTemp(OnDrop)](UDragDropOperation* Operation)
		{
			const UCrossguardItemDragOperation* Drag = Cast<UCrossguardItemDragOperation>(Operation);
			UCrossguardInventoryComponent* Target = GetInventory();
			if (Drag == nullptr || Target == nullptr)
			{
				return false;
			}
			if (Drag->bFromLoot)
			{
				Target->RequestTakeFromContainer(Drag->Item.Id);
			}
			else
			{
				Callback(*Target, Drag->Item);
			}
			Select(0);
			return true;
		};
		if (!bEmpty)
		{
			const FLinearColor Color = CrossguardUI::RarityColor(Rarity);
			Button->SetLabelColor(Color);
		}
		AddTo(EquipmentList, Button, 2.0f);
	};

	// Weapon sets: equip the selected weapon, or take the weapon off.
	for (int32 Set = 0; Set < 2; ++Set)
	{
		for (const ECrossguardEquipmentSlot Hand : { ECrossguardEquipmentSlot::MainHand, ECrossguardEquipmentSlot::OffHand })
		{
			const FCrossguardWeaponItem& Weapon = Equipment->GetWeaponInSet(Set, Hand);
			const FString Name = Weapon.IsEmpty() ? FString(TEXT("-")) : CrossguardUI::WeaponName(Weapon, Equipment);
			const FString Label = FString::Printf(TEXT("Set %d%s %s: %s"), Set + 1, Set == Equipment->GetActiveWeaponSet() ? TEXT("*") : TEXT(""),
				Hand == ECrossguardEquipmentSlot::MainHand ? TEXT("Main") : TEXT("Off"), *Name);
			const bool bWeaponSelected = bHasSelected && Selected.Category == ECrossguardItemCategory::Weapon;
			const int32 SelectedId = SelectedItemId;
			const bool bOccupied = !Weapon.IsEmpty();
			AddSlot(Label, Weapon.Rarity, Weapon.IsEmpty(), [this, Set, Hand, bWeaponSelected, SelectedId, bOccupied](UCrossguardInventoryComponent& Target)
			{
				if (bWeaponSelected)
				{
					Target.RequestEquipItem(SelectedId, Hand, Set);
					SelectedItemId = 0;
				}
				else if (bOccupied)
				{
					Target.RequestUnequipWeapon(Hand, Set);
				}
			},
			[Set, Hand](UCrossguardInventoryComponent& Target, const FCrossguardInventoryItem& Dropped)
			{
				if (Dropped.Category == ECrossguardItemCategory::Weapon)
				{
					Target.RequestEquipItem(Dropped.Id, Hand, Set);
				}
			});
		}
	}

	// Armor.
	for (int32 Index = 0; Index < CrossguardInventoryWidgetLocal::ArmorSlotCount; ++Index)
	{
		const ECrossguardArmorSlot ArmorSlot = static_cast<ECrossguardArmorSlot>(Index);
		const FCrossguardArmorItem& Armor = Equipment->GetArmorItem(ArmorSlot);
		const FString Label = FString::Printf(TEXT("%s: %s"), *CrossguardUI::UIEnumName(ArmorSlot),
			Armor.IsEmpty() ? TEXT("-") : *CrossguardUI::ArmorName(Armor, Equipment));
		const bool bArmorSelected = bHasSelected && Selected.Category == ECrossguardItemCategory::Armor;
		const int32 SelectedId = SelectedItemId;
		const bool bOccupied = !Armor.IsEmpty();
		AddSlot(Label, Armor.Rarity, Armor.IsEmpty(), [this, ArmorSlot, bArmorSelected, SelectedId, bOccupied](UCrossguardInventoryComponent& Target)
		{
			if (bArmorSelected)
			{
				Target.RequestEquipItem(SelectedId);
				SelectedItemId = 0;
			}
			else if (bOccupied)
			{
				Target.RequestUnequipArmor(ArmorSlot);
			}
		},
		[](UCrossguardInventoryComponent& Target, const FCrossguardInventoryItem& Dropped)
		{
			if (Dropped.Category == ECrossguardItemCategory::Armor)
			{
				Target.RequestEquipItem(Dropped.Id);
			}
		});
	}

	// Backpack and Quiver.
	for (const ECrossguardInventoryContainer Kind : { ECrossguardInventoryContainer::Backpack, ECrossguardInventoryContainer::Quiver })
	{
		const FCrossguardInventoryItem& Worn = Inventory->GetEquippedContainer(Kind);
		const FIntPoint Size = Inventory->GetContainerSize(Kind);
		const FString Label = Worn.IsEmpty() ? FString::Printf(TEXT("%s: -"), *CrossguardUI::UIEnumName(Kind))
			: FString::Printf(TEXT("%s: %s (%d squares)"), *CrossguardUI::UIEnumName(Kind), *CrossguardUI::UIEnumName(Worn.Rarity), Size.X * Size.Y);
		const bool bContainerSelected = bHasSelected && Selected.Category == ECrossguardItemCategory::Container;
		const int32 SelectedId = SelectedItemId;
		const bool bOccupied = !Worn.IsEmpty();
		AddSlot(Label, Worn.Rarity, Worn.IsEmpty(), [this, Kind, bContainerSelected, SelectedId, bOccupied](UCrossguardInventoryComponent& Target)
		{
			if (bContainerSelected)
			{
				Target.RequestEquipItem(SelectedId);
				SelectedItemId = 0;
			}
			else if (bOccupied)
			{
				// Must be empty (the server says so otherwise).
				Target.RequestUnequipContainer(Kind);
			}
		},
		[](UCrossguardInventoryComponent& Target, const FCrossguardInventoryItem& Dropped)
		{
			if (Dropped.Category == ECrossguardItemCategory::Container)
			{
				Target.RequestEquipItem(Dropped.Id);
			}
		});
	}

	// What is in hand.
	FCrossguardInventoryItem InHand;
	const FString HandLine = Inventory->GetReadiedItemId() != 0 && Inventory->FindItem(Inventory->GetReadiedItemId(), InHand)
		? FString::Printf(TEXT("In hand: %s"), *CrossguardUI::ItemLabel(InHand, Inventory, Equipment))
		: FString(Equipment->AreWeaponsReady() ? TEXT("In hand: weapon set") : TEXT("In hand: nothing"));
	AddTo(EquipmentList, MakeText(HandLine, 10, FLinearColor(0.8f, 0.8f, 0.8f)), 6.0f);
}

void UCrossguardInventoryWidget::RedrawActions()
{
	ActionList->ClearChildren();
	ACrossguardCharacter* Character = GetCrossguardCharacter();
	UCrossguardInventoryComponent* Inventory = Character != nullptr ? Character->GetInventoryComponent() : nullptr;
	const UCrossguardEquipmentComponent* Equipment = Character != nullptr ? Character->GetEquipmentComponent() : nullptr;
	if (Inventory == nullptr)
	{
		return;
	}

	// Hotbar keys 3-7: click one to clear it.
	UHorizontalBox* HotbarRow = MakeRow();
	AddTo(HotbarRow, MakeText(TEXT("Hotbar:"), 10));
	for (int32 Index = 0; Index < CrossguardInventoryWidgetLocal::HotbarKeyCount; ++Index)
	{
		const TArray<FCrossguardHotbarSlot>& Hotbar = Inventory->GetHotbar();
		FString Label = FString::Printf(TEXT("%d: -"), Index + 3);
		if (Hotbar.IsValidIndex(Index) && !Hotbar[Index].IsEmpty())
		{
			FCrossguardInventoryItem Shown;
			Shown.Category = Hotbar[Index].Category;
			Shown.ItemId = Hotbar[Index].ItemId;
			Shown.Rarity = Hotbar[Index].Rarity;
			Label = FString::Printf(TEXT("%d: %s"), Index + 3, *CrossguardUI::ItemName(Shown, Inventory, Equipment));
		}
		UCrossguardUIButton* Key = MakeButton(Label, [this, Index]()
		{
			if (UCrossguardInventoryComponent* Target = GetInventory())
			{
				Target->RequestClearHotbarSlot(Index);
			}
			MarkDirty();
		}, 9);
		Key->SetToolTipText(FText::FromString(TEXT("Click to clear this key")));
		AddTo(HotbarRow, Key);
	}
	AddTo(ActionList, HotbarRow);

	FCrossguardInventoryItem Selected;
	if (SelectedItemId == 0 || !Inventory->FindItem(SelectedItemId, Selected))
	{
		SetText(SelectedText, TEXT("Nothing selected."));
		return;
	}
	SetText(SelectedText, FString::Printf(TEXT("Selected: %s\n%s"), *CrossguardUI::ItemLabel(Selected, Inventory, Equipment),
		*CrossguardInventoryWidgetLocal::DescribeItem(Selected, Equipment)));

	UHorizontalBox* Row = MakeRow();
	AddTo(ActionList, Row);
	const int32 Id = Selected.Id;
	// Callbacks look the inventory up again when clicked (the character may have changed since).
	const auto AddAction = [this, Row](const FString& Label, TFunction<void(UCrossguardInventoryComponent&)> OnClick)
	{
		AddTo(Row, MakeButton(Label, [this, Callback = MoveTemp(OnClick)]()
		{
			if (UCrossguardInventoryComponent* Target = GetInventory())
			{
				Callback(*Target);
			}
			MarkDirty();
		}, 10));
	};

	switch (Selected.Category)
	{
	case ECrossguardItemCategory::Weapon:
		AddAction(TEXT("Main hand"), [Id](UCrossguardInventoryComponent& Target) { Target.RequestEquipItem(Id, ECrossguardEquipmentSlot::MainHand, -1); });
		AddAction(TEXT("Off hand"), [Id](UCrossguardInventoryComponent& Target) { Target.RequestEquipItem(Id, ECrossguardEquipmentSlot::OffHand, -1); });
		break;
	case ECrossguardItemCategory::Armor:
	case ECrossguardItemCategory::Container:
		AddAction(TEXT("Equip"), [Id](UCrossguardInventoryComponent& Target) { Target.RequestEquipItem(Id); });
		break;
	case ECrossguardItemCategory::Consumable:
	case ECrossguardItemCategory::Throwable:
		AddAction(TEXT("Take in hand"), [Id](UCrossguardInventoryComponent& Target) { Target.RequestReadyItem(Id); });
		for (int32 Index = 0; Index < CrossguardInventoryWidgetLocal::HotbarKeyCount; ++Index)
		{
			AddAction(FString::Printf(TEXT("Key %d"), Index + 3), [Id, Index](UCrossguardInventoryComponent& Target) { Target.RequestAssignHotbarSlot(Index, Id); });
		}
		break;
	case ECrossguardItemCategory::SkillGem:
		AddAction(TEXT("Learn (between raids)"), [Id](UCrossguardInventoryComponent& Target) { Target.RequestUseItem(Id); });
		break;
	default:
		break;
	}
	AddAction(bPlaceRotated ? TEXT("Rotate: on") : TEXT("Rotate: off"), [this](UCrossguardInventoryComponent&) { bPlaceRotated = !bPlaceRotated; });
	AddAction(TEXT("Drop"), [this, Id](UCrossguardInventoryComponent& Target)
	{
		Target.RequestDropItem(Id);
		SelectedItemId = 0;
	});
	if (Inventory->GetOpenContainer() != nullptr)
	{
		AddAction(TEXT("Put in loot"), [this, Id](UCrossguardInventoryComponent& Target)
		{
			Target.RequestPutInContainer(Id);
			SelectedItemId = 0;
		});
	}
	AddAction(TEXT("Deselect"), [this](UCrossguardInventoryComponent&) { SelectedItemId = 0; });
}

void UCrossguardInventoryWidget::Select(int32 ItemId)
{
	SelectedItemId = ItemId;
	FCrossguardInventoryItem Item;
	const UCrossguardInventoryComponent* Inventory = GetInventory();
	bPlaceRotated = Inventory != nullptr && Inventory->FindItem(ItemId, Item) && Item.bRotated;
	MarkDirty();
}

void UCrossguardInventoryWidget::ClickItem(const FCrossguardInventoryItem& Item)
{
	UCrossguardInventoryComponent* Inventory = GetInventory();
	if (Inventory == nullptr)
	{
		return;
	}
	if (SelectedItemId == Item.Id)
	{
		Select(0);
		return;
	}
	FCrossguardInventoryItem Selected;
	if (SelectedItemId != 0 && Inventory->FindItem(SelectedItemId, Selected) && Selected.CanStackWith(Item))
	{
		// Onto the top-left square of the same item: the server merges as many as fit.
		Inventory->RequestMoveItem(SelectedItemId, Item.Container, Item.Position, Item.bRotated);
		Select(0);
		return;
	}
	Select(Item.Id);
}

void UCrossguardInventoryWidget::RightClickItem(const FCrossguardInventoryItem& Item)
{
	ACrossguardCharacter* Character = GetCrossguardCharacter();
	UCrossguardInventoryComponent* Inventory = Character != nullptr ? Character->GetInventoryComponent() : nullptr;
	if (Inventory == nullptr)
	{
		return;
	}
	switch (Item.Category)
	{
	case ECrossguardItemCategory::Weapon:
		Inventory->RequestEquipItem(Item.Id,
			CrossguardInventoryWidgetLocal::IsShield(Item.Weapon, Character->GetEquipmentComponent()) ? ECrossguardEquipmentSlot::OffHand
																									  : ECrossguardEquipmentSlot::MainHand,
			-1);
		break;
	case ECrossguardItemCategory::Armor:
	case ECrossguardItemCategory::Container:
		Inventory->RequestEquipItem(Item.Id);
		break;
	case ECrossguardItemCategory::Consumable:
	case ECrossguardItemCategory::Throwable:
		Inventory->RequestReadyItem(Item.Id);
		break;
	default:
		// Skill Gems learn; anything else the server refuses as not usable.
		Inventory->RequestUseItem(Item.Id);
		break;
	}
	Select(0);
}

void UCrossguardInventoryWidget::ClickSquare(ECrossguardInventoryContainer Container, FIntPoint Position)
{
	UCrossguardInventoryComponent* Inventory = GetInventory();
	if (Inventory == nullptr || SelectedItemId == 0)
	{
		return;
	}
	Inventory->RequestMoveItem(SelectedItemId, Container, Position, bPlaceRotated);
	Select(0);
}

void UCrossguardInventoryWidget::DropItem(ECrossguardInventoryContainer Container, FIntPoint Position, const FCrossguardInventoryItem& Item, bool bFromLoot)
{
	UCrossguardInventoryComponent* Inventory = GetInventory();
	if (Inventory == nullptr)
	{
		return;
	}
	if (bFromLoot)
	{
		// Taking puts it wherever it fits; the server has no "take to this square".
		Inventory->RequestTakeFromContainer(Item.Id);
	}
	else
	{
		Inventory->RequestMoveItem(Item.Id, Container, Position, Item.bRotated);
	}
	Select(0);
}

void UCrossguardInventoryWidget::DropOnLoot(const FCrossguardInventoryItem& Item, bool bFromLoot)
{
	UCrossguardInventoryComponent* Inventory = GetInventory();
	if (Inventory == nullptr || bFromLoot || Inventory->GetOpenContainer() == nullptr)
	{
		return;
	}
	Inventory->RequestPutInContainer(Item.Id);
	Select(0);
}

void UCrossguardInventoryWidget::TakeAll()
{
	UCrossguardInventoryComponent* Inventory = GetInventory();
	if (Inventory == nullptr || Inventory->GetOpenContainer() == nullptr)
	{
		return;
	}
	// A copy: on a listen server each request changes the list at once.
	const TArray<FCrossguardInventoryItem> Items = Inventory->GetOpenContainerItems();
	for (const FCrossguardInventoryItem& Item : Items)
	{
		Inventory->RequestTakeFromContainer(Item.Id);
	}
	MarkDirty();
}

void UCrossguardInventoryWidget::OnWindowClosed()
{
	Select(0);
	// Closing the screen closes the body or chest too.
	UCrossguardInventoryComponent* Inventory = GetInventory();
	if (Inventory != nullptr && Inventory->GetOpenContainer() != nullptr)
	{
		Inventory->RequestCloseContainer();
	}
}
