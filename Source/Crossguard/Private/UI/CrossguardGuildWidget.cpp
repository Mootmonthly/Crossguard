#include "UI/CrossguardGuildWidget.h"

#include "Character/CrossguardCharacter.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Game/CrossguardGuildSubsystem.h"
#include "Game/CrossguardPlayerState.h"
#include "UI/CrossguardUIButton.h"
#include "UI/CrossguardUIHelpers.h"

namespace CrossguardGuildWidgetLocal
{
	// How far "Invite nearest player" looks, in cm. Invites have no distance rule; this only picks
	// whom to ask.
	constexpr float InviteSearchDistance = 1000.0f;

	// docs/guild-rules.md, "Ranks and permissions". Display only.
	bool CanManage(ECrossguardGuildRank Rank) { return Rank != ECrossguardGuildRank::Member; }

	bool CanKick(ECrossguardGuildRank Rank, ECrossguardGuildRank Target, bool bSelf)
	{
		return !bSelf && (Rank == ECrossguardGuildRank::Leader || (Rank == ECrossguardGuildRank::Officer && Target == ECrossguardGuildRank::Member));
	}
}

UCrossguardGuildWidget::UCrossguardGuildWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	RefreshInterval = 0.1f;
}

UCrossguardGuildComponent* UCrossguardGuildWidget::GetGuild() const
{
	const ACrossguardPlayerState* PlayerState = GetCrossguardPlayerState();
	return PlayerState != nullptr ? PlayerState->GetGuild() : nullptr;
}

void UCrossguardGuildWidget::BuildTree()
{
	BuildFrame(TEXT("Guild   (U to close)"), FVector2D(1080.0f, 640.0f));

	HeaderText = MakeText(TEXT("Not in a guild"), 14);
	AddTo(Body, HeaderText);
	PermissionText = MakeText(FString(), 10, FLinearColor(0.7f, 0.7f, 0.7f));
	PermissionText->SetAutoWrapText(true);
	AddTo(Body, PermissionText);

	// Founding: kept across redraws so typing is not lost.
	CreateRow = MakeRow();
	AddTo(Body, CreateRow, 6.0f);
	AddTo(CreateRow, MakeText(TEXT("New guild name (3-24 letters, digits, spaces, -):"), 11));
	NameField = MakeTextField(TEXT("Iron Wolves"));
	USizeBox* NameSize = Make<USizeBox>();
	NameSize->SetWidthOverride(220.0f);
	NameSize->SetContent(NameField);
	AddTo(CreateRow, NameSize);
	AddTo(CreateRow, MakeButton(TEXT("Create guild"), [this]()
	{
		if (UCrossguardGuildComponent* Guild = GetGuild(); Guild != nullptr && NameField != nullptr)
		{
			Guild->RequestCreate(NameField->GetText().ToString());
		}
	}));

	TopControls = MakeColumn();
	AddTo(Body, TopControls, 6.0f);

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
		ListSize->SetHeightOverride(340.0f);
		ListSize->SetContent(Scroll);
		AddTo(Column, ListSize);
		AddTo(Columns, Column, 8.0f);
		return List;
	};
	MemberList = AddList(TEXT("Members"), 420.0f);
	BankList = AddList(TEXT("Bank (shared stash)"), 280.0f);
	DepositList = AddList(TEXT("Your items (deposit)"), 280.0f);

	UHorizontalBox* GoldRow = MakeRow();
	AddTo(Body, GoldRow, 8.0f);
	AddTo(GoldRow, MakeText(TEXT("Gold amount:"), 11));
	AmountField = MakeTextField(TEXT("100"));
	USizeBox* AmountSize = Make<USizeBox>();
	AmountSize->SetWidthOverride(120.0f);
	AmountSize->SetContent(AmountField);
	AddTo(GoldRow, AmountSize);
	AddTo(GoldRow, MakeButton(TEXT("Deposit gold"), [this]()
	{
		if (UCrossguardGuildComponent* Guild = GetGuild())
		{
			Guild->RequestMoveGold(ReadAmountField());
		}
	}));
	WithdrawGoldButton = MakeButton(TEXT("Withdraw gold (Officer+)"), [this]()
	{
		if (UCrossguardGuildComponent* Guild = GetGuild())
		{
			Guild->RequestMoveGold(-ReadAmountField());
		}
	});
	AddTo(GoldRow, WithdrawGoldButton);
}

