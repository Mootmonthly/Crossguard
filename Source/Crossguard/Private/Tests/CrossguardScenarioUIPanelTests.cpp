// UI tests for the attributes (K) and party (P) windows, the team list and readable rarity colours
// (docs/ui.md). Like Crossguard.Scenario.UI.WidgetsBind: nothing is drawn under -nullrhi, so this
// builds each widget for the PIE player, changes the game state, and reads back what it would show
// and which buttons it would grey out, driving the same functions the buttons call.
// Framework: CrossguardScenario.h, docs/editor-tests.md.

#include "Tests/CrossguardScenario.h"
#include "Tests/CrossguardTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Blueprint/UserWidget.h"
#include "Character/CrossguardCharacter.h"
#include "Engine/GameInstance.h"
#include "Game/CrossguardPartyComponent.h"
#include "Game/CrossguardPartySubsystem.h"
#include "Game/CrossguardPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "UI/CrossguardAttributesWidget.h"
#include "UI/CrossguardPartyWidget.h"
#include "UI/CrossguardTeamListWidget.h"
#include "UI/CrossguardUIHelpers.h"

namespace CrossguardScenarioUIPanels
{
	// The scene row (Y), away from the other UI test's.
	constexpr float PanelRow = 5500.0f;
	// WCAG AA for text.
	constexpr float MinContrast = 4.5f;
	// UCrossguardUIButton's default background and hover lift, the window panel (CrossguardWidgetBase.cpp).
	constexpr float ButtonGrey = 0.12f;
	constexpr float HoverLift = 0.05f;
	constexpr float WindowPanel = 0.03f;

	APlayerController* PanelControllerOf(const ACrossguardCharacter* Character)
	{
		return Character != nullptr ? Cast<APlayerController>(Character->GetController()) : nullptr;
	}

	UCrossguardPartySubsystem* PanelPartiesOf(CrossguardScenario::FScenarioContext& C)
	{
		UGameInstance* GameInstance = C.World() != nullptr ? C.World()->GetGameInstance() : nullptr;
		UCrossguardPartySubsystem* Subsystem = GameInstance != nullptr ? GameInstance->GetSubsystem<UCrossguardPartySubsystem>() : nullptr;
		C.Require(Subsystem != nullptr, TEXT("the game instance has a UCrossguardPartySubsystem"));
		return Subsystem;
	}

	FLinearColor Lifted(const FLinearColor& Color, float Lift)
	{
		return FLinearColor(FMath::Min(Color.R + Lift, 1.0f), FMath::Min(Color.G + Lift, 1.0f), FMath::Min(Color.B + Lift, 1.0f), Color.A);
	}

	bool AnyLineContains(const TArray<FString>& Lines, const FString& Text)
	{
		for (const FString& Line : Lines)
		{
			if (Line.Contains(Text))
			{
				return true;
			}
		}
		return false;
	}
}

// Every rarity's label colour reads at WCAG AA (4.5:1) or better on its item background (also
// hovered) and on a window's panel; white labels on the default grey button too.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardUIRarityContrast, "Crossguard.UI.RarityContrast", CROSSGUARD_TEST_FLAGS)

