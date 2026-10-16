#include "Game/CrossguardSaveSubsystem.h"

#include "Character/CrossguardCharacter.h"
#include "CoreGlobals.h"
#include "Crossguard.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Game/CrossguardGuildSubsystem.h"
#include "Game/CrossguardLocalSaveBackend.h"
#include "Game/CrossguardPlayerState.h"
#include "Items/CrossguardEquipmentComponent.h"
#include "Items/CrossguardInventoryComponent.h"
#include "Items/CrossguardStashComponent.h"
#include "Misc/DateTime.h"
#include "Skills/CrossguardLoadoutComponent.h"
#include "Skills/CrossguardSkillCatalog.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static_assert(static_cast<int>(ECrossguardItemCategory::Other) == static_cast<int>(Crossguard::EItemCategory::Other),
	"ECrossguardItemCategory must mirror Crossguard::EItemCategory");
static_assert(static_cast<int>(ECrossguardRarity::Mystic) == static_cast<int>(Crossguard::ERarity::Mystic),
	"ECrossguardRarity must mirror Crossguard::ERarity");
static_assert(static_cast<int>(ECrossguardInventoryContainer::Quiver) == static_cast<int>(Crossguard::EContainerKind::Quiver),
	"ECrossguardInventoryContainer must mirror Crossguard::EContainerKind");

// File-local helpers in a named namespace: the module builds as a unity build.
namespace CrossguardSaveDetail
{
	std::string ToUtf8(const FString& Value)
	{
		return std::string(TCHAR_TO_UTF8(*Value));
	}

	FString FromUtf8(const std::string& Value)
	{
		return FString(UTF8_TO_TCHAR(Value.c_str()));
	}

	ACrossguardCharacter* CharacterOf(const ACrossguardPlayerState* PlayerState)
	{
		return PlayerState != nullptr ? PlayerState->GetPawn<ACrossguardCharacter>() : nullptr;
	}

	// Items whose size and stack come from the item table (UCrossguardInventoryComponent's rule).
	bool IsTableCategory(ECrossguardItemCategory Category)
	{
		return Category == ECrossguardItemCategory::Ammo || Category == ECrossguardItemCategory::Consumable
			|| Category == ECrossguardItemCategory::Throwable || Category == ECrossguardItemCategory::Material
			|| Category == ECrossguardItemCategory::Currency || Category == ECrossguardItemCategory::Trinket
			|| Category == ECrossguardItemCategory::Tool;
	}

	// The game stores affix values as floats: write the shortest decimal that reads back as the same
	// float (0.03, not 0.029999999329447746).
	double TidyFloat(float Value)
	{
		for (int Digits = 6; Digits <= 9; ++Digits)
		{
			char Buffer[32];
			std::snprintf(Buffer, sizeof(Buffer), "%.*g", Digits, static_cast<double>(Value));
			const double Candidate = std::strtod(Buffer, nullptr);
			if (static_cast<float>(Candidate) == Value)
			{
				return Candidate;
			}
		}
		return static_cast<double>(Value);
	}

	// Backpacks and Quivers have no item id: the save names them by kind.
	const TCHAR* ContainerKindId(ECrossguardInventoryContainer Kind)
	{
		return Kind == ECrossguardInventoryContainer::Quiver ? TEXT("quiver") : TEXT("backpack");
	}

	std::vector<Crossguard::FRolledAffix> ToSavedAffixes(const TArray<FCrossguardAffix>& Affixes)
	{
		std::vector<Crossguard::FRolledAffix> Saved;
		for (const FCrossguardAffix& Affix : Affixes)
		{
			Saved.push_back({ToUtf8(Affix.AffixId.ToString()), TidyFloat(Affix.Value)});
		}
		return Saved;
	}

	TArray<FCrossguardAffix> ToGameAffixes(const std::vector<Crossguard::FRolledAffix>& Affixes)
	{
		TArray<FCrossguardAffix> Game;
		for (const Crossguard::FRolledAffix& Affix : Affixes)
		{
			FCrossguardAffix& Added = Game.AddDefaulted_GetRef();
			Added.AffixId = FName(*FromUtf8(Affix.Id));
			Added.Value = static_cast<float>(Affix.Value);
		}
		return Game;
	}

