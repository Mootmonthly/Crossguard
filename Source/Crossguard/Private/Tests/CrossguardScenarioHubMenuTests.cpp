// Scenario tests: the main menu, the hub marker and the hub's raid portal / party board
// (docs/hub-and-menu.md). No travel is ever started here: the menu's URLs are read back (Play and
// Join are not clicked), the hub marker's decision is checked through ACrossguardGameMode::IsHubMap
// (InitGame cannot run again in a running world), and the portal plans the raid launch
// (bPlanOnly: UCrossguardPartySubsystem::PlanRaidTravel) instead of travelling.
// Framework: CrossguardScenario.h, docs/editor-tests.md.

#include "Tests/CrossguardScenario.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Blueprint/UserWidget.h"
#include "Character/CrossguardCharacter.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Game/CrossguardConnectionErrorSubsystem.h"
#include "Game/CrossguardGameMode.h"
#include "Game/CrossguardHubMarker.h"
#include "Game/CrossguardMenuGameMode.h"
#include "Game/CrossguardPartyComponent.h"
#include "Game/CrossguardPartySubsystem.h"
#include "Game/CrossguardPlayerState.h"
#include "Game/CrossguardSettingsSave.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/CrossguardPartyBoard.h"
#include "Interaction/CrossguardRaidPortal.h"
#include "UI/CrossguardHUD.h"
#include "UI/CrossguardMainMenuWidget.h"
#include "UI/CrossguardPartyWidget.h"
#include "UI/CrossguardUIButton.h"

namespace CrossguardScenarioHubMenu
{
	// The scene row (Y), apart from the other files' rows.
	constexpr float HubMenuRow = 14000.0f;

	FString HubMenuResultName(ECrossguardPartyResult Value)
	{
		return StaticEnum<ECrossguardPartyResult>()->GetNameStringByValue(static_cast<int64>(Value));
	}

	FString HubMenuTravelName(ECrossguardRaidTravel Value)
	{
		return StaticEnum<ECrossguardRaidTravel>()->GetNameStringByValue(static_cast<int64>(Value));
	}

	UCrossguardPartySubsystem* HubMenuParties(CrossguardScenario::FScenarioContext& C)
	{
		UGameInstance* GameInstance = C.World() != nullptr ? C.World()->GetGameInstance() : nullptr;
		UCrossguardPartySubsystem* Subsystem = GameInstance != nullptr ? GameInstance->GetSubsystem<UCrossguardPartySubsystem>() : nullptr;
		C.Require(Subsystem != nullptr, TEXT("the server game instance has a UCrossguardPartySubsystem"));
		return Subsystem;
	}
}

// The menu's URL helpers: what Play, Join and the trip back to the menu open.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardMenuUrls, "Crossguard.Menu.Urls", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCrossguardMenuUrls::RunTest(const FString& Parameters)
{
	const FString HostUrl = ACrossguardMenuGameMode::BuildHostUrl(TEXT("/Game/Maps/L_Hub"));
	TestEqual(TEXT("Play: the hub as a listen server with ?Hub"), HostUrl, FString(TEXT("/Game/Maps/L_Hub?listen?Hub")));
	const FURL Parsed(nullptr, *HostUrl, TRAVEL_Absolute);
	TestEqual(TEXT("Play URL map"), Parsed.Map, FString(TEXT("/Game/Maps/L_Hub")));
	TestTrue(TEXT("Play URL listens"), Parsed.HasOption(TEXT("listen")));
	TestTrue(TEXT("Play URL is a hub"), Parsed.HasOption(ACrossguardGameMode::HubOption));
	TestTrue(TEXT("Play without a map: nothing"), ACrossguardMenuGameMode::BuildHostUrl(TEXT("  ")).IsEmpty());

	TestEqual(TEXT("Join: trimmed address"), ACrossguardMenuGameMode::BuildJoinUrl(TEXT("  192.168.1.20:7777 ")), FString(TEXT("192.168.1.20:7777")));
	TestEqual(TEXT("Join: host name"), ACrossguardMenuGameMode::BuildJoinUrl(TEXT("host.example")), FString(TEXT("host.example")));
	TestTrue(TEXT("Join: empty refused"), ACrossguardMenuGameMode::BuildJoinUrl(TEXT("   ")).IsEmpty());
	TestTrue(TEXT("Join: spaces refused"), ACrossguardMenuGameMode::BuildJoinUrl(TEXT("1.2.3.4 5")).IsEmpty());
	TestTrue(TEXT("Join: options refused"), ACrossguardMenuGameMode::BuildJoinUrl(TEXT("1.2.3.4?Party=x")).IsEmpty());
	TestTrue(TEXT("Join: map paths refused"), ACrossguardMenuGameMode::BuildJoinUrl(TEXT("/Game/Maps/L_Hub")).IsEmpty());

	TestEqual(TEXT("Back to the menu with a reason"), ACrossguardMenuGameMode::BuildMenuUrl(TEXT("/Game/Maps/L_MainMenu"), ACrossguardMenuGameMode::RaidOverReason),
		FString(TEXT("/Game/Maps/L_MainMenu?MenuReason=RaidOver")));
	TestEqual(TEXT("Reason keys keep letters and digits only"), ACrossguardMenuGameMode::BuildMenuUrl(TEXT("/Game/M"), TEXT("Raid Over?x=1")),
		FString(TEXT("/Game/M?MenuReason=RaidOverx1")));
	TestEqual(TEXT("No reason: the map alone"), ACrossguardMenuGameMode::BuildMenuUrl(TEXT("/Game/M"), FString()), FString(TEXT("/Game/M")));
	TestTrue(TEXT("No menu map: nothing"), ACrossguardMenuGameMode::BuildMenuUrl(FString(), TEXT("RaidOver")).IsEmpty());
	TestEqual(TEXT("Raid over message"), ACrossguardMenuGameMode::ReasonMessage(ACrossguardMenuGameMode::RaidOverReason), FString(TEXT("Your raid is over.")));
	TestTrue(TEXT("Unknown reason: no message"), ACrossguardMenuGameMode::ReasonMessage(TEXT("Nope")).IsEmpty());
	// A refused Join (the server's reason) is shown on the menu.
	TestEqual(TEXT("Connection failure line"), UCrossguardConnectionErrorSubsystem::MenuLine(TEXT("No match ticket: start the raid from the hub.")),
		FString(TEXT("Connection failed: No match ticket: start the raid from the hub.")));
	TestTrue(TEXT("No failure: no line"), UCrossguardConnectionErrorSubsystem::MenuLine(FString()).IsEmpty());
	return true;
}

