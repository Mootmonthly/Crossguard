// Scenario tests: a team that has finished the raid leaves the match server for its hub destination
// while other teams still raid (docs/hub-and-menu.md, "A team goes back"): a hub server
// (HubServerAddress), the development whole-server return (dev listen mode, no hub server), or the
// menu (not development mode, no hub server: forced with Crossguard::SetDevListenModeOverrideForTests).
// The members' travel is planned only here (ACrossguardGameMode::bHubReturnPlanOnly): the plan is read
// back per member. The countdown is shortened for the run (placeholder: 10 s).
// Framework: CrossguardScenario.h, docs/editor-tests.md.

#include "Tests/CrossguardScenario.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Camera/PlayerCameraManager.h"
#include "Character/CrossguardCharacter.h"
#include "Game/CrossguardDevMode.h"
#include "Game/CrossguardGameMode.h"
#include "Game/CrossguardMenuGameMode.h"
#include "Game/CrossguardPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "UI/CrossguardHUD.h"
#include "UI/CrossguardHudWidget.h"

namespace CrossguardScenarioTeamReturn
{
	using CrossguardScenario::FScenarioContext;

	constexpr float TeamReturnTestCountdown = 1.0f;
	const TCHAR* const TeamReturnHubServer = TEXT("203.0.113.9:7777");
	const TCHAR* const TeamReturnPartyA = TEXT("teamreturnA");
	// Development ids, set straight on the player states ("dev-<name>": the URL carries ?DevPlayerId=<name>).
	const TCHAR* const TeamReturnFirstId = TEXT("dev-teamreturn-a1");
	const TCHAR* const TeamReturnSecondId = TEXT("dev-teamreturn-a2");

	// Short countdown, plan only. HubServer: the hub server address to set (empty: none). False
	// (scenario over) without a game mode, or when -HubServer= on the command line would override it.
	bool PrepareTeamReturn(FScenarioContext& C, const FString& HubServer)
	{
		ACrossguardGameMode* Mode = C.GameMode();
		if (!C.Require(Mode != nullptr && Mode->bIsRaid, TEXT("a raid map's game mode")))
		{
			return false;
		}
		if (Mode->GetHubServerAddress() != Mode->HubServerAddress)
		{
			C.Skip(TEXT("-HubServer= is on the command line: the test's address would be ignored"));
			return false;
		}
		Mode->HubServerAddress = HubServer;
		Mode->HubReturnCountdownSeconds = TeamReturnTestCountdown;
		Mode->bHubReturnPlanOnly = true;
		return true;
	}

	// Two raiders on team A (party key TeamReturnPartyA, development ids). False if they did not spawn.
	bool SpawnTeamA(FScenarioContext& C)
	{
		ACrossguardCharacter* First = C.SpawnPlayer(TEXT("A1"), FVector(0.0f, 1000.0f, 0.0f), 0.0f, TeamReturnPartyA);
		ACrossguardCharacter* Second = C.SpawnPlayer(TEXT("A2"), FVector(0.0f, 1300.0f, 0.0f), 0.0f, TeamReturnPartyA);
		ACrossguardCharacter* Local = C.LocalPlayer();
		if (!C.Require(First != nullptr && Second != nullptr && Local != nullptr && C.PlayerState(First) != nullptr && C.PlayerState(Second) != nullptr
				&& C.PlayerState(Local) != nullptr,
				TEXT("team A's two raiders and the PIE player spawned with player states")))
		{
			return false;
		}
		C.PlayerState(First)->DevPlayerId = TeamReturnFirstId;
		C.PlayerState(Second)->DevPlayerId = TeamReturnSecondId;
		const int32 TeamA = C.PlayerState(First)->GetTeamId();
		C.Test.TestTrue(TEXT("Team A is one team"), TeamA >= 0 && C.PlayerState(Second)->GetTeamId() == TeamA);
		C.Test.TestNotEqual(TEXT("The PIE player is on another team (team B)"), C.PlayerState(Local)->GetTeamId(), TeamA);
		C.SetNumber(TEXT("TeamA"), TeamA);
		C.SetNumber(TEXT("TeamB"), C.PlayerState(Local)->GetTeamId());
		return true;
	}

