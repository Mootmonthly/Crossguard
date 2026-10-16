#include "Game/CrossguardGuildSubsystem.h"

#include "Character/CrossguardCharacter.h"
#include "Crossguard.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/CrossguardGameMode.h"
#include "Game/CrossguardPlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "Items/CrossguardInventoryComponent.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"

static_assert(static_cast<int>(ECrossguardGuildResult::InvalidData) == static_cast<int>(Crossguard::EGuildError::InvalidData),
	"ECrossguardGuildResult must mirror Crossguard::EGuildError");
static_assert(static_cast<int>(ECrossguardGuildRank::Leader) == static_cast<int>(Crossguard::EGuildRank::Leader),
	"ECrossguardGuildRank must mirror Crossguard::EGuildRank");

namespace
{
	std::string ToStd(const FString& Value)
	{
		return std::string(TCHAR_TO_UTF8(*Value));
	}

	FString ToFString(const std::string& Value)
	{
		return FString(UTF8_TO_TCHAR(Value.c_str()));
	}

	UCrossguardInventoryComponent* InventoryOf(const ACrossguardPlayerState* PlayerState)
	{
		const ACrossguardCharacter* Character = PlayerState != nullptr ? PlayerState->GetPawn<ACrossguardCharacter>() : nullptr;
		return Character != nullptr ? Character->GetInventoryComponent() : nullptr;
	}

	// The item definition id the bank logs: weapon, armor or table item.
	FName DefinitionId(const FCrossguardInventoryItem& Item)
	{
		return Item.Category == ECrossguardItemCategory::Weapon ? Item.Weapon.WeaponId
			: Item.Category == ECrossguardItemCategory::Armor ? Item.Armor.ArmorId : Item.ItemId;
	}
}

FString UCrossguardGuildSubsystem::PlayerIdOf(const APlayerState* PlayerState)
{
	if (PlayerState == nullptr)
	{
		return FString();
	}
	const FUniqueNetIdRepl& UniqueId = PlayerState->GetUniqueId();
	if (IsOnlineId(UniqueId))
	{
		return UniqueId.ToString();
	}
	// Development stand-in, set by the server at login (never in Shipping).
	const ACrossguardPlayerState* WithDevId = Cast<ACrossguardPlayerState>(PlayerState);
	if (WithDevId != nullptr && !WithDevId->DevPlayerId.IsEmpty())
	{
		return WithDevId->DevPlayerId;
	}
	return FString::Printf(TEXT("session-%d"), PlayerState->GetPlayerId());
}

bool UCrossguardGuildSubsystem::IsOnlineId(const FUniqueNetIdRepl& UniqueId)
{
	// The Null subsystem's ids ("DESKTOP-...-<guid>") are valid but new on every Play: a save keyed
	// by one is never found again. Steam (or any real subsystem) gives the same id every time.
	static const FName NullSubsystem(TEXT("NULL"));
	return UniqueId.IsValid() && UniqueId.GetType() != NullSubsystem;
}

double UCrossguardGuildSubsystem::Now()
{
	// Wall-clock seconds, so invite expiry would survive a save and reload.
	return static_cast<double>(FDateTime::UtcNow().ToUnixTimestamp());
}

ECrossguardGuildResult UCrossguardGuildSubsystem::ToResult(Crossguard::EGuildError Error)
{
	return static_cast<ECrossguardGuildResult>(static_cast<uint8>(Error));
}

FString UCrossguardGuildSubsystem::BankKey(const std::string& GuildId, int SlotId)
{
	return FString::Printf(TEXT("%s/%d"), *ToFString(GuildId), SlotId);
}

bool UCrossguardGuildSubsystem::IsBetweenRaids(const ACrossguardPlayerState* Actor) const
{
	// Assumption: like the stash, the bank is reached between raids only.
	const ACrossguardGameMode* GameMode = Actor != nullptr ? Actor->GetWorld()->GetAuthGameMode<ACrossguardGameMode>() : nullptr;
	return GameMode != nullptr && !GameMode->bIsRaid;
}