	Crossguard::FSavedItem ToSaved(const FCrossguardInventoryItem& Item)
	{
		Crossguard::FSavedItem Saved;
		Saved.Category = static_cast<Crossguard::EItemCategory>(static_cast<uint8>(Item.Category));
		ECrossguardRarity Rarity = Item.Rarity;
		FName Id = Item.ItemId;
		switch (Item.Category)
		{
		case ECrossguardItemCategory::Weapon:
			Id = Item.Weapon.WeaponId;
			Rarity = Item.Weapon.Rarity;
			Saved.Affixes = ToSavedAffixes(Item.Weapon.Affixes);
			break;
		case ECrossguardItemCategory::Armor:
			Id = Item.Armor.ArmorId;
			Rarity = Item.Armor.Rarity;
			Saved.Affixes = ToSavedAffixes(Item.Armor.Affixes);
			break;
		case ECrossguardItemCategory::Container:
			Saved.ContainerKind = static_cast<Crossguard::EContainerKind>(static_cast<uint8>(Item.ContainerKind));
			if (Id.IsNone())
			{
				Id = FName(ContainerKindId(Item.ContainerKind));
			}
			break;
		default:
			break;
		}
		Saved.ItemId = Id.IsNone() ? std::string() : ToUtf8(Id.ToString());
		Saved.Rarity = static_cast<Crossguard::ERarity>(static_cast<uint8>(Rarity));
		Saved.Count = Item.Count;
		Saved.Size = {Item.Size.X, Item.Size.Y};
		Saved.Placement = {Item.Position.X, Item.Position.Y, Item.bRotated};
		Saved.Grid = static_cast<Crossguard::EContainerKind>(static_cast<uint8>(Item.Container));
		return Saved;
	}

	FCrossguardInventoryItem ToGame(const Crossguard::FSavedItem& Saved)
	{
		FCrossguardInventoryItem Item;
		Item.Category = static_cast<ECrossguardItemCategory>(static_cast<uint8>(Saved.Category));
		Item.Rarity = static_cast<ECrossguardRarity>(static_cast<uint8>(Saved.Rarity));
		const FName Id(*FromUtf8(Saved.ItemId));
		switch (Item.Category)
		{
		case ECrossguardItemCategory::Weapon:
			Item.Weapon.WeaponId = Id;
			Item.Weapon.Rarity = Item.Rarity;
			Item.Weapon.Affixes = ToGameAffixes(Saved.Affixes);
			break;
		case ECrossguardItemCategory::Armor:
			Item.Armor.ArmorId = Id;
			Item.Armor.Rarity = Item.Rarity;
			Item.Armor.Affixes = ToGameAffixes(Saved.Affixes);
			break;
		case ECrossguardItemCategory::Container:
			Item.ContainerKind = static_cast<ECrossguardInventoryContainer>(static_cast<uint8>(Saved.ContainerKind));
			Item.ItemId = Id == FName(ContainerKindId(Item.ContainerKind)) ? NAME_None : Id;
			break;
		default:
			Item.ItemId = Id;
			break;
		}
		Item.Count = Saved.Count;
		Item.Size = FIntPoint(Saved.Size.Width, Saved.Size.Height);
		Item.Position = FIntPoint(Saved.Placement.X, Saved.Placement.Y);
		Item.bRotated = Saved.Placement.bRotated;
		Item.Container = static_cast<ECrossguardInventoryContainer>(static_cast<uint8>(Saved.Grid));
		Item.bWasEquipped = Saved.Worn != Crossguard::ESavedWorn::None;
		Item.WornSlot = Saved.Worn == Crossguard::ESavedWorn::OffHand ? ECrossguardEquipmentSlot::OffHand : ECrossguardEquipmentSlot::MainHand;
		Item.WornSet = Saved.WornSet;
		return Item;
	}

	// Size and stack size from the character's data tables. False if the game has no such item.
	bool DescribeItem(const UCrossguardInventoryComponent& Inventory, const FCrossguardInventoryItem& Item, FIntPoint& OutSize, int32& OutMaxStack)
	{
		OutSize = Inventory.GetItemSize(Item);
		OutMaxStack = 1;
		if (OutSize.X <= 0 || OutSize.Y <= 0)
		{
			return false;
		}
		const FCrossguardItemRow* Row = IsTableCategory(Item.Category) && Inventory.ItemTable != nullptr
			? Inventory.ItemTable->FindRow<FCrossguardItemRow>(Item.ItemId, TEXT("Save"), false)
			: nullptr;
		if (Row != nullptr)
		{
			OutMaxStack = FMath::Max(1, Row->MaxStack);
		}
		return true;
	}