	// The plan's entry for PlayerId, or null.
	const FCrossguardTeamHubTravel* TeamReturnEntry(const TArray<FCrossguardTeamHubTravel>& Plan, const FString& PlayerId)
	{
		return Plan.FindByPredicate([&PlayerId](const FCrossguardTeamHubTravel& Travel) { return Travel.PlayerId == PlayerId; });
	}

	// The controller's camera shows Target, or is blending to it.
	bool TeamReturnWatches(const APlayerController* Controller, const AActor* Target)
	{
		const APlayerCameraManager* Camera = Controller != nullptr ? Controller->PlayerCameraManager.Get() : nullptr;
		return Controller != nullptr && Target != nullptr
			&& (Controller->GetViewTarget() == Target || (Camera != nullptr && Camera->PendingViewTarget.Target.Get() == Target));
	}

	// The PIE player's HUD message line ("" without a HUD widget).
	FString TeamReturnHudLine(FScenarioContext& C)
	{
		const UWorld* World = C.World();
		const APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
		const ACrossguardHUD* Hud = Controller != nullptr ? Controller->GetHUD<ACrossguardHUD>() : nullptr;
		const UCrossguardHudWidget* Widget = Hud != nullptr ? Hud->GetHudWidget() : nullptr;
		return Widget != nullptr ? Widget->GetMessageLine() : FString();
	}
}

