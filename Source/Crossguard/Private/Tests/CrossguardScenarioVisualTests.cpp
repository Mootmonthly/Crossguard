// Visual scenario tests (Crossguard.Visual.*): the checklist's "looks" rows as screenshots for a
// person to judge. They only run in a rendering editor (Tools\run_editor_tests.bat -Visual;
// CROSSGUARD_VISUAL_FLAGS keeps them off a -nullrhi run) and write PNGs under
// Saved\EditorTests\Screenshots\<Test>\. The camera is a camera actor (C.LookFrom) 3 m from the
// subject at eye height, as the PIE player's viewport (standalone) or a client's (networked). The
// tests assert only what can be read back (the subject exists, the state is on); the pictures are
// the point. docs/editor-tests.md, "Visual run".

#include "Tests/CrossguardScenario.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AbilitySystem/CrossguardGameplayTags.h"
#include "Character/CrossguardCharacter.h"
#include "Character/CrossguardMonster.h"
#include "Engine/DataTable.h"
#include "Game/CrossguardGameMode.h"
#include "Interaction/CrossguardGatherNode.h"
#include "Items/CrossguardInventoryComponent.h"
#include "Items/CrossguardLoot.h"
#include "Items/CrossguardLootContainer.h"
#include "UI/CrossguardGuildWidget.h"
#include "UI/CrossguardHUD.h"
#include "UI/CrossguardInventoryWidget.h"
#include "UI/CrossguardTradeWidget.h"

namespace CrossguardScenarioVisual
{
	using CrossguardScenario::FScenarioContext;

	// Scenes sit at Row (cm) on the 200 m floor (within +-100 m), away from the PIE player's start at (-30 m, -30 m);
	// the subject stands at X = 0 and the camera looks at it from CameraDistance away.
	constexpr float CameraDistance = 300.0f;
	constexpr float EyeHeight = 150.0f;
	// A frame for the new view target and the last frame to draw before a shot.
	constexpr float SettleSeconds = 0.3f;

	// A standing character's eyes at FloorPoint.
	FVector EyesAt(const FVector& FloorPoint)
	{
		return FloorPoint + FVector(0.0f, 0.0f, EyeHeight);
	}

	// The camera CameraDistance in -X from Subject (a floor point), at eye height, looking at its eyes.
	void CameraOn(FScenarioContext& C, const FVector& Subject, int32 Client = -1)
	{
		C.LookFrom(Subject + FVector(-CameraDistance, 0.0f, EyeHeight), EyesAt(Subject), Client);
	}
}

// --- Ragdoll ---------------------------------------------------------------------------------------

// Checklist 4 / 6: a killed character's body ragdolls. before.png: a dummy standing; ragdoll.png:
// 1.5 s after a lethal hit.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardVisualRagdoll, "Crossguard.Visual.RagdollBody", CROSSGUARD_VISUAL_FLAGS)

bool FCrossguardVisualRagdoll::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioVisual;

	static const FVector Spot(0.0f, 6000.0f, 0.0f);
	FScenario Scenario(*this);

	Scenario.Step(TEXT("A dummy, the camera 3 m away"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Dummy = C.SpawnDummy(TEXT("Dummy"), Spot, 180.0f);
		C.Require(Dummy != nullptr, TEXT("dummy spawned"));
		CameraOn(C, Spot);
	});
	Scenario.Wait(SettleSeconds);
	Scenario.Step(TEXT("before.png"), [](FScenarioContext& C) { C.Screenshot(TEXT("before")); });

	Scenario.Step(TEXT("The dummy dies"), [](FScenarioContext& C)
	{
		if (ACrossguardCharacter* Dummy = C.Get<ACrossguardCharacter>(TEXT("Dummy")))
		{
			C.Kill(Dummy);
			C.Test.TestTrue(TEXT("The dummy is dead"), C.IsDead(Dummy));
		}
	});
	Scenario.Wait(1.5f);
	Scenario.Step(TEXT("ragdoll.png"), [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Dummy = C.Get<ACrossguardCharacter>(TEXT("Dummy"));
		C.Test.TestTrue(TEXT("The body simulates physics"), Dummy != nullptr && Dummy->GetMesh()->IsSimulatingPhysics());
		C.Screenshot(TEXT("ragdoll"));
	});

	Scenario.Run();
	return true;
}

