// Scenario tests: gathering nodes (docs/next-build-checklist.md, gathering rows). A native
// ACrossguardGatherNode is spawned (a Blueprint only adds a mesh) with the gather_herb / gather_ore
// loot tables. Framework: CrossguardScenario.h, docs/editor-tests.md.
//
// F -> ACrossguardCharacter::TryInteract.

#include "Tests/CrossguardScenario.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Character/CrossguardCharacter.h"
#include "CrossguardRules/Config.h"
#include "Engine/DataTable.h"
#include "Game/CrossguardGameMode.h"
#include "Game/CrossguardPlayerState.h"
#include "Interaction/CrossguardGatherNode.h"
#include "Items/CrossguardInventoryComponent.h"
#include "Items/CrossguardItemTypes.h"
#include "Items/CrossguardLoot.h"

namespace CrossguardScenarioGathering
{
	using CrossguardScenario::FScenarioContext;

	const FName GatherHerbTable(TEXT("gather_herb"));
	const FName GatherOreTable(TEXT("gather_ore"));
	const FName HerbSilverleaf(TEXT("herb_silverleaf"));
	const FName HerbBloodroot(TEXT("herb_bloodroot"));
	const FName OreIron(TEXT("ore_iron"));
	const FName OreMithril(TEXT("ore_mithril"));

	// Let spawned characters land, and the view (read through the camera manager) settle after aiming.
	constexpr float GatherLandSeconds = 0.5f;
	constexpr float GatherViewSettleSeconds = 0.2f;
	// The node's default channel (placeholder 2 s) and some slack for its 0.1 s checks.
	constexpr float GatherChannelSeconds = 2.0f;
	constexpr float GatherSlackSeconds = 0.5f;
	// The node stands 1.5 m in front of the player.
	const FVector GatherPlayerPoint(0.0f, 1000.0f, 0.0f);
	const FVector GatherNodePoint(150.0f, 1000.0f, 0.0f);

	// Skips (with a warning) unless the game mode's loot tables and the character's item table hold
	// the gathering data: the editor's DT_Loot / DT_Items must be reimported after the JSON changed.
	bool HasGatherData(FScenarioContext& C, const ACrossguardCharacter* Player, FName TableId, FName ItemId)
	{
		const ACrossguardGameMode* Mode = C.GameMode();
		if (Mode == nullptr || Mode->Loot.LootTables == nullptr)
		{
			C.Skip(TEXT("the game mode's Loot Tables are unset (import Data/Loot/LootTable.json as DT_Loot and set BP_CrossguardGameMode, Crossguard | Loot)"));
			return false;
		}
		if (Mode->Loot.LootTables->FindRow<FCrossguardLootTableRow>(TableId, TEXT("Scenario"), false) == nullptr)
		{
			C.Skip(FString::Printf(TEXT("%s has no %s row (reimport it from Data/Loot/LootTable.json)"), *Mode->Loot.LootTables->GetName(),
				*TableId.ToString()));
			return false;
		}
		const UDataTable* Items = Player != nullptr && Player->GetInventoryComponent() != nullptr ? Player->GetInventoryComponent()->ItemTable.Get() : nullptr;
		if (Items == nullptr || Items->FindRow<FCrossguardItemRow>(ItemId, TEXT("Scenario"), false) == nullptr)
		{
			C.Skip(FString::Printf(TEXT("the character's Item Table has no %s row (reimport DT_Items from Data/Items/ItemTable.json)"), *ItemId.ToString()));
			return false;
		}
		return true;
	}

	ACrossguardCharacter* GatherPlayer(FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Player"));
		if (Player == nullptr)
		{
			C.Fail(TEXT("the player is gone"));
		}
		return Player;
	}

	ACrossguardGatherNode* GatherNodeOf(FScenarioContext& C)
	{
		ACrossguardGatherNode* Node = C.Get<ACrossguardGatherNode>(TEXT("Node"));
		if (Node == nullptr)
		{
			C.Fail(TEXT("the gather node is gone"));
		}
		return Node;
	}

	int32 GatheredCountOf(const ACrossguardCharacter* Player, FName First, FName Second)
	{
		const UCrossguardInventoryComponent* Inventory = Player != nullptr ? Player->GetInventoryComponent() : nullptr;
		return Inventory != nullptr ? Inventory->CountItem(First) + Inventory->CountItem(Second) : 0;
	}

	int32 RaidXpOf(FScenarioContext& C, const ACrossguardCharacter* Player)
	{
		const ACrossguardPlayerState* State = C.PlayerState(Player);
		return State != nullptr ? State->GetRaidXp() : -1;
	}

