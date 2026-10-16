// Scenario tests: reconnecting to a raid after a crash or logout (docs/persistence.md, "Reconnecting").
// Framework: CrossguardScenario.h, docs/editor-tests.md.
//
// A disconnect is the engine's own path: destroying a PlayerController that has no player (as the net
// driver does when a connection closes) runs PawnLeavingGame and Logout. A reconnect is a login as the
// game mode sees it (InitNewPlayer, then HandleStartingNewPlayer), with a controlled player id: scenario
// players have no online id, so they get a development id (ACrossguardPlayerState::DevPlayerId, the
// ?DevPlayerId= login option, honoured during automation tests).

#include "Tests/CrossguardScenario.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AbilitySystem/CrossguardGameplayTags.h"
#include "Character/CrossguardCharacter.h"
#include "Game/CrossguardGameMode.h"
#include "Game/CrossguardPlayerController.h"
#include "Game/CrossguardPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Items/CrossguardInventoryComponent.h"
#include "Items/CrossguardLootContainer.h"

namespace CrossguardScenarioReconnect
{
	using CrossguardScenario::FScenarioContext;

	// "?DevPlayerId=<name>" becomes "dev-<name>".
	const TCHAR* const ReconnectNameA = TEXT("reconnect-a");
	const TCHAR* const ReconnectIdA = TEXT("dev-reconnect-a");
	const TCHAR* const ReconnectNameB = TEXT("reconnect-b");
	const TCHAR* const ReconnectIdB = TEXT("dev-reconnect-b");
	const TCHAR* const ReconnectItem = TEXT("healing_potion");
	// A body is spawned at the character's transform when it dies.
	constexpr float ReconnectBodyRadius = 300.0f;

	// The game mode must use ACrossguardPlayerController (or a Blueprint of it): it is what holds the
	// character. Skips the scenario otherwise.
	bool RequireReconnectController(FScenarioContext& C)
	{
		const ACrossguardGameMode* Mode = C.GameMode();
		if (Mode == nullptr || Mode->PlayerControllerClass == nullptr || !Mode->PlayerControllerClass->IsChildOf(ACrossguardPlayerController::StaticClass()))
		{
			C.Skip(TEXT("The game mode's Player Controller Class is not ACrossguardPlayerController (set it on BP_CrossguardGameMode)"));
			return false;
		}
		return true;
	}

	// A raider with a development player id, a potion and some raid XP, at Health 60 %.
	ACrossguardCharacter* SpawnReconnectRaider(FScenarioContext& C, FName Name, const FVector& FloorPoint, const FString& DevId)
	{
		ACrossguardCharacter* Raider = C.SpawnPlayer(Name, FloorPoint);
		ACrossguardPlayerState* Progression = C.PlayerState(Raider);
		if (!C.Require(Raider != nullptr && Progression != nullptr, TEXT("the raider spawned with its player state")))
		{
			return nullptr;
		}
		// Set before anything keys by it (saving is off in tests).
		Progression->DevPlayerId = DevId;
		C.GiveItem(Raider, ReconnectItem, 1);
		Progression->AwardXp(ECrossguardXpSource::BasicMonster);
		C.SetHealth(Raider, C.MaxHealth(Raider) * 0.6f);
		C.Test.TestTrue(TEXT("The raider is in the raid"), Progression->IsInRaid());
		C.Test.TestTrue(TEXT("The raider has raid XP"), Progression->GetRaidXp() > 0);
		return Raider;
	}

	// What the net driver does when the connection closes: the controller goes, without a player.
	void DisconnectReconnectRaider(FScenarioContext& C, FName Name)
	{
		APlayerController* Controller = Cast<APlayerController>(C.FindActor(FName(*(Name.ToString() + TEXT(".Controller")))));
		if (C.Require(Controller != nullptr, TEXT("the raider's controller exists")))
		{
			Controller->Destroy();
		}
	}