ACrossguardPlayerState* UCrossguardGuildSubsystem::FindOnline(const UWorld* World, const FString& PlayerId) const
{
	const AGameStateBase* GameState = World != nullptr ? World->GetGameState() : nullptr;
	if (GameState == nullptr)
	{
		return nullptr;
	}
	for (APlayerState* PlayerState : GameState->PlayerArray)
	{
		if (PlayerIdOf(PlayerState) == PlayerId)
		{
			return Cast<ACrossguardPlayerState>(PlayerState);
		}
	}
	return nullptr;
}

// --- Membership -----------------------------------------------------------------------------------

ECrossguardGuildResult UCrossguardGuildSubsystem::Create(ACrossguardPlayerState* Actor, const FString& Name)
{
	if (Actor == nullptr)
	{
		return ECrossguardGuildResult::NoPlayer;
	}
	const std::string GuildId = ToStd(FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens));
	const Crossguard::EGuildError Error = Registry.Create(GuildId, ToStd(Name), ToStd(PlayerIdOf(Actor)));
	if (Error == Crossguard::EGuildError::None)
	{
		RefreshGuild(GuildId);
	}
	return ToResult(Error);
}

ECrossguardGuildResult UCrossguardGuildSubsystem::Invite(ACrossguardPlayerState* Actor, ACrossguardPlayerState* Invitee)
{
	if (Actor == nullptr || Invitee == nullptr)
	{
		return ECrossguardGuildResult::NoPlayer;
	}
	const Crossguard::EGuildError Error = Registry.Invite(ToStd(PlayerIdOf(Actor)), ToStd(PlayerIdOf(Invitee)), Now());
	if (Error == Crossguard::EGuildError::None)
	{
		RefreshPlayer(Invitee);
		RefreshPlayer(Actor);
	}
	return ToResult(Error);
}

ECrossguardGuildResult UCrossguardGuildSubsystem::Accept(ACrossguardPlayerState* Actor, const FString& GuildId)
{
	if (Actor == nullptr)
	{
		return ECrossguardGuildResult::NoPlayer;
	}
	const Crossguard::EGuildError Error = Registry.Accept(ToStd(GuildId), ToStd(PlayerIdOf(Actor)), Now());
	if (Error == Crossguard::EGuildError::None)
	{
		RefreshGuild(ToStd(GuildId));
	}
	return ToResult(Error);
}

ECrossguardGuildResult UCrossguardGuildSubsystem::Decline(ACrossguardPlayerState* Actor, const FString& GuildId)
{
	if (Actor == nullptr)
	{
		return ECrossguardGuildResult::NoPlayer;
	}
	const Crossguard::EGuildError Error = Registry.Decline(ToStd(GuildId), ToStd(PlayerIdOf(Actor)));
	if (Error == Crossguard::EGuildError::None)
	{
		RefreshPlayer(Actor);
	}
	return ToResult(Error);
}

ECrossguardGuildResult UCrossguardGuildSubsystem::Kick(ACrossguardPlayerState* Actor, const FString& TargetId)
{
	if (Actor == nullptr)
	{
		return ECrossguardGuildResult::NoPlayer;
	}
	const Crossguard::FGuild* Guild = Registry.GuildOf(ToStd(PlayerIdOf(Actor)));
	const std::string GuildId = Guild != nullptr ? Guild->Id : std::string();
	const Crossguard::EGuildError Error = Registry.Kick(ToStd(PlayerIdOf(Actor)), ToStd(TargetId));
	if (Error == Crossguard::EGuildError::None)
	{
		RefreshGuild(GuildId, FindOnline(Actor->GetWorld(), TargetId));
	}
	return ToResult(Error);
}