	void SnapshotCarried(const ACrossguardCharacter& Character, std::vector<Crossguard::FSavedItem>& Out)
	{
		const UCrossguardEquipmentComponent* Equipment = Character.GetEquipmentComponent();
		const UCrossguardInventoryComponent* Inventory = Character.GetInventoryComponent();
		const auto AddWorn = [&Out, Inventory](FCrossguardInventoryItem Item, Crossguard::ESavedWorn Worn, int32 Set)
		{
			if (Inventory != nullptr)
			{
				Item.Size = Inventory->GetItemSize(Item);
			}
			Crossguard::FSavedItem Saved = ToSaved(Item);
			Saved.Worn = Worn;
			Saved.WornSet = Set;
			Saved.Placement = Crossguard::FGridPlacement();
			Saved.Grid = Crossguard::EContainerKind::Main;
			Out.push_back(Saved);
		};

		if (Equipment != nullptr)
		{
			for (int32 Set = 0; Set < Crossguard::WeaponSetCount; ++Set)
			{
				for (const ECrossguardEquipmentSlot Slot : {ECrossguardEquipmentSlot::MainHand, ECrossguardEquipmentSlot::OffHand})
				{
					const FCrossguardWeaponItem& Weapon = Equipment->GetWeaponInSet(Set, Slot);
					if (!Weapon.IsEmpty())
					{
						FCrossguardInventoryItem Item;
						Item.Category = ECrossguardItemCategory::Weapon;
						Item.Weapon = Weapon;
						AddWorn(Item, Slot == ECrossguardEquipmentSlot::OffHand ? Crossguard::ESavedWorn::OffHand : Crossguard::ESavedWorn::MainHand, Set);
					}
				}
			}
			for (uint8 SlotIndex = 0; SlotIndex < static_cast<uint8>(Crossguard::ArmorSlotCount); ++SlotIndex)
			{
				const FCrossguardArmorItem& Armor = Equipment->GetArmorItem(static_cast<ECrossguardArmorSlot>(SlotIndex));
				if (!Armor.IsEmpty())
				{
					FCrossguardInventoryItem Item;
					Item.Category = ECrossguardItemCategory::Armor;
					Item.Armor = Armor;
					AddWorn(Item, Crossguard::ESavedWorn::Armor, 0);
				}
			}
		}
		if (Inventory != nullptr)
		{
			for (const ECrossguardInventoryContainer Kind : {ECrossguardInventoryContainer::Backpack, ECrossguardInventoryContainer::Quiver})
			{
				const FCrossguardInventoryItem& Container = Inventory->GetEquippedContainer(Kind);
				if (!Container.IsEmpty())
				{
					AddWorn(Container, Crossguard::ESavedWorn::Container, 0);
				}
			}
			for (const FCrossguardInventoryItem& Item : Inventory->GetItems())
			{
				Out.push_back(ToSaved(Item));
			}
		}
	}

	Crossguard::FSaveValidationRules RulesFor(const ACrossguardPlayerState& PlayerState, const ACrossguardCharacter* Character)
	{
		Crossguard::FSaveValidationRules Rules;
		Rules.Progression = PlayerState.GetProgressionConfig();
		const UCrossguardInventoryComponent* Inventory = Character != nullptr ? Character->GetInventoryComponent() : nullptr;
		if (Inventory != nullptr)
		{
			Rules.LookUpItem = [Inventory](const Crossguard::FSavedItem& Saved, Crossguard::FSavedItemInfo& OutInfo)
			{
				FIntPoint Size;
				int32 MaxStack = 1;
				if (!DescribeItem(*Inventory, ToGame(Saved), Size, MaxStack))
				{
					return false;
				}
				OutInfo.Size = {Size.X, Size.Y};
				OutInfo.MaxStack = MaxStack;
				return true;
			};
		}
		const UCrossguardLoadoutComponent* Loadout = Character != nullptr ? Character->GetLoadoutComponent() : nullptr;
		const UCrossguardSkillCatalog* Catalog = Loadout != nullptr ? Loadout->SkillCatalog.Get() : nullptr;
		if (Catalog != nullptr)
		{
			Rules.IsKnownSkill = [Catalog](const std::string& SkillId) { return Catalog->FindSkill(FName(*FromUtf8(SkillId))) != nullptr; };
		}
		return Rules;
	}
}

// --- Watcher --------------------------------------------------------------------------------------

void UCrossguardSaveWatcher::Watch(UCrossguardSaveSubsystem* InSubsystem, ACrossguardPlayerState* InPlayerState)
{
	Subsystem = InSubsystem;
	if (InPlayerState == nullptr)
	{
		return;
	}
	if (PlayerState.Get() != InPlayerState)
	{
		PlayerState = InPlayerState;
		InPlayerState->OnProgressionChanged.AddUniqueDynamic(this, &UCrossguardSaveWatcher::HandleChange);
		if (UCrossguardStashComponent* Stash = InPlayerState->GetStash())
		{
			Stash->OnStashChanged.AddUniqueDynamic(this, &UCrossguardSaveWatcher::HandleChange);
		}
	}
	ACrossguardCharacter* Character = CrossguardSaveDetail::CharacterOf(InPlayerState);
	if (Character != nullptr && WatchedPawn.Get() != Character)
	{
		WatchedPawn = Character;
		if (UCrossguardInventoryComponent* Inventory = Character->GetInventoryComponent())
		{
			Inventory->OnInventoryChanged.AddUniqueDynamic(this, &UCrossguardSaveWatcher::HandleChange);
		}
		if (UCrossguardEquipmentComponent* Equipment = Character->GetEquipmentComponent())
		{
			Equipment->OnEquipmentChanged.AddUniqueDynamic(this, &UCrossguardSaveWatcher::HandleChange);
		}
		if (UCrossguardLoadoutComponent* Loadout = Character->GetLoadoutComponent())
		{
			Loadout->OnLoadoutChanged.AddUniqueDynamic(this, &UCrossguardSaveWatcher::HandleChange);
		}
	}
}

