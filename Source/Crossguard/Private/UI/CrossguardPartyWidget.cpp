#include "UI/CrossguardPartyWidget.h"

#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/World.h"
#include "Game/CrossguardGuildSubsystem.h"
#include "Game/CrossguardPlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "Interaction/CrossguardRaidPortal.h"
#include "UI/CrossguardUIButton.h"
#include "UI/CrossguardUIHelpers.h"

namespace CrossguardPartyWidgetLocal
{
	const FLinearColor RefusalColor(1.0f, 0.55f, 0.3f);
	const FLinearColor DimColor(0.7f, 0.7f, 0.7f);
	const FLinearColor AwayColor(1.0f, 0.7f, 0.35f);

	// "2:35 left", "expired".
	FString TimeLeftText(float Seconds)
	{
		if (Seconds <= 0.0f)
		{
			return TEXT("expired");
		}
		const int32 Whole = FMath::CeilToInt(Seconds);
		return FString::Printf(TEXT("%d:%02d left"), Whole / 60, Whole % 60);
	}
}

UCrossguardPartyWidget::UCrossguardPartyWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	RefreshInterval = 0.2f;
}

UCrossguardPartyComponent* UCrossguardPartyWidget::GetParty() const
{
	const ACrossguardPlayerState* PlayerState = GetCrossguardPlayerState();
	return PlayerState != nullptr ? PlayerState->GetParty() : nullptr;
}

void UCrossguardPartyWidget::BuildTree()
{
	BuildFrame(TEXT("Party   (P to close)"), FVector2D(900.0f, 600.0f));

	HeaderText = MakeText(TEXT("Not in a party"), 14);
	AddTo(Body, HeaderText);
	AddTo(Body, MakeText(TEXT("Up to 8 players. Only the leader invites, kicks and makes someone leader; party changes and the raid start happen in the hub."),
		10, CrossguardPartyWidgetLocal::DimColor), 4.0f);

	// Invite by name: kept across redraws so typing is not lost.
	UHorizontalBox* InviteRow = MakeRow();
	AddTo(Body, InviteRow, 8.0f);
	AddTo(InviteRow, MakeText(TEXT("Invite player:"), 11));
	NameField = MakeTextField(TEXT("Player name"));
	USizeBox* NameSize = Make<USizeBox>();
	NameSize->SetWidthOverride(220.0f);
	NameSize->SetContent(NameField);
	AddTo(InviteRow, NameSize);
	AddTo(InviteRow, MakeButton(TEXT("Invite"), [this]()
	{
		if (NameField != nullptr)
		{
			InviteByName(NameField->GetText().ToString().TrimStartAndEnd());
		}
	}));

	ActionList = MakeColumn();
	AddTo(Body, ActionList, 6.0f);

	UHorizontalBox* Columns = MakeRow();
	AddTo(Body, Columns, 8.0f);
	const auto AddList = [this, Columns](const FString& Title, float Width) -> UVerticalBox*
	{
		UVerticalBox* Column = MakeColumn();
		AddTo(Column, MakeText(Title, 12, FLinearColor(1.0f, 0.85f, 0.5f)));
		UScrollBox* Scroll = Make<UScrollBox>();
		UVerticalBox* List = MakeColumn();
		Scroll->AddChild(List);
		USizeBox* ListSize = Make<USizeBox>();
		ListSize->SetWidthOverride(Width);
		ListSize->SetHeightOverride(330.0f);
		ListSize->SetContent(Scroll);
		AddTo(Column, ListSize);
		AddTo(Columns, Column, 8.0f);
		return List;
	};
	MemberList = AddList(TEXT("Members"), 420.0f);
	InviteList = AddList(TEXT("Invites to you (180 s each)"), 420.0f);
}

void UCrossguardPartyWidget::OnWindowOpened()
{
	BindParty();
	ShowStatus(FString(), FLinearColor::White);
	LastSignature.Reset();
}