// --- Gather node -----------------------------------------------------------------------------------

// Checklist 4: a gather node's mesh, and the node hiding once depleted. before.png: a gather_herb
// node with a player at it; depleted.png: after the player gathered it (2 s). Skipped without the
// gathering loot table.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardVisualGatherNode, "Crossguard.Visual.GatherNode", CROSSGUARD_VISUAL_FLAGS)

bool FCrossguardVisualGatherNode::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioVisual;

	static const FVector NodeSpot(0.0f, 6500.0f, 0.0f);
	static const FName GatherHerb(TEXT("gather_herb"));
	FScenario Scenario(*this);

	Scenario.Step(TEXT("A gather_herb node, a player 1.5 m from it, the camera to the side"), [](FScenarioContext& C)
	{
		const ACrossguardGameMode* Mode = C.GameMode();
		if (Mode == nullptr || Mode->Loot.LootTables == nullptr || Mode->Loot.LootTables->FindRow<FCrossguardLootTableRow>(GatherHerb, TEXT("Visual"), false) == nullptr)
		{
			C.Skip(TEXT("the game mode's Loot Tables have no gather_herb row (DT_Loot)"));
			return;
		}
		ACrossguardGatherNode* Node = C.SpawnActor<ACrossguardGatherNode>(TEXT("Node"), NodeSpot, 0.0f,
			[](ACrossguardGatherNode& Spawned) { Spawned.LootTableId = GatherHerb; });
		// Within reach, as Raid.GatherNode's 1.5 m; on the camera's far side so the node stays in view.
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), NodeSpot + FVector(0.0f, 150.0f, 0.0f), -90.0f);
		if (!C.Require(Node != nullptr && Player != nullptr, TEXT("gather node and player spawned")))
		{
			return;
		}
		C.AimAt(Player, Node->GetActorLocation());
		CameraOn(C, NodeSpot);
	});
	Scenario.Wait(0.5f);
	Scenario.Step(TEXT("before.png"), [](FScenarioContext& C) { C.Screenshot(TEXT("before")); });

	Scenario.Step(TEXT("The player gathers it"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Player"));
		ACrossguardGatherNode* Node = C.Get<ACrossguardGatherNode>(TEXT("Node"));
		if (Player == nullptr || Node == nullptr)
		{
			return;
		}
		Player->TryInteract();
		C.Test.AddInfo(FString::Printf(TEXT("Gathering from %s, velocity %s"), *Player->GetActorLocation().ToCompactString(), *Player->GetVelocity().ToCompactString()));
		C.SetNumber(TEXT("StartX"), Player->GetActorLocation().X);
		C.SetNumber(TEXT("StartY"), Player->GetActorLocation().Y);
		C.SetNumber(TEXT("StartZ"), Player->GetActorLocation().Z);
		C.Require(Node->GetGatherer() == Player, TEXT("the player is gathering the node"));
	});
	Scenario.WaitUntil(TEXT("the node is depleted"), 5.0f, [](FScenarioContext& C)
	{
		const ACrossguardGatherNode* Node = C.Get<ACrossguardGatherNode>(TEXT("Node"));
		const ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Player"));
		if (Node == nullptr || Player == nullptr)
		{
			return false;
		}
		if (Node->GetGatherer() == nullptr && !Node->IsDepleted())
		{
			const FVector Start(C.Number(TEXT("StartX")), C.Number(TEXT("StartY")), C.Number(TEXT("StartZ")));
			C.Fail(FString::Printf(TEXT("The channel broke; the player is now at %s, %.0f cm from where it started (velocity %s)"),
				*Player->GetActorLocation().ToCompactString(), FVector::Dist(Start, Player->GetActorLocation()), *Player->GetVelocity().ToCompactString()));
			return false;
		}
		return Node->IsDepleted();
	});
	Scenario.Wait(SettleSeconds);
	Scenario.Step(TEXT("depleted.png (the node should be gone from view)"), [](FScenarioContext& C) { C.Screenshot(TEXT("depleted")); });

	Scenario.Run();
	return true;
}