	// Spawns the player facing a node with TableId; remembers the raid XP before gathering.
	void SpawnGatherScene(FScenarioContext& C, FName TableId, FName ItemId)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), GatherPlayerPoint, 0.0f);
		if (!C.Require(Player != nullptr, TEXT("player spawned")) || !HasGatherData(C, Player, TableId, ItemId))
		{
			return;
		}
		ACrossguardGatherNode* Node = C.SpawnActor<ACrossguardGatherNode>(TEXT("Node"), GatherNodePoint, 180.0f,
			[TableId](ACrossguardGatherNode& Spawned) { Spawned.LootTableId = TableId; });
		if (!C.Require(Node != nullptr, TEXT("gather node spawned")))
		{
			return;
		}
		C.Test.TestEqual(TEXT("The node's channel is the placeholder 2 s"), Node->GatherSeconds, GatherChannelSeconds);
		C.Test.TestEqual(TEXT("The node is once per raid (RespawnSeconds 0)"), Node->RespawnSeconds, 0.0f);
		const ACrossguardPlayerState* State = C.PlayerState(Player);
		if (!C.Require(State != nullptr && State->IsInRaid(), TEXT("the player is in a raid")))
		{
			return;
		}
		C.SetNumber(TEXT("RaidXp"), State->GetRaidXp());
		C.AimAt(Player, Node->GetActorLocation());
	}
}

// ------------------------------------------------------------------------------------------------
// Checklist: "Place a CrossguardGatherNode with Loot Table Id gather_herb, press F at it and stand
// still -> after 2 s herbs are in your inventory, +10 raid XP, the node disappears; F at it again
// does nothing."
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioGatherNodeHerb, "Crossguard.Scenario.Raid.GatherNode", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioGatherNodeHerb::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioGathering;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A player faces a gather_herb node"), [](FScenarioContext& C)
	{
		SpawnGatherScene(C, GatherHerbTable, HerbSilverleaf);
	});

	Scenario.Wait(GatherLandSeconds + GatherViewSettleSeconds);

	Scenario.Step(TEXT("F starts the channel"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = GatherPlayer(C);
		ACrossguardGatherNode* Node = GatherNodeOf(C);
		if (Player == nullptr || Node == nullptr)
		{
			return;
		}
		C.AimAt(Player, Node->GetActorLocation());
		C.ClearLog();
		Player->TryInteract();
		if (!C.Require(Node->GetGatherer() == Player, TEXT("the player is gathering the node")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("Log: started gathering"), C.LogContains(TEXT("started gathering")));
		C.Test.TestEqual(TEXT("Nothing gathered yet"), GatheredCountOf(Player, HerbSilverleaf, HerbBloodroot), 0);
	});

	Scenario.Wait(GatherChannelSeconds + GatherSlackSeconds);

	Scenario.Step(TEXT("Done: herbs in the inventory, +10 raid XP, the node is depleted"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = GatherPlayer(C);
		ACrossguardGatherNode* Node = GatherNodeOf(C);
		if (Player == nullptr || Node == nullptr)
		{
			return;
		}
		C.Test.TestTrue(TEXT("Log: gathered"), C.LogContains(TEXT("gathered")));
		C.Test.TestFalse(TEXT("The channel is over"), Node->IsBeingGathered());
		C.Test.TestTrue(TEXT("The node is depleted"), Node->IsDepleted());
		C.Test.TestTrue(TEXT("At least one herb is in the inventory"), GatheredCountOf(Player, HerbSilverleaf, HerbBloodroot) >= 1);
		const int32 Expected = static_cast<int32>(C.Number(TEXT("RaidXp"))) + Crossguard::FXpConfig().GatherNode;
		C.Test.TestEqual(TEXT("+10 raid XP (GatherNode)"), RaidXpOf(C, Player), Expected);
		C.SetNumber(TEXT("Herbs"), GatheredCountOf(Player, HerbSilverleaf, HerbBloodroot));
		C.SetNumber(TEXT("RaidXpAfter"), RaidXpOf(C, Player));
	});

	Scenario.Step(TEXT("F at the depleted node is refused"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = GatherPlayer(C);
		ACrossguardGatherNode* Node = GatherNodeOf(C);
		if (Player == nullptr || Node == nullptr)
		{
			return;
		}
		C.AimAt(Player, Node->GetActorLocation());
		C.ClearLog();
		Player->TryInteract();
		C.Test.TestFalse(TEXT("No channel starts"), Node->IsBeingGathered());
		C.Test.TestTrue(TEXT("Log: cannot gather ... depleted"), C.LogContains(TEXT("depleted")));
	});

	Scenario.Wait(GatherChannelSeconds + GatherSlackSeconds);

	Scenario.Step(TEXT("Still nothing more: same herbs, same XP"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = GatherPlayer(C);
		ACrossguardGatherNode* Node = GatherNodeOf(C);
		if (Player == nullptr || Node == nullptr)
		{
			return;
		}
		C.Test.TestTrue(TEXT("The node stays depleted (once per raid)"), Node->IsDepleted());
		C.Test.TestEqual(TEXT("No more herbs"), GatheredCountOf(Player, HerbSilverleaf, HerbBloodroot), static_cast<int32>(C.Number(TEXT("Herbs"))));
		C.Test.TestEqual(TEXT("No more XP"), RaidXpOf(C, Player), static_cast<int32>(C.Number(TEXT("RaidXpAfter"))));
	});

	Scenario.Run();
	return true;
}