void UCrossguardSaveWatcher::HandleChange()
{
	UCrossguardSaveSubsystem* Saves = Subsystem.Get();
	ACrossguardPlayerState* Watched = PlayerState.Get();
	if (Saves != nullptr && Watched != nullptr && !Saves->IsApplying())
	{
		Saves->MarkDirty(Watched);
	}
}

// --- Subsystem ------------------------------------------------------------------------------------

UCrossguardSaveSubsystem* UCrossguardSaveSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject != nullptr ? WorldContextObject->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
	return GameInstance != nullptr ? GameInstance->GetSubsystem<UCrossguardSaveSubsystem>() : nullptr;
}

void UCrossguardSaveSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	BackendObject = NewObject<UCrossguardLocalSaveBackend>(this);
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &UCrossguardSaveSubsystem::HandleWorldCleanup);
}

void UCrossguardSaveSubsystem::Deinitialize()
{
	WriteDirty();
	FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
	Players.Reset();
	Watchers.Reset();
	Super::Deinitialize();
}

void UCrossguardSaveSubsystem::HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	// Map change or shutdown: nothing waiting may be lost.
	if (World != nullptr && World->GetGameInstance() == GetGameInstance())
	{
		FlushPendingSaves();
	}
}

bool UCrossguardSaveSubsystem::IsSavingEnabled() const
{
	// Automation tests never touch real saves unless a test asks for it (EnableForTesting).
	return bSavingEnabled && (!GIsAutomationTesting || bEnabledForTesting);
}

ICrossguardSaveBackend* UCrossguardSaveSubsystem::GetBackend() const
{
	return Cast<ICrossguardSaveBackend>(BackendObject.Get());
}

void UCrossguardSaveSubsystem::SetBackend(UObject* NewBackend)
{
	if (Cast<ICrossguardSaveBackend>(NewBackend) == nullptr)
	{
		UE_LOG(LogCrossguard, Error, TEXT("SetBackend: %s does not implement ICrossguardSaveBackend; kept %s"), *GetNameSafe(NewBackend),
			*GetNameSafe(BackendObject.Get()));
		return;
	}
	BackendObject = NewBackend;
}

void UCrossguardSaveSubsystem::EnableForTesting(const FString& Directory)
{
	UCrossguardLocalSaveBackend* Local = NewObject<UCrossguardLocalSaveBackend>(this);
	Local->SetDirectory(Directory);
	BackendObject = Local;
	bEnabledForTesting = true;
	bSaveSessionIds = true;
	Players.Reset();
	Watchers.Reset();
}

void UCrossguardSaveSubsystem::DisableForTesting()
{
	bEnabledForTesting = false;
	Players.Reset();
	Watchers.Reset();
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		GameInstance->GetTimerManager().ClearTimer(FlushTimer);
	}
	BackendObject = NewObject<UCrossguardLocalSaveBackend>(this);
}

FString UCrossguardSaveSubsystem::PlayerIdFor(const ACrossguardPlayerState* PlayerState) const
{
	if (PlayerState == nullptr || !PlayerState->HasAuthority() || !IsSavingEnabled())
	{
		return FString();
	}
	const UWorld* World = PlayerState->GetWorld();
	if (World == nullptr || World->GetNetMode() == NM_Client)
	{
		return FString();
	}
	const FString PlayerId = UCrossguardGuildSubsystem::PlayerIdOf(PlayerState);
	if (!bSaveSessionIds && !PlayerState->GetUniqueId().IsValid())
	{
		return FString();
	}
	return PlayerId;
}

ECrossguardSaveState UCrossguardSaveSubsystem::GetSaveState(const ACrossguardPlayerState* PlayerState) const
{
	const FPlayerEntry* Entry = Players.Find(PlayerIdFor(PlayerState));
	return Entry != nullptr ? Entry->State : ECrossguardSaveState::None;
}

bool UCrossguardSaveSubsystem::CanSave(const ACrossguardPlayerState* PlayerState, const FPlayerEntry*& OutEntry) const
{
	OutEntry = Players.Find(PlayerIdFor(PlayerState));
	// Only once the loaded save is on this very player state: saving earlier would overwrite it
	// with a fresh character.
	return OutEntry != nullptr && OutEntry->State == ECrossguardSaveState::Ready && OutEntry->AppliedTo.Get() == PlayerState
		&& GetBackend() != nullptr;
}