// --- Chest -----------------------------------------------------------------------------------------

// Checklist 4: a chest_common chest. chest.png: the native loot container (a Blueprint would add the
// mesh; the native one shows what it has).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardVisualChest, "Crossguard.Visual.Chest", CROSSGUARD_VISUAL_FLAGS)

bool FCrossguardVisualChest::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioVisual;

	static const FVector Spot(0.0f, 7000.0f, 0.0f);
	FScenario Scenario(*this);

	Scenario.Step(TEXT("A chest_common chest, the camera 3 m away"), [](FScenarioContext& C)
	{
		ACrossguardLootContainer* Chest = C.SpawnActor<ACrossguardLootContainer>(TEXT("Chest"), Spot + FVector(0.0f, 0.0f, 50.0f), 180.0f,
			[](ACrossguardLootContainer& Spawned) { Spawned.LootTableId = FName(TEXT("chest_common")); });
		if (!C.Require(Chest != nullptr, TEXT("chest spawned")))
		{
			return;
		}
		C.Test.AddInfo(FString::Printf(TEXT("The chest holds %d stack(s)"), Chest->GetItems().Num()));
		C.LookFrom(Spot + FVector(-CameraDistance, 0.0f, EyeHeight), Spot + FVector(0.0f, 0.0f, 50.0f));
	});
	Scenario.Wait(SettleSeconds);
	Scenario.Step(TEXT("chest.png"), [](FScenarioContext& C) { C.Screenshot(TEXT("chest")); });

	Scenario.Run();
	return true;
}

// --- Monster attack --------------------------------------------------------------------------------

// Checklist 4: a monster fighting. approach.png: a built-in melee monster as it aggroes a player 3 m
// away; attack.png: the frame it lands a hit (the player is kept alive). The camera looks along the
// line between them from the side.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardVisualMonsterAttack, "Crossguard.Visual.MonsterAttack", CROSSGUARD_VISUAL_FLAGS)

bool FCrossguardVisualMonsterAttack::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioVisual;

	static const FVector MonsterSpot(0.0f, 7500.0f, 0.0f);
	static const FVector PlayerSpot(0.0f, 7800.0f, 0.0f);
	FScenario Scenario(*this);

	Scenario.Step(TEXT("A melee monster and a player 3 m apart"), [](FScenarioContext& C)
	{
		ACrossguardMonster* Monster = C.SpawnMonster(TEXT("Monster"), NAME_None, MonsterSpot, 90.0f);
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), PlayerSpot, -90.0f);
		if (!C.Require(Monster != nullptr && Player != nullptr, TEXT("monster and player spawned")))
		{
			return;
		}
		C.SetNumber(TEXT("PlayerHealth"), C.Health(Player));
		// From the side, at the midpoint.
		C.LookFrom(FVector(-400.0f, 7650.0f, EyeHeight), FVector(0.0f, 7650.0f, 100.0f));
	});
	Scenario.Wait(SettleSeconds);
	Scenario.Step(TEXT("approach.png"), [](FScenarioContext& C) { C.Screenshot(TEXT("approach")); });

	Scenario.WaitUntil(TEXT("the monster hits the player"), 10.0f, [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Player"));
		if (Player == nullptr)
		{
			return false;
		}
		const bool bHit = C.LogContains(FString::Printf(TEXT("%s took "), *Player->GetName()));
		if (bHit)
		{
			C.Screenshot(TEXT("attack"));
			C.SetHealth(Player, static_cast<float>(C.Number(TEXT("PlayerHealth"))));
		}
		return bHit;
	});

	Scenario.Run();
	return true;
}