// The main menu widget builds for the PIE player and its buttons are bound (Play / Join are read back,
// not clicked: they would travel).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioMainMenuBinds, "Crossguard.Scenario.Hub.MainMenuBinds", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioMainMenuBinds::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("The main menu builds, its buttons exist and Play / Join build the right URLs"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.LocalPlayer();
		APlayerController* Controller = Player != nullptr ? Cast<APlayerController>(Player->GetController()) : nullptr;
		if (!C.Require(Controller != nullptr && Controller->GetLocalPlayer() != nullptr, TEXT("the PIE player has a local player controller")))
		{
			return;
		}
		UCrossguardMainMenuWidget* Menu = CreateWidget<UCrossguardMainMenuWidget>(Controller, UCrossguardMainMenuWidget::StaticClass());
		if (!C.Require(Menu != nullptr, TEXT("the main menu widget is created")))
		{
			return;
		}
		Menu->TakeWidget();
		Menu->Refresh();
		C.Test.TestTrue(TEXT("The menu built its tree in C++"), Menu->IsBuiltInCode());
		const TArray<FString> Expected = { TEXT("Play"), TEXT("Join"), TEXT("Settings"), TEXT("Quit") };
		C.Test.TestEqual(TEXT("Main buttons, top to bottom"), FString::Join(Menu->GetMainButtonLabels(), TEXT(",")), FString::Join(Expected, TEXT(",")));
		for (const FString& Label : Expected)
		{
			C.Test.TestNotNull(*FString::Printf(TEXT("Button %s"), *Label), Menu->FindMainButton(Label));
		}

		// No menu game mode in the test world: Play uses the default hub map.
		C.Test.TestEqual(TEXT("Play URL"), Menu->GetPlayUrl(), ACrossguardMenuGameMode::BuildHostUrl(TEXT("/Game/Maps/L_Hub")));

		Menu->SetJoinAddress(TEXT(" 127.0.0.1:7777 "));
		C.Test.TestEqual(TEXT("Join URL from the field"), Menu->GetJoinUrl(), FString(TEXT("127.0.0.1:7777")));
		Menu->SetJoinAddress(FString());
		C.Test.TestTrue(TEXT("Empty field: no Join URL"), Menu->GetJoinUrl().IsEmpty());
		// Safe to click: with no address it refuses instead of travelling.
		Menu->FindMainButton(TEXT("Join"))->Click();
		C.Test.TestFalse(TEXT("Join with no address says so"), Menu->GetStatusLine().IsEmpty());

		// Settings: opens, steps clamp; not saved (the test leaves the machine's settings alone).
		C.Test.TestFalse(TEXT("Settings start closed"), Menu->IsSettingsOpen());
		Menu->FindMainButton(TEXT("Settings"))->Click();
		C.Test.TestTrue(TEXT("Settings open"), Menu->IsSettingsOpen());
		const UCrossguardSettingsSave* Settings = Menu->GetSettings();
		if (C.Require(Settings != nullptr, TEXT("the menu has settings")))
		{
			const float Before = Settings->FieldOfView;
			Menu->StepSetting(1, Before >= UCrossguardSettingsSave::MaxFieldOfView ? -1 : 1);
			C.Test.TestNotEqual(TEXT("A field of view step changes it"), Settings->FieldOfView, Before);
			for (int32 Step = 0; Step < 30; ++Step)
			{
				Menu->StepSetting(2, 1);
			}
			C.Test.TestEqual(TEXT("Volume clamps at 100%"), Settings->MasterVolume, 1.0f);
			C.Test.TestTrue(*FString::Printf(TEXT("Settings line (%s)"), *Menu->GetSettingsLine()), Menu->GetSettingsLine().Contains(TEXT("100%")));
		}
		Menu->FindMainButton(TEXT("Settings"))->Click();
		C.Test.TestFalse(TEXT("Settings close"), Menu->IsSettingsOpen());
	});

	Scenario.Run();
	return true;
}