// With a hub server (HubServerAddress, as -HubServer=): two teams, A (two raiders) and B (the PIE
// player). A2 dies for the raid and watches A1; A1 extracts: A2 is not sent to the menu but watches its
// body, and team A counts down alone (B unaffected). At its end both members travel to the hub server
// with ?Party=<key> at once; both are leaving and A2's disconnect is no hold for a reconnect. Then B
// (the last team, holding the server's own player) extracts: it leaves as a team too, no whole-server
// travel.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioTeamReturnHubServer, "Crossguard.Scenario.HubReturn.TeamToHubServer", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioTeamReturnHubServer::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioTeamReturn;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A hub server is set; team A dies and extracts while team B (the PIE player) raids"), [](FScenarioContext& C)
	{
		if (!PrepareTeamReturn(C, TeamReturnHubServer) || !SpawnTeamA(C))
		{
			return;
		}
		ACrossguardGameMode* Mode = C.GameMode();
		ACrossguardCharacter* First = C.Get<ACrossguardCharacter>(TEXT("A1"));
		ACrossguardCharacter* Second = C.Get<ACrossguardCharacter>(TEXT("A2"));
		APlayerController* SecondController = Cast<APlayerController>(C.FindActor(TEXT("A2.Controller")));
		const int32 TeamA = static_cast<int32>(C.Number(TEXT("TeamA")));
		const int32 TeamB = static_cast<int32>(C.Number(TEXT("TeamB")));
		if (!C.Require(First != nullptr && Second != nullptr && SecondController != nullptr, TEXT("team A's raiders and A2's controller")))
		{
			return;
		}
		const FCrossguardHubDestination Destination = Mode->GetHubDestination(SecondController);
		C.Test.TestTrue(TEXT("Destination: the hub server"),
			Destination.Kind == ECrossguardHubDestination::HubServer && Destination.Address == TeamReturnHubServer);

		Mode->HandlePermanentDeath(Second);
		C.Test.TestTrue(TEXT("A2 watches A1 (a teammate still in the raid)"), TeamReturnWatches(SecondController, First));
		C.Test.TestFalse(TEXT("A1 still raids: no countdown for team A"), Mode->IsTeamHubReturnPending(TeamA));

		Mode->HandleExtraction(First);
		C.Test.TestFalse(TEXT("Ruled: A2 (dead, nobody left to watch) is not sent to the menu"), Mode->WasSentToMenu(TeamReturnSecondId));
		C.Test.TestTrue(TEXT("A2 watches its body"), TeamReturnWatches(SecondController, Second));
		C.Test.TestTrue(TEXT("Team A finished: its countdown runs"), Mode->IsTeamHubReturnPending(TeamA));
		C.Test.TestTrue(FString::Printf(TEXT("Team A's countdown is the short one (%.2f s left)"), Mode->GetTeamHubReturnSecondsLeft(TeamA)),
			Mode->GetTeamHubReturnSecondsLeft(TeamA) > 0.0f && Mode->GetTeamHubReturnSecondsLeft(TeamA) <= TeamReturnTestCountdown + 0.01f);
		C.Test.TestFalse(TEXT("Team B still raids: no countdown for it"), Mode->IsTeamHubReturnPending(TeamB));
		C.Test.TestFalse(TEXT("No whole-server return"), Mode->IsHubReturnPending() || Mode->HasHubReturnStarted());
		C.Test.TestTrue(TEXT("Log: team A returns while others raid"), C.LogContains(TEXT("returning to the hub in 1 s while others raid")));
	});

	Scenario.WaitUntil(TEXT("team A's countdown ends and its plan is made"), TeamReturnTestCountdown + 3.0f, [](FScenarioContext& C)
	{
		const ACrossguardGameMode* Mode = C.GameMode();
		return Mode != nullptr && Mode->GetTeamHubReturnPlan(static_cast<int32>(C.Number(TEXT("TeamA")))) != nullptr;
	});

	Scenario.Step(TEXT("Team A's plan: both travel to the hub server with the party key; B unaffected"), [](FScenarioContext& C)
	{
		ACrossguardGameMode* Mode = C.GameMode();
		const int32 TeamA = static_cast<int32>(C.Number(TEXT("TeamA")));
		const TArray<FCrossguardTeamHubTravel>* Plan = Mode != nullptr ? Mode->GetTeamHubReturnPlan(TeamA) : nullptr;
		if (!C.Require(Plan != nullptr && Plan->Num() == 2, TEXT("a plan for both members")))
		{
			return;
		}
		const FString Expected = ACrossguardGameMode::BuildTeamHubJoinUrl(TeamReturnHubServer, TeamReturnPartyA);
		C.Test.TestEqual(TEXT("Hub server URL"), Expected, FString::Printf(TEXT("%s?Party=%s"), TeamReturnHubServer, TeamReturnPartyA));
		for (const TCHAR* Id : {TeamReturnFirstId, TeamReturnSecondId})
		{
			const FCrossguardTeamHubTravel* Travel = TeamReturnEntry(*Plan, Id);
			if (!C.Require(Travel != nullptr, FString::Printf(TEXT("%s is in the plan"), Id)))
			{
				return;
			}
			C.Test.AddInfo(FString::Printf(TEXT("%s: %s"), Id, *Travel->Url));
			C.Test.TestTrue(FString(Id) + TEXT(" travels to the hub server"), Travel->Mode == ECrossguardTeamHubTravel::HubServer);
			// "dev-teamreturn-a1" travels as ?DevPlayerId=teamreturn-a1.
			C.Test.TestEqual(FString(Id) + TEXT(": <hub server>?Party=<key>?DevPlayerId=<name>"), Travel->Url,
				Expected + TEXT("?DevPlayerId=") + FString(Id).RightChop(4));
		}
		C.Test.TestTrue(TEXT("Both are leaving for the hub"), Mode->IsLeavingForHub(TeamReturnFirstId) && Mode->IsLeavingForHub(TeamReturnSecondId));
		C.Test.TestFalse(TEXT("A2 still not sent to the menu"), Mode->WasSentToMenu(TeamReturnSecondId));
		C.Test.TestTrue(TEXT("Log: team A's return planned"), C.LogContains(FString::Printf(TEXT("Team %d return to the hub planned"), TeamA)));

		// Team B plays on.
		const ACrossguardCharacter* Local = C.LocalPlayer();
		C.Test.TestTrue(TEXT("The PIE player is still in the raid"), Local != nullptr && C.PlayerState(Local) != nullptr && C.PlayerState(Local)->IsInRaid());
		C.Test.TestFalse(TEXT("No team B countdown"), Mode->IsTeamHubReturnPending(static_cast<int32>(C.Number(TEXT("TeamB")))));
		C.Test.TestFalse(TEXT("No whole-server return"), Mode->IsHubReturnPending() || Mode->HasHubReturnStarted());
		C.Test.TestFalse(TEXT("Team B's HUD shows no countdown (team A's only)"), TeamReturnHudLine(C).StartsWith(TEXT("Returning to the hub")));

		// Leaving is a deliberate travel: no hold for a reconnect when their connections close.
		for (const TCHAR* Name : {TEXT("A1.Controller"), TEXT("A2.Controller")})
		{
			if (APlayerController* Controller = Cast<APlayerController>(C.FindActor(Name)))
			{
				C.Test.TestFalse(FString::Printf(TEXT("%s is not held for a reconnect"), Name), Mode->HoldForReconnect(Controller));
			}
		}
		if (APlayerController* SecondController = Cast<APlayerController>(C.FindActor(TEXT("A2.Controller"))))
		{
			// What the net driver does when the connection closes.
			SecondController->Destroy();
		}
	});

	Scenario.Wait(0.3f);

	Scenario.Step(TEXT("A2 gone: not held; then team B (the last) extracts"), [](FScenarioContext& C)
	{
		ACrossguardGameMode* Mode = C.GameMode();
		ACrossguardCharacter* Local = C.LocalPlayer();
		if (!C.Require(Mode != nullptr && Local != nullptr, TEXT("the game mode and the PIE player")))
		{
			return;
		}
		C.Test.TestNull(TEXT("A2 left for the hub: not held"), Mode->FindHeldPlayer(TeamReturnSecondId));
		C.Test.TestTrue(TEXT("A2 cannot log in to this raid again"), Mode->HasFinishedRaidHere(TeamReturnSecondId));
		Mode->HandleExtraction(Local);
		const int32 TeamB = static_cast<int32>(C.Number(TEXT("TeamB")));
		C.Test.TestTrue(TEXT("The last team leaves as a team too"), Mode->IsTeamHubReturnPending(TeamB));
		C.Test.TestFalse(TEXT("No whole-server return with a hub server"), Mode->IsHubReturnPending() || Mode->HasHubReturnStarted());
	});

	Scenario.WaitUntil(TEXT("team B's plan is made"), TeamReturnTestCountdown + 3.0f, [](FScenarioContext& C)
	{
		const ACrossguardGameMode* Mode = C.GameMode();
		return Mode != nullptr && Mode->GetTeamHubReturnPlan(static_cast<int32>(C.Number(TEXT("TeamB")))) != nullptr;
	});

	Scenario.Step(TEXT("Team B travels to the hub server; the server never travels"), [](FScenarioContext& C)
	{
		ACrossguardGameMode* Mode = C.GameMode();
		const TArray<FCrossguardTeamHubTravel>* Plan = Mode != nullptr ? Mode->GetTeamHubReturnPlan(static_cast<int32>(C.Number(TEXT("TeamB")))) : nullptr;
		if (!C.Require(Plan != nullptr && Plan->Num() == 1, TEXT("a plan for the PIE player")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("To the hub server"), (*Plan)[0].Mode == ECrossguardTeamHubTravel::HubServer
			&& (*Plan)[0].Url.StartsWith(TeamReturnHubServer));
		C.Test.TestFalse(TEXT("No whole-server return"), Mode->HasHubReturnStarted());
		Mode->HubServerAddress.Reset();
	});

	Scenario.Run();
	return true;
}