ECrossguardGuildResult UCrossguardGuildSubsystem::SetRank(ACrossguardPlayerState* Actor, const FString& TargetId, ECrossguardGuildRank NewRank)
{
	if (Actor == nullptr)
	{
		return ECrossguardGuildResult::NoPlayer;
	}
	const std::string ActorId = ToStd(PlayerIdOf(Actor));
	const std::string Target = ToStd(TargetId);
	Crossguard::EGuildError Error = Crossguard::EGuildError::None;
	switch (NewRank)
	{
	case ECrossguardGuildRank::Leader:
		Error = Registry.TransferLeadership(ActorId, Target);
		break;
	case ECrossguardGuildRank::Officer:
		Error = Registry.Promote(ActorId, Target);
		break;
	case ECrossguardGuildRank::Member:
		Error = Registry.Demote(ActorId, Target);
		break;
	default:
		// Not a rank (a client can send any byte).
		return ECrossguardGuildResult::InvalidData;
	}
	if (const Crossguard::FGuild* Guild = Registry.GuildOf(ActorId); Error == Crossguard::EGuildError::None && Guild != nullptr)
	{
		RefreshGuild(Guild->Id);
	}
	return ToResult(Error);
}

ECrossguardGuildResult UCrossguardGuildSubsystem::Leave(ACrossguardPlayerState* Actor)
{
	if (Actor == nullptr)
	{
		return ECrossguardGuildResult::NoPlayer;
	}
	const std::string ActorId = ToStd(PlayerIdOf(Actor));
	const Crossguard::FGuild* Guild = Registry.GuildOf(ActorId);
	if (Guild == nullptr)
	{
		return ECrossguardGuildResult::NotInGuild;
	}
	const std::string GuildId = Guild->Id;
	// The last member takes the bank: between raids only (not into a raid inventory), and it must
	// fit first, gold included.
	TArray<FCrossguardInventoryItem> BankStacks;
	if (Guild->Members.size() == 1)
	{
		if ((Guild->Bank.Gold > 0 || !Guild->Bank.Items.empty()) && !IsBetweenRaids(Actor))
		{
			return ECrossguardGuildResult::OnlyBetweenRaids;
		}
		if (!Actor->CanReceiveGold(Guild->Bank.Gold))
		{
			return ECrossguardGuildResult::GoldOverflow;
		}
		for (const Crossguard::FGuildBankItem& Stored : Guild->Bank.Items)
		{
			if (const FCrossguardInventoryItem* Item = BankItemData.Find(BankKey(GuildId, Stored.SlotId)))
			{
				BankStacks.Add(*Item);
			}
		}
		UCrossguardInventoryComponent* Inventory = InventoryOf(Actor);
		if (!BankStacks.IsEmpty() && (Inventory == nullptr || !Inventory->ExchangeItems({}, BankStacks, false)))
		{
			return ECrossguardGuildResult::NoRoomInInventory;
		}
	}
	const Crossguard::FGuildLeaveResult Result = Registry.Leave(ActorId);
	if (!Result.IsValid())
	{
		return ToResult(Result.Error);
	}
	if (Result.bDisbanded)
	{
		for (const Crossguard::FGuildBankItem& Stored : Result.Items)
		{
			BankItemData.Remove(BankKey(GuildId, Stored.SlotId));
		}
		if (UCrossguardInventoryComponent* Inventory = InventoryOf(Actor); Inventory != nullptr && !BankStacks.IsEmpty())
		{
			Inventory->ExchangeItems({}, BankStacks, true);
		}
		if (Result.Gold > 0)
		{
			// Checked to fit above.
			Actor->AddGold(static_cast<int32>(Result.Gold));
		}
		UE_LOG(LogCrossguard, Log, TEXT("Guild %s disbanded"), *ToFString(GuildId));
	}
	RefreshGuild(GuildId, Actor);
	return ECrossguardGuildResult::None;
}

// --- Bank -----------------------------------------------------------------------------------------

