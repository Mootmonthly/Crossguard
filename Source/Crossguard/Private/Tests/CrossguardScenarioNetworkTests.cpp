// Networked scenario tests: a server and real PIE clients in one process (FScenario::Clients). What
// only a second client can show: replicated equipment tags and deaths, stealth relevancy on the wire,
// concurrent loot RPCs from two connections, Hunter's Tracking marks staying on the tracker's screen.
// Framework: CrossguardScenario.h; docs/editor-tests.md "Networked scenarios"; docs/anti-cheat.md
// "Stealth position leak".

#include "Tests/CrossguardScenario.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AbilitySystem/CrossguardGameplayTags.h"
#include "Character/CrossguardCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Items/CrossguardInventoryComponent.h"
#include "Items/CrossguardLootContainer.h"
#include "Skills/CrossguardTrackingComponent.h"

namespace CrossguardScenarioNet
{
	// The scene row (cm): away from the Player Start at (-30 m, -30 m) where every client's pawn appears.
	constexpr float SceneY = 8000.0f;
	// Game seconds a replicated change may take to show on a client (one process: usually a frame or two).
	constexpr float ReplicationWait = 5.0f;
	constexpr float StealthSeconds = 12.0f;

	// Server: client pawns in a row at SceneY, 3 m apart from X = 0, facing +X.
	bool PlaceClients(CrossguardScenario::FScenarioContext& C)
	{
		for (int32 Index = 0; Index < C.ClientCount(); ++Index)
		{
			ACrossguardCharacter* Pawn = C.ServerPawnOf(Index);
			if (!C.Require(Pawn != nullptr, FString::Printf(TEXT("client %d has a server pawn"), Index)))
			{
				return false;
			}
			C.Place(Pawn, FVector(300.0f * Index, SceneY, 0.0f), 0.0f);
		}
		return true;
	}

	int32 FootprintCount(const ACrossguardCharacter* Character)
	{
		const UCrossguardTrackingComponent* Tracking = Character != nullptr ? Character->FindComponentByClass<UCrossguardTrackingComponent>() : nullptr;
		return Tracking != nullptr ? Tracking->GetInstanceCount() : 0;
	}

	int32 CountItems(const TArray<FCrossguardInventoryItem>& Items)
	{
		int32 Count = 0;
		for (const FCrossguardInventoryItem& Item : Items)
		{
			Count += Item.Count;
		}
		return Count;
	}
}

// --- Equipment tags -------------------------------------------------------------------------------

// Checklist 6: `showdebug` on a client shows the other player's Equipment.Wielding.* tags. The host
// and a client each equip a weapon; the client sees the host's tag and its own, and the host's goes
// when it unequips.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardNetWieldingTags, "Crossguard.Net.WieldingTagsReplicate", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardNetWieldingTags::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioNet;

	FScenario Scenario(*this);
	Scenario.Clients(2);

	Scenario.Step(TEXT("The host wields a sword, client 1 an axe"), [](FScenarioContext& C)
	{
		if (!PlaceClients(C))
		{
			return;
		}
		ACrossguardCharacter* Host = C.ServerPawnOf(0);
		ACrossguardCharacter* Other = C.ServerPawnOf(1);
		if (!C.Equip(Host, TEXT("sword_one_handed")) || !C.Equip(Other, TEXT("axe_one_handed")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("Server: the host has Equipment.Wielding.Sword"), C.HasTag(Host, CrossguardTags::Equipment_Wielding_Sword));
		C.Test.TestTrue(TEXT("Server: client 1 has Equipment.Wielding.Axe"), C.HasTag(Other, CrossguardTags::Equipment_Wielding_Axe));
	});

	Scenario.WaitUntilOnClient(1, TEXT("client 1 sees the host's Wielding.Sword and its own Wielding.Axe"), ReplicationWait, [](FScenarioContext& C, UWorld&)
	{
		const AActor* HostCopy = C.OnClient(C.ServerPawnOf(0), 1);
		const ACrossguardCharacter* Own = C.ClientPawn(1);
		return HostCopy != nullptr && Own != nullptr && C.HasTag(HostCopy, CrossguardTags::Equipment_Wielding_Sword)
			&& C.HasTag(Own, CrossguardTags::Equipment_Wielding_Axe);
	});

	Scenario.Step(TEXT("The host puts the sword away"), [](FScenarioContext& C)
	{
		C.Unequip(C.ServerPawnOf(0), ECrossguardEquipmentSlot::MainHand);
		C.Test.TestFalse(TEXT("Server: the host's Wielding.Sword is gone"), C.HasTag(C.ServerPawnOf(0), CrossguardTags::Equipment_Wielding_Sword));
	});

	Scenario.WaitUntilOnClient(1, TEXT("client 1 sees the host's Wielding.Sword gone"), ReplicationWait, [](FScenarioContext& C, UWorld&)
	{
		const AActor* HostCopy = C.OnClient(C.ServerPawnOf(0), 1);
		return HostCopy != nullptr && !C.HasTag(HostCopy, CrossguardTags::Equipment_Wielding_Sword);
	});

	Scenario.Run();
	return true;
}