// DEV-ONLY (listen server): development mode (PIE), no hub server: a finished team (A) does not leave
// early but waits; once the last team (B, the PIE player) is done, the whole server's countdown runs
// and its return is a server travel to the hub map.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioTeamReturnDevWholeServer, "Crossguard.Scenario.HubReturn.DevWholeServerFallback", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioTeamReturnDevWholeServer::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioTeamReturn;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("Development mode, no hub server; team A extracts while B raids"), [](FScenarioContext& C)
	{
		if (!PrepareTeamReturn(C, FString()))
		{
			return;
		}
		ACrossguardGameMode* Mode = C.GameMode();
		if (Mode->GetHubMapName().IsEmpty())
		{
			C.Skip(TEXT("The game mode has no Hub Map Name: no development whole-server return to test"));
			return;
		}
		if (!C.Require(Crossguard::IsDevListenMode(C.World()), TEXT("PIE is development listen mode")) || !SpawnTeamA(C))
		{
			return;
		}
		const FCrossguardHubDestination Destination = Mode->GetHubDestination(nullptr);
		C.Test.TestTrue(TEXT("Destination: the development whole-server return to the hub map"),
			Destination.Kind == ECrossguardHubDestination::DevServerTravel && Destination.Address == Mode->GetHubMapName());
		Mode->HandleExtraction(C.Get<ACrossguardCharacter>(TEXT("A1")));
		Mode->HandleExtraction(C.Get<ACrossguardCharacter>(TEXT("A2")));
		const int32 TeamA = static_cast<int32>(C.Number(TEXT("TeamA")));
		C.Test.TestFalse(TEXT("Team A does not leave early: it waits for the whole server"), Mode->IsTeamHubReturnPending(TeamA));
		C.Test.TestTrue(TEXT("Log: waiting for the other teams"), C.LogContains(TEXT("waiting for the other teams (development listen server)")));
		C.Test.TestFalse(TEXT("No whole-server countdown while B raids"), Mode->IsHubReturnPending());
		C.Test.TestFalse(TEXT("Not leaving"), Mode->IsLeavingForHub(TeamReturnFirstId));
	});

	Scenario.Wait(TeamReturnTestCountdown + 0.5f);

	Scenario.Step(TEXT("Still no plan for A; B extracts: the whole server counts down"), [](FScenarioContext& C)
	{
		ACrossguardGameMode* Mode = C.GameMode();
		ACrossguardCharacter* Local = C.LocalPlayer();
		if (!C.Require(Mode != nullptr && Local != nullptr, TEXT("the game mode and the PIE player")))
		{
			return;
		}
		C.Test.TestNull(TEXT("No team plan for A"), Mode->GetTeamHubReturnPlan(static_cast<int32>(C.Number(TEXT("TeamA")))));
		Mode->HandleExtraction(Local);
		C.Test.TestTrue(TEXT("The whole server's countdown runs"), Mode->IsHubReturnPending());
		C.Test.TestFalse(TEXT("No team countdown for B"), Mode->IsTeamHubReturnPending(static_cast<int32>(C.Number(TEXT("TeamB")))));
	});

	Scenario.WaitUntil(TEXT("the whole server's return starts"), TeamReturnTestCountdown + 3.0f, [](FScenarioContext& C)
	{
		return C.GameMode() != nullptr && C.GameMode()->HasHubReturnStarted();
	});

	Scenario.Step(TEXT("The whole server travels to the hub map"), [](FScenarioContext& C)
	{
		const ACrossguardGameMode* Mode = C.GameMode();
		if (!C.Require(Mode != nullptr, TEXT("the game mode")))
		{
			return;
		}
		C.Test.TestEqual(TEXT("A server travel to the hub map"), Mode->GetHubReturnUrl(),
			ACrossguardGameMode::BuildHubReturnUrl(Mode->GetHubMapName(), Mode->GetNetMode() == NM_ListenServer));
		C.Test.TestTrue(TEXT("Log: the return was planned"), C.LogContains(TEXT("Return to the hub planned")));
	});

	Scenario.Run();
	return true;
}