ECrossguardGuildResult UCrossguardGuildSubsystem::MoveGold(ACrossguardPlayerState* Actor, int32 Amount)
{
	if (Actor == nullptr)
	{
		return ECrossguardGuildResult::NoPlayer;
	}
	if (!IsBetweenRaids(Actor))
	{
		return ECrossguardGuildResult::OnlyBetweenRaids;
	}
	const std::string ActorId = ToStd(PlayerIdOf(Actor));
	Crossguard::EGuildError Error = Crossguard::EGuildError::InvalidAmount;
	if (Amount == MIN_int32)
	{
		return ECrossguardGuildResult::InvalidAmount;
	}
	if (Amount > 0)
	{
		if (Actor->GetGold() < Amount)
		{
			return ECrossguardGuildResult::NotEnoughBankedGold;
		}
		Error = Registry.DepositGold(ActorId, Amount, Now());
		if (Error == Crossguard::EGuildError::None)
		{
			Actor->SpendGold(Amount);
		}
	}
	else if (Amount < 0)
	{
		if (!Actor->CanReceiveGold(-static_cast<int64>(Amount)))
		{
			return ECrossguardGuildResult::GoldOverflow;
		}
		Error = Registry.WithdrawGold(ActorId, -static_cast<int64>(Amount), Now());
		if (Error == Crossguard::EGuildError::None)
		{
			Actor->AddGold(-Amount);
		}
	}
	if (const Crossguard::FGuild* Guild = Registry.GuildOf(ActorId); Error == Crossguard::EGuildError::None && Guild != nullptr)
	{
		RefreshGuild(Guild->Id);
	}
	return ToResult(Error);
}

ECrossguardGuildResult UCrossguardGuildSubsystem::DepositItem(ACrossguardPlayerState* Actor, int32 InventoryItemId)
{
	if (Actor == nullptr)
	{
		return ECrossguardGuildResult::NoPlayer;
	}
	if (!IsBetweenRaids(Actor))
	{
		return ECrossguardGuildResult::OnlyBetweenRaids;
	}
	UCrossguardInventoryComponent* Inventory = InventoryOf(Actor);
	FCrossguardInventoryItem Item;
	if (Inventory == nullptr || !Inventory->FindItem(InventoryItemId, Item) || InventoryItemId == Inventory->GetReadiedItemId())
	{
		return ECrossguardGuildResult::NoSuchInventoryItem;
	}
	Crossguard::FGuildBankItem Stored;
	Stored.ItemId = ToStd(DefinitionId(Item).ToString());
	Stored.Rarity = static_cast<Crossguard::ERarity>(static_cast<uint8>(Item.Rarity));
	Stored.Count = Item.Count;
	Stored.Size = Crossguard::FGridSize{FMath::Max(Item.Size.X, 1), FMath::Max(Item.Size.Y, 1)};
	const std::string ActorId = ToStd(PlayerIdOf(Actor));
	const Crossguard::FGuildItemResult Result = Registry.DepositItem(ActorId, Stored, nullptr, Now());
	if (!Result.IsValid())
	{
		return ToResult(Result.Error);
	}
	Inventory->RemoveItem(InventoryItemId);
	const Crossguard::FGuild* Guild = Registry.GuildOf(ActorId);
	BankItemData.Add(BankKey(Guild->Id, Result.Item.SlotId), Item);
	RefreshGuild(Guild->Id);
	return ECrossguardGuildResult::None;
}

ECrossguardGuildResult UCrossguardGuildSubsystem::WithdrawItem(ACrossguardPlayerState* Actor, int32 SlotId)
{
	return GiveItem(Actor, PlayerIdOf(Actor), SlotId);
}