// --- Death ---------------------------------------------------------------------------------------

// Checklist 6: both clients see a body ragdoll. Client 2 dies to client 1; client 1's copy and
// client 2's own body simulate physics and have no capsule collision, as does the host's view.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardNetDeathRagdoll, "Crossguard.Net.DeathRagdollsOnEveryClient", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardNetDeathRagdoll::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioNet;

	FScenario Scenario(*this);
	Scenario.Clients(3);

	Scenario.Step(TEXT("Client 1 kills client 2"), [](FScenarioContext& C)
	{
		if (!PlaceClients(C))
		{
			return;
		}
		ACrossguardCharacter* Killer = C.ServerPawnOf(1);
		ACrossguardCharacter* Victim = C.ServerPawnOf(2);
		C.Remember(TEXT("Victim"), Victim);
		C.Kill(Victim, Killer);
		C.Test.TestTrue(TEXT("Server: the victim is dead"), C.IsDead(Victim));
	});

	for (int32 Client = 1; Client <= 2; ++Client)
	{
		Scenario.WaitUntilOnClient(Client, TEXT("the victim's body ragdolls"), ReplicationWait, [](FScenarioContext& C, UWorld& ClientWorld)
		{
			const ACrossguardCharacter* Copy = nullptr;
			for (TActorIterator<ACrossguardCharacter> It(&ClientWorld); It; ++It)
			{
				if (C.IsDead(*It))
				{
					Copy = *It;
				}
			}
			return Copy != nullptr && Copy->GetMesh()->IsSimulatingPhysics()
				&& Copy->GetCapsuleComponent()->GetCollisionEnabled() == ECollisionEnabled::NoCollision;
		});
	}

	Scenario.Step(TEXT("The copies are the victim; the host's view ragdolls too"), [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Victim = C.Get<ACrossguardCharacter>(TEXT("Victim"));
		if (Victim == nullptr)
		{
			return;
		}
		for (int32 Client = 1; Client <= 2; ++Client)
		{
			const ACrossguardCharacter* Copy = C.OnClient<ACrossguardCharacter>(Victim, Client);
			C.Test.TestNotNull(*FString::Printf(TEXT("Client %d has the victim"), Client), Copy);
			if (Copy != nullptr)
			{
				C.Test.TestTrue(*FString::Printf(TEXT("Client %d: the victim's copy is dead and ragdolled"), Client),
					C.IsDead(Copy) && Copy->GetMesh()->IsSimulatingPhysics());
			}
		}
		C.Test.TestTrue(TEXT("The host sees the server actor ragdoll"), Victim->GetMesh()->IsSimulatingPhysics());
	});

	Scenario.Run();
	return true;
}

// --- Stealth -------------------------------------------------------------------------------------