bool FCrossguardUIRarityContrast::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenarioUIPanels;
	for (const ECrossguardRarity Rarity : { ECrossguardRarity::Common, ECrossguardRarity::Uncommon, ECrossguardRarity::Rare, ECrossguardRarity::Epic,
			 ECrossguardRarity::Legendary, ECrossguardRarity::Mystic })
	{
		const FString Name = CrossguardUI::UIEnumName(Rarity);
		const FLinearColor Text = CrossguardUI::RarityTextColor(Rarity);
		const FLinearColor Background = CrossguardUI::RarityBackgroundColor(Rarity);
		const float OnCell = CrossguardUI::ContrastRatio(Text, Background);
		const float OnHover = CrossguardUI::ContrastRatio(Text, Lifted(Background, HoverLift));
		const float OnPanel = CrossguardUI::ContrastRatio(Text, FLinearColor(WindowPanel, WindowPanel, WindowPanel));
		AddInfo(FString::Printf(TEXT("%s: text %s on %s: %.2f:1 (hovered %.2f:1, on a window %.2f:1)"), *Name, *Text.ToFColor(true).ToHex(),
			*Background.ToFColor(true).ToHex(), OnCell, OnHover, OnPanel));
		TestTrue(*FString::Printf(TEXT("%s label on its background >= 4.5:1 (%.2f)"), *Name, OnCell), OnCell >= MinContrast);
		TestTrue(*FString::Printf(TEXT("%s label on its hovered background >= 4.5:1 (%.2f)"), *Name, OnHover), OnHover >= MinContrast);
		TestTrue(*FString::Printf(TEXT("%s text on a window panel >= 4.5:1 (%.2f)"), *Name, OnPanel), OnPanel >= MinContrast);
	}
	const FLinearColor Grey(ButtonGrey, ButtonGrey, ButtonGrey);
	TestTrue(TEXT("White on the default button >= 4.5:1"), CrossguardUI::ContrastRatio(FLinearColor::White, Grey) >= MinContrast);
	TestTrue(TEXT("White on the hovered default button >= 4.5:1"), CrossguardUI::ContrastRatio(FLinearColor::White, Lifted(Grey, HoverLift)) >= MinContrast);
	return true;
}

