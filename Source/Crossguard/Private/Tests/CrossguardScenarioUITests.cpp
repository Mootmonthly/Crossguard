// Scenario test: the placeholder UI (docs/ui.md) builds and binds for the PIE player. Nothing is
// drawn under -nullrhi, so this checks what each widget would show: it constructs every widget
// class for the local player's controller, feeds it game state (items, a body, a skill, a guild)
// and reads back its counts and lines, and drives a few clicks through the same functions the
// buttons call. Framework: CrossguardScenario.h, docs/editor-tests.md.

#include "Tests/CrossguardScenario.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Blueprint/UserWidget.h"
#include "Character/CrossguardCharacter.h"
#include "Game/CrossguardGuildComponent.h"
#include "Game/CrossguardPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Items/CrossguardInventoryComponent.h"
#include "Items/CrossguardLootContainer.h"
#include "Skills/CrossguardLoadoutComponent.h"
#include "UI/CrossguardGuildWidget.h"
#include "UI/CrossguardHUD.h"
#include "UI/CrossguardHudWidget.h"
#include "UI/CrossguardInventoryWidget.h"
#include "UI/CrossguardSkillWheelWidget.h"
#include "UI/CrossguardTradeWidget.h"

namespace CrossguardScenarioUI
{
	// The scene row (Y). The PIE player is moved here; the dummy stands 2 m in front of it.
	constexpr float Row = 4500.0f;
	constexpr float BodyTimeout = 3.0f;

	// Data/Items/ItemTable.json rows (functions: no FName construction during static init).
	FName UIPotion() { return FName(TEXT("healing_potion")); }
	FName UIBandage() { return FName(TEXT("bandage")); }
	FName UIRuby() { return FName(TEXT("ruby")); }