// Checklist 4 (stealth vanishes on the other screen) and docs/anti-cheat.md: on a dedicated server,
// the enemy client loses the stealthed character (its actor goes with the channel) while the ally
// client keeps it; it comes back cleanly when stealth ends. The time from stealth start to the drop
// is measured and reported: the net driver keeps a no-longer-relevant actor for RelevantTimeout
// (engine default 5 s). No ruling on that yet, so it is reported, not asserted.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardNetStealth, "Crossguard.Net.StealthHidesFromEnemyClient", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardNetStealth::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioNet;

	// Dedicated server: a listen host would see everything (no connection to filter).
	FScenario Scenario(*this);
	Scenario.Clients(3, true);

	Scenario.Step(TEXT("Ally (0) and Sneak (1) on one team, Enemy (2) on another, 3 m apart"), [](FScenarioContext& C)
	{
		if (!PlaceClients(C))
		{
			return;
		}
		ACrossguardCharacter* Ally = C.ServerPawnOf(0);
		ACrossguardCharacter* Sneak = C.ServerPawnOf(1);
		ACrossguardCharacter* Enemy = C.ServerPawnOf(2);
		C.Remember(TEXT("Sneak"), Sneak);
		C.MakeAllies(Ally, Sneak);
		IConsoleVariable* Switch = IConsoleManager::Get().FindConsoleVariable(TEXT("crossguard.StealthHidesFromEnemies"));
		if (!C.Require(Switch != nullptr, TEXT("crossguard.StealthHidesFromEnemies exists")))
		{
			return;
		}
		if (!Switch->GetBool())
		{
			C.Skip(TEXT("crossguard.StealthHidesFromEnemies is off; set it to 1 to run this test"));
			return;
		}
		C.Test.TestTrue(TEXT("Ally and Sneak are allies"), C.PlayerState(Ally) != nullptr && C.PlayerState(Sneak) != nullptr
			&& C.PlayerState(Ally)->GetTeamId() == C.PlayerState(Sneak)->GetTeamId());
		C.Test.TestTrue(TEXT("Enemy is on another team"), C.PlayerState(Enemy) != nullptr
			&& C.PlayerState(Enemy)->GetTeamId() != C.PlayerState(Sneak)->GetTeamId());
		if (!C.Equip(Sneak, TEXT("sword_one_handed")))
		{
			return;
		}
		C.Require(C.GrantSkills(Sneak, {TEXT("stealth")}), TEXT("Stealth granted to the sneak"));
	});

	Scenario.WaitUntilOnClient(2, TEXT("the enemy client has the sneak"), ReplicationWait, [](FScenarioContext& C, UWorld&)
	{
		return C.OnClient(C.FindActor(TEXT("Sneak")), 2) != nullptr;
	});
	Scenario.WaitUntilOnClient(0, TEXT("the ally client has the sneak and knows the teams"), ReplicationWait, [](FScenarioContext& C, UWorld&)
	{
		const ACrossguardCharacter* SneakCopy = C.OnClient<ACrossguardCharacter>(C.FindActor(TEXT("Sneak")), 0);
		const ACrossguardCharacter* Own = C.ClientPawn(0);
		return SneakCopy != nullptr && Own != nullptr && C.PlayerState(SneakCopy) != nullptr && C.PlayerState(Own) != nullptr
			&& C.PlayerState(SneakCopy)->GetTeamId() == C.PlayerState(Own)->GetTeamId();
	});
	Scenario.WaitUntilOnClient(1, TEXT("the sneak's own client has the Stealth skill"), ReplicationWait, [](FScenarioContext& C, UWorld&)
	{
		return C.HasSkill(C.ClientPawn(1), TEXT("stealth"));
	});

	Scenario.Step(TEXT("The sneak activates Stealth on its own client (predicted, as E does)"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Own = C.ClientPawn(1);
		if (!C.Require(Own != nullptr, TEXT("the sneak's client pawn")))
		{
			return;
		}
		C.Require(C.ActivateSkill(Own, TEXT("stealth")), TEXT("Stealth activates on the client"));
	});

	Scenario.WaitUntil(TEXT("the server has State.Stealthed on the sneak"), 3.0f, [](FScenarioContext& C)
	{
		if (!C.HasTag(C.FindActor(TEXT("Sneak")), CrossguardTags::State_Stealthed))
		{
			return false;
		}
		C.SetNumber(TEXT("StealthedAt"), C.Now());
		return true;
	});

	Scenario.WaitUntilOnClient(2, TEXT("the enemy client loses the sneak (the ally keeps it)"), StealthSeconds, [](FScenarioContext& C, UWorld&)
	{
		if (C.OnClient(C.FindActor(TEXT("Sneak")), 0) == nullptr)
		{
			C.Fail(TEXT("The ally client lost the stealthed sneak"));
			return false;
		}
		return C.OnClient(C.FindActor(TEXT("Sneak")), 2) == nullptr;
	});

	Scenario.Step(TEXT("How long the enemy client kept the stealthed character"), [](FScenarioContext& C)
	{
		const float Leak = C.Now() - static_cast<float>(C.Number(TEXT("StealthedAt")));
		C.SetNumber(TEXT("Leak"), Leak);
		C.Test.AddInfo(FString::Printf(TEXT("STEALTH LEAK: the enemy client dropped the stealthed character %.2f s after stealth began (engine RelevantTimeout default 5 s)"), Leak));
		C.Test.TestTrue(TEXT("Stealth is still on"), C.HasTag(C.FindActor(TEXT("Sneak")), CrossguardTags::State_Stealthed));
		C.Test.TestTrue(TEXT("The drop came within RelevantTimeout + 1 s"), Leak <= 6.0f);
	});

	Scenario.WaitUntil(TEXT("stealth ends"), StealthSeconds + 2.0f, [](FScenarioContext& C)
	{
		return !C.HasTag(C.FindActor(TEXT("Sneak")), CrossguardTags::State_Stealthed);
	});

	Scenario.WaitUntilOnClient(2, TEXT("the sneak is back on the enemy client, clean"), ReplicationWait, [](FScenarioContext& C, UWorld&)
	{
		const ACrossguardCharacter* Sneak = C.Get<ACrossguardCharacter>(TEXT("Sneak"));
		const ACrossguardCharacter* Copy = C.OnClient<ACrossguardCharacter>(Sneak, 2);
		return Sneak != nullptr && Copy != nullptr && !C.HasTag(Copy, CrossguardTags::State_Stealthed) && C.Health(Copy) > 0.0f
			&& FVector::Dist(Copy->GetActorLocation(), Sneak->GetActorLocation()) < 100.0f;
	});

	Scenario.Step(TEXT("The copy's state matches the server"), [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Sneak = C.Get<ACrossguardCharacter>(TEXT("Sneak"));
		const ACrossguardCharacter* Copy = C.OnClient<ACrossguardCharacter>(Sneak, 2);
		if (Sneak == nullptr || Copy == nullptr)
		{
			return;
		}
		C.Test.TestEqual(TEXT("Health replicated"), C.Health(Copy), C.Health(Sneak), 0.5f);
		C.Test.TestTrue(TEXT("The sword is back in the copy's hand"), C.HasTag(Copy, CrossguardTags::Equipment_Wielding_Sword));
		C.Test.TestTrue(TEXT("The ally client still has the sneak"), C.OnClient(Sneak, 0) != nullptr);
	});

	Scenario.Run();
	return true;
}