// --- Loading --------------------------------------------------------------------------------------

void UCrossguardSaveSubsystem::PlayerJoined(ACrossguardPlayerState* PlayerState)
{
	const FString PlayerId = PlayerIdFor(PlayerState);
	if (PlayerId.IsEmpty())
	{
		return;
	}
	FPlayerEntry& Entry = Players.FindOrAdd(PlayerId);
	if (Entry.PlayerState.Get() == PlayerState && Entry.State != ECrossguardSaveState::None)
	{
		// Already loading or loaded for this player state.
		return;
	}
	Entry.PlayerState = PlayerState;
	Entry.AppliedTo = nullptr;
	Entry.bReady = false;
	if (Entry.bHasLast && Entry.State == ECrossguardSaveState::Ready)
	{
		// Seamless travel: a new player state for a player still on this server. The latest
		// record (never older than the stored one) goes onto it.
		Entry.Pending = Entry.Last;
		Entry.bHasPending = true;
		return;
	}
	StartLoad(PlayerId, false);
}

void UCrossguardSaveSubsystem::StartLoad(const FString& PlayerId, bool bForceReload)
{
	ICrossguardSaveBackend* Backend = GetBackend();
	FPlayerEntry* Entry = Players.Find(PlayerId);
	if (Backend == nullptr || Entry == nullptr)
	{
		return;
	}
	Entry->State = ECrossguardSaveState::Loading;
	Entry->bHasPending = false;
	if (!bForceReload)
	{
		Entry->bHasLast = false;
	}
	TWeakObjectPtr<UCrossguardSaveSubsystem> WeakThis(this);
	Backend->LoadCharacter(PlayerId, [WeakThis, PlayerId](const FCrossguardSaveLoadResult& Result)
	{
		if (UCrossguardSaveSubsystem* Self = WeakThis.Get())
		{
			Self->HandleLoaded(PlayerId, Result);
		}
	});
}

bool UCrossguardSaveSubsystem::HandleLoaded(const FString& PlayerId, const FCrossguardSaveLoadResult& Result)
{
	FPlayerEntry* Entry = Players.Find(PlayerId);
	if (Entry == nullptr || Entry->State != ECrossguardSaveState::Loading)
	{
		// The player left (or saving was switched off) meanwhile.
		return false;
	}

	bool bApplied = false;
	switch (Result.Status)
	{
	case ECrossguardSaveLoadStatus::NotFound:
		UE_LOG(LogCrossguard, Log, TEXT("No save for %s: a new character"), *PlayerId);
		Entry->State = ECrossguardSaveState::Ready;
		Entry->bLastLoadAccepted = true;
		break;
	case ECrossguardSaveLoadStatus::Failed:
		UE_LOG(LogCrossguard, Error, TEXT("Could not load the save for %s (%s): not saving this player this session"), *PlayerId, *Result.Error);
		Entry->State = ECrossguardSaveState::Failed;
		Entry->bLastLoadAccepted = false;
		return false;
	case ECrossguardSaveLoadStatus::Corrupt:
	case ECrossguardSaveLoadStatus::Found:
	{
		Crossguard::FSaveParseResult Parsed;
		if (Result.Status == ECrossguardSaveLoadStatus::Found)
		{
			Parsed = Crossguard::ParseCharacterSave(CrossguardSaveDetail::ToUtf8(Result.Document), CrossguardSaveDetail::ToUtf8(PlayerId));
		}
		else
		{
			Parsed.Error = Crossguard::ESaveLoadError::TooLarge;
			Parsed.Detail = CrossguardSaveDetail::ToUtf8(Result.Error);
		}
		if (!Parsed.IsValid())
		{
			// Refused: nothing of it is applied, and the file is kept aside. The player continues
			// with what they have (a fresh character when joining).
			const FString Reason = FString::Printf(TEXT("%s: %s"), UTF8_TO_TCHAR(Crossguard::SaveLoadErrorName(Parsed.Error)),
				*CrossguardSaveDetail::FromUtf8(Parsed.Detail));
			// A warning, not an error: the player plays on and nothing is lost (the file is kept).
			UE_LOG(LogCrossguard, Warning, TEXT("Save for %s refused (%s)"), *PlayerId, *Reason);
			if (ICrossguardSaveBackend* Backend = GetBackend())
			{
				Backend->QuarantineCharacter(PlayerId, Reason);
			}
			Entry->State = ECrossguardSaveState::Ready;
			Entry->bLastLoadAccepted = false;
			if (!Entry->bReady)
			{
				return false;
			}
			// Already playing (a reload): the current state is what gets saved from now on.
			Entry->AppliedTo = Entry->PlayerState;
			return false;
		}
		if (Parsed.bMigrated)
		{
			UE_LOG(LogCrossguard, Log, TEXT("Save for %s migrated from version %d"), *PlayerId, Parsed.LoadedVersion);
		}
		Entry->State = ECrossguardSaveState::Ready;
		Entry->bLastLoadAccepted = true;
		Entry->Pending = MoveTemp(Parsed.Save);
		Entry->bHasPending = true;
		bApplied = true;
		break;
	}
	}

	// The pawn is already there: apply now.
	ACrossguardPlayerState* PlayerState = Entry->PlayerState.Get();
	if (Entry->bReady && PlayerState != nullptr)
	{
		PlayerReady(PlayerState);
	}
	return bApplied;
}

