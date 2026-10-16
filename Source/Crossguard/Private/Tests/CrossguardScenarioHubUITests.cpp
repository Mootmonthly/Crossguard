// Scenario tests: the stash and vendor screens (docs/ui.md) on a hub (no raid). As
// CrossguardScenarioUITests.cpp, nothing is drawn under -nullrhi: the widgets are built for the PIE
// player's controller, fed game state, and read back; drags and clicks go through the same functions
// the grids and buttons call. Crossguard.Visual.HubUI takes the pictures (rendering editor only).
// Framework: CrossguardScenario.h, docs/editor-tests.md.

#include "Tests/CrossguardScenario.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Blueprint/UserWidget.h"
#include "Character/CrossguardCharacter.h"
#include "Game/CrossguardPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/CrossguardStashChest.h"
#include "Interaction/CrossguardVendorNPC.h"
#include "Items/CrossguardInventoryComponent.h"
#include "Items/CrossguardStashComponent.h"
#include "Items/CrossguardVendorComponent.h"
#include "UI/CrossguardHUD.h"
#include "UI/CrossguardStashWidget.h"
#include "UI/CrossguardVendorWidget.h"

namespace CrossguardScenarioHubUI
{
	// The scene row (Y), apart from the other UI tests' rows.
	constexpr float HubRow = 5500.0f;
	// CrossguardRules FInventoryConfig / FEconomyConfig and Data/Items/Items.json.
	constexpr int32 TabPrice = 3000;
	constexpr int32 CommonGemPrice = 50;
	// The picture's settle time (as CrossguardScenarioVisualTests.cpp).
	constexpr float HubSettleSeconds = 0.3f;

	FName HubPotion() { return FName(TEXT("healing_potion")); }
	FName HubRuby() { return FName(TEXT("ruby")); }
	FName HubSapphire() { return FName(TEXT("sapphire")); }
	FName HubEmerald() { return FName(TEXT("emerald")); }

	APlayerController* HubControllerOf(const ACrossguardCharacter* Character)
	{
		return Character != nullptr ? Cast<APlayerController>(Character->GetController()) : nullptr;
	}

	ACrossguardHUD* HubHudOf(const ACrossguardCharacter* Character)
	{
		const APlayerController* Controller = HubControllerOf(Character);
		return Controller != nullptr ? Cast<ACrossguardHUD>(Controller->GetHUD()) : nullptr;
	}

	// The first inventory stack of ItemId (null id 0 when none).
	FCrossguardInventoryItem HubStackOf(const UCrossguardInventoryComponent* Inventory, FName ItemId)
	{
		if (Inventory != nullptr)
		{
			for (const FCrossguardInventoryItem& Item : Inventory->GetItems())
			{
				if (Item.ItemId == ItemId)
				{
					return Item;
				}
			}
		}
		return FCrossguardInventoryItem();
	}