ECrossguardGuildResult UCrossguardGuildSubsystem::GiveGold(ACrossguardPlayerState* Actor, const FString& RecipientId, int32 Amount)
{
	if (Actor == nullptr)
	{
		return ECrossguardGuildResult::NoPlayer;
	}
	if (!IsBetweenRaids(Actor))
	{
		return ECrossguardGuildResult::OnlyBetweenRaids;
	}
	// Assumption: redistributed gold goes straight to the recipient's banked gold, so they must be
	// on this server (no mail without a backend).
	ACrossguardPlayerState* Recipient = FindOnline(Actor->GetWorld(), RecipientId);
	if (Recipient == nullptr)
	{
		return ECrossguardGuildResult::RecipientOffline;
	}
	if (Amount > 0 && !Recipient->CanReceiveGold(Amount))
	{
		return ECrossguardGuildResult::GoldOverflow;
	}
	const std::string ActorId = ToStd(PlayerIdOf(Actor));
	const Crossguard::EGuildError Error = Registry.RedistributeGold(ActorId, ToStd(RecipientId), Amount, Now());
	if (Error != Crossguard::EGuildError::None)
	{
		return ToResult(Error);
	}
	Recipient->AddGold(Amount);
	RefreshGuild(Registry.GuildOf(ActorId)->Id);
	return ECrossguardGuildResult::None;
}

ECrossguardGuildResult UCrossguardGuildSubsystem::GiveItem(ACrossguardPlayerState* Actor, const FString& RecipientId, int32 SlotId)
{
	if (Actor == nullptr)
	{
		return ECrossguardGuildResult::NoPlayer;
	}
	if (!IsBetweenRaids(Actor))
	{
		return ECrossguardGuildResult::OnlyBetweenRaids;
	}
	const std::string ActorId = ToStd(PlayerIdOf(Actor));
	const Crossguard::FGuild* Guild = Registry.GuildOf(ActorId);
	if (Guild == nullptr)
	{
		return ECrossguardGuildResult::NotInGuild;
	}
	const std::string GuildId = Guild->Id;
	const FCrossguardInventoryItem* Item = BankItemData.Find(BankKey(GuildId, SlotId));
	ACrossguardPlayerState* Recipient = FindOnline(Actor->GetWorld(), RecipientId);
	UCrossguardInventoryComponent* Inventory = InventoryOf(Recipient);
	if (Recipient == nullptr)
	{
		return ECrossguardGuildResult::RecipientOffline;
	}
	if (Item == nullptr)
	{
		return ECrossguardGuildResult::UnknownItem;
	}
	// Room first: the bank keeps the stack if it would not fit.
	if (Inventory == nullptr || !Inventory->ExchangeItems({}, {*Item}, false))
	{
		return ECrossguardGuildResult::NoRoomInInventory;
	}
	// To oneself: a withdrawal; to someone else: a redistribution (the library tells them apart).
	const Crossguard::FGuildItemResult Result = Recipient == Actor ? Registry.WithdrawItem(ActorId, SlotId, Now())
		: Registry.RedistributeItem(ActorId, ToStd(RecipientId), SlotId, Now());
	if (!Result.IsValid())
	{
		return ToResult(Result.Error);
	}
	const FCrossguardInventoryItem Given = *Item;
	BankItemData.Remove(BankKey(GuildId, SlotId));
	Inventory->ExchangeItems({}, {Given}, true);
	RefreshGuild(GuildId);
	return ECrossguardGuildResult::None;
}

// --- Views ----------------------------------------------------------------------------------------

void UCrossguardGuildSubsystem::RefreshGuild(const std::string& GuildId, ACrossguardPlayerState* Extra)
{
	UWorld* World = GetGameInstance() != nullptr ? GetGameInstance()->GetWorld() : nullptr;
	const AGameStateBase* GameState = World != nullptr ? World->GetGameState() : nullptr;
	if (GameState != nullptr)
	{
		for (APlayerState* PlayerState : GameState->PlayerArray)
		{
			ACrossguardPlayerState* Player = Cast<ACrossguardPlayerState>(PlayerState);
			const Crossguard::FGuild* Guild = Registry.GuildOf(ToStd(PlayerIdOf(Player)));
			if (Player != nullptr && Guild != nullptr && Guild->Id == GuildId)
			{
				RefreshPlayer(Player);
			}
		}
	}
	if (Extra != nullptr)
	{
		RefreshPlayer(Extra);
	}
}