	APlayerController* ControllerOf(const ACrossguardCharacter* Character)
	{
		return Character != nullptr ? Cast<APlayerController>(Character->GetController()) : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioUIWidgetsBind, "Crossguard.Scenario.UI.WidgetsBind", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioUIWidgetsBind::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("The PIE player with items, a skill, and a dummy carrying a ruby"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.LocalPlayer();
		APlayerController* Controller = CrossguardScenarioUI::ControllerOf(Player);
		if (!C.Require(Player != nullptr && Controller != nullptr && Controller->GetLocalPlayer() != nullptr,
				TEXT("the PIE player has a local player controller")))
		{
			return;
		}
		C.Place(Player, FVector(-100.0f, CrossguardScenarioUI::Row, 0.0f), 0.0f);
		ACrossguardCharacter* Dummy = C.SpawnDummy(TEXT("Dummy"), FVector(100.0f, CrossguardScenarioUI::Row, 0.0f));
		if (!C.Require(Dummy != nullptr, TEXT("the dummy spawned")))
		{
			return;
		}
		C.Require(C.GiveItem(Player, CrossguardScenarioUI::UIPotion(), 2) == 0 && C.GiveItem(Player, CrossguardScenarioUI::UIBandage(), 1) == 0
				&& C.GiveItem(Player, CrossguardScenarioUI::UIRuby(), 1) == 0,
			TEXT("the player's items fit"));
		C.Require(C.GiveItem(Dummy, CrossguardScenarioUI::UIRuby(), 1, ECrossguardRarity::Rare) == 0, TEXT("the dummy's ruby fits"));
		if (!C.GrantSkills(Player, { TEXT("fireball") }))
		{
			C.Test.AddWarning(TEXT("fireball could not be granted: the skill wheel is checked with no entries"));
		}
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("The dummy dies"), [](FScenarioContext& C)
	{
		if (ACrossguardCharacter* Dummy = C.Get<ACrossguardCharacter>(TEXT("Dummy")))
		{
			C.Kill(Dummy, C.LocalPlayer());
		}
	});

	Scenario.WaitUntil(TEXT("its body holds the ruby"), CrossguardScenarioUI::BodyTimeout, [](FScenarioContext& C)
	{
		ACrossguardLootContainer* Body = C.FindNearest<ACrossguardLootContainer>(FVector(100.0f, CrossguardScenarioUI::Row, 0.0f), 400.0f);
		if (Body != nullptr && Body->GetItems().Num() > 0)
		{
			C.Remember(TEXT("Body"), Body);
			return true;
		}
		return false;
	});

	Scenario.Step(TEXT("Every widget builds, binds and shows the game state"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.LocalPlayer();
		APlayerController* Controller = CrossguardScenarioUI::ControllerOf(Player);
		ACrossguardLootContainer* Body = C.Get<ACrossguardLootContainer>(TEXT("Body"));
		if (!C.Require(Player != nullptr && Controller != nullptr && Body != nullptr, TEXT("player, controller and body")))
		{
			return;
		}
		UCrossguardInventoryComponent* Inventory = Player->GetInventoryComponent();

		// The game mode's HUD class (a Blueprint game mode may override it).
		if (const ACrossguardHUD* Hud = Cast<ACrossguardHUD>(Controller->GetHUD()))
		{
			C.Test.TestNotNull(TEXT("The HUD made its HUD widget"), Hud->GetHudWidget());
			C.Test.TestNotNull(TEXT("The HUD made its inventory window"), Hud->GetInventoryWidget());
			C.Test.TestFalse(TEXT("No window is open at the start"), Hud->IsAnyWindowOpen());
		}
		else
		{
			C.Test.AddWarning(FString::Printf(TEXT("The player's HUD is %s, not ACrossguardHUD (the game mode Blueprint overrides HUD Class?)"),
				*GetNameSafe(Controller->GetHUD())));
		}

		// HUD: Health and the hotbar.
		UCrossguardHudWidget* HudWidget = CreateWidget<UCrossguardHudWidget>(Controller, UCrossguardHudWidget::StaticClass());
		if (!C.Require(HudWidget != nullptr, TEXT("the HUD widget is created")))
		{
			return;
		}
		HudWidget->TakeWidget();
		HudWidget->Refresh();
		C.Test.TestTrue(TEXT("The HUD built its tree in C++"), HudWidget->IsBuiltInCode());
		C.Test.TestTrue(*FString::Printf(TEXT("The HUD shows Health (%s)"), *HudWidget->GetHealthLine()), HudWidget->GetHealthLine().StartsWith(TEXT("HP ")));
		C.Test.TestTrue(*FString::Printf(TEXT("The hotbar line covers keys 3-7 (%s)"), *HudWidget->GetHotbarLine()),
			HudWidget->GetHotbarLine().Contains(TEXT("3:")) && HudWidget->GetHotbarLine().Contains(TEXT("7:")));
		C.Test.AddInfo(FString::Printf(TEXT("HUD: %s | %s | prompt '%s'"), *HudWidget->GetHealthLine(), *HudWidget->GetHotbarLine(),
			*HudWidget->GetInteractPrompt()));

		// Inventory: one cell per stack; then the body side by side, and Take all.
		UCrossguardInventoryWidget* InventoryWidget = CreateWidget<UCrossguardInventoryWidget>(Controller, UCrossguardInventoryWidget::StaticClass());
		if (!C.Require(InventoryWidget != nullptr, TEXT("the inventory widget is created")))
		{
			return;
		}
		InventoryWidget->TakeWidget();
		InventoryWidget->Refresh();
		C.Test.TestEqual(TEXT("One inventory cell per stack"), InventoryWidget->GetItemCellCount(), Inventory->GetItems().Num());
		C.Test.TestTrue(TEXT("The inventory holds stacks"), Inventory->GetItems().Num() >= 3);
		C.Test.TestEqual(TEXT("No loot cells while nothing is open"), InventoryWidget->GetLootCellCount(), 0);

		if (Inventory->GetItems().Num() > 0)
		{
			const FCrossguardInventoryItem First = Inventory->GetItems()[0];
			InventoryWidget->ClickItem(First);
			C.Test.TestEqual(TEXT("Clicking an item selects it"), InventoryWidget->GetSelectedItemId(), First.Id);
			InventoryWidget->ClickItem(First);
			C.Test.TestEqual(TEXT("Clicking it again deselects it"), InventoryWidget->GetSelectedItemId(), 0);
		}

		if (C.Require(Inventory->OpenLootContainer(Body), TEXT("the player opens the body")))
		{
			InventoryWidget->Refresh();
			C.Test.TestEqual(TEXT("One loot cell per stack on the body"), InventoryWidget->GetLootCellCount(), Inventory->GetOpenContainerItems().Num());
			C.Test.TestTrue(TEXT("The body shows the ruby"), InventoryWidget->GetLootCellCount() > 0);
			const int32 RubiesBefore = Inventory->CountItem(CrossguardScenarioUI::UIRuby());
			InventoryWidget->TakeAll();
			C.Test.TestEqual(TEXT("Take all moved the ruby into the inventory"), Inventory->CountItem(CrossguardScenarioUI::UIRuby()), RubiesBefore + 1);
			C.Test.TestEqual(TEXT("The body is empty"), Body->GetItems().Num(), 0);
			InventoryWidget->Refresh();
			C.Test.TestEqual(TEXT("Cells follow the new stacks"), InventoryWidget->GetItemCellCount(), Inventory->GetItems().Num());
			Inventory->RequestCloseContainer();
		}

		// Skill wheel: one entry per castable skill; picking one selects it for E.
		UCrossguardSkillWheelWidget* Wheel = CreateWidget<UCrossguardSkillWheelWidget>(Controller, UCrossguardSkillWheelWidget::StaticClass());
		if (C.Require(Wheel != nullptr, TEXT("the skill wheel is created")))
		{
			Wheel->TakeWidget();
			Wheel->Refresh();
			const TArray<FName> Skills = Player->GetLoadoutComponent()->GetActivatableSkills();
			C.Test.TestEqual(TEXT("One wheel entry per castable skill"), Wheel->GetEntryCount(), Skills.Num());
			if (Skills.Num() > 0)
			{
				Wheel->SelectEntry(0);
				C.Test.TestTrue(TEXT("Picking an entry selects its skill"), Player->GetLoadoutComponent()->GetSelectedSkill() == Skills[0]);
			}
		}

		// Trade: no trade yet.
		UCrossguardTradeWidget* TradeWidget = CreateWidget<UCrossguardTradeWidget>(Controller, UCrossguardTradeWidget::StaticClass());
		if (C.Require(TradeWidget != nullptr, TEXT("the trade widget is created")))
		{
			TradeWidget->TakeWidget();
			TradeWidget->Refresh();
			C.Test.TestTrue(*FString::Printf(TEXT("The trade window says there is no trade (%s)"), *TradeWidget->GetStateLine()),
				TradeWidget->GetStateLine().StartsWith(TEXT("No trade")));
			C.Test.TestEqual(TEXT("No offer rows without a trade"), TradeWidget->GetMyOfferRowCount(), 0);
		}

		// Guild: none, then founded through the component (standalone: the request runs at once).
		UCrossguardGuildWidget* GuildWidget = CreateWidget<UCrossguardGuildWidget>(Controller, UCrossguardGuildWidget::StaticClass());
		const ACrossguardPlayerState* PlayerState = C.PlayerState(Player);
		UCrossguardGuildComponent* Guild = PlayerState != nullptr ? PlayerState->GetGuild() : nullptr;
		if (C.Require(GuildWidget != nullptr && Guild != nullptr, TEXT("the guild widget and component")))
		{
			GuildWidget->TakeWidget();
			GuildWidget->Refresh();
			C.Test.TestEqual(TEXT("Not in a guild: no roster"), GuildWidget->GetMemberRowCount(), 0);
			Guild->RequestCreate(TEXT("UI Testers"));
			GuildWidget->Refresh();
			C.Test.TestTrue(*FString::Printf(TEXT("The header names the guild (%s)"), *GuildWidget->GetHeaderLine()),
				GuildWidget->GetHeaderLine().Contains(TEXT("UI Testers")));
			C.Test.TestEqual(TEXT("The roster shows the founder"), GuildWidget->GetMemberRowCount(), 1);
			C.Test.TestEqual(TEXT("The bank is empty"), GuildWidget->GetBankRowCount(), 0);
		}

		for (UUserWidget* Widget : TArray<UUserWidget*>{ HudWidget, InventoryWidget, Wheel, TradeWidget, GuildWidget })
		{
			if (Widget != nullptr)
			{
				Widget->RemoveFromParent();
			}
		}
	});

	Scenario.Run();
	return true;
}

#endif