	// A login as the game mode sees it: a new controller of the game mode's class, InitNewPlayer with
	// Options and, if accepted, HandleStartingNewPlayer. Returns the controller (null if refused; the
	// refused controller is destroyed, as Login does). OutError: InitNewPlayer's answer.
	APlayerController* ReconnectLogin(FScenarioContext& C, FName Name, const FString& Options, FString& OutError)
	{
		UWorld* PlayWorld = C.World();
		ACrossguardGameMode* Mode = C.GameMode();
		if (PlayWorld == nullptr || Mode == nullptr)
		{
			C.Fail(TEXT("ReconnectLogin: no PIE world or game mode"));
			return nullptr;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		APlayerController* Controller = PlayWorld->SpawnActor<APlayerController>(Mode->PlayerControllerClass, FVector(0.0f, 1000.0f, 0.0f),
			FRotator::ZeroRotator, Params);
		if (!C.Require(Controller != nullptr && Controller->PlayerState != nullptr, TEXT("the login's controller and player state spawned")))
		{
			return nullptr;
		}
		Controller->SetAsLocalPlayerController();
		OutError = Mode->InitNewPlayer(Controller, FUniqueNetIdRepl(), Options);
		if (!OutError.IsEmpty())
		{
			Controller->Destroy();
			return nullptr;
		}
		Mode->HandleStartingNewPlayer_Implementation(Controller);
		C.Remember(Name, Controller);
		return Controller;
	}
}

// Disconnect mid-raid: the character stays (same inventory, Health, raid XP), held for the same id; a
// login with the same id while still connected, or with another id, does not get it; the same id
// takes it back (same pawn, same player state).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioReconnectRetakesCharacter, "Crossguard.Scenario.Reconnect.RetakesCharacter", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioReconnectRetakesCharacter::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioReconnect;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A raider with a potion, raid XP and 60 % Health"), [](FScenarioContext& C)
	{
		if (!RequireReconnectController(C))
		{
			return;
		}
		ACrossguardCharacter* Raider = SpawnReconnectRaider(C, TEXT("Raider"), FVector(0.0f, 1000.0f, 0.0f), ReconnectIdA);
		if (Raider == nullptr)
		{
			return;
		}
		C.SetNumber(TEXT("RaidXp"), C.PlayerState(Raider)->GetRaidXp());
		C.SetNumber(TEXT("Health"), C.Health(Raider));
		C.SetNumber(TEXT("Potions"), Raider->GetInventoryComponent()->CountItem(ReconnectItem));
		C.SetNumber(TEXT("Team"), C.PlayerState(Raider)->GetTeamId());
		C.Test.TestEqual(TEXT("The potion is carried"), Raider->GetInventoryComponent()->CountItem(ReconnectItem), 1);

		// Same id while the raider is still connected: refused, nothing taken over.
		FString Error;
		const APlayerController* Twin = ReconnectLogin(C, TEXT("Twin"), FString::Printf(TEXT("?Name=Twin?DevPlayerId=%s"), ReconnectNameA), Error);
		C.Test.TestNull(TEXT("A second login with the same id is refused while the raider is connected"), Twin);
		C.Test.TestEqual(TEXT("Refusal reason"), Error, FString(TEXT("You are already in this match.")));
	});

	Scenario.Wait(0.3f);

	Scenario.Step(TEXT("The raider's connection closes"), [](FScenarioContext& C)
	{
		DisconnectReconnectRaider(C, TEXT("Raider"));
	});

	Scenario.Wait(0.3f);

	Scenario.Step(TEXT("The character stays in the world, unpossessed, still the player's"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Raider = C.Get<ACrossguardCharacter>(TEXT("Raider"));
		const ACrossguardGameMode* Mode = C.GameMode();
		if (!C.Require(Raider != nullptr && Mode != nullptr, TEXT("the raider's character was not destroyed")))
		{
			return;
		}
		const ACrossguardPlayerState* Held = Mode->FindHeldPlayer(ReconnectIdA);
		if (!C.Require(Held != nullptr, TEXT("the game mode holds the player for a reconnect")))
		{
			return;
		}
		C.Test.TestNull(TEXT("Nobody controls the character"), Raider->GetController());
		C.Test.TestTrue(TEXT("The character keeps its player state"), Raider->GetPlayerState() == Held);
		C.Test.TestTrue(TEXT("Still in the raid"), Held->IsInRaid());
		C.Test.TestFalse(TEXT("The raid is not over for them"), Held->HasFinishedRaid());
		C.Test.TestEqual(TEXT("Raid XP kept"), Held->GetRaidXp(), static_cast<int32>(C.Number(TEXT("RaidXp"))));
		C.Test.TestEqual(TEXT("Inventory kept"), Raider->GetInventoryComponent()->CountItem(ReconnectItem),
			static_cast<int32>(C.Number(TEXT("Potions"))));
		C.Test.TestEqual(TEXT("Health kept"), C.Health(Raider), static_cast<float>(C.Number(TEXT("Health"))), 0.01f);
		C.Test.TestTrue(TEXT("The character is alive"), !C.IsDead(Raider));

		// Another id: a new player with a player state of their own, not the held one.
		FString Error;
		APlayerController* Other = ReconnectLogin(C, TEXT("Other"), TEXT("?Name=Other?DevPlayerId=someone-else"), Error);
		if (C.Require(Other != nullptr, FString::Printf(TEXT("another player can log in (%s)"), *Error)))
		{
			C.Test.TestTrue(TEXT("Another id does not get the held player state"), Other->PlayerState.Get() != Held);
			C.Test.TestTrue(TEXT("Another id does not get the held character"), Other->GetPawn() != Raider);
			C.Test.TestNotNull(TEXT("The player is still held"), Mode->FindHeldPlayer(ReconnectIdA));
		}
	});

	Scenario.Wait(0.3f);

	Scenario.Step(TEXT("The same id logs in again and takes the character back"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Raider = C.Get<ACrossguardCharacter>(TEXT("Raider"));
		const ACrossguardGameMode* Mode = C.GameMode();
		if (Raider == nullptr || Mode == nullptr)
		{
			C.Fail(TEXT("the held character is gone"));
			return;
		}
		const ACrossguardPlayerState* Held = Mode->FindHeldPlayer(ReconnectIdA);
		FString Error;
		APlayerController* Back = ReconnectLogin(C, TEXT("Raider.Controller2"), FString::Printf(TEXT("?Name=Raider?DevPlayerId=%s"), ReconnectNameA), Error);
		if (!C.Require(Back != nullptr, FString::Printf(TEXT("the reconnect is accepted (%s)"), *Error)))
		{
			return;
		}
		const ACrossguardPlayerState* Progression = Back->GetPlayerState<ACrossguardPlayerState>();
		C.Test.TestTrue(TEXT("The held player state is back on the new controller"), Progression != nullptr && Progression == Held);
		C.Test.TestTrue(TEXT("The same character is possessed (no new pawn)"), Raider->GetController() == Back && Back->GetPawn() == Raider);
		C.Test.TestNull(TEXT("No longer held"), Mode->FindHeldPlayer(ReconnectIdA));
		if (Progression != nullptr)
		{
			C.Test.TestTrue(TEXT("Still in the raid"), Progression->IsInRaid());
			C.Test.TestEqual(TEXT("Same raid XP"), Progression->GetRaidXp(), static_cast<int32>(C.Number(TEXT("RaidXp"))));
			C.Test.TestEqual(TEXT("Same team"), Progression->GetTeamId(), static_cast<int32>(C.Number(TEXT("Team"))));
			C.Test.TestEqual(TEXT("Same player id"), Progression->DevPlayerId, FString(ReconnectIdA));
		}
		C.Test.TestEqual(TEXT("Same inventory"), Raider->GetInventoryComponent()->CountItem(ReconnectItem), static_cast<int32>(C.Number(TEXT("Potions"))));
		C.Test.TestEqual(TEXT("Same Health (no healing by reconnecting)"), C.Health(Raider), static_cast<float>(C.Number(TEXT("Health"))), 0.01f);
	});

	Scenario.Run();
	return true;
}