// --- Concurrent loot -----------------------------------------------------------------------------

// Checklist 6: two real clients taking every stack of one body in the same frame, by RPC, never
// duplicate an item: every stack ends up in one inventory or the other, the totals unchanged.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardNetConcurrentLoot, "Crossguard.Net.ConcurrentLootFromTwoClients", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardNetConcurrentLoot::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioNet;

	FScenario Scenario(*this);
	Scenario.Clients(3);

	Scenario.Step(TEXT("A geared dummy between clients 1 and 2 dies"), [](FScenarioContext& C)
	{
		if (!PlaceClients(C))
		{
			return;
		}
		ACrossguardCharacter* A = C.ServerPawnOf(1);
		ACrossguardCharacter* B = C.ServerPawnOf(2);
		C.Place(A, FVector(350.0f, SceneY, 0.0f), 0.0f);
		C.Place(B, FVector(550.0f, SceneY, 0.0f), 180.0f);
		ACrossguardCharacter* Dummy = C.SpawnDummy(TEXT("Dummy"), FVector(450.0f, SceneY, 0.0f));
		if (!C.Require(Dummy != nullptr, TEXT("dummy spawned")))
		{
			return;
		}
		C.Require(C.GiveItem(Dummy, TEXT("healing_potion"), 3) == 0 && C.GiveItem(Dummy, TEXT("bandage"), 2) == 0
			&& C.GiveItem(Dummy, TEXT("ruby"), 1, ECrossguardRarity::Rare) == 0, TEXT("the dummy holds 3 potions, 2 bandages, a rare ruby"));
		C.Kill(Dummy, A);
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Both open the body on the server; both clients take every stack in one frame"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = C.ServerPawnOf(1);
		ACrossguardCharacter* B = C.ServerPawnOf(2);
		ACrossguardCharacter* ClientA = C.ClientPawn(1);
		ACrossguardCharacter* ClientB = C.ClientPawn(2);
		ACrossguardLootContainer* Body = C.FindNearest<ACrossguardLootContainer>(FVector(450.0f, SceneY, 0.0f), 400.0f);
		if (!C.Require(A != nullptr && B != nullptr && ClientA != nullptr && ClientB != nullptr && Body != nullptr, TEXT("both players, their client pawns and the body")))
		{
			return;
		}
		C.Remember(TEXT("Body"), Body);
		if (!C.Require(A->GetInventoryComponent()->OpenLootContainer(Body) && B->GetInventoryComponent()->OpenLootContainer(Body), TEXT("both reach and open the body")))
		{
			return;
		}
		const TArray<FCrossguardInventoryItem> Stacks = Body->GetItems();
		C.SetNumber(TEXT("Stacks"), Stacks.Num());
		C.SetNumber(TEXT("Total"), CountItems(Stacks) + CountItems(A->GetInventoryComponent()->GetItems()) + CountItems(B->GetInventoryComponent()->GetItems()));
		C.Test.TestEqual(TEXT("The body holds 3 stacks"), Stacks.Num(), 3);
		// Client-side requests: on a client RequestTakeFromContainer sends the ServerTakeFromContainer RPC.
		for (int32 Index = 0; Index < Stacks.Num(); ++Index)
		{
			UCrossguardInventoryComponent* First = Index % 2 == 0 ? ClientA->GetInventoryComponent() : ClientB->GetInventoryComponent();
			UCrossguardInventoryComponent* Second = Index % 2 == 0 ? ClientB->GetInventoryComponent() : ClientA->GetInventoryComponent();
			First->RequestTakeFromContainer(Stacks[Index].Id);
			Second->RequestTakeFromContainer(Stacks[Index].Id);
		}
	});

	Scenario.WaitUntil(TEXT("the RPCs arrive and the body is empty"), ReplicationWait, [](FScenarioContext& C)
	{
		const ACrossguardLootContainer* Body = C.Get<ACrossguardLootContainer>(TEXT("Body"));
		return Body != nullptr && Body->GetItems().IsEmpty();
	});

	Scenario.Step(TEXT("Nothing duplicated, nothing lost"), [](FScenarioContext& C)
	{
		const ACrossguardCharacter* A = C.ServerPawnOf(1);
		const ACrossguardCharacter* B = C.ServerPawnOf(2);
		const ACrossguardLootContainer* Body = C.Get<ACrossguardLootContainer>(TEXT("Body"));
		if (A == nullptr || B == nullptr || Body == nullptr)
		{
			return;
		}
		const int32 Total = CountItems(Body->GetItems()) + CountItems(A->GetInventoryComponent()->GetItems()) + CountItems(B->GetInventoryComponent()->GetItems());
		C.Test.TestEqual(TEXT("Items across the body and both inventories"), Total, static_cast<int32>(C.Number(TEXT("Total"))));
		C.Test.TestEqual(TEXT("Stacks across both inventories"), A->GetInventoryComponent()->GetItems().Num() + B->GetInventoryComponent()->GetItems().Num(),
			static_cast<int32>(C.Number(TEXT("Stacks"))));
		// Who gets what depends on whose packets the server reads first (one connection may take all);
		// the late requests are refused as UnknownItem (the stack is gone).
		C.Test.AddInfo(FString::Printf(TEXT("Client 1 took %d stack(s), client 2 %d"), A->GetInventoryComponent()->GetItems().Num(),
			B->GetInventoryComponent()->GetItems().Num()));
	});

	Scenario.WaitUntilOnClient(1, TEXT("client 1's inventory matches the server's"), ReplicationWait, [](FScenarioContext& C, UWorld&)
	{
		const ACrossguardCharacter* A = C.ServerPawnOf(1);
		const ACrossguardCharacter* ClientA = C.ClientPawn(1);
		return A != nullptr && ClientA != nullptr
			&& CountItems(ClientA->GetInventoryComponent()->GetItems()) == CountItems(A->GetInventoryComponent()->GetItems());
	});

	Scenario.Run();
	return true;
}

