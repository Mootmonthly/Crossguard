// The hub-to-raid launch played for real (docs/parties.md, "Raid launch"): a listen server and a
// client in one party, in the hub, with progression and gear; the leader launches the raid, PIE
// travels seamlessly to a saved raid map and both arrive with everything they had. Then both extract
// and the server travels back to the hub (L_Hub, docs/hub-and-menu.md "Back to the hub"), where they
// arrive with what they carried out. The party launch tests (CrossguardScenarioPartyTests.cpp) and
// the hub return tests (CrossguardScenarioHubReturnTests.cpp) stop at the travel plan; this one travels.
//
// Seamless travel in single-process PIE is off by default: the test sets net.AllowPIESeamlessTravel
// to 1 for its run (restored afterwards, also after a failure). The raid map is the dev sandbox
// (/Game/Maps/Dev/L_DevSandbox, made by Tools\editor_setup.bat -CreateMap); without it the test is
// skipped. Saving is on for the run (UCrossguardSaveSubsystem::EnableForTesting, a temporary folder
// under Saved/Automation/TravelTests, deleted afterwards): the carried gear crosses the travel in the
// save subsystem's record, not on the pawn (the hub pawns are destroyed).
// Framework: CrossguardScenario.h ("ExpectServerTravel"); docs/editor-tests.md ("Networked scenarios").

#include "Tests/CrossguardScenario.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Character/CrossguardCharacter.h"
#include "CrossguardRules/Save.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Game/CrossguardGameMode.h"
#include "Game/CrossguardGuildSubsystem.h"
#include "Game/CrossguardLocalSaveBackend.h"
#include "Game/CrossguardPartyComponent.h"
#include "Game/CrossguardPartySubsystem.h"
#include "Game/CrossguardPlayerState.h"
#include "Game/CrossguardSaveSubsystem.h"
#include "Game/CrossguardTeamLibrary.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Items/CrossguardEquipmentComponent.h"
#include "Items/CrossguardInventoryComponent.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

namespace CrossguardScenarioTravel
{
	using CrossguardScenario::FScenarioContext;

	// The raid map: saved, so PIE can load it (an unsaved map cannot be travelled to).
	const TCHAR* const SeamlessRaidMap = TEXT("/Game/Maps/Dev/L_DevSandbox");
	const TCHAR* const SeamlessTravelCVarName = TEXT("net.AllowPIESeamlessTravel");
	// The way back (Tools\editor_setup.bat -CreateHub); without it the test stops on the raid map.
	const TCHAR* const SeamlessHubMap = TEXT("/Game/Maps/L_Hub");
	// Real seconds for the server to load the raid map (async load in the editor; generous).
	constexpr float SeamlessTravelRealSeconds = 120.0f;
	// Game seconds on the raid map for the players to come in (each client loads the map too).
	constexpr float SeamlessArrivalSeconds = 60.0f;
	// No debounced save fires during the test (the records are taken at once anyway).
	constexpr float SeamlessDebounceSeconds = 600.0f;

	// One traveller as they were in the hub, just before the launch.
	struct FSeamlessTraveller
	{
		// UCrossguardGuildSubsystem::PlayerIdOf: the save and party key.
		FString PlayerId;
		FString DevPlayerId;
		// APlayerState::GetPlayerId: copied to the new player state by seamless travel only (a fresh
		// login gets a new one).
		int32 EnginePlayerId = INDEX_NONE;
		FString PartyKey;
		int32 TotalXp = 0;
		int32 Level = 0;
		int32 Gold = 0;
		FString MainHand;
		TArray<FString> Carried;
	};

	// Shared by the steps. Its destructor runs when the scenario is over (after PIE ended), also after
	// a failure: the cvar goes back and the save folder goes.
	struct FSeamlessTravelState
	{
		FString SaveDirectory;
		FString PartyId;
		TArray<FSeamlessTraveller> Before;
		bool bCVarChanged = false;
		FString PreviousCVar;

