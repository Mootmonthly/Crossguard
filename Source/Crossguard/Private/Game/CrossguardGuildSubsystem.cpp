#include "Game/CrossguardGuildSubsystem.h"

#include "Character/CrossguardCharacter.h"
#include "Crossguard.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/CrossguardGameMode.h"
#include "Game/CrossguardNakamaClient.h"
#include "Game/CrossguardNakamaSaveBackend.h"
#include "Game/CrossguardNakamaSubsystem.h"
#include "Game/CrossguardPlayerState.h"
#include "Game/CrossguardSaveSubsystem.h"
#include "GameFramework/GameStateBase.h"
#include "Items/CrossguardInventoryComponent.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

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

	// One field of a crossguard_guild payload: its name and its value as raw JSON.
	TPair<FString, FString> GuildField(const TCHAR* Name, const FString& Json)
	{
		return TPair<FString, FString>(FString(Name), Json);
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
	// Verified by the server at login (docs/anti-cheat.md, "Player identity").
	const ACrossguardPlayerState* WithNakamaId = Cast<ACrossguardPlayerState>(PlayerState);
	if (WithNakamaId != nullptr && !WithNakamaId->NakamaUserId.IsEmpty())
	{
		return WithNakamaId->NakamaUserId;
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

void UCrossguardGuildSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UCrossguardNakamaSubsystem>();
	const UCrossguardNakamaSubsystem* Nakama = GetGameInstance()->GetSubsystem<UCrossguardNakamaSubsystem>();
	bBackend = Nakama != nullptr && Nakama->IsBackendEnabled();
	if (bBackend)
	{
		RefreshHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UCrossguardGuildSubsystem::RefreshTick), GuildRefreshSeconds);
		UE_LOG(LogCrossguard, Log, TEXT("Guilds: in Nakama (refreshed every %.0f s)"), GuildRefreshSeconds);
	}
}

void UCrossguardGuildSubsystem::Deinitialize()
{
	if (RefreshHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(RefreshHandle);
		RefreshHandle.Reset();
	}
	Super::Deinitialize();
}