// --- Dragonification -------------------------------------------------------------------------------

// Checklist 4: the Dragonification form. before.png: a player; dragon.png: the same player after
// casting Dragonification (State.Dragonified on). The code gives the form no mesh or scale change
// yet, so the two should look alike until art arrives.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardVisualDragonification, "Crossguard.Visual.Dragonification", CROSSGUARD_VISUAL_FLAGS)

bool FCrossguardVisualDragonification::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioVisual;

	static const FVector Spot(0.0f, 8000.0f, 0.0f);
	FScenario Scenario(*this);

	Scenario.Step(TEXT("A player with Dragonification, the camera 3 m away"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Dragon = C.SpawnPlayer(TEXT("Dragon"), Spot, 180.0f);
		if (!C.Require(Dragon != nullptr, TEXT("player spawned")))
		{
			return;
		}
		C.Require(C.GrantSkills(Dragon, {TEXT("dragonification")}), TEXT("Dragonification granted"));
		CameraOn(C, Spot);
	});
	Scenario.Wait(SettleSeconds);
	Scenario.Step(TEXT("before.png"), [](FScenarioContext& C) { C.Screenshot(TEXT("before")); });

	Scenario.Step(TEXT("Dragonification"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Dragon = C.Get<ACrossguardCharacter>(TEXT("Dragon"));
		if (Dragon != nullptr && C.Require(C.ActivateSkill(Dragon, TEXT("dragonification")), TEXT("Dragonification activates")))
		{
			C.Test.TestTrue(TEXT("State.Dragonified"), C.HasTag(Dragon, CrossguardTags::State_Dragonified));
		}
	});
	Scenario.Wait(1.0f);
	Scenario.Step(TEXT("dragon.png"), [](FScenarioContext& C) { C.Screenshot(TEXT("dragon")); });

	Scenario.Run();
	return true;
}

// --- Stealth, from an enemy's and an ally's screen -------------------------------------------------

// Checklist 4: "stealth: vanish on the other screen". Listen server + 2 clients: the host is the
// sneak's ally, client 2 its enemy; both viewports look at the sneak (client 1). Per screen:
// before.png, stealth.png (1 s into Stealth: gone from the enemy's, still on the ally's), after.png
// (back on both when Stealth ends).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardVisualStealthViews, "Crossguard.Visual.StealthViews", CROSSGUARD_VISUAL_FLAGS)