void UCrossguardSaveSubsystem::PlayerReady(ACrossguardPlayerState* PlayerState)
{
	const FString PlayerId = PlayerIdFor(PlayerState);
	FPlayerEntry* Entry = PlayerId.IsEmpty() ? nullptr : Players.Find(PlayerId);
	if (Entry == nullptr)
	{
		return;
	}
	Entry->PlayerState = PlayerState;
	Entry->bReady = true;
	if (Entry->State != ECrossguardSaveState::Ready)
	{
		// Loading (applied when it arrives) or failed (never saved).
		return;
	}

	TObjectPtr<UCrossguardSaveWatcher>& Watcher = Watchers.FindOrAdd(PlayerId);
	if (Watcher == nullptr)
	{
		Watcher = NewObject<UCrossguardSaveWatcher>(this);
	}
	Watcher->Watch(this, PlayerState);

	// A new player state for a player this server already has (seamless travel replaces the player
	// state and the pawn): the latest record goes onto it, carried gear included.
	if (!Entry->bHasPending && Entry->bHasLast && Entry->AppliedTo.Get() != PlayerState)
	{
		Entry->Pending = Entry->Last;
		Entry->bHasPending = true;
	}
	if (Entry->bHasPending)
	{
		Crossguard::FCharacterSave Record = MoveTemp(Entry->Pending);
		Entry->bHasPending = false;
		Entry->Pending = Crossguard::FCharacterSave();
		Apply(*Entry, *PlayerState, MoveTemp(Record));
	}
	Entry->AppliedTo = PlayerState;
	// Write what the player has now: repairs made on load, or a new character's first save.
	SaveNow(PlayerState);
}

void UCrossguardSaveSubsystem::Apply(FPlayerEntry& Entry, ACrossguardPlayerState& PlayerState, Crossguard::FCharacterSave Record)
{
	using namespace CrossguardSaveDetail;

	ACrossguardCharacter* Character = CharacterOf(&PlayerState);
	const FString PlayerId = FromUtf8(Record.PlayerId);
	const std::vector<Crossguard::FSaveIssue> Issues = Crossguard::ValidateCharacterSave(Record, RulesFor(PlayerState, Character));
	for (const Crossguard::FSaveIssue& Issue : Issues)
	{
		UE_LOG(LogCrossguard, Warning, TEXT("Save for %s repaired: %s: %s"), *PlayerId, UTF8_TO_TCHAR(Crossguard::SaveIssueName(Issue.Kind)),
			*FromUtf8(Issue.Detail));
	}

	TGuardValue<bool> ApplyingGuard(bApplying, true);

	FCrossguardAttributeAllocation Allocation;
	Allocation.Strength = Record.Attributes.Str;
	Allocation.Agility = Record.Attributes.Agi;
	Allocation.Intelligence = Record.Attributes.Int;
	TArray<FName> Library;
	for (const std::string& Skill : Record.SkillLibrary)
	{
		Library.Add(FName(*FromUtf8(Skill)));
	}
	PlayerState.RestoreSavedProgression(Record.TotalXp, Allocation, Record.RespecTokens, static_cast<int32>(Record.Gold), Library);

	UCrossguardInventoryComponent* Inventory = Character != nullptr ? Character->GetInventoryComponent() : nullptr;
	if (Inventory != nullptr)
	{
		// The record is the truth: whatever the pawn spawned with goes.
		Inventory->TakeEverything();
		TArray<FCrossguardInventoryItem> Carried;
		for (const Crossguard::FSavedItem& Saved : Record.Carried)
		{
			Carried.Add(ToGame(Saved));
		}
		// Worn gear goes back on, the rest into the grids; what does not fit goes to the stash.
		for (const FCrossguardInventoryItem& Left : Inventory->RestoreFromBody(Carried))
		{
			Crossguard::FSavedItem Saved = ToSaved(Left);
			FIntPoint Size;
			int32 MaxStack = 1;
			if (DescribeItem(*Inventory, Left, Size, MaxStack))
			{
				Saved.Size = {Size.X, Size.Y};
			}
			if (Crossguard::PlaceInStash(Record.StashTabs, Saved, Crossguard::FInventoryConfig()))
			{
				UE_LOG(LogCrossguard, Warning, TEXT("Save for %s: carried %s did not fit; put in the stash"), *PlayerId, *FromUtf8(Saved.ItemId));
			}
			else
			{
				UE_LOG(LogCrossguard, Warning, TEXT("Save for %s: carried %s did not fit anywhere; quarantined"), *PlayerId, *FromUtf8(Saved.ItemId));
				Record.QuarantinedItems.push_back(Saved);
			}
		}
	}

	if (UCrossguardStashComponent* Stash = PlayerState.GetStash())
	{
		TArray<FCrossguardStashTab> Tabs;
		for (const Crossguard::FSavedStashTab& SavedTab : Record.StashTabs)
		{
			FCrossguardStashTab& Tab = Tabs.AddDefaulted_GetRef();
			for (const Crossguard::FSavedItem& Saved : SavedTab.Items)
			{
				FCrossguardInventoryItem Item = ToGame(Saved);
				Item.bWasEquipped = false;
				FIntPoint Size;
				int32 MaxStack = 1;
				if (Inventory != nullptr && DescribeItem(*Inventory, Item, Size, MaxStack))
				{
					Item.MaxStack = MaxStack;
				}
				Tab.Items.Add(Item);
			}
		}
		Stash->RestoreTabs(Tabs);
	}

	UCrossguardLoadoutComponent* Loadout = Character != nullptr ? Character->GetLoadoutComponent() : nullptr;
	if (Loadout != nullptr)
	{
		TArray<FName> Skills;
		for (const std::string& Skill : Record.EquippedSkills)
		{
			Skills.Add(FName(*FromUtf8(Skill)));
		}
		// Validated again by the loadout (Skill Library, Capacity).
		Loadout->RequestLoadout(Skills);
	}

	// What the live game does not hold: the quarantine, and the gear while there is no pawn.
	Entry.Last = MoveTemp(Record);
	Entry.bHasLast = true;
	UE_LOG(LogCrossguard, Log, TEXT("Loaded the save for %s (%d issue(s) repaired)"), *PlayerId, static_cast<int32>(Issues.size()));
}

