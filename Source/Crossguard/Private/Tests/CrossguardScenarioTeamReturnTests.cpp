// Scenario tests: a team that has finished the raid goes back to the hub early while other teams
// still raid (docs/hub-and-menu.md, "A team goes back early"). The members' travel is planned only
// here (ACrossguardGameMode::bHubReturnPlanOnly): the plan is read back per member. The countdown is
// shortened for the run (placeholder: 10 s). Scenario players have no connection, so the leader's
// address is ACrossguardGameMode::TeamHubTestLeaderAddress.
// Framework: CrossguardScenario.h, docs/editor-tests.md.

#include "Tests/CrossguardScenario.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Camera/PlayerCameraManager.h"
#include "Character/CrossguardCharacter.h"
#include "Game/CrossguardGameMode.h"
#include "Game/CrossguardPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "UI/CrossguardHUD.h"
#include "UI/CrossguardHudWidget.h"

namespace CrossguardScenarioTeamReturn
{
	using CrossguardScenario::FScenarioContext;

	constexpr float TeamReturnTestCountdown = 1.0f;
	const TCHAR* const TeamReturnLeaderAddress = TEXT("198.51.100.7");
	const TCHAR* const TeamReturnHubServer = TEXT("203.0.113.9:7777");
	const TCHAR* const TeamReturnPartyA = TEXT("teamreturnA");
	// Development ids, set straight on the player states ("dev-<name>": the URL carries ?DevPlayerId=<name>).
	const TCHAR* const TeamReturnFirstId = TEXT("dev-teamreturn-a1");
	const TCHAR* const TeamReturnSecondId = TEXT("dev-teamreturn-a2");