bool FCrossguardVisualStealthViews::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioVisual;

	static const FVector SneakSpot(0.0f, 8500.0f, 0.0f);
	FScenario Scenario(*this);
	Scenario.Clients(3);

	Scenario.Step(TEXT("Sneak (client 1) between Ally (host) and Enemy (client 2), 3 m each side"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Ally = C.ServerPawnOf(0);
		ACrossguardCharacter* Sneak = C.ServerPawnOf(1);
		ACrossguardCharacter* Enemy = C.ServerPawnOf(2);
		if (!C.Require(Ally != nullptr && Sneak != nullptr && Enemy != nullptr, TEXT("three client pawns")))
		{
			return;
		}
		C.Place(Ally, SneakSpot + FVector(0.0f, -300.0f, 0.0f), 90.0f);
		C.Place(Sneak, SneakSpot, 180.0f);
		C.Place(Enemy, SneakSpot + FVector(0.0f, 300.0f, 0.0f), -90.0f);
		C.MakeAllies(Ally, Sneak);
		C.Remember(TEXT("Sneak"), Sneak);
		C.Require(C.Equip(Sneak, TEXT("sword_one_handed")) && C.GrantSkills(Sneak, {TEXT("stealth")}), TEXT("the sneak has a sword and Stealth"));
	});
	Scenario.WaitUntilOnClient(1, TEXT("the sneak's client has Stealth, the enemy client has the sneak"), 5.0f, [](FScenarioContext& C, UWorld&)
	{
		return C.HasSkill(C.ClientPawn(1), TEXT("stealth")) && C.OnClient(C.FindActor(TEXT("Sneak")), 2) != nullptr;
	});
	Scenario.Step(TEXT("The ally's and the enemy's viewports look at the sneak"), [](FScenarioContext& C)
	{
		CameraOn(C, SneakSpot, 0);
		CameraOn(C, SneakSpot, 2);
	});
	Scenario.Wait(SettleSeconds);
	Scenario.Step(TEXT("before.png from the enemy's and the ally's screen"), [](FScenarioContext& C)
	{
		C.Screenshot(TEXT("enemy_before"), 2);
		C.Screenshot(TEXT("ally_before"), 0);
	});

	Scenario.Step(TEXT("The sneak activates Stealth on its client"), [](FScenarioContext& C)
	{
		C.Require(C.ActivateSkill(C.ClientPawn(1), TEXT("stealth")), TEXT("Stealth activates"));
	});
	Scenario.WaitUntil(TEXT("the server has State.Stealthed"), 3.0f, [](FScenarioContext& C)
	{
		return C.HasTag(C.FindActor(TEXT("Sneak")), CrossguardTags::State_Stealthed);
	});
	Scenario.Wait(1.0f);
	Scenario.Step(TEXT("stealth.png: gone from the enemy's screen, still on the ally's"), [](FScenarioContext& C)
	{
		C.Screenshot(TEXT("enemy_stealth"), 2);
		C.Screenshot(TEXT("ally_stealth"), 0);
	});

	Scenario.WaitUntil(TEXT("Stealth ends"), 15.0f, [](FScenarioContext& C)
	{
		return !C.HasTag(C.FindActor(TEXT("Sneak")), CrossguardTags::State_Stealthed);
	});
	Scenario.WaitUntilOnClient(2, TEXT("the sneak is back on the enemy client"), 5.0f, [](FScenarioContext& C, UWorld&)
	{
		return C.OnClient(C.FindActor(TEXT("Sneak")), 2) != nullptr;
	});
	Scenario.Wait(SettleSeconds);
	Scenario.Step(TEXT("after.png on both screens"), [](FScenarioContext& C)
	{
		C.Screenshot(TEXT("enemy_after"), 2);
		C.Screenshot(TEXT("ally_after"), 0);
	});

	Scenario.Run();
	return true;
}

// --- Placeholder UI --------------------------------------------------------------------------------

// docs/ui.md: the PIE player's own screen (its HUD draws on its viewport; no camera actor here).
// hud.png: the HUD with items, a sword and Fireball; inventory_body.png: the inventory window with a
// dead dummy's body open beside it; trade.png: the trade window after asking a partner 2 m away;
// guild.png: the guild window. Layout is the point (overlap, unreadable text, off-screen parts).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardVisualUI, "Crossguard.Visual.UI", CROSSGUARD_VISUAL_FLAGS)