// Not development mode (forced: as a dedicated or Shipping server) and no hub server: a solo raider
// dies for the raid while the PIE player raids; their team of one counts down and its plan is the main
// menu (with the NoHub message), never a whole-server travel. The destination order is checked too.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioTeamReturnNoHub, "Crossguard.Scenario.HubReturn.NoHubServerToMenu", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioTeamReturnNoHub::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioTeamReturn;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("Destination order; then dedicated mode, a solo raider dies"), [](FScenarioContext& C)
	{
		if (!PrepareTeamReturn(C, TeamReturnHubServer))
		{
			return;
		}
		ACrossguardGameMode* Mode = C.GameMode();
		Crossguard::SetDevListenModeOverrideForTests(C.World(), false);
		C.Test.TestFalse(TEXT("Forced: not development mode"), Crossguard::IsDevListenMode(C.World()));
		C.Test.TestTrue(TEXT("A hub server wins"), Mode->GetHubDestination(nullptr).Kind == ECrossguardHubDestination::HubServer);
		Mode->HubServerAddress.Reset();
		C.Test.TestTrue(TEXT("No hub server, not development: the menu"), Mode->GetHubDestination(nullptr).Kind == ECrossguardHubDestination::Menu);
		Crossguard::SetDevListenModeOverrideForTests(C.World(), true);
		C.Test.TestEqual(TEXT("Development mode: the whole-server return (or the menu without a hub map)"),
			static_cast<int32>(Mode->GetHubDestination(nullptr).Kind),
			static_cast<int32>(Mode->GetHubMapName().IsEmpty() ? ECrossguardHubDestination::Menu : ECrossguardHubDestination::DevServerTravel));
		Crossguard::SetDevListenModeOverrideForTests(C.World(), false);

		ACrossguardCharacter* Solo = C.SpawnPlayer(TEXT("Solo"), FVector(0.0f, 1000.0f, 0.0f));
		if (!C.Require(Solo != nullptr && C.PlayerState(Solo) != nullptr, TEXT("the solo raider spawned")))
		{
			return;
		}
		ACrossguardPlayerState* SoloState = C.PlayerState(Solo);
		SoloState->DevPlayerId = TEXT("dev-teamreturn-solo");
		C.SetNumber(TEXT("TeamSolo"), SoloState->GetTeamId());
		Mode->HandlePermanentDeath(Solo);
		C.Test.TestFalse(TEXT("Ruled: not sent to the menu on their own"), Mode->WasSentToMenu(TEXT("dev-teamreturn-solo")));
		C.Test.TestTrue(TEXT("The team of one counts down"), Mode->IsTeamHubReturnPending(SoloState->GetTeamId()));
	});

	Scenario.WaitUntil(TEXT("the solo plan is made"), TeamReturnTestCountdown + 3.0f, [](FScenarioContext& C)
	{
		const ACrossguardGameMode* Mode = C.GameMode();
		return Mode != nullptr && Mode->GetTeamHubReturnPlan(static_cast<int32>(C.Number(TEXT("TeamSolo")))) != nullptr;
	});

	Scenario.Step(TEXT("Nowhere to go: the main menu"), [](FScenarioContext& C)
	{
		const ACrossguardGameMode* Mode = C.GameMode();
		const TArray<FCrossguardTeamHubTravel>* Plan = Mode != nullptr ? Mode->GetTeamHubReturnPlan(static_cast<int32>(C.Number(TEXT("TeamSolo")))) : nullptr;
		Crossguard::SetDevListenModeOverrideForTests(C.World(), TOptional<bool>());
		if (!C.Require(Plan != nullptr && Plan->Num() == 1, TEXT("a plan for one")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("To the menu"), (*Plan)[0].Mode == ECrossguardTeamHubTravel::Menu && (*Plan)[0].Url.IsEmpty());
		C.Test.TestTrue(TEXT("Log: no hub server"), C.LogContains(TEXT("goes to the menu (no hub server)")));
		C.Test.TestFalse(TEXT("No whole-server return"), Mode->IsHubReturnPending() || Mode->HasHubReturnStarted());
		C.Test.TestFalse(TEXT("The menu says why"), ACrossguardMenuGameMode::ReasonMessage(ACrossguardMenuGameMode::NoHubReason).IsEmpty());
	});

	Scenario.Run();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