		~FSeamlessTravelState()
		{
			if (bCVarChanged)
			{
				if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(SeamlessTravelCVarName))
				{
					Var->Set(*PreviousCVar, ECVF_SetByCode);
				}
			}
			if (!SaveDirectory.IsEmpty())
			{
				IFileManager::Get().DeleteDirectory(*SaveDirectory, false, true);
			}
		}
	};

	// Server: client Index's player state (in the hub; ServerPawnOf matches by the engine PlayerId).
	ACrossguardPlayerState* SeamlessServerStateOf(const FScenarioContext& C, int32 Index)
	{
		const APlayerController* Controller = C.ServerControllerOf(Index);
		return Controller != nullptr ? Controller->GetPlayerState<ACrossguardPlayerState>() : nullptr;
	}

	// Server: the player state with this engine PlayerId, or null.
	ACrossguardPlayerState* SeamlessStateByEngineId(const FScenarioContext& C, int32 EnginePlayerId)
	{
		const AGameStateBase* GameState = C.World() != nullptr ? C.World()->GetGameState() : nullptr;
		if (GameState == nullptr)
		{
			return nullptr;
		}
		for (APlayerState* PlayerState : GameState->PlayerArray)
		{
			if (PlayerState != nullptr && PlayerState->GetPlayerId() == EnginePlayerId)
			{
				return Cast<ACrossguardPlayerState>(PlayerState);
			}
		}
		return nullptr;
	}

	FString SeamlessWeaponText(const FCrossguardWeaponItem& Weapon)
	{
		return Weapon.WeaponId.IsNone() ? FString(TEXT("(none)")) : FString::Printf(TEXT("%s r%d"), *Weapon.WeaponId.ToString(), static_cast<int32>(Weapon.Rarity));
	}

	// The inventory as "category id rarity xCount" lines, sorted, counts summed per kind (stacks may
	// regroup when the record is put back; positions are not compared).
	TArray<FString> SeamlessCarried(const ACrossguardCharacter* Character)
	{
		TArray<FString> Lines;
		const UCrossguardInventoryComponent* Inventory = Character != nullptr ? Character->GetInventoryComponent() : nullptr;
		if (Inventory == nullptr)
		{
			return Lines;
		}
		TMap<FString, int32> Counts;
		for (const FCrossguardInventoryItem& Item : Inventory->GetItems())
		{
			FName Id = Item.ItemId;
			ECrossguardRarity Rarity = Item.Rarity;
			if (Item.Category == ECrossguardItemCategory::Weapon)
			{
				Id = Item.Weapon.WeaponId;
				Rarity = Item.Weapon.Rarity;
			}
			else if (Item.Category == ECrossguardItemCategory::Armor)
			{
				Id = Item.Armor.ArmorId;
				Rarity = Item.Armor.Rarity;
			}
			const FString Kind = FString::Printf(TEXT("c%d %s r%d"), static_cast<int32>(Item.Category), *Id.ToString(), static_cast<int32>(Rarity));
			Counts.FindOrAdd(Kind) += Item.Count;
		}
		for (const TPair<FString, int32>& Pair : Counts)
		{
			Lines.Add(FString::Printf(TEXT("%s x%d"), *Pair.Key, Pair.Value));
		}
		Lines.Sort();
		return Lines;
	}

	FString SeamlessMainHand(const ACrossguardCharacter* Character)
	{
		const UCrossguardEquipmentComponent* Equipment = Character != nullptr ? Character->GetEquipmentComponent() : nullptr;
		return Equipment != nullptr ? SeamlessWeaponText(Equipment->GetItem(ECrossguardEquipmentSlot::MainHand)) : FString(TEXT("(no equipment)"));
	}

	FSeamlessTraveller SeamlessSnapshot(const ACrossguardPlayerState& PlayerState)
	{
		FSeamlessTraveller Traveller;
		Traveller.PlayerId = UCrossguardGuildSubsystem::PlayerIdOf(&PlayerState);
		Traveller.DevPlayerId = PlayerState.DevPlayerId;
		Traveller.EnginePlayerId = PlayerState.GetPlayerId();
		Traveller.PartyKey = PlayerState.PartyKey;
		Traveller.TotalXp = PlayerState.GetTotalXp();
		Traveller.Level = PlayerState.GetCharacterLevel();
		Traveller.Gold = PlayerState.GetGold();
		const ACrossguardCharacter* Character = PlayerState.GetPawn<ACrossguardCharacter>();
		Traveller.MainHand = SeamlessMainHand(Character);
		Traveller.Carried = SeamlessCarried(Character);
		return Traveller;
	}

	// The server world is the raid map's.
	bool SeamlessIsRaidMap(const UWorld* World)
	{
		return World != nullptr && UWorld::RemovePIEPrefix(World->GetPackage()->GetName()) == SeamlessRaidMap;
	}

	bool SeamlessIsHubMap(const UWorld* World)
	{
		return World != nullptr && UWorld::RemovePIEPrefix(World->GetPackage()->GetName()) == SeamlessHubMap;
	}

	// Carried lines without gold coins (extracting banks them as gold).
	FString SeamlessCarriedWithoutCoins(const TArray<FString>& Carried)
	{
		const FString CoinPrefix = FString::Printf(TEXT("c%d "), static_cast<int32>(ECrossguardItemCategory::Currency));
		TArray<FString> Kept;
		for (const FString& Line : Carried)
		{
			if (!Line.StartsWith(CoinPrefix))
			{
				Kept.Add(Line);
			}
		}
		return FString::Join(Kept, TEXT(", "));
	}

	// Log lines that mean the travel was refused or a traveller was taken for someone else.
	const TCHAR* const SeamlessBadLines[] = {
		// UCrossguardSaveSubsystem::PlayerJoined: a second player state with an online player's id.
		TEXT("is online with that id"),
		// ACrossguardGameMode::InitNewPlayer (a fresh login, not seamless travel) refusing the id.
		TEXT("You are already in this match"),
		// ACrossguardGameMode::HandleSeamlessTravelPlayer.
		TEXT("could not rejoin a team after travel"),
		// AGameModeBase::CanServerTravel without the cvar.
		TEXT("NOT supported in single process PIE"),
	};
}