// The character dies while its player is away: an ordinary death (a body to loot), the raid fails for
// that player, and logging in to this raid again is refused.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioReconnectDiedWhileAway, "Crossguard.Scenario.Reconnect.DiedWhileAway", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioReconnectDiedWhileAway::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioReconnect;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A raider with a potion"), [](FScenarioContext& C)
	{
		if (RequireReconnectController(C))
		{
			SpawnReconnectRaider(C, TEXT("Raider"), FVector(0.0f, 1000.0f, 0.0f), ReconnectIdB);
		}
	});

	Scenario.Wait(0.3f);

	Scenario.Step(TEXT("The raider's connection closes"), [](FScenarioContext& C)
	{
		DisconnectReconnectRaider(C, TEXT("Raider"));
		const ACrossguardGameMode* Mode = C.GameMode();
		C.Require(Mode != nullptr && Mode->FindHeldPlayer(ReconnectIdB) != nullptr, TEXT("the player is held for a reconnect"));
	});

	Scenario.Step(TEXT("The away character is killed and its revive window runs out"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Raider = C.Get<ACrossguardCharacter>(TEXT("Raider"));
		ACrossguardGameMode* Mode = C.GameMode();
		if (Raider == nullptr || Mode == nullptr)
		{
			C.Fail(TEXT("the held character is gone"));
			return;
		}
		C.Kill(Raider);
		if (!C.Require(C.IsDead(Raider), TEXT("the away character is down")))
		{
			return;
		}
		// The 120 s revive window's end (ACrossguardCharacter::OnReviveWindowExpired calls this).
		Mode->HandlePermanentDeath(Raider);
	});

	Scenario.Wait(0.3f);

	Scenario.Step(TEXT("Raid failed for them; a body to loot; no way back in"), [](FScenarioContext& C)
	{
		ACrossguardGameMode* Mode = C.GameMode();
		if (Mode == nullptr)
		{
			C.Fail(TEXT("no game mode"));
			return;
		}
		C.Test.TestNull(TEXT("No longer held"), Mode->FindHeldPlayer(ReconnectIdB));
		C.Test.TestTrue(TEXT("Their raid here is over"), Mode->HasFinishedRaidHere(ReconnectIdB));
		if (const ACrossguardCharacter* Raider = Cast<ACrossguardCharacter>(C.FindActor(TEXT("Raider"))))
		{
			C.Test.TestNull(TEXT("The body is no longer the player's"), Raider->GetPlayerState());
			C.Test.TestNotNull(TEXT("A body to loot"), C.FindNearest<ACrossguardLootContainer>(Raider->GetActorLocation(), ReconnectBodyRadius));
		}

		FString Error;
		const APlayerController* Back = ReconnectLogin(C, TEXT("Raider.Controller2"), FString::Printf(TEXT("?Name=Raider?DevPlayerId=%s"), ReconnectNameB), Error);
		C.Test.TestNull(TEXT("Logging in to the raid again is refused"), Back);
		C.Test.TestEqual(TEXT("Refusal reason"), Error, FString(TEXT("Your raid is over.")));
	});

	Scenario.Run();
	return true;
}