void UCrossguardPartyWidget::BindParty()
{
	UCrossguardPartyComponent* Party = GetParty();
	if (Party == BoundParty.Get())
	{
		return;
	}
	if (UCrossguardPartyComponent* Old = BoundParty.Get())
	{
		Old->OnPartyRefused.RemoveDynamic(this, &UCrossguardPartyWidget::HandlePartyRefused);
	}
	if (Party != nullptr)
	{
		Party->OnPartyRefused.AddUniqueDynamic(this, &UCrossguardPartyWidget::HandlePartyRefused);
	}
	BoundParty = Party;
}

FString UCrossguardPartyWidget::RefusalText(ECrossguardPartyResult Result)
{
	switch (Result)
	{
	case ECrossguardPartyResult::InvalidPlayer:
		return TEXT("No player by that name is here.");
	case ECrossguardPartyResult::NotInParty:
		return TEXT("You are not in a party.");
	case ECrossguardPartyResult::AlreadyInParty:
		return TEXT("That player is already in a party.");
	case ECrossguardPartyResult::TargetNotInParty:
		return TEXT("That player is not in your party.");
	case ECrossguardPartyResult::CannotTargetSelf:
		return TEXT("You cannot do that to yourself.");
	case ECrossguardPartyResult::PartyFull:
		return TEXT("The party is full.");
	case ECrossguardPartyResult::AlreadyInvited:
		return TEXT("That player already has your invite.");
	case ECrossguardPartyResult::NoInvite:
		return TEXT("No invite to answer.");
	case ECrossguardPartyResult::InviteExpired:
		return TEXT("The invite has expired.");
	case ECrossguardPartyResult::NotLeader:
		return TEXT("Only the party's leader can do that.");
	case ECrossguardPartyResult::OnlyInHub:
		return TEXT("Party changes happen in the hub.");
	// The raid launch's own (the raid portal says the same).
	case ECrossguardPartyResult::NoRaidDestination:
	case ECrossguardPartyResult::HostInParty:
	case ECrossguardPartyResult::AlreadySearching:
	case ECrossguardPartyResult::SearchCancelled:
	case ECrossguardPartyResult::BackendUnavailable:
		return ACrossguardRaidPortal::RefusalMessage(Result);
	default:
		return FString::Printf(TEXT("Party: refused (%s)"), *CrossguardUI::UIEnumName(Result));
	}
}

void UCrossguardPartyWidget::HandlePartyRefused(ECrossguardPartyResult Result)
{
	ShowStatus(RefusalText(Result), CrossguardPartyWidgetLocal::RefusalColor);
}

APlayerState* UCrossguardPartyWidget::FindPlayerState(const FString& PlayerId) const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World != nullptr ? World->GetGameState() : nullptr;
	if (GameState == nullptr || PlayerId.IsEmpty())
	{
		return nullptr;
	}
	for (APlayerState* Other : GameState->PlayerArray)
	{
		if (Other != nullptr && UCrossguardGuildSubsystem::PlayerIdOf(Other) == PlayerId)
		{
			return Other;
		}
	}
	return nullptr;
}

bool UCrossguardPartyWidget::InviteByName(const FString& PlayerName)
{
	UCrossguardPartyComponent* Party = GetParty();
	const ACrossguardPlayerState* Self = GetCrossguardPlayerState();
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World != nullptr ? World->GetGameState() : nullptr;
	if (Party == nullptr || Self == nullptr || GameState == nullptr)
	{
		return false;
	}
	if (PlayerName.IsEmpty())
	{
		ShowStatus(TEXT("Type a player's name first."), CrossguardPartyWidgetLocal::RefusalColor);
		return false;
	}
	BindParty();
	for (APlayerState* Other : GameState->PlayerArray)
	{
		if (Other != nullptr && Other != Self && Other->GetPlayerName().Equals(PlayerName, ESearchCase::IgnoreCase))
		{
			Party->RequestInvite(Other);
			ShowStatus(FString::Printf(TEXT("Invited %s; they have 180 s to accept."), *Other->GetPlayerName()), FLinearColor::White);
			return true;
		}
	}
	ShowStatus(FString::Printf(TEXT("No player named %s on this server."), *PlayerName), CrossguardPartyWidgetLocal::RefusalColor);
	return false;
}