// ------------------------------------------------------------------------------------------------
// Checklist: "Press F at a gather_ore node and step away before 2 s -> the channel breaks: no ore,
// no XP, the node stays."
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioGatherNodeMoveInterrupts, "Crossguard.Scenario.Raid.GatherNodeMoveInterrupts", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioGatherNodeMoveInterrupts::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioGathering;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A player faces a gather_ore node"), [](FScenarioContext& C)
	{
		SpawnGatherScene(C, GatherOreTable, OreIron);
	});

	Scenario.Wait(GatherLandSeconds + GatherViewSettleSeconds);

	Scenario.Step(TEXT("F starts the channel"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = GatherPlayer(C);
		ACrossguardGatherNode* Node = GatherNodeOf(C);
		if (Player == nullptr || Node == nullptr)
		{
			return;
		}
		C.AimAt(Player, Node->GetActorLocation());
		C.ClearLog();
		Player->TryInteract();
		C.Require(Node->GetGatherer() == Player, TEXT("the player is gathering the node"));
	});

	Scenario.Wait(GatherChannelSeconds * 0.25f);

	Scenario.Step(TEXT("The player steps 1 m aside (still in range)"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = GatherPlayer(C);
		if (Player != nullptr)
		{
			C.Place(Player, GatherPlayerPoint + FVector(0.0f, 100.0f, 0.0f), 0.0f);
		}
	});

	Scenario.Wait(0.3f);

	Scenario.Step(TEXT("The channel broke: moved"), [](FScenarioContext& C)
	{
		ACrossguardGatherNode* Node = GatherNodeOf(C);
		if (Node == nullptr)
		{
			return;
		}
		C.Test.TestFalse(TEXT("Nobody is gathering"), Node->IsBeingGathered());
		C.Test.TestTrue(TEXT("Log: interrupted: moved"), C.LogContains(TEXT("interrupted: moved")));
	});

	Scenario.Wait(GatherChannelSeconds + GatherSlackSeconds);

	Scenario.Step(TEXT("No ore, no XP, the node stays"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = GatherPlayer(C);
		ACrossguardGatherNode* Node = GatherNodeOf(C);
		if (Player == nullptr || Node == nullptr)
		{
			return;
		}
		C.Test.TestFalse(TEXT("The node is not depleted"), Node->IsDepleted());
		C.Test.TestFalse(TEXT("Log: no gathered line"), C.LogContains(TEXT(" gathered ")));
		C.Test.TestEqual(TEXT("No ore"), GatheredCountOf(Player, OreIron, OreMithril), 0);
		C.Test.TestEqual(TEXT("No raid XP"), RaidXpOf(C, Player), static_cast<int32>(C.Number(TEXT("RaidXp"))));
	});

	Scenario.Run();
	return true;
}