UWorld* UCrossguardGuildSubsystem::GetServerWorld() const
{
	return GetGameInstance() != nullptr ? GetGameInstance()->GetWorld() : nullptr;
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
	if (bBackend)
	{
		// The guild id is the backend's (its Nakama group).
		return SendBackendOp(TEXT("create"), Actor, {GuildField(TEXT("name"), FCrossguardNakamaClient::Quote(Name))});
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
	if (bBackend)
	{
		const FString InviteeId = PlayerIdOf(Invitee);
		if (!FCrossguardNakamaClient::IsUserId(InviteeId))
		{
			return ECrossguardGuildResult::NoPlayer;
		}
		DisplayNames.Add(InviteeId, Invitee->GetPlayerName());
		return SendBackendOp(TEXT("invite"), Actor, {GuildField(TEXT("target_id"), FCrossguardNakamaClient::Quote(InviteeId))});
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
	if (bBackend)
	{
		return SendBackendOp(TEXT("accept"), Actor, {GuildField(TEXT("guild_id"), FCrossguardNakamaClient::Quote(GuildId))});
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
	if (bBackend)
	{
		return SendBackendOp(TEXT("decline"), Actor, {GuildField(TEXT("guild_id"), FCrossguardNakamaClient::Quote(GuildId))});
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
	if (bBackend)
	{
		return SendBackendOp(TEXT("kick"), Actor, {GuildField(TEXT("target_id"), FCrossguardNakamaClient::Quote(TargetId))});
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
	if (bBackend)
	{
		const TCHAR* Op = NewRank == ECrossguardGuildRank::Leader ? TEXT("transfer")
			: NewRank == ECrossguardGuildRank::Officer ? TEXT("promote")
			: NewRank == ECrossguardGuildRank::Member ? TEXT("demote") : nullptr;
		// Not a rank (a client can send any byte).
		return Op != nullptr ? SendBackendOp(Op, Actor, {GuildField(TEXT("target_id"), FCrossguardNakamaClient::Quote(TargetId))}) : ECrossguardGuildResult::InvalidData;
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
		// The mirror may not know it yet: Nakama answers.
		return bBackend ? SendBackendOp(TEXT("leave"), Actor, {}) : ECrossguardGuildResult::NotInGuild;
	}
	const std::string GuildId = Guild->Id;
	if (bBackend && !(Guild->Members.size() == 1 && (Guild->Bank.Gold > 0 || !Guild->Bank.Items.empty())))
	{
		// Not the last member here (or an empty bank): nothing to take. Nakama decides (if it was the last
		// member after all, it answers BankChanged and the mirror refreshes).
		return SendBackendOp(TEXT("leave"), Actor, {});
	}
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
	if (bBackend)
	{
		// The last member: the bank goes into their save with the leave, exactly as this server sees it.
		if (BankStacks.Num() != static_cast<int32>(Guild->Bank.Items.size()) || Guild->Bank.Gold > MAX_int32)
		{
			// A stack this server cannot read (or gold beyond banked gold, refused above): not here.
			return ECrossguardGuildResult::BankChanged;
		}
		FString Slots;
		for (const Crossguard::FGuildBankItem& Stored : Guild->Bank.Items)
		{
			Slots += (Slots.IsEmpty() ? TEXT("") : TEXT(",")) + FString::FromInt(Stored.SlotId);
		}
		FBankChange Change;
		Change.GoldDelta = static_cast<int32>(Guild->Bank.Gold);
		Change.Add = BankStacks;
		return SendBankOp(TEXT("leave"), Actor, Actor,
			{GuildField(TEXT("expected_bank"), FString::Printf(TEXT("{\"gold\":%lld,\"slot_ids\":[%s]}"), static_cast<long long>(Guild->Bank.Gold), *Slots))}, Change);
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
	if (bBackend)
	{
		if (Amount == 0)
		{
			return ECrossguardGuildResult::InvalidAmount;
		}
		if (Amount > 0 ? Actor->GetGold() < Amount : !Actor->CanReceiveGold(-static_cast<int64>(Amount)))
		{
			return Amount > 0 ? ECrossguardGuildResult::NotEnoughBankedGold : ECrossguardGuildResult::GoldOverflow;
		}
		FBankChange Change;
		Change.GoldDelta = -Amount;
		return SendBankOp(Amount > 0 ? TEXT("deposit_gold") : TEXT("withdraw_gold"), Actor, Actor,
			{GuildField(TEXT("amount"), FString::FromInt(FMath::Abs(Amount)))}, Change);
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
	if (bBackend)
	{
		FBankChange Change;
		Change.RemoveIds.Add(InventoryItemId);
		return SendBankOp(TEXT("deposit_item"), Actor, Actor, {GuildField(TEXT("item"), BuildItemJson(Item))}, Change);
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
	if (bBackend)
	{
		if (Amount <= 0)
		{
			return ECrossguardGuildResult::InvalidAmount;
		}
		FBankChange Change;
		Change.GoldDelta = Amount;
		// To oneself: a withdrawal (the rules tell them apart, as in memory).
		const bool bSelf = Recipient == Actor;
		TArray<TPair<FString, FString>> Fields = {GuildField(TEXT("amount"), FString::FromInt(Amount))};
		if (!bSelf)
		{
			Fields.Add(GuildField(TEXT("target_id"), FCrossguardNakamaClient::Quote(RecipientId)));
		}
		return SendBankOp(bSelf ? TEXT("withdraw_gold") : TEXT("give_gold"), Actor, Recipient, MoveTemp(Fields), Change);
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
	if (bBackend)
	{
		FBankChange Change;
		Change.Add.Add(*Item);
		const bool bSelf = Recipient == Actor;
		TArray<TPair<FString, FString>> Fields = {GuildField(TEXT("slot_id"), FString::FromInt(SlotId))};
		if (!bSelf)
		{
			Fields.Add(GuildField(TEXT("target_id"), FCrossguardNakamaClient::Quote(RecipientId)));
		}
		return SendBankOp(bSelf ? TEXT("withdraw_item") : TEXT("give_item"), Actor, Recipient, MoveTemp(Fields), Change);
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
	if (bBackend)
	{
		// The backend's list: from guilds this server may not know.
		if (const TArray<FCrossguardGuildInviteView>* Pending = BackendInvites.Find(PlayerId))
		{
			for (FCrossguardGuildInviteView Invite : *Pending)
			{
				const FString* Name = DisplayNames.Find(Invite.InvitedBy);
				Invite.InvitedBy = Name != nullptr ? *Name : Invite.InvitedBy;
				View->Invites.Add(Invite);
			}
		}
	}
	const double Time = Now();
	for (const auto& [Id, Other] : Registry.All())
	{
		if (bBackend)
		{
			// Listed above (the mirror holds only some guilds' invites).
			break;
		}
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
	const FString JoinedId = PlayerState != nullptr ? PlayerIdOf(PlayerState) : FString();
	if (bBackend && FCrossguardNakamaClient::IsUserId(JoinedId))
	{
		// Their guild comes from Nakama (the answer refreshes them and their guildmates here).
		DisplayNames.Add(JoinedId, PlayerState->GetPlayerName());
		FetchFromBackend({JoinedId});
	}
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

// --- Backend (Nakama) ------------------------------------------------------------------------------

// Named, not anonymous: unity builds put this file next to the party subsystem's.
namespace CrossguardGuildBackend
{
	const TCHAR* GuildRpc = TEXT("crossguard_guild");
	const TCHAR* GuildGetRpc = TEXT("crossguard_guild_get");

	Crossguard::EGuildRank RankFromName(const FString& Name)
	{
		return Name == TEXT("Leader") ? Crossguard::EGuildRank::Leader
			: Name == TEXT("Officer") ? Crossguard::EGuildRank::Officer : Crossguard::EGuildRank::Member;
	}

	bool IsUniqueItem(const FCrossguardInventoryItem& Item)
	{
		// Weapons, armor and containers never stack: they come back out by their own id.
		return Item.Category == ECrossguardItemCategory::Weapon || Item.Category == ECrossguardItemCategory::Armor
			|| Item.Category == ECrossguardItemCategory::Container;
	}
}

ECrossguardGuildResult UCrossguardGuildSubsystem::ResultFromName(const FString& Name)
{
	if (Name.IsEmpty() || Name.Contains(TEXT(":")) || Name.StartsWith(TEXT("<")))
	{
		return ECrossguardGuildResult::BackendUnavailable;
	}
	// The backend's names are EGuildError's (the same as ECrossguardGuildResult's first entries) and
	// BankChanged.
	const UEnum* Enum = StaticEnum<ECrossguardGuildResult>();
	int64 Value = Enum->GetValueByNameString(Name);
	if (Value == INDEX_NONE)
	{
		Value = Enum->GetValueByNameString(TEXT("ECrossguardGuildResult::") + Name);
	}
	return Value == INDEX_NONE ? ECrossguardGuildResult::BackendUnavailable : static_cast<ECrossguardGuildResult>(Value);
}

FString UCrossguardGuildSubsystem::BuildOpPayload(const FString& Op, const FString& ActorId, const FString& ServerId,
	const TArray<TPair<FString, FString>>& Fields)
{
	FString Payload = FString::Printf(TEXT("{\"op\":%s,\"actor_id\":%s,\"server_id\":%s"), *FCrossguardNakamaClient::Quote(Op),
		*FCrossguardNakamaClient::Quote(ActorId), *FCrossguardNakamaClient::Quote(ServerId));
	for (const TPair<FString, FString>& Field : Fields)
	{
		Payload += FString::Printf(TEXT(",%s:%s"), *FCrossguardNakamaClient::Quote(Field.Key), *Field.Value);
	}
	return Payload + TEXT("}");
}

FString UCrossguardGuildSubsystem::BuildItemJson(const FCrossguardInventoryItem& Item)
{
	const ECrossguardRarity Rarity = Item.Category == ECrossguardItemCategory::Weapon ? Item.Weapon.Rarity
		: Item.Category == ECrossguardItemCategory::Armor ? Item.Armor.Rarity : Item.Rarity;
	return FString::Printf(TEXT("{\"item_id\":%s,\"rarity\":%d,\"count\":%d,\"width\":%d,\"height\":%d,\"payload\":%s}"),
		*FCrossguardNakamaClient::Quote(DefinitionId(Item).ToString()), static_cast<int32>(Rarity), Item.Count, FMath::Max(Item.Size.X, 1),
		FMath::Max(Item.Size.Y, 1), *FCrossguardNakamaClient::Quote(UCrossguardSaveSubsystem::ItemToPayload(Item)));
}

void UCrossguardGuildSubsystem::RefuseLater(const FString& ActorId, ECrossguardGuildResult Result)
{
	ACrossguardPlayerState* Actor = FindOnline(GetServerWorld(), ActorId);
	if (UCrossguardGuildComponent* View = Actor != nullptr ? Actor->FindComponentByClass<UCrossguardGuildComponent>() : nullptr)
	{
		View->Refuse(Result);
	}
}

ECrossguardGuildResult UCrossguardGuildSubsystem::SendBackendOp(const TCHAR* Op, ACrossguardPlayerState* Actor, TArray<TPair<FString, FString>> Fields)
{
	const FString ActorId = PlayerIdOf(Actor);
	UCrossguardNakamaSubsystem* Nakama = GetGameInstance() != nullptr ? GetGameInstance()->GetSubsystem<UCrossguardNakamaSubsystem>() : nullptr;
	// Nakama knows players by their Nakama user id (a verified login).
	if (!FCrossguardNakamaClient::IsUserId(ActorId))
	{
		UE_LOG(LogCrossguard, Warning, TEXT("Guild %s by %s: guilds on this server need a Nakama login (%s)"), Op, *Actor->GetPlayerName(), *ActorId);
		return ECrossguardGuildResult::NoPlayer;
	}
	if (Nakama == nullptr)
	{
		return ECrossguardGuildResult::BackendUnavailable;
	}
	DisplayNames.Add(ActorId, Actor->GetPlayerName());
	const FString OpName = Op;
	TWeakObjectPtr<UCrossguardGuildSubsystem> WeakThis(this);
	Nakama->CallServerRpc(CrossguardGuildBackend::GuildRpc, BuildOpPayload(Op, ActorId, Nakama->GetServerId(), Fields),
		[WeakThis, OpName, ActorId](const FCrossguardNakamaResponse& Response)
		{
			UCrossguardGuildSubsystem* Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			if (!Response.IsOk())
			{
				UE_LOG(LogCrossguard, Warning, TEXT("Guild %s by %s: Nakama did not take it (%s)"), *OpName, *ActorId, *Response.Describe());
				Self->RefuseLater(ActorId, ECrossguardGuildResult::BackendUnavailable);
				return;
			}
			const ECrossguardGuildResult Result = ResultFromName(Self->ApplyBackendState(Response.Body));
			if (Result != ECrossguardGuildResult::None)
			{
				Self->RefuseLater(ActorId, Result);
			}
		});
	return ECrossguardGuildResult::None;
}

bool UCrossguardGuildSubsystem::ApplyBankChange(ACrossguardPlayerState* Player, const FBankChange& Change, FBankUndo& OutUndo)
{
	UCrossguardInventoryComponent* Inventory = InventoryOf(Player);
	if (Player == nullptr || ((!Change.RemoveIds.IsEmpty() || !Change.Add.IsEmpty()) && Inventory == nullptr))
	{
		return false;
	}
	if (Change.GoldDelta < 0 ? Player->GetGold() < -Change.GoldDelta : !Player->CanReceiveGold(Change.GoldDelta))
	{
		return false;
	}
	OutUndo = FBankUndo();
	if (!Change.RemoveIds.IsEmpty() || !Change.Add.IsEmpty())
	{
		TSet<int32> Before;
		for (const FCrossguardInventoryItem& Held : Inventory->GetItems())
		{
			Before.Add(Held.Id);
		}
		if (!Inventory->ExchangeItems(Change.RemoveIds, Change.Add, true, &OutUndo.Removed))
		{
			return false;
		}
		OutUndo.Added = Change.Add;
		for (const FCrossguardInventoryItem& Held : Inventory->GetItems())
		{
			if (!Before.Contains(Held.Id))
			{
				OutUndo.AddedIds.Add(Held.Id);
			}
		}
	}
	if (Change.GoldDelta > 0)
	{
		Player->AddGold(Change.GoldDelta);
	}
	else if (Change.GoldDelta < 0)
	{
		Player->SpendGold(-Change.GoldDelta);
	}
	OutUndo.GoldDelta = Change.GoldDelta;
	return true;
}

void UCrossguardGuildSubsystem::UndoBankChange(ACrossguardPlayerState* Player, const FBankUndo& Undo)
{
	if (Player == nullptr)
	{
		// Gone: their stored save is the one from before the operation, which Nakama kept.
		return;
	}
	if (Undo.GoldDelta > 0 && !Player->SpendGold(Undo.GoldDelta))
	{
		UE_LOG(LogCrossguard, Error, TEXT("Guild bank: could not take back %d gold from %s (spent meanwhile)"), Undo.GoldDelta, *Player->GetPlayerName());
	}
	else if (Undo.GoldDelta < 0)
	{
		Player->AddGold(-Undo.GoldDelta);
	}
	UCrossguardInventoryComponent* Inventory = InventoryOf(Player);
	if (Inventory == nullptr)
	{
		return;
	}
	// What was given goes again: unique items by their new id, stackables by count (they may have
	// joined a stack).
	for (const FCrossguardInventoryItem& Given : Undo.Added)
	{
		if (!CrossguardGuildBackend::IsUniqueItem(Given) && !Inventory->ConsumeItem(Given.ItemId, Given.Count))
		{
			UE_LOG(LogCrossguard, Error, TEXT("Guild bank: could not take back %d x %s from %s (used meanwhile)"), Given.Count, *Given.ItemId.ToString(),
				*Player->GetPlayerName());
		}
	}
	TArray<int32> Taken;
	for (const int32 AddedId : Undo.AddedIds)
	{
		FCrossguardInventoryItem Added;
		if (Inventory->FindItem(AddedId, Added) && CrossguardGuildBackend::IsUniqueItem(Added))
		{
			Taken.Add(AddedId);
		}
	}
	if (!Taken.IsEmpty() && !Inventory->ExchangeItems(Taken, {}, true))
	{
		UE_LOG(LogCrossguard, Error, TEXT("Guild bank: could not take back %d stack(s) from %s (in hand?)"), Taken.Num(), *Player->GetPlayerName());
	}
	// What was taken comes back.
	if (!Undo.Removed.IsEmpty() && !Inventory->ExchangeItems({}, Undo.Removed, true))
	{
		for (const FCrossguardInventoryItem& Removed : Undo.Removed)
		{
			const int32 Left = Inventory->AddItem(Removed);
			if (Left > 0)
			{
				UE_LOG(LogCrossguard, Error, TEXT("Guild bank: %d x %s of %s did not fit back after a refused deposit and are lost"), Left,
					*DefinitionId(Removed).ToString(), *Player->GetPlayerName());
			}
		}
	}
}

ECrossguardGuildResult UCrossguardGuildSubsystem::SendBankOp(const TCHAR* Op, ACrossguardPlayerState* Actor, ACrossguardPlayerState* SavePlayer,
	TArray<TPair<FString, FString>> Fields, const FBankChange& Change)
{
	const FString ActorId = PlayerIdOf(Actor);
	const FString SaveId = PlayerIdOf(SavePlayer);
	if (!FCrossguardNakamaClient::IsUserId(ActorId) || !FCrossguardNakamaClient::IsUserId(SaveId))
	{
		UE_LOG(LogCrossguard, Warning, TEXT("Guild %s by %s: guilds on this server need a Nakama login (%s / %s)"), Op, *Actor->GetPlayerName(), *ActorId,
			*SaveId);
		return ECrossguardGuildResult::NoPlayer;
	}
	if (BankBusy.Contains(ActorId) || BankBusy.Contains(SaveId))
	{
		return ECrossguardGuildResult::Busy;
	}
	UCrossguardNakamaSubsystem* Nakama = GetGameInstance()->GetSubsystem<UCrossguardNakamaSubsystem>();
	UCrossguardSaveSubsystem* Saves = GetGameInstance()->GetSubsystem<UCrossguardSaveSubsystem>();
	UCrossguardNakamaSaveBackend* SaveBackend = Saves != nullptr ? Cast<UCrossguardNakamaSaveBackend>(Saves->GetBackendObject()) : nullptr;
	if (Nakama == nullptr || SaveBackend == nullptr)
	{
		UE_LOG(LogCrossguard, Error, TEXT("Guild %s by %s: the bank needs the Nakama save backend (-SaveBackend=Nakama)"), Op, *ActorId);
		return ECrossguardGuildResult::BackendUnavailable;
	}

	// The change happens here now; the save that holds it goes to Nakama with the bank's side.
	FBankUndo Undo;
	if (!ApplyBankChange(SavePlayer, Change, Undo))
	{
		return ECrossguardGuildResult::NoRoomInInventory;
	}
	FString SaveKey;
	FString Document;
	if (!Saves->BuildSaveDocument(SavePlayer, SaveKey, Document) || SaveKey != SaveId)
	{
		UndoBankChange(SavePlayer, Undo);
		UE_LOG(LogCrossguard, Warning, TEXT("Guild %s by %s: %s's save is not ready on this server"), Op, *ActorId, *SaveId);
		return ECrossguardGuildResult::BackendUnavailable;
	}
	// A retry of this very request (no answer) is recognised by Nakama and never applied twice.
	Fields.Add(GuildField(TEXT("request_id"), FCrossguardNakamaClient::Quote(FGuid::NewGuid().ToString(EGuidFormats::Digits))));
	const FString OpName = Op;
	const FString ServerId = Nakama->GetServerId();
	TWeakObjectPtr<UCrossguardGuildSubsystem> WeakThis(this);
	FString Error;
	const bool bSent = SaveBackend->SendWithSave(SaveId, Document, CrossguardGuildBackend::GuildRpc,
		[OpName, ActorId, ServerId, Fields](const FString& SaveMember)
		{
			TArray<TPair<FString, FString>> WithSave = Fields;
			WithSave.Add(GuildField(TEXT("save"), SaveMember));
			return BuildOpPayload(OpName, ActorId, ServerId, WithSave);
		},
		[WeakThis, OpName, ActorId, SaveId, Undo](const FCrossguardNakamaResponse& Response, bool bSaveWritten)
		{
			UCrossguardGuildSubsystem* Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->BankBusy.Remove(ActorId);
			Self->BankBusy.Remove(SaveId);
			const FString ResultName = Response.IsOk() ? Self->ApplyBackendState(Response.Body) : FString();
			if (bSaveWritten)
			{
				return;
			}
			// Not written (refused, or no answer): the player gets back what they had, and saves that.
			ACrossguardPlayerState* SavePlayerNow = Self->FindOnline(Self->GetServerWorld(), SaveId);
			UndoBankChange(SavePlayerNow, Undo);
			if (UCrossguardSaveSubsystem* SavesNow = Self->GetGameInstance()->GetSubsystem<UCrossguardSaveSubsystem>(); SavesNow != nullptr && SavePlayerNow != nullptr)
			{
				SavesNow->SaveNow(SavePlayerNow);
			}
			if (!Response.IsOk())
			{
				UE_LOG(LogCrossguard, Warning, TEXT("Guild %s by %s: not written (%s)"), *OpName, *ActorId, *Response.Describe());
				Self->RefuseLater(ActorId, ECrossguardGuildResult::BackendUnavailable);
			}
			else if (ResultFromName(ResultName) != ECrossguardGuildResult::None)
			{
				Self->RefuseLater(ActorId, ResultFromName(ResultName));
			}
			Self->RefreshPlayer(SavePlayerNow);
		},
		Error);
	if (!bSent)
	{
		UndoBankChange(SavePlayer, Undo);
		UE_LOG(LogCrossguard, Warning, TEXT("Guild %s by %s: not sent (%s)"), Op, *ActorId, *Error);
		return ECrossguardGuildResult::BackendUnavailable;
	}
	BankBusy.Add(ActorId);
	BankBusy.Add(SaveId);
	return ECrossguardGuildResult::None;
}

void UCrossguardGuildSubsystem::FetchFromBackend(const TArray<FString>& PlayerIds)
{
	UCrossguardNakamaSubsystem* Nakama = GetGameInstance() != nullptr ? GetGameInstance()->GetSubsystem<UCrossguardNakamaSubsystem>() : nullptr;
	if (Nakama == nullptr || PlayerIds.IsEmpty())
	{
		return;
	}
	FString Users;
	for (const FString& PlayerId : PlayerIds)
	{
		Users += (Users.IsEmpty() ? TEXT("") : TEXT(",")) + FCrossguardNakamaClient::Quote(PlayerId);
	}
	TWeakObjectPtr<UCrossguardGuildSubsystem> WeakThis(this);
	Nakama->CallServerRpc(CrossguardGuildBackend::GuildGetRpc, FString::Printf(TEXT("{\"user_ids\":[%s]}"), *Users),
		[WeakThis](const FCrossguardNakamaResponse& Response)
		{
			UCrossguardGuildSubsystem* Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			if (!Response.IsOk())
			{
				UE_LOG(LogCrossguard, Warning, TEXT("Guilds: could not read them from Nakama (%s)"), *Response.Describe());
				return;
			}
			Self->ApplyBackendState(Response.Body);
		});
}

bool UCrossguardGuildSubsystem::RefreshTick(float DeltaSeconds)
{
	const UWorld* World = GetServerWorld();
	const AGameStateBase* GameState = World != nullptr ? World->GetGameState() : nullptr;
	if (GameState == nullptr || World->GetNetMode() == NM_Client)
	{
		return true;
	}
	TArray<FString> Ids;
	for (const APlayerState* PlayerState : GameState->PlayerArray)
	{
		const FString PlayerId = PlayerIdOf(PlayerState);
		if (FCrossguardNakamaClient::IsUserId(PlayerId))
		{
			Ids.AddUnique(PlayerId);
		}
	}
	// The backend takes 256 at a time; a hub holds fewer.
	if (Ids.Num() > 256)
	{
		Ids.SetNum(256);
	}
	FetchFromBackend(Ids);
	return true;
}

void UCrossguardGuildSubsystem::ForgetMirroredGuild(const FString& GuildId)
{
	Registry.RemoveGuild(ToStd(GuildId));
	const FString Prefix = GuildId + TEXT("/");
	for (auto It = BankItemData.CreateIterator(); It; ++It)
	{
		if (It.Key().StartsWith(Prefix))
		{
			It.RemoveCurrent();
		}
	}
}

bool UCrossguardGuildSubsystem::ParseGuildView(const FJsonObject& View, Crossguard::FGuild& OutGuild, TMap<int32, FString>& OutPayloads)
{
	Crossguard::FGuild Guild;
	FString Text;
	if (!View.TryGetStringField(TEXT("id"), Text) || Text.IsEmpty())
	{
		return false;
	}
	Guild.Id = ToStd(Text);
	View.TryGetStringField(TEXT("name"), Text);
	Guild.Name = ToStd(Text);
	const TArray<TSharedPtr<FJsonValue>>* Members = nullptr;
	if (!View.TryGetArrayField(TEXT("members"), Members) || Members == nullptr)
	{
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Members)
	{
		const TSharedPtr<FJsonObject>* Member = nullptr;
		FString UserId;
		FString Rank;
		if (!Value.IsValid() || !Value->TryGetObject(Member) || Member == nullptr || !(*Member)->TryGetStringField(TEXT("user_id"), UserId))
		{
			return false;
		}
		(*Member)->TryGetStringField(TEXT("rank"), Rank);
		Guild.Members.push_back({ToStd(UserId), CrossguardGuildBackend::RankFromName(Rank)});
	}
	const TArray<TSharedPtr<FJsonValue>>* Invites = nullptr;
	if (View.TryGetArrayField(TEXT("invites"), Invites) && Invites != nullptr)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Invites)
		{
			const TSharedPtr<FJsonObject>* Invite = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Invite) || Invite == nullptr)
			{
				continue;
			}
			FString UserId;
			FString InvitedBy;
			double ExpiresAtMs = 0.0;
			(*Invite)->TryGetStringField(TEXT("user_id"), UserId);
			(*Invite)->TryGetStringField(TEXT("invited_by"), InvitedBy);
			(*Invite)->TryGetNumberField(TEXT("expires_at_ms"), ExpiresAtMs);
			// Now() is Unix seconds too.
			Guild.Invites.push_back({ToStd(UserId), ToStd(InvitedBy), ExpiresAtMs / 1000.0});
		}
	}
	const TSharedPtr<FJsonObject>* Bank = nullptr;
	if (!View.TryGetObjectField(TEXT("bank"), Bank) || Bank == nullptr || !Bank->IsValid())
	{
		return false;
	}
	FString Gold;
	int32 Width = 0;
	int32 Height = 0;
	int32 NextSlotId = 1;
	(*Bank)->TryGetStringField(TEXT("gold"), Gold);
	(*Bank)->TryGetNumberField(TEXT("width"), Width);
	(*Bank)->TryGetNumberField(TEXT("height"), Height);
	(*Bank)->TryGetNumberField(TEXT("next_slot_id"), NextSlotId);
	// An int64 as text (JSON numbers are doubles here).
	Guild.Bank.Gold = FCString::Atoi64(*Gold);
	Guild.Bank.NextSlotId = NextSlotId;
	Guild.Bank.Grid = Crossguard::FInventoryGrid(Crossguard::FGridSize{Width, Height});
	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if ((*Bank)->TryGetArrayField(TEXT("items"), Items) && Items != nullptr)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Items)
		{
			const TSharedPtr<FJsonObject>* Item = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Item) || Item == nullptr)
			{
				return false;
			}
			Crossguard::FGuildBankItem Stored;
			FString ItemId;
			FString Payload;
			int32 Rarity = 0;
			int32 X = 0;
			int32 Y = 0;
			bool bRotated = false;
			(*Item)->TryGetNumberField(TEXT("slot_id"), Stored.SlotId);
			(*Item)->TryGetStringField(TEXT("item_id"), ItemId);
			(*Item)->TryGetNumberField(TEXT("rarity"), Rarity);
			(*Item)->TryGetNumberField(TEXT("count"), Stored.Count);
			(*Item)->TryGetNumberField(TEXT("width"), Stored.Size.Width);
			(*Item)->TryGetNumberField(TEXT("height"), Stored.Size.Height);
			(*Item)->TryGetNumberField(TEXT("x"), X);
			(*Item)->TryGetNumberField(TEXT("y"), Y);
			(*Item)->TryGetBoolField(TEXT("rotated"), bRotated);
			(*Item)->TryGetStringField(TEXT("payload"), Payload);
			Stored.ItemId = ToStd(ItemId);
			Stored.Rarity = static_cast<Crossguard::ERarity>(FMath::Clamp(Rarity, 0, Crossguard::RarityCount - 1));
			Stored.Payload = ToStd(Payload);
			if (!Guild.Bank.Grid.Place(Stored.SlotId, Stored.Size, Crossguard::FGridPlacement{X, Y, bRotated}))
			{
				return false;
			}
			Guild.Bank.Items.push_back(Stored);
			OutPayloads.Add(Stored.SlotId, Payload);
		}
	}
	// The bank log stays the backend's (crossguard_guild_log); the game shows none yet.
	OutGuild = MoveTemp(Guild);
	return true;
}

FString UCrossguardGuildSubsystem::ApplyBackendState(const FString& Body)
{
	TSharedPtr<FJsonObject> Object;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Object) || !Object.IsValid())
	{
		UE_LOG(LogCrossguard, Warning, TEXT("Guilds: unreadable answer from Nakama: %s"), *Body.Left(200));
		return TEXT("<unreadable>");
	}
	FString Result;
	Object->TryGetStringField(TEXT("result"), Result);

	// Who to bring up to date afterwards: the players in the answer, and everyone in a guild it
	// touches (before and after).
	TSet<FString> Touched;
	const auto TouchGuild = [this, &Touched](const std::string& GuildId)
	{
		if (const Crossguard::FGuild* Guild = Registry.Find(GuildId))
		{
			for (const Crossguard::FGuildMember& Member : Guild->Members)
			{
				Touched.Add(ToFString(Member.PlayerId));
			}
		}
	};

	bool bDisbanded = false;
	FString AnswerGuildId;
	Object->TryGetBoolField(TEXT("disbanded"), bDisbanded);
	Object->TryGetStringField(TEXT("guild_id"), AnswerGuildId);
	if (bDisbanded && !AnswerGuildId.IsEmpty())
	{
		TouchGuild(ToStd(AnswerGuildId));
		ForgetMirroredGuild(AnswerGuildId);
	}

	const TSharedPtr<FJsonObject>* Guilds = nullptr;
	if (Object->TryGetObjectField(TEXT("guilds"), Guilds) && Guilds != nullptr && Guilds->IsValid())
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Guilds)->Values)
		{
			const TSharedPtr<FJsonObject>* View = nullptr;
			Crossguard::FGuild Guild;
			TMap<int32, FString> Payloads;
			if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(View) || View == nullptr || !View->IsValid() || !ParseGuildView(**View, Guild, Payloads))
			{
				UE_LOG(LogCrossguard, Warning, TEXT("Guilds: unreadable guild %s from Nakama"), *Pair.Key);
				continue;
			}
			TouchGuild(Guild.Id);
			// Every stack's game data again (affixes...): a stack this game cannot read is not offered.
			ForgetMirroredGuild(Pair.Key);
			const Crossguard::EGuildError Error = Registry.Mirror(Guild);
			if (Error != Crossguard::EGuildError::None)
			{
				UE_LOG(LogCrossguard, Warning, TEXT("Guilds: guild %s from Nakama not mirrored (%s)"), *Pair.Key,
					*StaticEnum<ECrossguardGuildResult>()->GetNameStringByValue(static_cast<int64>(ToResult(Error))));
				continue;
			}
			for (const TPair<int32, FString>& Payload : Payloads)
			{
				FCrossguardInventoryItem Item;
				if (UCrossguardSaveSubsystem::ItemFromPayload(Payload.Value, Item))
				{
					BankItemData.Add(BankKey(Guild.Id, Payload.Key), Item);
				}
				else
				{
					UE_LOG(LogCrossguard, Error, TEXT("Guilds: bank stack %d of %s has no readable item data"), Payload.Key, *Pair.Key);
				}
			}
			TouchGuild(Guild.Id);
		}
	}

	const TSharedPtr<FJsonObject>* Players = nullptr;
	if (Object->TryGetObjectField(TEXT("players"), Players) && Players != nullptr && Players->IsValid())
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Players)->Values)
		{
			const TSharedPtr<FJsonObject>* Player = nullptr;
			if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(Player) || Player == nullptr || !Player->IsValid())
			{
				continue;
			}
			Touched.Add(Pair.Key);
			FString GuildId;
			(*Player)->TryGetStringField(TEXT("guild_id"), GuildId);
			if (GuildId.IsEmpty())
			{
				// In no guild (left on another hub, kicked): the guild here forgets them.
				if (const Crossguard::FGuild* Old = Registry.GuildOf(ToStd(Pair.Key)))
				{
					TouchGuild(Old->Id);
				}
				Registry.DropPlayer(ToStd(Pair.Key));
			}
			TArray<FCrossguardGuildInviteView>& Pending = BackendInvites.FindOrAdd(Pair.Key);
			Pending.Reset();
			const TArray<TSharedPtr<FJsonValue>>* InviteValues = nullptr;
			if ((*Player)->TryGetArrayField(TEXT("invites"), InviteValues) && InviteValues != nullptr)
			{
				for (const TSharedPtr<FJsonValue>& Value : *InviteValues)
				{
					const TSharedPtr<FJsonObject>* Invite = nullptr;
					if (!Value.IsValid() || !Value->TryGetObject(Invite) || Invite == nullptr)
					{
						continue;
					}
					FCrossguardGuildInviteView& Out = Pending.AddDefaulted_GetRef();
					(*Invite)->TryGetStringField(TEXT("guild_id"), Out.GuildId);
					(*Invite)->TryGetStringField(TEXT("guild_name"), Out.GuildName);
					// The inviter's id; RefreshPlayer shows their name when known.
					(*Invite)->TryGetStringField(TEXT("invited_by"), Out.InvitedBy);
				}
			}
		}
	}

	const UWorld* World = GetServerWorld();
	for (const FString& PlayerId : Touched)
	{
		if (ACrossguardPlayerState* Online = FindOnline(World, PlayerId))
		{
			RefreshPlayer(Online);
		}
	}
	return Result;
}