int32 UCrossguardGuildWidget::ReadAmountField() const
{
	return AmountField != nullptr ? FMath::Max(0, FCString::Atoi(*AmountField->GetText().ToString())) : 0;
}

void UCrossguardGuildWidget::ShowRefusal(ECrossguardGuildResult Result)
{
	ShowStatus(FString::Printf(TEXT("Guild: refused (%s)"), *CrossguardUI::UIEnumName(Result)), FLinearColor(1.0f, 0.5f, 0.3f));
}

FString UCrossguardGuildWidget::BuildSignature() const
{
	const UCrossguardGuildComponent* Guild = GetGuild();
	if (Guild == nullptr)
	{
		return TEXT("none");
	}
	const ACrossguardCharacter* Character = GetCrossguardCharacter();
	const UCrossguardInventoryComponent* Inventory = Character != nullptr ? Character->GetInventoryComponent() : nullptr;
	const ACrossguardPlayerState* PlayerState = GetCrossguardPlayerState();
	FString Signature = FString::Printf(TEXT("%s|%s|%d|%lld|%d;"), *GetNameSafe(Character), *Guild->GuildId, static_cast<int32>(Guild->Rank),
		Guild->BankGold, PlayerState != nullptr && PlayerState->IsInRaid() ? 1 : 0);
	for (const FCrossguardGuildMemberView& Member : Guild->Members)
	{
		Signature += FString::Printf(TEXT("%s:%s:%d:%d,"), *Member.PlayerId, *Member.DisplayName, static_cast<int32>(Member.Rank), Member.bOnline ? 1 : 0);
	}
	for (const FCrossguardGuildInviteView& Invite : Guild->Invites)
	{
		Signature += Invite.GuildId + TEXT(",");
	}
	for (const FCrossguardGuildBankItemView& Stack : Guild->BankItems)
	{
		Signature += FString::Printf(TEXT("b%d:%d,"), Stack.SlotId, Stack.Item.Count);
	}
	if (Inventory != nullptr)
	{
		for (const FCrossguardInventoryItem& Item : Inventory->GetItems())
		{
			Signature += FString::Printf(TEXT("i%d:%d,"), Item.Id, Item.Count);
		}
	}
	return Signature;
}

void UCrossguardGuildWidget::NativeRefresh()
{
	const FString Signature = BuildSignature();
	if (Signature == LastSignature)
	{
		return;
	}
	LastSignature = Signature;
	Redraw();
}