	// A free square for a 1 x 1 item in a stash tab, searched from the bottom-right (-1, -1 if none).
	FIntPoint HubFreeStashSquare(const UCrossguardStashComponent& Stash, int32 Tab)
	{
		TSet<FIntPoint> Taken;
		if (Stash.GetTabs().IsValidIndex(Tab))
		{
			for (const FCrossguardInventoryItem& Item : Stash.GetTabs()[Tab].Items)
			{
				const FIntPoint Footprint = Item.bRotated ? FIntPoint(Item.Size.Y, Item.Size.X) : Item.Size;
				for (int32 X = 0; X < Footprint.X; ++X)
				{
					for (int32 Y = 0; Y < Footprint.Y; ++Y)
					{
						Taken.Add(Item.Position + FIntPoint(X, Y));
					}
				}
			}
		}
		const FIntPoint Size = Stash.GetTabSize();
		for (int32 Y = Size.Y - 1; Y >= 0; --Y)
		{
			for (int32 X = Size.X - 1; X >= 0; --X)
			{
				if (!Taken.Contains(FIntPoint(X, Y)))
				{
					return FIntPoint(X, Y);
				}
			}
		}
		return FIntPoint(-1, -1);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioUIStashAndVendor, "Crossguard.Scenario.UI.StashAndVendor", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioUIStashAndVendor::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;

	FScenario Scenario(*this);
	Scenario.Raid(false);

	Scenario.Step(TEXT("The PIE player in the hub with potions and three Common rubies; a stash chest and a vendor"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.LocalPlayer();
		APlayerController* Controller = CrossguardScenarioHubUI::HubControllerOf(Player);
		if (!C.Require(Player != nullptr && Controller != nullptr && Controller->GetLocalPlayer() != nullptr,
				TEXT("the PIE player has a local player controller")))
		{
			return;
		}
		C.Place(Player, FVector(0.0f, CrossguardScenarioHubUI::HubRow, 0.0f), 0.0f);
		C.Require(C.GiveItem(Player, CrossguardScenarioHubUI::HubPotion(), 2) == 0 && C.GiveItem(Player, CrossguardScenarioHubUI::HubRuby(), 3) == 0,
			TEXT("the player's items fit"));
		C.Require(C.SpawnActor<ACrossguardStashChest>(TEXT("Chest"), FVector(150.0f, CrossguardScenarioHubUI::HubRow, 0.0f)) != nullptr,
			TEXT("the stash chest spawned"));
		C.Require(C.SpawnActor<ACrossguardVendorNPC>(TEXT("Vendor"), FVector(150.0f, CrossguardScenarioHubUI::HubRow + 200.0f, 0.0f), 180.0f) != nullptr,
			TEXT("the vendor spawned"));
	});

	Scenario.Step(TEXT("F on the chest and the vendor opens their screens in the hub"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.LocalPlayer();
		ACrossguardPlayerState* Progression = Player != nullptr ? C.PlayerState(Player) : nullptr;
		ACrossguardStashChest* Chest = C.Get<ACrossguardStashChest>(TEXT("Chest"));
		ACrossguardVendorNPC* Vendor = C.Get<ACrossguardVendorNPC>(TEXT("Vendor"));
		if (!C.Require(Progression != nullptr && Chest != nullptr && Vendor != nullptr, TEXT("player state, chest and vendor")))
		{
			return;
		}
		C.Test.TestEqual(TEXT("The chest's prompt"), Chest->GetInteractPrompt(), FString(TEXT("F: Open stash")));
		C.ClearLog();
		Chest->Interact(Player);
		C.Test.TestTrue(TEXT("Log: the stash opened"), C.LogContains(TEXT("Stash opened")));
		Vendor->Interact(Player);
		C.Test.TestTrue(TEXT("Log: the vendor opened"), C.LogContains(TEXT("Vendor opened")));
		// With a game viewport the HUD shows the screens (the last one opened replaces the first).
		if (ACrossguardHUD* Hud = CrossguardScenarioHubUI::HubHudOf(Player); Hud != nullptr && Hud->GetVendorWidget() != nullptr)
		{
			C.Test.TestTrue(TEXT("The HUD shows the vendor screen"), Hud->IsWindowOpen(Hud->GetVendorWidget()));
			C.Test.TestFalse(TEXT("The stash screen made way for it"), Hud->IsWindowOpen(Hud->GetStashWidget()));
			Hud->CloseAllWindows();
		}
	});

	Scenario.Step(TEXT("The stash screen: tabs, cells, drag both ways, buy-tab state"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.LocalPlayer();
		APlayerController* Controller = CrossguardScenarioHubUI::HubControllerOf(Player);
		ACrossguardPlayerState* Progression = Player != nullptr ? C.PlayerState(Player) : nullptr;
		UCrossguardStashComponent* Stash = Progression != nullptr ? Progression->GetStash() : nullptr;
		UCrossguardInventoryComponent* Inventory = Player != nullptr ? Player->GetInventoryComponent() : nullptr;
		if (!C.Require(Controller != nullptr && Stash != nullptr && Inventory != nullptr, TEXT("controller, stash and inventory")))
		{
			return;
		}
		UCrossguardStashWidget* Widget = CreateWidget<UCrossguardStashWidget>(Controller, UCrossguardStashWidget::StaticClass());
		if (!C.Require(Widget != nullptr, TEXT("the stash widget is created")))
		{
			return;
		}
		Widget->TakeWidget();
		Widget->Refresh();
		C.Test.TestTrue(TEXT("The stash screen built its tree in C++"), Widget->IsBuiltInCode());
		C.Test.TestEqual(TEXT("One tab button per tab"), Widget->GetTabButtonCount(), Stash->GetTabCount());
		C.Test.TestTrue(*FString::Printf(TEXT("The tab grid is 10 x 30 (%d x %d)"), Widget->GetShownTabSize().X, Widget->GetShownTabSize().Y),
			Widget->GetShownTabSize() == FIntPoint(10, 30));
		C.Test.TestEqual(TEXT("One stash cell per stack in the tab"), Widget->GetStashCellCount(), Stash->GetTabs()[0].Items.Num());
		C.Test.TestEqual(TEXT("One inventory cell per stack"), Widget->GetInventoryCellCount(), Inventory->GetItems().Num());

		// Drag the potions from the inventory onto a free stash square, then back.
		const FCrossguardInventoryItem Potions = CrossguardScenarioHubUI::HubStackOf(Inventory, CrossguardScenarioHubUI::HubPotion());
		const FIntPoint Free = CrossguardScenarioHubUI::HubFreeStashSquare(*Stash, 0);
		if (C.Require(Potions.Id != 0 && Potions.Size == FIntPoint(1, 1) && Free.X >= 0, TEXT("a one-square potion stack and a free stash square")))
		{
			const int32 PotionsBefore = Inventory->CountItem(CrossguardScenarioHubUI::HubPotion());
			Widget->DropOnStash(Free, Potions, false);
			C.Test.TestEqual(TEXT("The dragged stack left the inventory"), Inventory->CountItem(CrossguardScenarioHubUI::HubPotion()), PotionsBefore - Potions.Count);
			FCrossguardInventoryItem Stored;
			for (const FCrossguardInventoryItem& Item : Stash->GetTabs()[0].Items)
			{
				Stored = Item.ItemId == CrossguardScenarioHubUI::HubPotion() && Item.Position == Free ? Item : Stored;
			}
			C.Test.TestTrue(*FString::Printf(TEXT("The stack is in the stash on the square it was dropped on (%d, %d)"), Free.X, Free.Y),
				Stored.Id != 0 && Stored.Count == Potions.Count);
			Widget->Refresh();
			C.Test.TestEqual(TEXT("The stash cells follow"), Widget->GetStashCellCount(), Stash->GetTabs()[0].Items.Num());

			Widget->DropOnInventory(ECrossguardInventoryContainer::Main, FIntPoint::ZeroValue, Stored, true);
			C.Test.TestEqual(TEXT("Dragged back out of the stash"), Inventory->CountItem(CrossguardScenarioHubUI::HubPotion()), PotionsBefore);
			FCrossguardInventoryItem Left;
			C.Test.TestFalse(TEXT("No longer in the stash"), Stash->FindStashItem(Stored.Id, Left));
		}

		// Buy tab: greyed without the gold, active with it; buying adds a tab.
		Progression->SpendGold(Progression->GetGold());
		Progression->AddGold(CrossguardScenarioHubUI::TabPrice - 1);
		Widget->Refresh();
		const bool bCanBuyMore = Stash->GetPurchasedTabCount() < Crossguard::FInventoryConfig().MaxPurchasedStashTabs;
		C.Test.TestFalse(*FString::Printf(TEXT("Buy tab is greyed 1 gold short (%s)"), *Widget->GetBuyTabLabel()), Widget->IsBuyTabActive());
		if (bCanBuyMore)
		{
			C.Test.TestTrue(TEXT("The button names the price"), Widget->GetBuyTabLabel().Contains(TEXT("3000")));
			Progression->AddGold(1);
			Widget->Refresh();
			C.Test.TestTrue(TEXT("Buy tab is active with 3000 gold"), Widget->IsBuyTabActive());
			const int32 TabsBefore = Stash->GetTabCount();
			Widget->BuyTab();
			C.Test.TestEqual(TEXT("Buying adds a tab"), Stash->GetTabCount(), TabsBefore + 1);
			C.Test.TestEqual(TEXT("It cost 3000"), Progression->GetGold(), 0);
			Widget->Refresh();
			C.Test.TestEqual(TEXT("A tab button for the new tab"), Widget->GetTabButtonCount(), TabsBefore + 1);
			C.Test.TestFalse(TEXT("Greyed again with no gold"), Widget->IsBuyTabActive());
			Widget->ShowTab(TabsBefore);
			Widget->Refresh();
			C.Test.TestEqual(TEXT("The new tab is shown"), Widget->GetShownTab(), TabsBefore);
			C.Test.TestEqual(TEXT("The new tab is empty"), Widget->GetStashCellCount(), 0);
		}
		else
		{
			C.Test.AddWarning(TEXT("The PIE player's stash already has every tab bought: the purchase was not checked"));
		}
		Widget->RemoveFromParent();
	});

	Scenario.Step(TEXT("The vendor screen: prices, selling a stack, selling every gemstone"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.LocalPlayer();
		APlayerController* Controller = CrossguardScenarioHubUI::HubControllerOf(Player);
		ACrossguardPlayerState* Progression = Player != nullptr ? C.PlayerState(Player) : nullptr;
		UCrossguardInventoryComponent* Inventory = Player != nullptr ? Player->GetInventoryComponent() : nullptr;
		if (!C.Require(Controller != nullptr && Progression != nullptr && Inventory != nullptr, TEXT("controller, player state and inventory")))
		{
			return;
		}
		UCrossguardVendorWidget* Widget = CreateWidget<UCrossguardVendorWidget>(Controller, UCrossguardVendorWidget::StaticClass());
		if (!C.Require(Widget != nullptr, TEXT("the vendor widget is created")))
		{
			return;
		}
		Widget->TakeWidget();
		Widget->Refresh();
		C.Test.TestTrue(TEXT("The vendor screen built its tree in C++"), Widget->IsBuiltInCode());
		C.Test.TestEqual(TEXT("One sell row per stack"), Widget->GetSellRowCount(), Inventory->GetItems().Num());
		bool bRubyRow = false;
		for (int32 Index = 0; Index < Widget->GetSellRowCount(); ++Index)
		{
			bRubyRow |= Widget->GetSellRowLabel(Index).Contains(TEXT("150 gold"));
		}
		C.Test.TestTrue(TEXT("The three Common rubies are listed at 150 gold"), bRubyRow);

		const FCrossguardInventoryItem Rubies = CrossguardScenarioHubUI::HubStackOf(Inventory, CrossguardScenarioHubUI::HubRuby());
		if (C.Require(Rubies.Id != 0 && Rubies.Count == 3, TEXT("a stack of 3 rubies")))
		{
			const int32 GoldBefore = Progression->GetGold();
			Widget->DropToSell(Rubies);
			C.Test.TestEqual(TEXT("Selling the stack paid 150"), Progression->GetGold(), GoldBefore + 3 * CrossguardScenarioHubUI::CommonGemPrice);
			C.Test.TestEqual(TEXT("The rubies are gone"), Inventory->CountItem(CrossguardScenarioHubUI::HubRuby()), 0);
			Widget->Refresh();
			C.Test.TestEqual(TEXT("The list follows"), Widget->GetSellRowCount(), Inventory->GetItems().Num());
		}

		C.Require(C.GiveItem(Player, CrossguardScenarioHubUI::HubSapphire(), 1) == 0 && C.GiveItem(Player, CrossguardScenarioHubUI::HubEmerald(), 1) == 0,
			TEXT("a sapphire and an emerald fit"));
		Widget->Refresh();
		C.Test.TestEqual(TEXT("Sell all gemstones is worth 100"), Widget->GetGemstonesValue(), 2 * CrossguardScenarioHubUI::CommonGemPrice);
		const int32 GoldBefore = Progression->GetGold();
		const int32 PotionsBefore = Inventory->CountItem(CrossguardScenarioHubUI::HubPotion());
		Widget->SellAllGemstones();
		C.Test.TestEqual(TEXT("Both gemstones sold"), Progression->GetGold(), GoldBefore + 2 * CrossguardScenarioHubUI::CommonGemPrice);
		C.Test.TestEqual(TEXT("No sapphire left"), Inventory->CountItem(CrossguardScenarioHubUI::HubSapphire()), 0);
		C.Test.TestEqual(TEXT("No emerald left"), Inventory->CountItem(CrossguardScenarioHubUI::HubEmerald()), 0);
		C.Test.TestEqual(TEXT("The potions were not sold"), Inventory->CountItem(CrossguardScenarioHubUI::HubPotion()), PotionsBefore);
		Widget->Refresh();
		C.Test.TestEqual(TEXT("Nothing left to sell as gemstones"), Widget->GetGemstonesValue(), 0);
		Widget->RemoveFromParent();
	});

	Scenario.Step(TEXT("In a raid the chest and the vendor refuse"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.LocalPlayer();
		ACrossguardStashChest* Chest = C.Get<ACrossguardStashChest>(TEXT("Chest"));
		ACrossguardVendorNPC* Vendor = C.Get<ACrossguardVendorNPC>(TEXT("Vendor"));
		if (!C.Require(Player != nullptr && Chest != nullptr && Vendor != nullptr, TEXT("player, chest and vendor")))
		{
			return;
		}
		C.SetRaid(true);
		C.ClearLog();
		Chest->Interact(Player);
		Vendor->Interact(Player);
		C.Test.TestFalse(TEXT("No stash opened in a raid"), C.LogContains(TEXT("Stash opened")));
		C.Test.TestTrue(TEXT("Log: stash refused"), C.LogContains(TEXT("Stash request rejected")));
		C.Test.TestFalse(TEXT("No vendor opened in a raid"), C.LogContains(TEXT("Vendor opened")));
		C.Test.TestTrue(TEXT("Log: vendor refused"), C.LogContains(TEXT("Vendor refused")));
		if (ACrossguardHUD* Hud = CrossguardScenarioHubUI::HubHudOf(Player); Hud != nullptr && Hud->GetStashWidget() != nullptr)
		{
			Hud->OpenStash();
			C.Test.TestFalse(TEXT("The console command does not open the stash in a raid"), Hud->IsWindowOpen(Hud->GetStashWidget()));
		}
	});

	Scenario.Run();
	return true;
}

// docs/ui.md: the stash and vendor screens on the PIE player's own viewport, in the hub. stash.png:
// the stash screen with potions stored and the inventory beside it; vendor.png: the vendor screen
// with rubies and a sapphire to sell. Layout is the point (overlap, unreadable text, off-screen parts).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardVisualHubUI, "Crossguard.Visual.HubUI", CROSSGUARD_VISUAL_FLAGS)

bool FCrossguardVisualHubUI::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;