bool UCrossguardSaveSubsystem::ReloadPlayer(ACrossguardPlayerState* PlayerState)
{
	const FString PlayerId = PlayerIdFor(PlayerState);
	if (PlayerId.IsEmpty())
	{
		return false;
	}
	FPlayerEntry& Entry = Players.FindOrAdd(PlayerId);
	Entry.PlayerState = PlayerState;
	Entry.bDirty = false;
	StartLoad(PlayerId, true);
	// The local backend answers at once; a remote one applies when its answer arrives.
	const FPlayerEntry* After = Players.Find(PlayerId);
	return After != nullptr && After->State == ECrossguardSaveState::Ready && After->bLastLoadAccepted && After->AppliedTo.Get() == PlayerState
		&& !After->bHasPending;
}

// --- Saving ---------------------------------------------------------------------------------------

bool UCrossguardSaveSubsystem::BuildRecord(ACrossguardPlayerState* PlayerState, Crossguard::FCharacterSave& OutRecord) const
{
	using namespace CrossguardSaveDetail;

	const FString PlayerId = PlayerIdFor(PlayerState);
	const FPlayerEntry* Entry = PlayerId.IsEmpty() ? nullptr : Players.Find(PlayerId);
	if (Entry == nullptr)
	{
		return false;
	}

	Crossguard::FCharacterSave Record;
	Record.PlayerId = ToUtf8(PlayerId);
	Record.TotalXp = PlayerState->GetTotalXp();
	const FCrossguardAttributeAllocation& Allocation = PlayerState->GetAttributeAllocation();
	Record.Attributes = {Allocation.Strength, Allocation.Agility, Allocation.Intelligence};
	Record.RespecTokens = PlayerState->GetRespecTokens();
	Record.Gold = PlayerState->GetGold();
	for (const FName& Skill : PlayerState->GetSkillLibrary())
	{
		Record.SkillLibrary.push_back(ToUtf8(Skill.ToString()));
	}

	if (const UCrossguardStashComponent* Stash = PlayerState->GetStash())
	{
		Record.PurchasedStashTabs = Stash->GetPurchasedTabCount();
		for (const FCrossguardStashTab& Tab : Stash->GetTabs())
		{
			Crossguard::FSavedStashTab& SavedTab = Record.StashTabs.emplace_back();
			for (const FCrossguardInventoryItem& Item : Tab.Items)
			{
				SavedTab.Items.push_back(ToSaved(Item));
			}
		}
	}

	const ACrossguardCharacter* Character = CharacterOf(PlayerState);
	const UCrossguardLoadoutComponent* Loadout = Character != nullptr ? Character->GetLoadoutComponent() : nullptr;
	if (Loadout != nullptr)
	{
		for (const FName& Skill : Loadout->GetEquippedSkills())
		{
			Record.EquippedSkills.push_back(ToUtf8(Skill.ToString()));
		}
	}
	else if (Entry->bHasLast)
	{
		Record.EquippedSkills = Entry->Last.EquippedSkills;
	}

	// During a raid the carried gear is at risk: it is not saved, so a crash or a disconnect counts
	// as a failed extraction. Between raids it is what the pawn carries, or (no pawn: logging out,
	// spectating) the last record's.
	Record.bInRaid = PlayerState->IsInRaid();
	if (!Record.bInRaid)
	{
		if (Character != nullptr)
		{
			SnapshotCarried(*Character, Record.Carried);
		}
		else if (Entry->bHasLast)
		{
			Record.Carried = Entry->Last.Carried;
		}
	}

	if (Entry->bHasLast)
	{
		Record.QuarantinedItems = Entry->Last.QuarantinedItems;
		Record.QuarantinedSkills = Entry->Last.QuarantinedSkills;
	}
	OutRecord = MoveTemp(Record);
	return true;
}