FString UCrossguardPartyWidget::BuildSignature() const
{
	const UCrossguardPartyComponent* Party = GetParty();
	const ACrossguardPlayerState* Self = GetCrossguardPlayerState();
	if (Party == nullptr || Self == nullptr)
	{
		return TEXT("none");
	}
	FString Signature = FString::Printf(TEXT("%s|%s|%d|%d;"), *Party->GetPartyId(), *Party->GetLeaderId(), Party->IsLeader() ? 1 : 0,
		Self->IsInRaid() ? 1 : 0);
	for (const FCrossguardPartyMemberView& Member : Party->GetMembers())
	{
		const ACrossguardPlayerState* Online = Cast<ACrossguardPlayerState>(FindPlayerState(Member.PlayerId));
		Signature += FString::Printf(TEXT("%s:%s:%d:%d:%d,"), *Member.PlayerId, *Member.DisplayName, Member.bLeader ? 1 : 0, Online != nullptr ? 1 : 0,
			Online != nullptr && Online->IsDisconnected() ? 1 : 0);
	}
	for (const FCrossguardPartyInviteView& Invite : Party->GetInvites())
	{
		Signature += FString::Printf(TEXT("i%s:%s:%d:%d,"), *Invite.PartyId, *Invite.InvitedBy, Invite.MemberCount,
			Party->GetInviteSecondsLeft(Invite.PartyId) > 0.0f ? 1 : 0);
	}
	return Signature;
}

void UCrossguardPartyWidget::NativeRefresh()
{
	BindParty();
	const FString Signature = BuildSignature();
	if (Signature != LastSignature)
	{
		LastSignature = Signature;
		Redraw();
	}
	UpdateCountdowns();
}

void UCrossguardPartyWidget::UpdateCountdowns()
{
	const UCrossguardPartyComponent* Party = GetParty();
	if (Party == nullptr)
	{
		return;
	}
	for (const TPair<FString, TObjectPtr<UTextBlock>>& Pair : CountdownTexts)
	{
		SetText(Pair.Value, CrossguardPartyWidgetLocal::TimeLeftText(Party->GetInviteSecondsLeft(Pair.Key)));
	}
}