// In the hub: the attributes window spends unspent points (Apply), greys the respec buttons until a
// token or 1000 gold is there, then respecs with each; the party window shows the inbox with its
// countdown, the party with the leader mark, "(disconnected)" for a held member, and the team list
// shows the same.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioUIPanelsBind, "Crossguard.Scenario.UI.PanelsBind", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioUIPanelsBind::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioUIPanels;

	FScenario Scenario(*this);
	Scenario.Raid(false);

	Scenario.Step(TEXT("Attributes window: Apply, then respec with a token and with gold"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.LocalPlayer();
		APlayerController* Controller = PanelControllerOf(Player);
		ACrossguardPlayerState* Progression = C.PlayerState(Player);
		if (!C.Require(Player != nullptr && Controller != nullptr && Progression != nullptr, TEXT("the PIE player with a controller and player state")))
		{
			return;
		}
		C.Place(Player, FVector(-100.0f, PanelRow, 0.0f), 0.0f);
		C.Test.TestFalse(TEXT("In the hub"), Progression->IsInRaid());
		// Level 2: 3 more points than the start (placeholder numbers: 30 + 3 per level).
		Progression->AwardXp(ECrossguardXpSource::Quest, 100);

		UCrossguardAttributesWidget* Attributes = CreateWidget<UCrossguardAttributesWidget>(Controller, UCrossguardAttributesWidget::StaticClass());
		if (!C.Require(Attributes != nullptr, TEXT("the attributes widget is created")))
		{
			return;
		}
		Attributes->TakeWidget();
		Attributes->OnWindowOpened();
		Attributes->Refresh();
		C.Test.TestTrue(TEXT("The attributes window built its tree in C++"), Attributes->IsBuiltInCode());
		const FCrossguardAttributeAllocation Start = Progression->GetAttributeAllocation();
		C.Test.AddInfo(FString::Printf(TEXT("Attributes: %s"), *Attributes->GetPointsLine()));
		C.Test.TestTrue(*FString::Printf(TEXT("The points line shows unspent points (%s)"), *Attributes->GetPointsLine()),
			Attributes->GetPointsLine().Contains(TEXT("unspent")));
		C.Test.TestFalse(TEXT("Nothing to apply before a change"), Attributes->CanApply());

		// Spend one unspent point: free.
		Attributes->AdjustDraft(0, 1);
		C.Test.TestFalse(TEXT("Adding a point is not a respec"), Attributes->DraftNeedsRespec());
		if (!C.Require(Attributes->CanApply(), TEXT("Apply is offered for an unspent point")))
		{
			return;
		}
		Attributes->Apply();
		C.Test.TestEqual(TEXT("Apply raised Strength by 1"), Progression->GetAttributeAllocation().Strength, Start.Strength + 1);
		Attributes->Refresh();
		C.Test.TestEqual(TEXT("The draft follows the new allocation"), Attributes->GetDraft().Strength, Start.Strength + 1);

		// Lower Strength: a respec. No token, no gold: both greyed.
		const int32 Gold = Progression->GetGold();
		if (Gold > 0)
		{
			Progression->SpendGold(Gold);
		}
		Attributes->AdjustDraft(0, -1);
		C.Test.TestTrue(TEXT("Lowering an attribute is a respec"), Attributes->DraftNeedsRespec());
		C.Test.TestFalse(TEXT("A respec is not a free Apply"), Attributes->CanApply());
		// Tests run without saves, so the PIE player starts with no tokens.
		const int32 Tokens = Progression->GetRespecTokens();
		if (Tokens == 0)
		{
			C.Test.TestFalse(TEXT("Respec with token greyed without a token"), Attributes->CanRespecWithToken());
		}
		C.Test.TestFalse(TEXT("Respec for gold greyed without the gold"), Attributes->CanRespecWithGold());

		// A token: the token respec goes through and spends it.
		Progression->GrantRespecTokens(1);
		Attributes->Refresh();
		if (C.Require(Attributes->CanRespecWithToken(), TEXT("Respec with token is offered with a token")))
		{
			Attributes->RespecWithToken();
			C.Test.TestEqual(TEXT("The token respec lowered Strength"), Progression->GetAttributeAllocation().Strength, Start.Strength);
			C.Test.TestEqual(TEXT("The token is spent"), Progression->GetRespecTokens(), Tokens);
		}

		// Gold: the gold respec goes through and costs the price.
		Attributes->Refresh();
		Progression->AddGold(Progression->GetRespecGoldPrice());
		Attributes->AdjustDraft(1, -1);
		if (C.Require(Attributes->CanRespecWithGold(), TEXT("Respec for gold is offered with the price banked")))
		{
			Attributes->RespecWithGold();
			C.Test.TestEqual(TEXT("The gold respec lowered Agility"), Progression->GetAttributeAllocation().Agility, Start.Agility - 1);
			C.Test.TestEqual(TEXT("The price is paid"), Progression->GetGold(), 0);
		}
		Attributes->RemoveFromParent();
	});

	Scenario.Step(TEXT("Party window: an invite in the inbox, then a party with a disconnected member"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.LocalPlayer();
		APlayerController* Controller = PanelControllerOf(Player);
		ACrossguardPlayerState* Self = C.PlayerState(Player);
		UCrossguardPartySubsystem* Parties = PanelPartiesOf(C);
		ACrossguardCharacter* Host = C.SpawnPlayer(TEXT("PanelHost"), FVector(200.0f, PanelRow, 0.0f));
		ACrossguardCharacter* Friend = C.SpawnPlayer(TEXT("PanelFriend"), FVector(400.0f, PanelRow, 0.0f));
		if (!C.Require(Controller != nullptr && Self != nullptr && Parties != nullptr && Host != nullptr && Friend != nullptr,
				TEXT("the PIE player, the party subsystem, PanelHost and PanelFriend")))
		{
			return;
		}
		UCrossguardPartyWidget* PartyWindow = CreateWidget<UCrossguardPartyWidget>(Controller, UCrossguardPartyWidget::StaticClass());
		UCrossguardTeamListWidget* TeamList = CreateWidget<UCrossguardTeamListWidget>(Controller, UCrossguardTeamListWidget::StaticClass());
		if (!C.Require(PartyWindow != nullptr && TeamList != nullptr, TEXT("the party window and team list are created")))
		{
			return;
		}
		PartyWindow->TakeWidget();
		TeamList->TakeWidget();
		PartyWindow->OnWindowOpened();
		PartyWindow->Refresh();
		C.Test.TestTrue(TEXT("The party window built its tree in C++"), PartyWindow->IsBuiltInCode());
		C.Test.TestEqual(TEXT("No party: no member rows"), PartyWindow->GetMemberRowCount(), 0);
		C.Test.TestEqual(TEXT("No invites yet"), PartyWindow->GetInviteRowCount(), 0);

		// PanelHost invites the PIE player: one inbox row with nearly 180 s left; declining empties it.
		C.Require(Parties->Invite(C.PlayerState(Host), Self) == ECrossguardPartyResult::None, TEXT("PanelHost invites the PIE player"));
		PartyWindow->Refresh();
		C.Test.TestEqual(TEXT("One invite row"), PartyWindow->GetInviteRowCount(), 1);
		const UCrossguardPartyComponent* MyParty = Self->GetParty();
		if (C.Require(MyParty != nullptr && MyParty->GetInvites().Num() == 1, TEXT("the PIE player's inbox has the invite")))
		{
			const float Left = MyParty->GetInviteSecondsLeft(MyParty->GetInvites()[0].PartyId);
			C.Test.TestTrue(*FString::Printf(TEXT("The invite shows about 180 s left (%.1f)"), Left), Left > 170.0f && Left <= 180.0f);
			Self->GetParty()->RequestDecline(MyParty->GetInvites()[0].PartyId);
		}
		PartyWindow->Refresh();
		C.Test.TestEqual(TEXT("Declined: no invite rows"), PartyWindow->GetInviteRowCount(), 0);

		// The PIE player invites PanelFriend by name (the window's Invite button), founding a party.
		C.Test.TestTrue(TEXT("Invite by name finds PanelFriend"), PartyWindow->InviteByName(TEXT("panelfriend")));
		C.Test.TestFalse(TEXT("Invite by name refuses an unknown name"), PartyWindow->InviteByName(TEXT("Nobody Here")));
		if (!C.Require(Self->GetParty()->IsLeader(), TEXT("the invite founded a party led by the PIE player")))
		{
			return;
		}
		C.Require(Parties->Accept(C.PlayerState(Friend), Self->GetParty()->GetPartyId()) == ECrossguardPartyResult::None, TEXT("PanelFriend accepts"));
		PartyWindow->Refresh();
		C.Test.TestEqual(TEXT("Two member rows"), PartyWindow->GetMemberRowCount(), 2);
		C.Test.AddInfo(FString::Printf(TEXT("Party: %s | %s"), *PartyWindow->GetHeaderLine(), *FString::Join(PartyWindow->GetMemberLines(), TEXT(" | "))));
		C.Test.TestTrue(TEXT("The leader is marked (you)"), AnyLineContains(PartyWindow->GetMemberLines(), TEXT("(you) [leader]")));
		C.Test.TestTrue(TEXT("PanelFriend is listed"), AnyLineContains(PartyWindow->GetMemberLines(), TEXT("PanelFriend")));
		C.Test.TestFalse(TEXT("Nobody is disconnected yet"), AnyLineContains(PartyWindow->GetMemberLines(), TEXT("(disconnected)")));
		TeamList->Refresh();
		C.Test.TestTrue(*FString::Printf(TEXT("The team list shows PanelFriend (%s)"), *FString::Join(TeamList->GetLines(), TEXT(", "))),
			TeamList->GetLines().Contains(TEXT("PanelFriend")));

		// PanelFriend held for a reconnect (what ACrossguardGameMode::HoldForReconnect sets).
		C.PlayerState(Friend)->SetDisconnected(true);
		PartyWindow->Refresh();
		TeamList->Refresh();
		C.Test.TestTrue(TEXT("The party window marks PanelFriend (disconnected)"), AnyLineContains(PartyWindow->GetMemberLines(), TEXT("PanelFriend (disconnected)")));
		C.Test.TestTrue(TEXT("The team list marks PanelFriend (disconnected)"), TeamList->GetLines().Contains(TEXT("PanelFriend (disconnected)")));
		C.PlayerState(Friend)->SetDisconnected(false);
		PartyWindow->Refresh();
		C.Test.TestFalse(TEXT("Back: no marker"), AnyLineContains(PartyWindow->GetMemberLines(), TEXT("(disconnected)")));

		PartyWindow->RemoveFromParent();
		TeamList->RemoveFromParent();
	});

	Scenario.Run();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