void UCrossguardSaveSubsystem::Write(const FString& PlayerId, FPlayerEntry& Entry)
{
	ICrossguardSaveBackend* Backend = GetBackend();
	if (Backend == nullptr || !Entry.bHasLast)
	{
		return;
	}
	Entry.Last.SavedAt = FDateTime::UtcNow().ToUnixTimestamp();
	const ACrossguardPlayerState* PlayerState = Entry.PlayerState.Get();
	const Crossguard::FProgressionConfig Progression = PlayerState != nullptr ? PlayerState->GetProgressionConfig() : Crossguard::FProgressionConfig();
	const FString Document = CrossguardSaveDetail::FromUtf8(Crossguard::WriteCharacterSave(Entry.Last, Progression));
	Entry.bDirty = false;
	Backend->SaveCharacter(PlayerId, Document, [PlayerId](bool bSaved, const FString& Error)
	{
		if (!bSaved)
		{
			UE_LOG(LogCrossguard, Error, TEXT("Could not save %s: %s"), *PlayerId, *Error);
		}
	});
}

void UCrossguardSaveSubsystem::SaveNow(ACrossguardPlayerState* PlayerState)
{
	const FPlayerEntry* Found = nullptr;
	if (!CanSave(PlayerState, Found))
	{
		return;
	}
	const FString PlayerId = PlayerIdFor(PlayerState);
	FPlayerEntry& Entry = Players.FindChecked(PlayerId);
	if (BuildRecord(PlayerState, Entry.Last))
	{
		Entry.bHasLast = true;
		Write(PlayerId, Entry);
	}
}

void UCrossguardSaveSubsystem::MarkDirty(ACrossguardPlayerState* PlayerState)
{
	const FPlayerEntry* Found = nullptr;
	if (bApplying || PlayerState == nullptr || PlayerState->IsInRaid() || !CanSave(PlayerState, Found))
	{
		return;
	}
	const FString PlayerId = PlayerIdFor(PlayerState);
	FPlayerEntry& Entry = Players.FindChecked(PlayerId);
	// The record is taken now, while the pawn is surely there; the write waits.
	if (!BuildRecord(PlayerState, Entry.Last))
	{
		return;
	}
	Entry.bHasLast = true;
	Entry.bDirty = true;
	UGameInstance* GameInstance = GetGameInstance();
	if (GameInstance != nullptr && !GameInstance->GetTimerManager().IsTimerActive(FlushTimer))
	{
		GameInstance->GetTimerManager().SetTimer(FlushTimer, FTimerDelegate::CreateUObject(this, &UCrossguardSaveSubsystem::FlushPendingSaves),
			FMath::Max(0.01f, SaveDebounceSeconds), false);
	}
}

void UCrossguardSaveSubsystem::FlushPendingSaves()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		GameInstance->GetTimerManager().ClearTimer(FlushTimer);
	}
	WriteDirty();
}

void UCrossguardSaveSubsystem::WriteDirty()
{
	for (TPair<FString, FPlayerEntry>& Pair : Players)
	{
		if (Pair.Value.bDirty && Pair.Value.State == ECrossguardSaveState::Ready)
		{
			Write(Pair.Key, Pair.Value);
		}
	}
}

void UCrossguardSaveSubsystem::PlayerLeaving(ACrossguardPlayerState* PlayerState)
{
	const FString PlayerId = PlayerIdFor(PlayerState);
	if (PlayerId.IsEmpty() || !Players.Contains(PlayerId))
	{
		return;
	}
	SaveNow(PlayerState);
	// Forgotten: another server may change the save before this player comes back here.
	Players.Remove(PlayerId);
	Watchers.Remove(PlayerId);
}