// A placed ACrossguardHubMarker makes the map a hub (bIsRaid off), as ?Hub does.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioHubMarker, "Crossguard.Scenario.Hub.HubMarker", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioHubMarker::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;

	FScenario Scenario(*this);
	Scenario.Raid(true);

	Scenario.Step(TEXT("Without a marker the map is a raid; with one it is a hub"), [](FScenarioContext& C)
	{
		ACrossguardGameMode* GameMode = C.GameMode();
		if (!C.Require(GameMode != nullptr, TEXT("a Crossguard game mode")))
		{
			return;
		}
		if (ACrossguardHubMarker::IsHubWorld(C.World()))
		{
			C.Skip(TEXT("the test map already holds a hub marker"));
			return;
		}
		C.Test.TestFalse(TEXT("No marker, no ?Hub: not a hub"), GameMode->IsHubMap(FString()));
		C.Test.TestTrue(TEXT("?Hub alone makes a hub"), GameMode->IsHubMap(TEXT("?Hub")));

		ACrossguardHubMarker* Marker = C.SpawnActor<ACrossguardHubMarker>(TEXT("HubMarker"), FVector(0.0f, CrossguardScenarioHubMenu::HubMenuRow, 0.0f));
		if (!C.Require(Marker != nullptr, TEXT("the marker spawned")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("The world holds a hub marker"), ACrossguardHubMarker::IsHubWorld(C.World()));
		C.Test.TestTrue(TEXT("With the marker the map is a hub"), GameMode->IsHubMap(FString()));
		// What InitGame does with it on a hub map.
		C.SetRaid(!GameMode->IsHubMap(FString()));
		C.Test.TestFalse(TEXT("bIsRaid is off"), GameMode->bIsRaid);

		Marker->Destroy();
		C.Test.TestFalse(TEXT("Marker gone: not a hub"), GameMode->IsHubMap(FString()));
		C.SetRaid(true);
	});

	Scenario.Run();
	return true;
}