void UCrossguardPartyWidget::Redraw()
{
	using CrossguardPartyWidgetLocal::DimColor;
	ActionList->ClearChildren();
	MemberList->ClearChildren();
	InviteList->ClearChildren();
	CountdownTexts.Reset();
	MemberLines.Reset();
	MemberRows = 0;
	InviteRows = 0;

	const UCrossguardPartyComponent* Party = GetParty();
	const ACrossguardPlayerState* Self = GetCrossguardPlayerState();
	if (Party == nullptr || Self == nullptr)
	{
		HeaderLine = TEXT("No player state.");
		SetText(HeaderText, HeaderLine);
		return;
	}
	// Hint only: the server's hub check is the game mode's bIsRaid.
	const bool bInHub = !Self->IsInRaid();
	const bool bLeader = Party->IsLeader();
	const bool bInParty = Party->IsInParty();
	// Each callback finds the party component again when clicked.
	const auto MakeAction = [this](const FString& Label, bool bActive, TFunction<void(UCrossguardPartyComponent&)> OnClick)
	{
		UCrossguardUIButton* Button = MakeButton(Label, [this, Callback = MoveTemp(OnClick)]()
		{
			if (UCrossguardPartyComponent* Target = GetParty())
			{
				BindParty();
				Callback(*Target);
			}
		}, 10);
		Button->SetActive(bActive);
		return Button;
	};

	if (bInParty)
	{
		HeaderLine = FString::Printf(TEXT("Party of %d%s"), Party->GetMembers().Num(), bLeader ? TEXT(": you lead") : TEXT(""));
	}
	else
	{
		HeaderLine = TEXT("Not in a party. Invite someone to found one (you lead it), or accept an invite.");
	}
	if (!bInHub)
	{
		HeaderLine += TEXT("   (in a raid: the party is the raid team; changes wait for the hub)");
	}
	SetText(HeaderText, HeaderLine);

	// Party actions.
	UHorizontalBox* Actions = MakeRow();
	AddTo(Actions, MakeAction(TEXT("Leave party"), bInParty && bInHub, [](UCrossguardPartyComponent& Target) { Target.RequestLeave(); }));
	AddTo(Actions, MakeAction(bInParty ? TEXT("Start raid (party)") : TEXT("Start raid (solo)"), (bLeader || !bInParty) && bInHub,
		[](UCrossguardPartyComponent& Target) { Target.RequestStartRaid(); }));
	AddTo(ActionList, Actions);

	// Members, in join order.
	const FString MyId = UCrossguardGuildSubsystem::PlayerIdOf(Self);
	for (const FCrossguardPartyMemberView& Member : Party->GetMembers())
	{
		const bool bSelf = Member.PlayerId == MyId;
		const ACrossguardPlayerState* Online = Cast<ACrossguardPlayerState>(FindPlayerState(Member.PlayerId));
		const bool bAway = Online != nullptr && Online->IsDisconnected();
		const FString Line = FString::Printf(TEXT("%s%s%s%s"), *Member.DisplayName, bSelf ? TEXT(" (you)") : TEXT(""),
			Member.bLeader ? TEXT(" [leader]") : TEXT(""), bAway ? TEXT(" (disconnected)") : (Online == nullptr ? TEXT(" (not here)") : TEXT("")));
		UHorizontalBox* Row = MakeRow();
		USizeBox* NameBox = Make<USizeBox>();
		NameBox->SetWidthOverride(230.0f);
		NameBox->SetContent(MakeText(Line, 11, bAway ? CrossguardPartyWidgetLocal::AwayColor : (Online == nullptr ? DimColor : FLinearColor::White)));
		AddTo(Row, NameBox);
		if (!bSelf)
		{
			const FString Id = Member.PlayerId;
			AddTo(Row, MakeAction(TEXT("Make leader"), bLeader, [Id](UCrossguardPartyComponent& Target) { Target.RequestPromote(Id); }));
			AddTo(Row, MakeAction(TEXT("Kick"), bLeader && bInHub, [Id](UCrossguardPartyComponent& Target) { Target.RequestKick(Id); }));
		}
		AddTo(MemberList, Row);
		MemberLines.Add(Line);
		++MemberRows;
	}
	if (!bInParty)
	{
		AddTo(MemberList, MakeText(TEXT("No party."), 10, DimColor));
	}

	// The invite inbox: accept (hub only) or delete (anywhere).
	for (const FCrossguardPartyInviteView& Invite : Party->GetInvites())
	{
		const FString Id = Invite.PartyId;
		const bool bLive = Party->GetInviteSecondsLeft(Id) > 0.0f;
		UVerticalBox* Entry = MakeColumn();
		AddTo(Entry, MakeText(FString::Printf(TEXT("From %s (%d member%s)"), *Invite.InvitedBy, Invite.MemberCount, Invite.MemberCount == 1 ? TEXT("") : TEXT("s")),
			11, bLive ? FLinearColor::White : DimColor));
		UHorizontalBox* Row = MakeRow();
		UTextBlock* Countdown = MakeText(CrossguardPartyWidgetLocal::TimeLeftText(Party->GetInviteSecondsLeft(Id)), 10, DimColor);
		USizeBox* CountdownBox = Make<USizeBox>();
		CountdownBox->SetWidthOverride(80.0f);
		CountdownBox->SetContent(Countdown);
		AddTo(Row, CountdownBox);
		AddTo(Row, MakeAction(TEXT("Accept"), bLive && bInHub && !bInParty, [Id](UCrossguardPartyComponent& Target) { Target.RequestAccept(Id); }));
		AddTo(Row, MakeAction(TEXT("Decline"), true, [Id](UCrossguardPartyComponent& Target) { Target.RequestDecline(Id); }));
		AddTo(Entry, Row);
		AddTo(InviteList, Entry, 4.0f);
		CountdownTexts.Add(Id, Countdown);
		++InviteRows;
	}
	if (Party->GetInvites().IsEmpty())
	{
		AddTo(InviteList, MakeText(TEXT("No invites."), 10, DimColor));
	}
	else
	{
		AddTo(InviteList, MakeAction(TEXT("Decline all"), true, [](UCrossguardPartyComponent& Target) { Target.RequestDeclineAll(); }), 6.0f);
	}
}