	FScenario Scenario(*this);
	Scenario.Raid(false);

	Scenario.Step(TEXT("The PIE player in the hub with potions, rubies and a sapphire; potions in the stash; the stash screen"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.LocalPlayer();
		ACrossguardHUD* Hud = CrossguardScenarioHubUI::HubHudOf(Player);
		ACrossguardPlayerState* Progression = Player != nullptr ? C.PlayerState(Player) : nullptr;
		if (!C.Require(Hud != nullptr && Hud->GetStashWidget() != nullptr && Progression != nullptr && Progression->GetStash() != nullptr,
				TEXT("the PIE player has an ACrossguardHUD with its stash screen")))
		{
			return;
		}
		C.Place(Player, FVector(0.0f, CrossguardScenarioHubUI::HubRow, 0.0f), 0.0f);
		C.Require(C.GiveItem(Player, CrossguardScenarioHubUI::HubPotion(), 4) == 0 && C.GiveItem(Player, CrossguardScenarioHubUI::HubRuby(), 3) == 0
				&& C.GiveItem(Player, CrossguardScenarioHubUI::HubSapphire(), 1, ECrossguardRarity::Rare) == 0,
			TEXT("the player's items fit"));
		Player->GetInventoryComponent()->RequestSplitItem(CrossguardScenarioHubUI::HubStackOf(Player->GetInventoryComponent(), CrossguardScenarioHubUI::HubPotion()).Id, 2);
		Progression->GetStash()->RequestStoreAnywhere(CrossguardScenarioHubUI::HubStackOf(Player->GetInventoryComponent(), CrossguardScenarioHubUI::HubPotion()).Id);
		Progression->AddGold(3500);
		Hud->OpenStash();
		C.Test.TestTrue(TEXT("The stash screen is open"), Hud->IsWindowOpen(Hud->GetStashWidget()));
	});
	Scenario.Wait(CrossguardScenarioHubUI::HubSettleSeconds);
	Scenario.Step(TEXT("stash.png; then the vendor screen"), [](FScenarioContext& C)
	{
		C.Screenshot(TEXT("stash"));
		if (ACrossguardHUD* Hud = CrossguardScenarioHubUI::HubHudOf(C.LocalPlayer()))
		{
			Hud->OpenVendor();
			C.Test.TestTrue(TEXT("The vendor screen is open"), Hud->IsWindowOpen(Hud->GetVendorWidget()));
		}
	});
	Scenario.Wait(CrossguardScenarioHubUI::HubSettleSeconds);
	Scenario.Step(TEXT("vendor.png"), [](FScenarioContext& C)
	{
		C.Screenshot(TEXT("vendor"));
		if (ACrossguardHUD* Hud = CrossguardScenarioHubUI::HubHudOf(C.LocalPlayer()))
		{
			Hud->CloseAllWindows();
		}
	});

	Scenario.Run();
	return true;
}

#endif