// A teammate sees the marker: a dedicated server with one real client (the teammate). A raider on the
// client's team disconnects: the raider's player state, as replicated to the teammate's client, says
// IsDisconnected (the party panel and the HUD team list show "(disconnected)" from it); after the
// reconnect it clears there too.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardNetReconnectMarker, "Crossguard.Net.ReconnectShowsDisconnected", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardNetReconnectMarker::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioReconnect;

	// Game seconds a replicated change may take to reach the client.
	static constexpr float MarkerReplicationWait = 5.0f;
	FScenario Scenario(*this);
	Scenario.Clients(1, true);

	Scenario.Step(TEXT("A raider on the client's team"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Teammate = C.ServerPawnOf(0);
		if (!RequireReconnectController(C) || !C.Require(Teammate != nullptr, TEXT("the client has a server pawn")))
		{
			return;
		}
		C.Place(Teammate, FVector(0.0f, 1300.0f, 0.0f), 0.0f);
		ACrossguardCharacter* Raider = SpawnReconnectRaider(C, TEXT("Raider"), FVector(0.0f, 1000.0f, 0.0f), ReconnectIdA);
		if (Raider == nullptr)
		{
			return;
		}
		C.MakeAllies(Raider, Teammate);
		C.Remember(TEXT("RaiderState"), C.PlayerState(Raider));
		C.Test.TestFalse(TEXT("Connected: no marker on the server"), C.PlayerState(Raider)->IsDisconnected());
	});

	Scenario.WaitUntilOnClient(0, TEXT("the teammate's client has the raider's player state, not disconnected"), MarkerReplicationWait,
		[](FScenarioContext& C, UWorld&)
	{
		const ACrossguardPlayerState* Copy = C.OnClient<ACrossguardPlayerState>(C.FindActor(TEXT("RaiderState")), 0);
		return Copy != nullptr && !Copy->IsDisconnected();
	});

	Scenario.Step(TEXT("The raider's connection closes"), [](FScenarioContext& C)
	{
		DisconnectReconnectRaider(C, TEXT("Raider"));
		const ACrossguardGameMode* Mode = C.GameMode();
		const ACrossguardPlayerState* Held = Mode != nullptr ? Mode->FindHeldPlayer(ReconnectIdA) : nullptr;
		if (C.Require(Held != nullptr, TEXT("the player is held for a reconnect")))
		{
			C.Test.TestTrue(TEXT("Held: marked disconnected on the server"), Held->IsDisconnected());
		}
	});

	Scenario.WaitUntilOnClient(0, TEXT("the teammate's client sees the raider disconnected"), MarkerReplicationWait, [](FScenarioContext& C, UWorld&)
	{
		const ACrossguardPlayerState* Copy = C.OnClient<ACrossguardPlayerState>(C.FindActor(TEXT("RaiderState")), 0);
		return Copy != nullptr && Copy->IsDisconnected();
	});

	Scenario.Step(TEXT("The same id logs in again"), [](FScenarioContext& C)
	{
		FString Error;
		const APlayerController* Back = ReconnectLogin(C, TEXT("Raider.Controller2"), FString::Printf(TEXT("?Name=Raider?DevPlayerId=%s"), ReconnectNameA), Error);
		if (!C.Require(Back != nullptr, FString::Printf(TEXT("the reconnect is accepted (%s)"), *Error)))
		{
			return;
		}
		const ACrossguardPlayerState* Progression = Back->GetPlayerState<ACrossguardPlayerState>();
		C.Test.TestTrue(TEXT("The held player state is back"), Progression != nullptr && Progression == C.FindActor(TEXT("RaiderState")));
		C.Test.TestFalse(TEXT("Back: no marker on the server"), Progression != nullptr && Progression->IsDisconnected());
	});

	Scenario.WaitUntilOnClient(0, TEXT("the marker clears on the teammate's client"), MarkerReplicationWait, [](FScenarioContext& C, UWorld&)
	{
		const ACrossguardPlayerState* Copy = C.OnClient<ACrossguardPlayerState>(C.FindActor(TEXT("RaiderState")), 0);
		return Copy != nullptr && !Copy->IsDisconnected();
	});

	Scenario.Run();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
