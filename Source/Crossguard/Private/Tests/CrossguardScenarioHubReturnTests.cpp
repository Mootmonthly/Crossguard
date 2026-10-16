// Scenario tests: DEV-ONLY (listen server): the whole server going back to the hub once nobody is left
// in the raid (docs/hub-and-menu.md, "Development mode"; PIE is development listen mode). The travel itself is planned only here
// (ACrossguardGameMode::bHubReturnPlanOnly); Net.SeamlessTravelHubToRaid travels there and back for real.
// The countdown and the grace are shortened for the run (placeholders: 10 s and 120 s).
// Framework: CrossguardScenario.h, docs/editor-tests.md.

#include "Tests/CrossguardScenario.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Character/CrossguardCharacter.h"
#include "Game/CrossguardGameMode.h"
#include "Game/CrossguardPlayerController.h"
#include "Game/CrossguardPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "UI/CrossguardHUD.h"
#include "UI/CrossguardHudWidget.h"

namespace CrossguardScenarioHubReturn
{
	using CrossguardScenario::FScenarioContext;

	// Game seconds for the run (the game mode's placeholders are 10 s and 120 s).
	constexpr float HubReturnTestCountdown = 1.0f;
	constexpr float HubReturnTestGrace = 3.0f;
	// "?DevPlayerId=<name>" becomes "dev-<name>"; set straight on the player state here.
	const TCHAR* const HubReturnAwayName = TEXT("hubreturn-away");
	const TCHAR* const HubReturnAwayId = TEXT("dev-hubreturn-away");

	// Short countdown and grace, plan only. False (scenario failed) without a game mode.
	bool PrepareHubReturn(FScenarioContext& C)
	{
		ACrossguardGameMode* Mode = C.GameMode();
		if (!C.Require(Mode != nullptr && Mode->bIsRaid, TEXT("a raid map's game mode")))
		{
			return false;
		}
		if (Mode->GetHubMapName().IsEmpty())
		{
			C.Skip(TEXT("The game mode has no Hub Map Name: no trip back to the hub to test"));
			return false;
		}
		if (Mode->GetHubDestination(nullptr).Kind != ECrossguardHubDestination::DevServerTravel)
		{
			C.Skip(TEXT("Not the development whole-server return (a hub server is set, or not dev listen mode)"));
			return false;
		}
		Mode->HubReturnCountdownSeconds = HubReturnTestCountdown;
		Mode->HeldPlayerReturnGraceSeconds = HubReturnTestGrace;
		Mode->bHubReturnPlanOnly = true;
		return true;
	}

	// The URL the game mode should plan: the hub map, ?Hub, ?listen only on a listen server.
	void CheckHubReturnUrl(FScenarioContext& C)
	{
		const ACrossguardGameMode* Mode = C.GameMode();
		if (Mode == nullptr)
		{
			return;
		}
		const FString Url = Mode->GetHubReturnUrl();
		C.Test.AddInfo(FString::Printf(TEXT("Planned return: %s"), *Url));
		C.Test.TestEqual(TEXT("The plan is a server travel to the hub map"), Url,
			ACrossguardGameMode::BuildHubReturnUrl(Mode->GetHubMapName(), Mode->GetNetMode() == NM_ListenServer));
		C.Test.TestTrue(TEXT("The URL starts with the hub map"), Url.StartsWith(Mode->GetHubMapName()));
		C.Test.TestTrue(TEXT("The URL plays the map as the hub (?Hub)"), Url.Contains(TEXT("?Hub")));
		C.Test.TestTrue(TEXT("Log: the return was planned"), C.LogContains(TEXT("Return to the hub planned")));
	}