// Listen server + 1 client in the hub, one party. Each has banked XP, gold, a Rare / Uncommon weapon
// in hand and potions, bandages and a ruby in the inventory. The leader (the host) launches the raid:
// a real seamless ServerTravel to L_DevSandbox. On the raid map: both players there (two player
// states, no more), same engine PlayerId, player id and DevPlayerId, same party (key, party view,
// allies on one team), same XP, level and gold, the same weapon in hand and the same inventory, the
// raid begun; their saves still tracked and ready, and nothing refused as a duplicate login. Both
// extract: after the countdown the server travels back to L_Hub (skipped without it): the hub plays as
// the hub, both are there with the gear carried out, the banked XP and gold, the same party, not in a
// raid and free to raid again.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardNetSeamlessTravelHubToRaid, "Crossguard.Net.SeamlessTravelHubToRaid", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardNetSeamlessTravelHubToRaid::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioTravel;

	if (!FPackageName::DoesPackageExist(FString(SeamlessRaidMap)))
	{
		AddWarning(FString::Printf(TEXT("Skipped: %s does not exist (Tools\\editor_setup.bat -CreateMap makes it). The travel needs a saved raid map."),
			SeamlessRaidMap));
		return true;
	}

	const TSharedRef<FSeamlessTravelState> State = MakeShared<FSeamlessTravelState>();

	FScenario Scenario(*this);
	Scenario.Clients(2);
	Scenario.Raid(false);
	// Two travels: to the raid and back to the hub.
	Scenario.Timeout(480.0f);

	Scenario.Step(TEXT("Hub: seamless travel allowed in PIE, saving on"), [State](FScenarioContext& C)
	{
		IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(SeamlessTravelCVarName);
		if (Var == nullptr)
		{
			C.Skip(FString::Printf(TEXT("%s does not exist in this engine: single-process PIE cannot travel seamlessly (docs/first-compile-triage.md, Batch 3)"),
				SeamlessTravelCVarName));
			return;
		}
		State->PreviousCVar = Var->GetString();
		State->bCVarChanged = true;
		Var->Set(TEXT("1"), ECVF_SetByCode);
		if (!C.Require(Var->GetInt() == 1, FString::Printf(TEXT("%s is 1 (was %s; set from a higher-priority source?)"), SeamlessTravelCVarName,
				*State->PreviousCVar)))
		{
			return;
		}

		UCrossguardSaveSubsystem* Saves = UCrossguardSaveSubsystem::Get(C.World());
		if (!C.Require(Saves != nullptr, TEXT("the game instance has a UCrossguardSaveSubsystem")))
		{
			return;
		}
		State->SaveDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("TravelTests"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
		Saves->EnableForTesting(State->SaveDirectory);
		Saves->SaveDebounceSeconds = SeamlessDebounceSeconds;
		for (int32 Index = 0; Index < C.ClientCount(); ++Index)
		{
			ACrossguardPlayerState* PlayerState = SeamlessServerStateOf(C, Index);
			if (!C.Require(PlayerState != nullptr && C.ServerPawnOf(Index) != nullptr, FString::Printf(TEXT("client %d has a player state and a pawn"), Index)))
			{
				return;
			}
			// What login did while saving was off.
			Saves->PlayerJoined(PlayerState);
			Saves->PlayerReady(PlayerState);
			if (!C.Require(Saves->GetSaveState(PlayerState) == ECrossguardSaveState::Ready, FString::Printf(TEXT("client %d's save is ready"), Index)))
			{
				return;
			}
		}
		if (!C.Require(SeamlessServerStateOf(C, 0) != SeamlessServerStateOf(C, 1)
				&& UCrossguardGuildSubsystem::PlayerIdOf(SeamlessServerStateOf(C, 0)) != UCrossguardGuildSubsystem::PlayerIdOf(SeamlessServerStateOf(C, 1)),
				TEXT("the two players have different player ids")))
		{
			return;
		}
	});

	Scenario.Step(TEXT("Hub: one party; XP, gold and gear"), [State](FScenarioContext& C)
	{
		UGameInstance* GameInstance = C.World()->GetGameInstance();
		UCrossguardPartySubsystem* Parties = GameInstance != nullptr ? GameInstance->GetSubsystem<UCrossguardPartySubsystem>() : nullptr;
		ACrossguardPlayerState* Host = SeamlessServerStateOf(C, 0);
		ACrossguardPlayerState* Guest = SeamlessServerStateOf(C, 1);
		if (!C.Require(Parties != nullptr && Host != nullptr && Guest != nullptr && Host->GetParty() != nullptr, TEXT("parties and both player states")))
		{
			return;
		}
		if (!C.Require(Parties->Invite(Host, Guest) == ECrossguardPartyResult::None, TEXT("the host invites client 1")))
		{
			return;
		}
		State->PartyId = Host->GetParty()->GetPartyId();
		if (!C.Require(Parties->Accept(Guest, State->PartyId) == ECrossguardPartyResult::None, TEXT("client 1 accepts")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("Hub: the two are allies"), UCrossguardTeamLibrary::AreAllies(C.ServerPawnOf(0), C.ServerPawnOf(1)));

		// Banked between raids (not in a raid: XP goes straight to the bank).
		for (int32 Quest = 0; Quest < 3; ++Quest)
		{
			Host->AwardXp(ECrossguardXpSource::Quest, 100);
		}
		Guest->AwardXp(ECrossguardXpSource::Quest, 100);
		Host->AddGold(1234);
		Guest->AddGold(567);

		// Carried gear (the character Blueprint's debug loadout may hold more: compared as a whole).
		ACrossguardCharacter* HostPawn = C.ServerPawnOf(0);
		ACrossguardCharacter* GuestPawn = C.ServerPawnOf(1);
		C.Equip(HostPawn, TEXT("sword_one_handed"), ECrossguardEquipmentSlot::MainHand, ECrossguardRarity::Rare);
		C.Equip(GuestPawn, TEXT("axe_one_handed"), ECrossguardEquipmentSlot::MainHand, ECrossguardRarity::Uncommon);
		C.Require(C.GiveItem(HostPawn, TEXT("healing_potion"), 3) == 0 && C.GiveItem(HostPawn, TEXT("ruby"), 1, ECrossguardRarity::Rare) == 0,
			TEXT("the host's items fit"));
		C.Require(C.GiveItem(GuestPawn, TEXT("bandage"), 2) == 0 && C.GiveItem(GuestPawn, TEXT("ruby"), 1, ECrossguardRarity::Uncommon) == 0,
			TEXT("client 1's items fit"));

		C.GameMode()->RaidMapName = SeamlessRaidMap;
	});

	Scenario.Step(TEXT("Hub: what each traveller has; the launch plan is a server travel"), [State](FScenarioContext& C)
	{
		UGameInstance* GameInstance = C.World()->GetGameInstance();
		UCrossguardPartySubsystem* Parties = GameInstance != nullptr ? GameInstance->GetSubsystem<UCrossguardPartySubsystem>() : nullptr;
		ACrossguardPlayerState* Host = SeamlessServerStateOf(C, 0);
		if (!C.Require(Parties != nullptr && Host != nullptr, TEXT("parties and the host")))
		{
			return;
		}
		ECrossguardRaidTravel Mode = ECrossguardRaidTravel::NoDestination;
		FString Url;
		if (!C.Require(Parties->PlanRaidTravel(Host, Mode, Url) == ECrossguardPartyResult::None && Mode == ECrossguardRaidTravel::ServerTravel,
				TEXT("the plan: a server travel (the whole server is the party)")))
		{
			return;
		}
		if (!Url.StartsWith(SeamlessRaidMap))
		{
			C.Skip(FString::Printf(TEXT("The raid map is %s, not %s (-RaidMap= on the command line?)"), *Url, SeamlessRaidMap));
			return;
		}
		C.Test.TestTrue(FString::Printf(TEXT("The URL carries the party (%s)"), *Url), Url.Contains(TEXT("Party=") + State->PartyId));

		State->Before.Reset();
		for (int32 Index = 0; Index < C.ClientCount(); ++Index)
		{
			const ACrossguardPlayerState* PlayerState = SeamlessServerStateOf(C, Index);
			if (!C.Require(PlayerState != nullptr, FString::Printf(TEXT("client %d's player state"), Index)))
			{
				return;
			}
			const FSeamlessTraveller& Traveller = State->Before.Add_GetRef(SeamlessSnapshot(*PlayerState));
			C.Test.AddInfo(FString::Printf(TEXT("Hub, client %d: id %s, dev id '%s', engine id %d, party %s, XP %d (level %d), gold %d, main hand %s, carried [%s]"),
				Index, *Traveller.PlayerId, *Traveller.DevPlayerId, Traveller.EnginePlayerId, *Traveller.PartyKey, Traveller.TotalXp, Traveller.Level,
				Traveller.Gold, *Traveller.MainHand, *FString::Join(Traveller.Carried, TEXT(", "))));
			C.Test.TestEqual(FString::Printf(TEXT("Hub, client %d: party key is the party id"), Index), Traveller.PartyKey, State->PartyId);
			C.Test.TestTrue(FString::Printf(TEXT("Hub, client %d: XP banked"), Index), Traveller.TotalXp > 0);
			C.Test.TestFalse(FString::Printf(TEXT("Hub, client %d: not in a raid"), Index), PlayerState->IsInRaid());
		}
	});

	Scenario.Step(TEXT("The leader launches the raid: seamless server travel"), [State](FScenarioContext& C)
	{
		UGameInstance* GameInstance = C.World()->GetGameInstance();
		UCrossguardPartySubsystem* Parties = GameInstance != nullptr ? GameInstance->GetSubsystem<UCrossguardPartySubsystem>() : nullptr;
		if (!C.Require(Parties != nullptr, TEXT("parties")))
		{
			return;
		}
		C.ClearLog();
		const ECrossguardPartyResult Result = Parties->StartRaid(SeamlessServerStateOf(C, 0));
		if (Result != ECrossguardPartyResult::None)
		{
			C.Fail(FString::Printf(TEXT("StartRaid refused: %s%s"), *StaticEnum<ECrossguardPartyResult>()->GetNameStringByValue(static_cast<int64>(Result)),
				C.LogContains(TEXT("NOT supported in single process PIE")) ? TEXT(" (the engine refuses seamless travel in single-process PIE despite the cvar)") : TEXT("")));
			return;
		}
		C.Test.TestTrue(TEXT("Log: the raid starts for 2 players"), C.LogContains(TEXT("starts the raid for 2 player(s)")));
		C.ExpectServerTravel(SeamlessRaidMap, SeamlessTravelRealSeconds);
	});

	Scenario.WaitUntil(TEXT("raid map, server: both travellers have a pawn and their raid has begun"), SeamlessArrivalSeconds, [State](FScenarioContext& C)
	{
		if (!SeamlessIsRaidMap(C.World()))
		{
			return false;
		}
		for (const FSeamlessTraveller& Traveller : State->Before)
		{
			const ACrossguardPlayerState* PlayerState = SeamlessStateByEngineId(C, Traveller.EnginePlayerId);
			if (PlayerState == nullptr || PlayerState->GetPawn<ACrossguardCharacter>() == nullptr || !PlayerState->IsInRaid())
			{
				return false;
			}
		}
		return true;
	}, 0.25f);

	for (int32 Index = 0; Index < 2; ++Index)
	{
		Scenario.WaitUntilOnClient(Index, TEXT("on the raid map with its own pawn and its party"), SeamlessArrivalSeconds,
			[State, Index](FScenarioContext& C, UWorld& ClientWorld)
			{
				const APlayerController* Controller = C.ClientController(Index);
				const ACrossguardPlayerState* Own = Controller != nullptr ? Controller->GetPlayerState<ACrossguardPlayerState>() : nullptr;
				return SeamlessIsRaidMap(&ClientWorld) && C.ClientPawn(Index) != nullptr && Own != nullptr && State->Before.IsValidIndex(Index)
					&& Own->GetPlayerId() == State->Before[Index].EnginePlayerId && Own->GetParty() != nullptr
					&& Own->GetParty()->GetPartyId() == State->PartyId;
			}, 0.25f);
	}

	Scenario.Step(TEXT("Raid map: everything came along"), [State](FScenarioContext& C)
	{
		const ACrossguardGameMode* Mode = C.GameMode();
		const AGameStateBase* GameState = C.World()->GetGameState();
		UCrossguardSaveSubsystem* Saves = UCrossguardSaveSubsystem::Get(C.World());
		if (!C.Require(Mode != nullptr && GameState != nullptr && Saves != nullptr, TEXT("the raid map's game mode, game state and saves")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("The raid map plays as a raid (game mode bIsRaid; no ?Hub carried over)"), Mode->bIsRaid);
		C.Test.TestEqual(TEXT("Two player states on the raid map (none left over or duplicated)"), GameState->PlayerArray.Num(), State->Before.Num());

		TArray<ACrossguardCharacter*> Pawns;
		for (int32 Index = 0; Index < State->Before.Num(); ++Index)
		{
			const FSeamlessTraveller& Was = State->Before[Index];
			ACrossguardPlayerState* PlayerState = SeamlessStateByEngineId(C, Was.EnginePlayerId);
			if (!C.Require(PlayerState != nullptr, FString::Printf(TEXT("client %d's player state kept its engine PlayerId %d (seamless, not a fresh login)"), Index,
					Was.EnginePlayerId)))
			{
				return;
			}
			ACrossguardCharacter* Pawn = PlayerState->GetPawn<ACrossguardCharacter>();
			Pawns.Add(Pawn);
			const FSeamlessTraveller Now = SeamlessSnapshot(*PlayerState);
			C.Test.AddInfo(FString::Printf(TEXT("Raid, client %d: id %s, dev id '%s', party %s, team %d, XP %d (level %d), gold %d, main hand %s, carried [%s]"),
				Index, *Now.PlayerId, *Now.DevPlayerId, *Now.PartyKey, PlayerState->GetTeamId(), Now.TotalXp, Now.Level, Now.Gold, *Now.MainHand,
				*FString::Join(Now.Carried, TEXT(", "))));

			const FString Who = FString::Printf(TEXT("Client %d"), Index);
			C.Test.TestEqual(Who + TEXT(": DevPlayerId"), Now.DevPlayerId, Was.DevPlayerId);
			C.Test.TestEqual(Who + TEXT(": player id (save and party key)"), Now.PlayerId, Was.PlayerId);
			C.Test.TestEqual(Who + TEXT(": party key"), Now.PartyKey, State->PartyId);
			C.Test.TestTrue(Who + TEXT(": party view shows the party"), PlayerState->GetParty() != nullptr && PlayerState->GetParty()->GetPartyId() == State->PartyId);
			C.Test.TestEqual(Who + TEXT(": banked XP"), Now.TotalXp, Was.TotalXp);
			C.Test.TestEqual(Who + TEXT(": level"), Now.Level, Was.Level);
			C.Test.TestEqual(Who + TEXT(": gold"), Now.Gold, Was.Gold);
			C.Test.TestTrue(Who + TEXT(": in the raid"), PlayerState->IsInRaid());
			C.Test.TestEqual(Who + TEXT(": raid XP starts at 0"), PlayerState->GetRaidXp(), 0);
			C.Test.TestEqual(Who + TEXT(": weapon in hand"), Now.MainHand, Was.MainHand);
			C.Test.TestEqual(Who + TEXT(": carried items"), FString::Join(Now.Carried, TEXT(", ")), FString::Join(Was.Carried, TEXT(", ")));

			// The save entry followed the new player state (not dropped, not taken for a second login).
			C.Test.TestTrue(Who + TEXT(": save still tracked and ready"), Saves->GetSaveState(PlayerState) == ECrossguardSaveState::Ready);
			Crossguard::FCharacterSave Record;
			if (C.Test.TestTrue(Who + TEXT(": the save subsystem builds a record for the new player state"), Saves->BuildRecord(PlayerState, Record)))
			{
				C.Test.TestEqual(Who + TEXT(": the record's player id"), FString(UTF8_TO_TCHAR(Record.PlayerId.c_str())), Was.PlayerId);
				C.Test.TestTrue(Who + TEXT(": the record is a raid's (carried gear at risk)"), Record.bInRaid);
			}
			const FString SaveFile = FPaths::Combine(State->SaveDirectory, UCrossguardLocalSaveBackend::FileNameFor(Was.PlayerId));
			C.Test.TestTrue(Who + TEXT(": the raid start was saved"), IFileManager::Get().FileSize(*SaveFile) > 0);
		}

		if (Pawns.Num() == 2)
		{
			C.Test.TestTrue(TEXT("Both have pawns on the raid map"), Pawns[0] != nullptr && Pawns[1] != nullptr);
			C.Test.TestTrue(TEXT("Still allies on the raid map"), UCrossguardTeamLibrary::AreAllies(Pawns[0], Pawns[1]));
			const ACrossguardPlayerState* First = C.PlayerState(Pawns[0]);
			const ACrossguardPlayerState* Second = C.PlayerState(Pawns[1]);
			C.Test.TestTrue(TEXT("One team, assigned by the raid map's game mode"), First != nullptr && Second != nullptr && First->GetTeamId() >= 0
				&& First->GetTeamId() == Second->GetTeamId());
		}

		for (const TCHAR* Bad : SeamlessBadLines)
		{
			C.Test.TestFalse(FString::Printf(TEXT("Log has no \"%s\""), Bad), C.LogContains(Bad));
		}
	});

	// The way back (docs/hub-and-menu.md, "Back to the hub"): both extract, nobody is left in the raid,
	// and after the countdown (1 s here) the whole server travels seamlessly to L_Hub.
	Scenario.Step(TEXT("Raid: both extract; the server returns to the hub"), [State](FScenarioContext& C)
	{
		ACrossguardGameMode* Mode = C.GameMode();
		UCrossguardSaveSubsystem* Saves = UCrossguardSaveSubsystem::Get(C.World());
		if (!C.Require(Mode != nullptr && Saves != nullptr, TEXT("the raid map's game mode and saves")))
		{
			return;
		}
		if (!FPackageName::DoesPackageExist(FString(SeamlessHubMap)) || Mode->GetHubMapName() != SeamlessHubMap)
		{
			Saves->DisableForTesting();
			C.Skip(FString::Printf(TEXT("The way back is not tested: %s does not exist (Tools\\editor_setup.bat -CreateHub) or -HubMap= is set"),
				SeamlessHubMap));
			return;
		}
		Mode->HubReturnCountdownSeconds = 1.0f;
		Mode->bHubReturnTravelInTests = true;

		// What each one carries out (taken before the pawns go); XP and gold once banked.
		TArray<ACrossguardCharacter*> Extracting;
		for (FSeamlessTraveller& Traveller : State->Before)
		{
			ACrossguardPlayerState* PlayerState = SeamlessStateByEngineId(C, Traveller.EnginePlayerId);
			ACrossguardCharacter* Pawn = PlayerState != nullptr ? PlayerState->GetPawn<ACrossguardCharacter>() : nullptr;
			if (!C.Require(Pawn != nullptr, FString::Printf(TEXT("traveller %d has a pawn on the raid map"), Traveller.EnginePlayerId)))
			{
				return;
			}
			Traveller.MainHand = SeamlessMainHand(Pawn);
			Traveller.Carried = SeamlessCarried(Pawn);
			Extracting.Add(Pawn);
		}
		C.ClearLog();
		for (int32 Index = 0; Index < Extracting.Num(); ++Index)
		{
			Mode->HandleExtraction(Extracting[Index]);
			const ACrossguardPlayerState* PlayerState = SeamlessStateByEngineId(C, State->Before[Index].EnginePlayerId);
			if (C.Require(PlayerState != nullptr && !PlayerState->IsInRaid(), FString::Printf(TEXT("client %d extracted"), Index)))
			{
				State->Before[Index].TotalXp = PlayerState->GetTotalXp();
				State->Before[Index].Level = PlayerState->GetCharacterLevel();
				State->Before[Index].Gold = PlayerState->GetGold();
			}
		}
		if (!C.Require(Mode->IsHubReturnPending(), TEXT("nobody left in the raid: the countdown to the hub runs")))
		{
			return;
		}
		C.ExpectServerTravel(SeamlessHubMap, SeamlessTravelRealSeconds);
	});

	Scenario.WaitUntil(TEXT("hub, server: both travellers have a pawn"), SeamlessArrivalSeconds, [State](FScenarioContext& C)
	{
		if (!SeamlessIsHubMap(C.World()))
		{
			return false;
		}
		for (const FSeamlessTraveller& Traveller : State->Before)
		{
			const ACrossguardPlayerState* PlayerState = SeamlessStateByEngineId(C, Traveller.EnginePlayerId);
			if (PlayerState == nullptr || PlayerState->GetPawn<ACrossguardCharacter>() == nullptr)
			{
				return false;
			}
		}
		return true;
	}, 0.25f);

	for (int32 Index = 0; Index < 2; ++Index)
	{
		Scenario.WaitUntilOnClient(Index, TEXT("in the hub with its own pawn and its party"), SeamlessArrivalSeconds,
			[State, Index](FScenarioContext& C, UWorld& ClientWorld)
			{
				const APlayerController* Controller = C.ClientController(Index);
				const ACrossguardPlayerState* Own = Controller != nullptr ? Controller->GetPlayerState<ACrossguardPlayerState>() : nullptr;
				return SeamlessIsHubMap(&ClientWorld) && C.ClientPawn(Index) != nullptr && Own != nullptr && Own->GetParty() != nullptr
					&& Own->GetParty()->GetPartyId() == State->PartyId;
			}, 0.25f);
	}

	Scenario.Step(TEXT("Hub: back with what they carried out, party kept, free to raid again"), [State](FScenarioContext& C)
	{
		ACrossguardGameMode* Mode = C.GameMode();
		const AGameStateBase* GameState = C.World()->GetGameState();
		UCrossguardSaveSubsystem* Saves = UCrossguardSaveSubsystem::Get(C.World());
		if (!C.Require(Mode != nullptr && GameState != nullptr && Saves != nullptr, TEXT("the hub's game mode, game state and saves")))
		{
			return;
		}
		C.Test.TestFalse(TEXT("The hub plays as the hub (bIsRaid off)"), Mode->bIsRaid);
		C.Test.TestEqual(TEXT("Two player states in the hub"), GameState->PlayerArray.Num(), State->Before.Num());

		TArray<ACrossguardCharacter*> Pawns;
		for (int32 Index = 0; Index < State->Before.Num(); ++Index)
		{
			const FSeamlessTraveller& Was = State->Before[Index];
			ACrossguardPlayerState* PlayerState = SeamlessStateByEngineId(C, Was.EnginePlayerId);
			if (!C.Require(PlayerState != nullptr, FString::Printf(TEXT("client %d's player state came back (engine PlayerId %d)"), Index, Was.EnginePlayerId)))
			{
				return;
			}
			ACrossguardCharacter* Pawn = PlayerState->GetPawn<ACrossguardCharacter>();
			Pawns.Add(Pawn);
			const FSeamlessTraveller Now = SeamlessSnapshot(*PlayerState);
			C.Test.AddInfo(FString::Printf(TEXT("Hub again, client %d: id %s, party %s, team %d, XP %d (level %d), gold %d, main hand %s, carried [%s]"),
				Index, *Now.PlayerId, *Now.PartyKey, PlayerState->GetTeamId(), Now.TotalXp, Now.Level, Now.Gold, *Now.MainHand,
				*FString::Join(Now.Carried, TEXT(", "))));

			const FString Who = FString::Printf(TEXT("Hub again, client %d"), Index);
			C.Test.TestEqual(Who + TEXT(": player id"), Now.PlayerId, Was.PlayerId);
			C.Test.TestEqual(Who + TEXT(": party key"), Now.PartyKey, State->PartyId);
			C.Test.TestTrue(Who + TEXT(": party view shows the party"), PlayerState->GetParty() != nullptr && PlayerState->GetParty()->GetPartyId() == State->PartyId);
			C.Test.TestEqual(Who + TEXT(": banked XP (extraction included)"), Now.TotalXp, Was.TotalXp);
			C.Test.TestEqual(Who + TEXT(": level"), Now.Level, Was.Level);
			C.Test.TestEqual(Who + TEXT(": gold"), Now.Gold, Was.Gold);
			C.Test.TestFalse(Who + TEXT(": not in a raid"), PlayerState->IsInRaid());
			C.Test.TestFalse(Who + TEXT(": the raid's end does not carry over (may spawn and raid again)"), PlayerState->HasFinishedRaid());
			C.Test.TestEqual(Who + TEXT(": weapon in hand"), Now.MainHand, Was.MainHand);
			C.Test.TestEqual(Who + TEXT(": carried items (gold coins banked)"), SeamlessCarriedWithoutCoins(Now.Carried), SeamlessCarriedWithoutCoins(Was.Carried));
			C.Test.TestTrue(Who + TEXT(": save still tracked and ready"), Saves->GetSaveState(PlayerState) == ECrossguardSaveState::Ready);
			Crossguard::FCharacterSave Record;
			if (C.Test.TestTrue(Who + TEXT(": the save subsystem builds a record"), Saves->BuildRecord(PlayerState, Record)))
			{
				C.Test.TestFalse(Who + TEXT(": the record is not a raid's"), Record.bInRaid);
			}
		}
		if (Pawns.Num() == 2)
		{
			C.Test.TestTrue(TEXT("Still allies in the hub"), UCrossguardTeamLibrary::AreAllies(Pawns[0], Pawns[1]));
		}
		// The leader can launch again (the plan only).
		UGameInstance* GameInstance = C.World()->GetGameInstance();
		UCrossguardPartySubsystem* Parties = GameInstance != nullptr ? GameInstance->GetSubsystem<UCrossguardPartySubsystem>() : nullptr;
		ECrossguardRaidTravel Mode2 = ECrossguardRaidTravel::NoDestination;
		FString Url;
		C.Test.TestTrue(TEXT("Hub again: the leader may launch the next raid"), Parties != nullptr && State->Before.Num() > 0
			&& Parties->PlanRaidTravel(SeamlessStateByEngineId(C, State->Before[0].EnginePlayerId), Mode2, Url) == ECrossguardPartyResult::None);

		C.Test.TestTrue(TEXT("Log: the server travelled back"), C.LogContains(TEXT("server travel back to the hub")));
		for (const TCHAR* Bad : SeamlessBadLines)
		{
			C.Test.TestFalse(FString::Printf(TEXT("Log has no \"%s\""), Bad), C.LogContains(Bad));
		}

		// Done: no more saves into the folder (the state's destructor deletes it).
		Saves->DisableForTesting();
	});

	Scenario.Run();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