// The raid portal launches for the leader (or a solo player) and refuses a member; the party board
// opens the party window.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioRaidPortal, "Crossguard.Scenario.Hub.RaidPortal", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioRaidPortal::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioHubMenu;

	FScenario Scenario(*this);
	Scenario.Raid(false);

	Scenario.Step(TEXT("Leader A with member B, Solo alone, a portal in plan-only mode"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = C.SpawnPlayer(TEXT("A"), FVector(0.0f, HubMenuRow, 0.0f));
		ACrossguardCharacter* B = C.SpawnPlayer(TEXT("B"), FVector(200.0f, HubMenuRow, 0.0f));
		ACrossguardCharacter* Solo = C.SpawnPlayer(TEXT("Solo"), FVector(400.0f, HubMenuRow, 0.0f));
		UCrossguardPartySubsystem* Parties = HubMenuParties(C);
		if (!C.Require(A != nullptr && B != nullptr && Solo != nullptr && Parties != nullptr && C.GameMode() != nullptr, TEXT("A, B and Solo spawned")))
		{
			return;
		}
		C.Require(Parties->Invite(C.PlayerState(A), C.PlayerState(B)) == ECrossguardPartyResult::None, TEXT("A invites B"));
		C.Require(Parties->Accept(C.PlayerState(B), C.PlayerState(A)->GetParty()->GetPartyId()) == ECrossguardPartyResult::None, TEXT("B accepts"));
		// Others are on the server (Solo, the PIE player): the plan is a client travel to the raid server.
		C.GameMode()->RaidMapName = TEXT("/Game/Maps/Dev/L_DevSandbox");
		C.GameMode()->RaidServerAddress = TEXT("127.0.0.1:7777");
		C.SpawnActor<ACrossguardRaidPortal>(TEXT("Portal"), FVector(200.0f, HubMenuRow + 300.0f, 0.0f), 0.0f,
			[](ACrossguardRaidPortal& Portal) { Portal.bPlanOnly = true; });
	});

	Scenario.Step(TEXT("The member is refused, the leader and Solo would launch"), [](FScenarioContext& C)
	{
		ACrossguardRaidPortal* Portal = C.Get<ACrossguardRaidPortal>(TEXT("Portal"));
		ACrossguardCharacter* A = C.Get<ACrossguardCharacter>(TEXT("A"));
		ACrossguardCharacter* B = C.Get<ACrossguardCharacter>(TEXT("B"));
		ACrossguardCharacter* Solo = C.Get<ACrossguardCharacter>(TEXT("Solo"));
		if (!C.Require(Portal != nullptr && A != nullptr && B != nullptr && Solo != nullptr, TEXT("portal and players")))
		{
			return;
		}
		C.Test.TestEqual(TEXT("Prompt"), Portal->GetInteractPrompt(), FString(TEXT("F: Enter the raid")));

		Portal->Interact(B);
		C.Test.TestEqual(TEXT("Member refused"), HubMenuResultName(Portal->GetLastResult()), HubMenuResultName(ECrossguardPartyResult::NotLeader));
		C.Test.TestTrue(TEXT("No URL for the member"), Portal->GetLastUrl().IsEmpty());
		C.Test.TestTrue(TEXT("The member's message names the leader"), ACrossguardRaidPortal::RefusalMessage(ECrossguardPartyResult::NotLeader).Contains(TEXT("leader")));

		const FString PartyId = C.PlayerState(A)->GetParty()->GetPartyId();
		Portal->Interact(A);
		C.Test.TestEqual(TEXT("Leader launches"), HubMenuResultName(Portal->GetLastResult()), HubMenuResultName(ECrossguardPartyResult::None));
		C.Test.TestEqual(TEXT("Leader: client travel"), HubMenuTravelName(Portal->GetLastMode()), HubMenuTravelName(ECrossguardRaidTravel::ClientTravel));
		C.Test.TestEqual(TEXT("Leader: URL with the party key"), Portal->GetLastUrl(),
			FString::Printf(TEXT("127.0.0.1:7777?%s=%s"), ACrossguardGameMode::PartyOption, *PartyId));

		Portal->Interact(Solo);
		C.Test.TestEqual(TEXT("Solo launches"), HubMenuResultName(Portal->GetLastResult()), HubMenuResultName(ECrossguardPartyResult::None));
		C.Test.TestEqual(TEXT("Solo: no party key"), Portal->GetLastUrl(), FString(TEXT("127.0.0.1:7777")));

		C.SetRaid(true);
		Portal->Interact(A);
		C.Test.TestEqual(TEXT("In a raid: refused"), HubMenuResultName(Portal->GetLastResult()), HubMenuResultName(ECrossguardPartyResult::OnlyInHub));
		C.SetRaid(false);
		// Leave nothing that could travel when PIE ends.
		C.GameMode()->RaidServerAddress.Reset();
		C.GameMode()->RaidMapName.Reset();
	});

	Scenario.Step(TEXT("The party board opens the PIE player's party window"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.LocalPlayer();
		APlayerController* Controller = Player != nullptr ? Cast<APlayerController>(Player->GetController()) : nullptr;
		ACrossguardPartyBoard* Board = C.SpawnActor<ACrossguardPartyBoard>(TEXT("Board"), FVector(-300.0f, HubMenuRow, 0.0f));
		if (!C.Require(Player != nullptr && Controller != nullptr && Board != nullptr, TEXT("PIE player and board")))
		{
			return;
		}
		C.Test.TestEqual(TEXT("Board prompt"), Board->GetInteractPrompt(), FString(TEXT("F: Party board")));
		ACrossguardHUD* Hud = Cast<ACrossguardHUD>(Controller->GetHUD());
		if (Hud == nullptr || Hud->GetPartyWidget() == nullptr)
		{
			// Headless (-nullrhi): the HUD makes no widgets; the board's request is still sent.
			C.Test.AddInfo(TEXT("No HUD party window (no game viewport): only the call is checked"));
			Board->Interact(Player);
			return;
		}
		Hud->CloseAllWindows();
		Board->Interact(Player);
		C.Test.TestTrue(TEXT("The party window is open"), Hud->IsWindowOpen(Hud->GetPartyWidget()));
		Hud->CloseAllWindows();
	});

	Scenario.Run();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