	// A login as the game mode sees it (as the reconnect tests do): a new controller of the game mode's
	// class, InitNewPlayer with Options, then HandleStartingNewPlayer. Null (and OutError) if refused.
	APlayerController* HubReturnLogin(FScenarioContext& C, FName Name, const FString& Options, FString& OutError)
	{
		UWorld* PlayWorld = C.World();
		ACrossguardGameMode* Mode = C.GameMode();
		if (PlayWorld == nullptr || Mode == nullptr)
		{
			C.Fail(TEXT("HubReturnLogin: no PIE world or game mode"));
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

	// What the net driver does when a connection closes: the controller goes, without a player.
	void HubReturnDisconnect(FScenarioContext& C, FName ControllerName)
	{
		APlayerController* Controller = Cast<APlayerController>(C.FindActor(ControllerName));
		if (C.Require(Controller != nullptr, FString::Printf(TEXT("%s exists"), *ControllerName.ToString())))
		{
			Controller->Destroy();
		}
	}

	// The PIE player's HUD message line ("" without a HUD widget).
	FString HubReturnHudLine(FScenarioContext& C)
	{
		const UWorld* World = C.World();
		const APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
		const ACrossguardHUD* Hud = Controller != nullptr ? Controller->GetHUD<ACrossguardHUD>() : nullptr;
		const UCrossguardHudWidget* Widget = Hud != nullptr ? Hud->GetHudWidget() : nullptr;
		return Widget != nullptr ? Widget->GetMessageLine() : FString();
	}
}

// A raider still in the raid keeps it going: another player extracting starts nothing. Once the last
// one (the PIE player) extracts, the countdown starts (HUD "Returning to the hub in N s") and at its
// end the server's travel targets the hub.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioHubReturnLastExtraction, "Crossguard.Scenario.HubReturn.LastExtractionStartsCountdown",
	CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioHubReturnLastExtraction::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioHubReturn;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("Two raiders: the PIE player and another; the other extracts"), [](FScenarioContext& C)
	{
		if (!PrepareHubReturn(C))
		{
			return;
		}
		ACrossguardCharacter* Local = C.LocalPlayer();
		ACrossguardCharacter* Raider = C.SpawnPlayer(TEXT("Raider"), FVector(0.0f, 1000.0f, 0.0f));
		if (!C.Require(Local != nullptr && Raider != nullptr && C.PlayerState(Local) != nullptr && C.PlayerState(Raider) != nullptr,
				TEXT("both raiders spawned with player states")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("The PIE player is in the raid"), C.PlayerState(Local)->IsInRaid());
		// Unpossessing on extraction clears the pawn's player state: hold it first.
		const ACrossguardPlayerState* RaiderState = C.PlayerState(Raider);
		C.GameMode()->HandleExtraction(Raider);
		C.Test.TestFalse(TEXT("The other raider extracted"), RaiderState->IsInRaid());
		C.Test.TestFalse(TEXT("The PIE player is still in the raid: no countdown"), C.GameMode()->IsHubReturnPending());
	});

	// Longer than the countdown would be.
	Scenario.Wait(HubReturnTestCountdown + 0.5f);

	Scenario.Step(TEXT("Still no return while the PIE player raids; then it extracts too"), [](FScenarioContext& C)
	{
		ACrossguardGameMode* Mode = C.GameMode();
		ACrossguardCharacter* Local = C.Get<ACrossguardCharacter>(TEXT("LocalPlayer"));
		if (Mode == nullptr || Local == nullptr)
		{
			C.Fail(TEXT("the game mode or the PIE player is gone"));
			return;
		}
		C.Test.TestFalse(TEXT("A remaining raider prevents the return"), Mode->IsHubReturnPending() || Mode->HasHubReturnStarted());

		Mode->HandleExtraction(Local);
		C.Test.TestTrue(TEXT("Nobody left in the raid: the countdown runs"), Mode->IsHubReturnPending());
		C.Test.TestFalse(TEXT("Not waiting for anyone away"), Mode->IsHubReturnWaitingForHeldPlayers());
		C.Test.TestTrue(FString::Printf(TEXT("The countdown is the short one (%.2f s left)"), Mode->GetHubReturnSecondsLeft()),
			Mode->GetHubReturnSecondsLeft() > 0.0f && Mode->GetHubReturnSecondsLeft() <= HubReturnTestCountdown + 0.01f);
		C.Test.TestFalse(TEXT("Not travelled yet"), Mode->HasHubReturnStarted());
		C.Test.TestTrue(TEXT("Log: raid over, returning"), C.LogContains(TEXT("Raid over: returning to the hub in 1 s")));
		const FString HudLine = HubReturnHudLine(C);
		if (HudLine.IsEmpty())
		{
			C.Test.AddWarning(TEXT("The PIE player has no HUD widget: the countdown line is not checked"));
		}
		else
		{
			C.Test.TestEqual(TEXT("HUD: the countdown line"), HudLine, FString(TEXT("Returning to the hub in 1 s")));
		}
	});

	Scenario.WaitUntil(TEXT("the countdown ends and the return starts"), HubReturnTestCountdown + 3.0f, [](FScenarioContext& C)
	{
		return C.GameMode() != nullptr && C.GameMode()->HasHubReturnStarted();
	});

	Scenario.Step(TEXT("The return targets the hub; everyone's raid is over"), [](FScenarioContext& C)
	{
		CheckHubReturnUrl(C);
		const ACrossguardGameMode* Mode = C.GameMode();
		C.Test.TestFalse(TEXT("The countdown is done"), Mode != nullptr && Mode->IsHubReturnPending());
	});

	Scenario.Run();
	return true;
}

// Only a disconnected raider (held for a reconnect) is left: the countdown is the longer grace. A
// reconnect calls it off; when the grace runs out with the raider away again, they are released (a
// failed extraction, no way back into this raid) and the return goes ahead.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioHubReturnHeldGrace, "Crossguard.Scenario.HubReturn.HeldOnlyWaitsForGrace", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioHubReturnHeldGrace::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioHubReturn;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A raider with a development id; the PIE player extracts"), [](FScenarioContext& C)
	{
		ACrossguardGameMode* Mode = C.GameMode();
		if (Mode == nullptr || Mode->PlayerControllerClass == nullptr || !Mode->PlayerControllerClass->IsChildOf(ACrossguardPlayerController::StaticClass()))
		{
			C.Skip(TEXT("The game mode's Player Controller Class is not ACrossguardPlayerController (set it on BP_CrossguardGameMode)"));
			return;
		}
		if (!PrepareHubReturn(C))
		{
			return;
		}
		ACrossguardCharacter* Local = C.LocalPlayer();
		ACrossguardCharacter* Raider = C.SpawnPlayer(TEXT("Raider"), FVector(0.0f, 1000.0f, 0.0f));
		if (!C.Require(Local != nullptr && Raider != nullptr && C.PlayerState(Raider) != nullptr, TEXT("both raiders spawned")))
		{
			return;
		}
		// Set before anything keys by it (saving is off in tests).
		C.PlayerState(Raider)->DevPlayerId = HubReturnAwayId;
		Mode->HandleExtraction(Local);
		C.Test.TestFalse(TEXT("The raider is still in the raid: no countdown"), Mode->IsHubReturnPending());
	});

	Scenario.Wait(0.3f);

	Scenario.Step(TEXT("The raider's connection closes"), [](FScenarioContext& C)
	{
		HubReturnDisconnect(C, TEXT("Raider.Controller"));
	});

	// The game mode counts the raid the tick after the logout.
	Scenario.Wait(0.3f);

	Scenario.Step(TEXT("Only a raider away: the grace, not the short countdown"), [](FScenarioContext& C)
	{
		const ACrossguardGameMode* Mode = C.GameMode();
		if (!C.Require(Mode != nullptr && Mode->FindHeldPlayer(HubReturnAwayId) != nullptr, TEXT("the raider is held for a reconnect")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("The countdown runs"), Mode->IsHubReturnPending());
		C.Test.TestTrue(TEXT("It waits for the raider away"), Mode->IsHubReturnWaitingForHeldPlayers());
		C.Test.TestTrue(FString::Printf(TEXT("The grace is used (%.2f s left, countdown %.0f s)"), Mode->GetHubReturnSecondsLeft(), HubReturnTestCountdown),
			Mode->GetHubReturnSecondsLeft() > HubReturnTestCountdown + 0.5f);
	});

	Scenario.Step(TEXT("The raider logs in again: the return is called off"), [](FScenarioContext& C)
	{
		FString Error;
		const APlayerController* Back = HubReturnLogin(C, TEXT("Raider.Controller2"),
			FString::Printf(TEXT("?Name=Raider?DevPlayerId=%s"), HubReturnAwayName), Error);
		const ACrossguardGameMode* Mode = C.GameMode();
		if (!C.Require(Back != nullptr && Mode != nullptr, FString::Printf(TEXT("the reconnect is accepted (%s)"), *Error)))
		{
			return;
		}
		C.Test.TestTrue(TEXT("The raider has their character back"), Back->GetPawn() != nullptr && Back->GetPawn() == C.FindActor(TEXT("Raider")));
		C.Test.TestFalse(TEXT("A raider is back in the raid: no countdown"), Mode->IsHubReturnPending());
		C.Test.TestTrue(TEXT("Log: return called off"), C.LogContains(TEXT("Return to the hub called off")));
	});

	Scenario.Wait(HubReturnTestGrace + 0.5f);

	Scenario.Step(TEXT("No return while they raid; their connection closes again"), [](FScenarioContext& C)
	{
		const ACrossguardGameMode* Mode = C.GameMode();
		if (!C.Require(Mode != nullptr, TEXT("the game mode")))
		{
			return;
		}
		C.Test.TestFalse(TEXT("Past the grace with the raider back: nothing started"), Mode->IsHubReturnPending() || Mode->HasHubReturnStarted());
		HubReturnDisconnect(C, TEXT("Raider.Controller2"));
	});

	Scenario.Wait(0.3f);

	Scenario.Step(TEXT("Away again: the grace starts over"), [](FScenarioContext& C)
	{
		const ACrossguardGameMode* Mode = C.GameMode();
		if (!C.Require(Mode != nullptr && Mode->FindHeldPlayer(HubReturnAwayId) != nullptr, TEXT("the raider is held again")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("Waiting for the raider away"), Mode->IsHubReturnWaitingForHeldPlayers());
		C.SetNumber(TEXT("ReleasedAt"), C.Now() + Mode->GetHubReturnSecondsLeft());
	});

	Scenario.WaitUntil(TEXT("the grace runs out and the return starts"), HubReturnTestGrace + 3.0f, [](FScenarioContext& C)
	{
		return C.GameMode() != nullptr && C.GameMode()->HasHubReturnStarted();
	});

	Scenario.Step(TEXT("The raider away failed the raid; the return targets the hub"), [](FScenarioContext& C)
	{
		const ACrossguardGameMode* Mode = C.GameMode();
		if (Mode == nullptr)
		{
			C.Fail(TEXT("no game mode"));
			return;
		}
		C.Test.TestTrue(TEXT("Not before the grace was up"), C.Now() >= C.Number(TEXT("ReleasedAt")) - 0.05);
		C.Test.TestNull(TEXT("No longer held"), Mode->FindHeldPlayer(HubReturnAwayId));
		C.Test.TestTrue(TEXT("Their raid here is over (a failed extraction)"), Mode->HasFinishedRaidHere(HubReturnAwayId));
		C.Test.TestTrue(TEXT("Log: released as the raid ended"), C.LogContains(TEXT("was still away when the raid ended")));
		if (const ACrossguardCharacter* Body = Cast<ACrossguardCharacter>(C.FindActor(TEXT("Raider"))))
		{
			C.Test.TestNull(TEXT("The character left behind is no longer the player's"), Body->GetPlayerState());
		}
		CheckHubReturnUrl(C);
	});

	Scenario.Run();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