void UCrossguardGuildWidget::Redraw()
{
	TopControls->ClearChildren();
	MemberList->ClearChildren();
	BankList->ClearChildren();
	DepositList->ClearChildren();
	MemberRows = 0;
	BankRows = 0;

	const UCrossguardGuildComponent* Guild = GetGuild();
	const ACrossguardPlayerState* PlayerState = GetCrossguardPlayerState();
	if (Guild == nullptr || PlayerState == nullptr)
	{
		HeaderLine = TEXT("No player state.");
		SetText(HeaderText, HeaderLine);
		return;
	}
	const ACrossguardCharacter* Character = GetCrossguardCharacter();
	const UCrossguardInventoryComponent* Inventory = Character != nullptr ? Character->GetInventoryComponent() : nullptr;
	const UCrossguardEquipmentComponent* Equipment = Character != nullptr ? Character->GetEquipmentComponent() : nullptr;
	// Each callback finds the guild component again when clicked.
	const auto MakeAction = [this](const FString& Label, bool bActive, TFunction<void(UCrossguardGuildComponent&)> OnClick)
	{
		UCrossguardUIButton* Button = MakeButton(Label, [this, Callback = MoveTemp(OnClick)]()
		{
			if (UCrossguardGuildComponent* Target = GetGuild())
			{
				Callback(*Target);
			}
		}, 10);
		Button->SetActive(bActive);
		return Button;
	};

	if (!Guild->IsInGuild())
	{
		HeaderLine = TEXT("You are not in a guild.");
		SetText(HeaderText, HeaderLine);
		SetText(PermissionText, TEXT("Found one below, or accept an invite. Officers and the Leader invite players."));
		CreateRow->SetVisibility(ESlateVisibility::Visible);
		WithdrawGoldButton->SetActive(false);
		if (Guild->Invites.IsEmpty())
		{
			AddTo(TopControls, MakeText(TEXT("No invites."), 11, FLinearColor(0.7f, 0.7f, 0.7f)));
		}
		for (const FCrossguardGuildInviteView& Invite : Guild->Invites)
		{
			UHorizontalBox* Row = MakeRow();
			AddTo(Row, MakeText(FString::Printf(TEXT("Invite to %s from %s"), *Invite.GuildName, *Invite.InvitedBy), 11));
			const FString GuildId = Invite.GuildId;
			AddTo(Row, MakeAction(TEXT("Accept"), true, [GuildId](UCrossguardGuildComponent& Target) { Target.RequestAcceptInvite(GuildId); }));
			AddTo(Row, MakeAction(TEXT("Decline"), true, [GuildId](UCrossguardGuildComponent& Target) { Target.RequestDeclineInvite(GuildId); }));
			AddTo(TopControls, Row);
		}
		return;
	}

	const ECrossguardGuildRank MyRank = Guild->Rank;
	const bool bManage = CrossguardGuildWidgetLocal::CanManage(MyRank);
	const bool bLeader = MyRank == ECrossguardGuildRank::Leader;
	const bool bInRaid = PlayerState->IsInRaid();
	HeaderLine = FString::Printf(TEXT("%s   (you: %s)   Bank gold: %lld"), *Guild->GuildName, *CrossguardUI::UIEnumName(MyRank), Guild->BankGold);
	SetText(HeaderText, HeaderLine);
	SetText(PermissionText, FString::Printf(TEXT("You can: deposit%s%s%s.%s"),
		bManage ? TEXT(", withdraw, invite, kick Members") : TEXT(" (withdrawing, inviting and kicking need Officer)"),
		bLeader ? TEXT(", kick anyone, promote, demote, make leader") : TEXT(""),
		bLeader && Guild->Members.Num() > 1 ? TEXT(" (to leave, make someone else leader first)") : TEXT(""),
		bInRaid ? TEXT(" The bank works between raids only.") : TEXT("")));
	CreateRow->SetVisibility(ESlateVisibility::Collapsed);
	WithdrawGoldButton->SetActive(bManage);

	UHorizontalBox* Top = MakeRow();
	AddTo(Top, MakeAction(TEXT("Invite nearest player"), bManage, [this](UCrossguardGuildComponent& Target)
	{
		const ACrossguardCharacter* Self = GetCrossguardCharacter();
		const ACrossguardCharacter* Nearest = CrossguardUI::FindNearestPlayer(Self, CrossguardGuildWidgetLocal::InviteSearchDistance);
		if (Nearest == nullptr)
		{
			ShowStatus(TEXT("No player within 10 m."), FLinearColor(1.0f, 0.6f, 0.3f));
			return;
		}
		Target.RequestInvite(Nearest->GetPlayerState());
		ShowStatus(FString::Printf(TEXT("Invited %s."), *CrossguardUI::PlayerNameOf(Nearest)), FLinearColor::White);
	}));
	AddTo(Top, MakeAction(TEXT("Leave guild"), true, [](UCrossguardGuildComponent& Target) { Target.RequestLeave(); }));
	AddTo(TopControls, Top);

	// Roster.
	const FString MyId = UCrossguardGuildSubsystem::PlayerIdOf(PlayerState);
	for (const FCrossguardGuildMemberView& Member : Guild->Members)
	{
		const bool bSelf = Member.PlayerId == MyId;
		UHorizontalBox* Row = MakeRow();
		UTextBlock* Name = MakeText(FString::Printf(TEXT("%s%s  %s%s"), *Member.DisplayName, bSelf ? TEXT(" (you)") : TEXT(""),
			*CrossguardUI::UIEnumName(Member.Rank), Member.bOnline ? TEXT(", online") : TEXT("")), 11,
			Member.bOnline ? FLinearColor::White : FLinearColor(0.6f, 0.6f, 0.6f));
		AddTo(Row, Name);
		const FString Id = Member.PlayerId;
		if (!bSelf)
		{
			if (Member.Rank == ECrossguardGuildRank::Member)
			{
				AddTo(Row, MakeAction(TEXT("Promote"), bLeader, [Id](UCrossguardGuildComponent& Target) { Target.RequestSetRank(Id, ECrossguardGuildRank::Officer); }));
			}
			else if (Member.Rank == ECrossguardGuildRank::Officer)
			{
				AddTo(Row, MakeAction(TEXT("Demote"), bLeader, [Id](UCrossguardGuildComponent& Target) { Target.RequestSetRank(Id, ECrossguardGuildRank::Member); }));
			}
			AddTo(Row, MakeAction(TEXT("Make leader"), bLeader, [Id](UCrossguardGuildComponent& Target) { Target.RequestSetRank(Id, ECrossguardGuildRank::Leader); }));
			AddTo(Row, MakeAction(TEXT("Kick"), CrossguardGuildWidgetLocal::CanKick(MyRank, Member.Rank, bSelf),
				[Id](UCrossguardGuildComponent& Target) { Target.RequestKick(Id); }));
		}
		AddTo(MemberList, Row);
		++MemberRows;
	}

	// Bank items: officers withdraw.
	for (const FCrossguardGuildBankItemView& Stack : Guild->BankItems)
	{
		UHorizontalBox* Row = MakeRow();
		AddTo(Row, MakeText(CrossguardUI::ItemLabel(Stack.Item, Inventory, Equipment), 10, CrossguardUI::RarityColor(CrossguardUI::ItemRarity(Stack.Item))));
		const int32 SlotId = Stack.SlotId;
		AddTo(Row, MakeAction(TEXT("Withdraw"), bManage, [SlotId](UCrossguardGuildComponent& Target) { Target.RequestWithdrawItem(SlotId); }));
		AddTo(BankList, Row);
		++BankRows;
	}
	if (Guild->BankItems.IsEmpty())
	{
		AddTo(BankList, MakeText(TEXT("Empty."), 10, FLinearColor(0.7f, 0.7f, 0.7f)));
	}

	// Your stacks: anyone deposits (whole stacks, not the item in hand).
	if (Inventory != nullptr)
	{
		for (const FCrossguardInventoryItem& Item : Inventory->GetItems())
		{
			UHorizontalBox* Row = MakeRow();
			AddTo(Row, MakeText(CrossguardUI::ItemLabel(Item, Inventory, Equipment), 10, CrossguardUI::RarityColor(CrossguardUI::ItemRarity(Item))));
			const int32 ItemId = Item.Id;
			AddTo(Row, MakeAction(TEXT("Deposit"), Item.Id != Inventory->GetReadiedItemId(),
				[ItemId](UCrossguardGuildComponent& Target) { Target.RequestDepositItem(ItemId); }));
			AddTo(DepositList, Row);
		}
	}
}