bool FCrossguardVisualUI::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioVisual;

	static const FVector PlayerSpot(0.0f, 9000.0f, 0.0f);
	static const FVector DummySpot(150.0f, 9000.0f, 0.0f);
	FScenario Scenario(*this);

	Scenario.Step(TEXT("The PIE player with items, a sword and Fireball; a partner 2 m off; a dummy with a ruby"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.LocalPlayer();
		APlayerController* Controller = Player != nullptr ? Cast<APlayerController>(Player->GetController()) : nullptr;
		if (!C.Require(Player != nullptr && Controller != nullptr && Controller->GetLocalPlayer() != nullptr, TEXT("the PIE player has a local player controller")))
		{
			return;
		}
		if (!C.Require(Cast<ACrossguardHUD>(Controller->GetHUD()) != nullptr, FString::Printf(TEXT("the player's HUD is ACrossguardHUD (is %s)"), *GetNameSafe(Controller->GetHUD()))))
		{
			return;
		}
		C.Place(Player, PlayerSpot, 0.0f);
		ACrossguardCharacter* Partner = C.SpawnPlayer(TEXT("Partner"), PlayerSpot + FVector(0.0f, 200.0f, 0.0f), -90.0f);
		ACrossguardCharacter* Dummy = C.SpawnDummy(TEXT("Dummy"), DummySpot, 180.0f);
		if (!C.Require(Partner != nullptr && Dummy != nullptr, TEXT("partner and dummy spawned")) || !C.Equip(Player, TEXT("sword_one_handed")))
		{
			return;
		}
		C.Require(C.GiveItem(Player, TEXT("healing_potion"), 2) == 0 && C.GiveItem(Player, TEXT("bandage"), 1) == 0 && C.GiveItem(Player, TEXT("ruby"), 1) == 0,
			TEXT("the player's items fit"));
		C.Require(C.GiveItem(Dummy, TEXT("ruby"), 1, ECrossguardRarity::Rare) == 0, TEXT("the dummy's ruby fits"));
		C.GrantSkills(Player, {TEXT("fireball")});
		C.AimAt(Player, EyesAt(DummySpot));
		C.Kill(Dummy, Player);
	});
	Scenario.Wait(1.0f);
	Scenario.Step(TEXT("hud.png"), [](FScenarioContext& C) { C.Screenshot(TEXT("hud")); });

	Scenario.Step(TEXT("The inventory window with the body open"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.LocalPlayer();
		ACrossguardHUD* Hud = Player != nullptr ? Cast<ACrossguardHUD>(Cast<APlayerController>(Player->GetController())->GetHUD()) : nullptr;
		ACrossguardLootContainer* Body = C.FindNearest<ACrossguardLootContainer>(DummySpot, 400.0f);
		if (!C.Require(Hud != nullptr && Body != nullptr, TEXT("the HUD and the body")))
		{
			return;
		}
		C.Require(Player->GetInventoryComponent()->OpenLootContainer(Body), TEXT("the player opens the body"));
		if (!Hud->IsWindowOpen(Hud->GetInventoryWidget()))
		{
			Hud->ToggleInventory();
		}
		C.Test.TestTrue(TEXT("The inventory window is open"), Hud->IsWindowOpen(Hud->GetInventoryWidget()));
	});
	Scenario.Wait(SettleSeconds);
	Scenario.Step(TEXT("inventory_body.png; then the trade window"), [](FScenarioContext& C)
	{
		C.Screenshot(TEXT("inventory_body"));
		ACrossguardCharacter* Player = C.LocalPlayer();
		ACrossguardHUD* Hud = Player != nullptr ? Cast<ACrossguardHUD>(Cast<APlayerController>(Player->GetController())->GetHUD()) : nullptr;
		if (Hud == nullptr)
		{
			return;
		}
		Hud->CloseAllWindows();
		Hud->ToggleTrade();
		C.Test.TestTrue(TEXT("The trade window is open"), Hud->IsWindowOpen(Hud->GetTradeWidget()));
	});
	Scenario.Wait(SettleSeconds);
	Scenario.Step(TEXT("trade.png; then the guild window"), [](FScenarioContext& C)
	{
		C.Screenshot(TEXT("trade"));
		ACrossguardCharacter* Player = C.LocalPlayer();
		ACrossguardHUD* Hud = Player != nullptr ? Cast<ACrossguardHUD>(Cast<APlayerController>(Player->GetController())->GetHUD()) : nullptr;
		if (Hud == nullptr)
		{
			return;
		}
		Hud->CloseAllWindows();
		Hud->ToggleGuild();
		C.Test.TestTrue(TEXT("The guild window is open"), Hud->IsWindowOpen(Hud->GetGuildWidget()));
	});
	Scenario.Wait(SettleSeconds);
	Scenario.Step(TEXT("guild.png"), [](FScenarioContext& C)
	{
		C.Screenshot(TEXT("guild"));
		ACrossguardCharacter* Player = C.LocalPlayer();
		if (ACrossguardHUD* Hud = Player != nullptr ? Cast<ACrossguardHUD>(Cast<APlayerController>(Player->GetController())->GetHUD()) : nullptr)
		{
			Hud->CloseAllWindows();
		}
	});

	Scenario.Run();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