// ------------------------------------------------------------------------------------------------
// Checklist: "attacking ... breaks the channel". A swing at the air with a sword mid-channel: the
// channel breaks ("attacked"), no ore, no XP, the node stays.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioGatherNodeAttackInterrupts, "Crossguard.Scenario.Raid.GatherNodeAttackInterrupts", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioGatherNodeAttackInterrupts::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioGathering;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A player with a sword faces a gather_ore node"), [](FScenarioContext& C)
	{
		SpawnGatherScene(C, GatherOreTable, OreIron);
		if (!C.IsStopped())
		{
			C.Equip(GatherPlayer(C), TEXT("sword_one_handed"));
		}
	});

	Scenario.Wait(GatherLandSeconds + GatherViewSettleSeconds);

	Scenario.Step(TEXT("F starts the channel"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = GatherPlayer(C);
		ACrossguardGatherNode* Node = GatherNodeOf(C);
		if (Player == nullptr || Node == nullptr)
		{
			return;
		}
		C.AimAt(Player, Node->GetActorLocation());
		C.ClearLog();
		Player->TryInteract();
		C.Require(Node->GetGatherer() == Player, TEXT("the player is gathering the node"));
	});

	Scenario.Wait(GatherChannelSeconds * 0.25f);

	Scenario.Step(TEXT("The player swings"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = GatherPlayer(C);
		if (Player != nullptr)
		{
			C.Require(C.BasicAttack(Player), TEXT("the swing starts"));
		}
	});

	Scenario.Wait(0.3f);

	Scenario.Step(TEXT("The channel broke: attacked"), [](FScenarioContext& C)
	{
		ACrossguardGatherNode* Node = GatherNodeOf(C);
		if (Node == nullptr)
		{
			return;
		}
		C.Test.TestFalse(TEXT("Nobody is gathering"), Node->IsBeingGathered());
		C.Test.TestTrue(TEXT("Log: interrupted: attacked"), C.LogContains(TEXT("interrupted: attacked")));
	});

	Scenario.Wait(GatherChannelSeconds + GatherSlackSeconds);

	Scenario.Step(TEXT("No ore, no XP, the node stays"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = GatherPlayer(C);
		ACrossguardGatherNode* Node = GatherNodeOf(C);
		if (Player == nullptr || Node == nullptr)
		{
			return;
		}
		C.Test.TestFalse(TEXT("The node is not depleted"), Node->IsDepleted());
		C.Test.TestEqual(TEXT("No ore"), GatheredCountOf(Player, OreIron, OreMithril), 0);
		C.Test.TestEqual(TEXT("No raid XP"), RaidXpOf(C, Player), static_cast<int32>(C.Number(TEXT("RaidXp"))));
	});

	Scenario.Run();
	return true;
}

// ------------------------------------------------------------------------------------------------
// Checklist: "taking damage breaks the channel". 5 damage mid-channel (any amount counts): the
// channel breaks ("took damage"), no ore, no XP, the node stays.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioGatherNodeDamageInterrupts, "Crossguard.Scenario.Raid.GatherNodeDamageInterrupts", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioGatherNodeDamageInterrupts::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioGathering;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A player faces a gather_ore node"), [](FScenarioContext& C)
	{
		SpawnGatherScene(C, GatherOreTable, OreIron);
	});

	Scenario.Wait(GatherLandSeconds + GatherViewSettleSeconds);

	Scenario.Step(TEXT("F starts the channel"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = GatherPlayer(C);
		ACrossguardGatherNode* Node = GatherNodeOf(C);
		if (Player == nullptr || Node == nullptr)
		{
			return;
		}
		C.AimAt(Player, Node->GetActorLocation());
		C.ClearLog();
		Player->TryInteract();
		C.Require(Node->GetGatherer() == Player, TEXT("the player is gathering the node"));
	});

	Scenario.Wait(GatherChannelSeconds * 0.25f);

	Scenario.Step(TEXT("The player takes 5 damage"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = GatherPlayer(C);
		if (Player != nullptr)
		{
			C.Test.TestTrue(TEXT("The hit lands"), C.Damage(nullptr, Player, 5.0f) > 0.0f);
		}
	});

	Scenario.Wait(0.3f);

	Scenario.Step(TEXT("The channel broke: took damage"), [](FScenarioContext& C)
	{
		ACrossguardGatherNode* Node = GatherNodeOf(C);
		if (Node == nullptr)
		{
			return;
		}
		C.Test.TestFalse(TEXT("Nobody is gathering"), Node->IsBeingGathered());
		C.Test.TestTrue(TEXT("Log: interrupted: took damage"), C.LogContains(TEXT("interrupted: took damage")));
	});

	Scenario.Wait(GatherChannelSeconds + GatherSlackSeconds);

	Scenario.Step(TEXT("No ore, no XP, the node stays"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = GatherPlayer(C);
		ACrossguardGatherNode* Node = GatherNodeOf(C);
		if (Player == nullptr || Node == nullptr)
		{
			return;
		}
		C.Test.TestFalse(TEXT("The node is not depleted"), Node->IsDepleted());
		C.Test.TestEqual(TEXT("No ore"), GatheredCountOf(Player, OreIron, OreMithril), 0);
		C.Test.TestEqual(TEXT("No raid XP"), RaidXpOf(C, Player), static_cast<int32>(C.Number(TEXT("RaidXp"))));
	});

	Scenario.Run();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