// --- Hunter's Tracking ---------------------------------------------------------------------------

// Checklist 4: Hunter's Tracking's grey marks show on the tracker's screen only. Client 1 has the
// passive; the host (an enemy) walks; footprints appear on client 1's own pawn's tracking component,
// and nowhere else (the host's own, and the server's copy of the tracker).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardNetTrackingMarks, "Crossguard.Net.TrackingMarksOnTrackerOnly", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardNetTrackingMarks::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioNet;

	FScenario Scenario(*this);
	Scenario.Clients(2);

	Scenario.Step(TEXT("Client 1 has Hunter's Tracking; the host is an enemy"), [](FScenarioContext& C)
	{
		if (!PlaceClients(C))
		{
			return;
		}
		ACrossguardCharacter* Host = C.ServerPawnOf(0);
		ACrossguardCharacter* Tracker = C.ServerPawnOf(1);
		C.Test.TestTrue(TEXT("Host and tracker are on different teams"), C.PlayerState(Host) != nullptr && C.PlayerState(Tracker) != nullptr
			&& C.PlayerState(Host)->GetTeamId() != C.PlayerState(Tracker)->GetTeamId());
		C.Require(C.GrantSkills(Tracker, {TEXT("hunters_tracking")}), TEXT("Hunter's Tracking granted"));
	});

	Scenario.WaitUntilOnClient(1, TEXT("the tracker's client has Passive.HuntersTracking"), ReplicationWait, [](FScenarioContext& C, UWorld&)
	{
		return C.HasTag(C.ClientPawn(1), CrossguardTags::Passive_HuntersTracking);
	});

	// The host moves 1.2 m a poll along -Y (as Skills.HuntersTracking moves its enemy), 20 steps at most; its
	// replicated position reaches the tracker's client, whose own tracking component records the prints.
	Scenario.WaitUntilOnClient(1, TEXT("the host walks and footprints appear on the tracker's client"), 10.0f, [](FScenarioContext& C, UWorld&)
	{
		ACrossguardCharacter* Host = C.ServerPawnOf(0);
		if (Host == nullptr)
		{
			return false;
		}
		const double Steps = C.Number(TEXT("Steps"));
		C.SetNumber(TEXT("Steps"), Steps + 1.0);
		C.Place(Host, FVector(0.0f, SceneY - 120.0f * static_cast<float>(FMath::Min(Steps, 20.0)), 0.0f), 0.0f);
		return FootprintCount(C.ClientPawn(1)) >= 3;
	}, 0.25f);

	Scenario.Step(TEXT("Marks on the tracker's client only"), [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Host = C.ServerPawnOf(0);
		const ACrossguardCharacter* Tracker = C.ServerPawnOf(1);
		C.Test.AddInfo(FString::Printf(TEXT("Footprints on the tracker's client: %d"), FootprintCount(C.ClientPawn(1))));
		C.Test.TestEqual(TEXT("No marks on the host's own screen"), FootprintCount(Host), 0);
		C.Test.TestEqual(TEXT("No marks on the server's copy of the tracker"), FootprintCount(Tracker), 0);
		C.Test.TestEqual(TEXT("No marks on client 1's copy of the host"), FootprintCount(C.OnClient<ACrossguardCharacter>(Host, 1)), 0);
	});

	Scenario.Run();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