	// Short countdown, plan only, a stand-in leader address. False (scenario over) without a game mode
	// or a hub map.
	bool PrepareTeamReturn(FScenarioContext& C)
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
		Mode->HubReturnCountdownSeconds = TeamReturnTestCountdown;
		Mode->bHubReturnPlanOnly = true;
		Mode->TeamHubTestLeaderAddress = TeamReturnLeaderAddress;
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

// Two teams: A (two raiders) and B (the PIE player). A2 dies for the raid and watches A1; A1 extracts:
// A2 is not sent to the menu but watches its body, and team A counts down alone (B unaffected). At its
// end A's plan: the leader (A1) hosts the hub locally with ?Party=<key>, A2 joins the leader's address
// after the join delay; both are leaving and A2's disconnect is no hold for a reconnect. Then B (the
// last team) extracts: the whole server returns as before.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioTeamReturnEarly, "Crossguard.Scenario.HubReturn.TeamFinishesEarly", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioTeamReturnEarly::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioTeamReturn;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("Team A dies and extracts while team B (the PIE player) raids"), [](FScenarioContext& C)
	{
		if (!PrepareTeamReturn(C) || !SpawnTeamA(C))
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
		C.Test.TestFalse(TEXT("No whole-server return while B raids"), Mode->IsHubReturnPending() || Mode->HasHubReturnStarted());
		C.Test.TestTrue(TEXT("Log: team A returns while others raid"), C.LogContains(TEXT("returning to the hub in 1 s while others raid")));
	});

	Scenario.WaitUntil(TEXT("team A's countdown ends and its plan is made"), TeamReturnTestCountdown + 3.0f, [](FScenarioContext& C)
	{
		const ACrossguardGameMode* Mode = C.GameMode();
		return Mode != nullptr && Mode->GetTeamHubReturnPlan(static_cast<int32>(C.Number(TEXT("TeamA")))) != nullptr;
	});

	Scenario.Step(TEXT("Team A's plan: the leader hosts, A2 joins the leader; B unaffected"), [](FScenarioContext& C)
	{
		ACrossguardGameMode* Mode = C.GameMode();
		const int32 TeamA = static_cast<int32>(C.Number(TEXT("TeamA")));
		const TArray<FCrossguardTeamHubTravel>* Plan = Mode != nullptr ? Mode->GetTeamHubReturnPlan(TeamA) : nullptr;
		if (!C.Require(Plan != nullptr, TEXT("team A's plan")))
		{
			return;
		}
		C.Test.TestEqual(TEXT("Two members go"), Plan->Num(), 2);
		const FCrossguardTeamHubTravel* Leader = TeamReturnEntry(*Plan, TeamReturnFirstId);
		const FCrossguardTeamHubTravel* Member = TeamReturnEntry(*Plan, TeamReturnSecondId);
		if (!C.Require(Leader != nullptr && Member != nullptr, TEXT("both members are in the plan")))
		{
			return;
		}
		C.Test.AddInfo(FString::Printf(TEXT("Leader: %s; member: %s in %.1f s"), *Leader->Url, *Member->Url, Member->DelaySeconds));
		C.Test.TestTrue(TEXT("A1 (first to join, no party known here) leads"), Leader->bLeader && !Member->bLeader);
		C.Test.TestTrue(TEXT("The leader hosts the hub on its own machine"), Leader->Mode == ECrossguardTeamHubTravel::HostHub);
		const FString HostUrl = ACrossguardGameMode::BuildTeamHubHostUrl(Mode->GetHubMapName(), TeamReturnPartyA);
		C.Test.TestTrue(TEXT("Host URL: <hub map>?listen?Hub?Party=<key>"), !HostUrl.IsEmpty() && Leader->Url.StartsWith(HostUrl));
		C.Test.TestTrue(TEXT("Host URL: listen, hub and party"), Leader->Url.Contains(TEXT("?listen")) && Leader->Url.Contains(TEXT("?Hub"))
			&& Leader->Url.Contains(FString::Printf(TEXT("?Party=%s"), TeamReturnPartyA)));
		C.Test.TestTrue(TEXT("Host URL carries the development id"), Leader->Url.EndsWith(TEXT("?DevPlayerId=teamreturn-a1")));
		C.Test.TestEqual(TEXT("The leader goes at once"), Leader->DelaySeconds, 0.0f);
		C.Test.TestTrue(TEXT("A2 joins the leader's hub"), Member->Mode == ECrossguardTeamHubTravel::JoinLeader);
		C.Test.TestTrue(TEXT("Join URL: <leader address>:<port>?Party=<key>"),
			Member->Url.StartsWith(FString::Printf(TEXT("%s:%d?Party=%s"), TeamReturnLeaderAddress, Mode->TeamHubListenPort, TeamReturnPartyA)));
		C.Test.TestEqual(TEXT("A2 waits for the leader's hub (join delay)"), Member->DelaySeconds, Mode->TeamHubJoinDelaySeconds);
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

	Scenario.Step(TEXT("A2 gone: not held; then team B extracts"), [](FScenarioContext& C)
	{
		ACrossguardGameMode* Mode = C.GameMode();
		ACrossguardCharacter* Local = C.LocalPlayer();
		if (!C.Require(Mode != nullptr && Local != nullptr, TEXT("the game mode and the PIE player")))
		{
			return;
		}
		C.Test.TestNull(TEXT("A2 left for the hub: not held"), Mode->FindHeldPlayer(TeamReturnSecondId));
		C.Test.TestTrue(TEXT("A2 cannot log in to this raid again"), Mode->HasFinishedRaidHere(TeamReturnSecondId));
		C.Test.TestFalse(TEXT("No whole-server return yet"), Mode->IsHubReturnPending());
		Mode->HandleExtraction(Local);
		C.Test.TestTrue(TEXT("The last team is done: the whole server's countdown runs"), Mode->IsHubReturnPending());
		C.Test.TestFalse(TEXT("No team countdown for B (the whole server goes)"), Mode->IsTeamHubReturnPending(static_cast<int32>(C.Number(TEXT("TeamB")))));
	});

	Scenario.WaitUntil(TEXT("the whole server's return starts"), TeamReturnTestCountdown + 3.0f, [](FScenarioContext& C)
	{
		return C.GameMode() != nullptr && C.GameMode()->HasHubReturnStarted();
	});

	Scenario.Step(TEXT("The whole server travels to the hub as before"), [](FScenarioContext& C)
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

// A dedicated hub server is set (HubServerAddress): both members of a finished team travel straight to
// it with ?Party=<key>, at once; nobody hosts.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioTeamReturnHubServer, "Crossguard.Scenario.HubReturn.TeamToHubServer", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioTeamReturnHubServer::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioTeamReturn;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A hub server is set; team A extracts while the PIE player raids"), [](FScenarioContext& C)
	{
		if (!PrepareTeamReturn(C))
		{
			return;
		}
		ACrossguardGameMode* Mode = C.GameMode();
		if (Mode->GetHubServerAddress() != Mode->HubServerAddress)
		{
			C.Skip(TEXT("-HubServer= is on the command line: the test's address would be ignored"));
			return;
		}
		Mode->HubServerAddress = TeamReturnHubServer;
		if (!SpawnTeamA(C))
		{
			return;
		}
		Mode->HandleExtraction(C.Get<ACrossguardCharacter>(TEXT("A1")));
		Mode->HandleExtraction(C.Get<ACrossguardCharacter>(TEXT("A2")));
		C.Test.TestTrue(TEXT("Team A's countdown runs"), Mode->IsTeamHubReturnPending(static_cast<int32>(C.Number(TEXT("TeamA")))));
	});

	Scenario.WaitUntil(TEXT("team A's plan is made"), TeamReturnTestCountdown + 3.0f, [](FScenarioContext& C)
	{
		const ACrossguardGameMode* Mode = C.GameMode();
		return Mode != nullptr && Mode->GetTeamHubReturnPlan(static_cast<int32>(C.Number(TEXT("TeamA")))) != nullptr;
	});

	Scenario.Step(TEXT("Both travel to the hub server with the party key"), [](FScenarioContext& C)
	{
		ACrossguardGameMode* Mode = C.GameMode();
		const TArray<FCrossguardTeamHubTravel>* Plan = Mode != nullptr ? Mode->GetTeamHubReturnPlan(static_cast<int32>(C.Number(TEXT("TeamA")))) : nullptr;
		if (!C.Require(Plan != nullptr && Plan->Num() == 2, TEXT("a plan for both members")))
		{
			return;
		}
		const FString Expected = ACrossguardGameMode::BuildTeamHubJoinUrl(TeamReturnHubServer, TeamReturnPartyA);
		for (const FCrossguardTeamHubTravel& Travel : *Plan)
		{
			C.Test.AddInfo(FString::Printf(TEXT("%s: %s"), *Travel.PlayerId, *Travel.Url));
			C.Test.TestTrue(Travel.PlayerId + TEXT(" travels to the hub server"), Travel.Mode == ECrossguardTeamHubTravel::HubServer);
			C.Test.TestTrue(Travel.PlayerId + TEXT(": <hub server>?Party=<key>"), !Expected.IsEmpty() && Travel.Url.StartsWith(Expected));
			C.Test.TestEqual(Travel.PlayerId + TEXT(" goes at once"), Travel.DelaySeconds, 0.0f);
		}
		C.Test.TestTrue(TEXT("The PIE player still raids"), C.LocalPlayer() != nullptr && C.PlayerState(C.LocalPlayer()) != nullptr
			&& C.PlayerState(C.LocalPlayer())->IsInRaid());
		Mode->HubServerAddress.Reset();
	});

	Scenario.Run();
	return true;
}

// A solo raider dies for the raid while the PIE player raids: ruled, no trip to the menu; their team
// of one counts down and its plan opens the hub on their own machine (no party key).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioTeamReturnSolo, "Crossguard.Scenario.HubReturn.SoloFinisherHostsHub", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioTeamReturnSolo::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioTeamReturn;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A solo raider dies for the raid"), [](FScenarioContext& C)
	{
		if (!PrepareTeamReturn(C))
		{
			return;
		}
		ACrossguardGameMode* Mode = C.GameMode();
		ACrossguardCharacter* Solo = C.SpawnPlayer(TEXT("Solo"), FVector(0.0f, 1000.0f, 0.0f));
		if (!C.Require(Solo != nullptr && C.PlayerState(Solo) != nullptr, TEXT("the solo raider spawned")))
		{
			return;
		}
		ACrossguardPlayerState* SoloState = C.PlayerState(Solo);
		SoloState->DevPlayerId = TEXT("dev-teamreturn-solo");
		C.SetNumber(TEXT("TeamSolo"), SoloState->GetTeamId());
		Mode->HandlePermanentDeath(Solo);
		C.Test.TestFalse(TEXT("Ruled: not sent to the menu"), Mode->WasSentToMenu(TEXT("dev-teamreturn-solo")));
		C.Test.TestTrue(TEXT("The team of one counts down"), Mode->IsTeamHubReturnPending(SoloState->GetTeamId()));
	});

	Scenario.WaitUntil(TEXT("the solo plan is made"), TeamReturnTestCountdown + 3.0f, [](FScenarioContext& C)
	{
		const ACrossguardGameMode* Mode = C.GameMode();
		return Mode != nullptr && Mode->GetTeamHubReturnPlan(static_cast<int32>(C.Number(TEXT("TeamSolo")))) != nullptr;
	});

	Scenario.Step(TEXT("The solo raider hosts the hub locally"), [](FScenarioContext& C)
	{
		const ACrossguardGameMode* Mode = C.GameMode();
		const TArray<FCrossguardTeamHubTravel>* Plan = Mode != nullptr ? Mode->GetTeamHubReturnPlan(static_cast<int32>(C.Number(TEXT("TeamSolo")))) : nullptr;
		if (!C.Require(Plan != nullptr && Plan->Num() == 1, TEXT("a plan for one")))
		{
			return;
		}
		const FCrossguardTeamHubTravel& Travel = (*Plan)[0];
		C.Test.AddInfo(FString::Printf(TEXT("Solo: %s"), *Travel.Url));
		C.Test.TestTrue(TEXT("Leader of a team of one"), Travel.bLeader);
		C.Test.TestTrue(TEXT("Hosts the hub on its own machine"), Travel.Mode == ECrossguardTeamHubTravel::HostHub);
		C.Test.TestEqual(TEXT("<hub map>?listen?Hub, no party key"), Travel.Url,
			ACrossguardGameMode::BuildTeamHubHostUrl(Mode->GetHubMapName(), FString()) + TEXT("?DevPlayerId=teamreturn-solo"));
		C.Test.TestFalse(TEXT("No ?Party="), Travel.Url.Contains(TEXT("?Party=")));
		C.Test.TestFalse(TEXT("No whole-server return: the PIE player raids"), Mode->IsHubReturnPending() || Mode->HasHubReturnStarted());
	});

	Scenario.Run();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