void UCrossguardGuildSubsystem::RefreshPlayer(ACrossguardPlayerState* PlayerState)
{
	UCrossguardGuildComponent* View = PlayerState != nullptr ? PlayerState->FindComponentByClass<UCrossguardGuildComponent>() : nullptr;
	if (View == nullptr)
	{
		return;
	}
	const FString PlayerId = PlayerIdOf(PlayerState);
	DisplayNames.Add(PlayerId, PlayerState->GetPlayerName());
	const Crossguard::FGuild* Guild = Registry.GuildOf(ToStd(PlayerId));

	View->GuildId = Guild != nullptr ? ToFString(Guild->Id) : FString();
	View->GuildName = Guild != nullptr ? ToFString(Guild->Name) : FString();
	const std::optional<Crossguard::EGuildRank> Rank = Registry.RankOf(ToStd(PlayerId));
	View->Rank = Rank.has_value() ? static_cast<ECrossguardGuildRank>(static_cast<uint8>(*Rank)) : ECrossguardGuildRank::Member;
	View->Members.Reset();
	View->BankItems.Reset();
	View->BankGold = 0;
	if (Guild != nullptr)
	{
		for (const Crossguard::FGuildMember& Member : Guild->Members)
		{
			FCrossguardGuildMemberView& Out = View->Members.AddDefaulted_GetRef();
			Out.PlayerId = ToFString(Member.PlayerId);
			Out.Rank = static_cast<ECrossguardGuildRank>(static_cast<uint8>(Member.Rank));
			Out.bOnline = FindOnline(PlayerState->GetWorld(), Out.PlayerId) != nullptr;
			const FString* Name = DisplayNames.Find(Out.PlayerId);
			Out.DisplayName = Name != nullptr ? *Name : Out.PlayerId;
		}
		View->BankGold = Guild->Bank.Gold;
		for (const Crossguard::FGuildBankItem& Stored : Guild->Bank.Items)
		{
			const FCrossguardInventoryItem* Item = BankItemData.Find(BankKey(Guild->Id, Stored.SlotId));
			const Crossguard::FGridItem* Placed = Guild->Bank.Grid.Find(Stored.SlotId);
			if (Item == nullptr || Placed == nullptr)
			{
				continue;
			}
			FCrossguardGuildBankItemView& Out = View->BankItems.AddDefaulted_GetRef();
			Out.SlotId = Stored.SlotId;
			Out.Item = *Item;
			Out.Item.Position = FIntPoint(Placed->Placement.X, Placed->Placement.Y);
			Out.Item.bRotated = Placed->Placement.bRotated;
		}
	}
	// Invites to this player from any guild, still open.
	View->Invites.Reset();
	const double Time = Now();
	for (const auto& [Id, Other] : Registry.All())
	{
		for (const Crossguard::FGuildInvite& PendingInvite : Other.Invites)
		{
			if (PendingInvite.PlayerId == ToStd(PlayerId) && Time < PendingInvite.ExpiresAt)
			{
				FCrossguardGuildInviteView& Out = View->Invites.AddDefaulted_GetRef();
				Out.GuildId = ToFString(Other.Id);
				Out.GuildName = ToFString(Other.Name);
				const FString* Name = DisplayNames.Find(ToFString(PendingInvite.InvitedBy));
				Out.InvitedBy = Name != nullptr ? *Name : ToFString(PendingInvite.InvitedBy);
			}
		}
	}
	// Player states replicate rarely by default: send it now.
	PlayerState->ForceNetUpdate();
	View->OnGuildChanged.Broadcast();
}

void UCrossguardGuildSubsystem::PlayerJoined(ACrossguardPlayerState* PlayerState)
{
	Registry.PruneExpiredInvites(Now());
	const Crossguard::FGuild* Guild = PlayerState != nullptr ? Registry.GuildOf(ToStd(PlayerIdOf(PlayerState))) : nullptr;
	if (Guild != nullptr)
	{
		RefreshGuild(Guild->Id, PlayerState);
	}
	else
	{
		RefreshPlayer(PlayerState);
	}
}
