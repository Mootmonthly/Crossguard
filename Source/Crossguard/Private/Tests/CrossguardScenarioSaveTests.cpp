// Scenario tests: character saves (docs/persistence.md). A player makes progress between raids, it
// is saved, the in-memory state is wiped, and loading the save brings back exactly the same record;
// a corrupt or tampered save is refused or repaired without harming the live character.
// Framework: CrossguardScenario.h, docs/editor-tests.md.
//
// Saving is off while automation tests run; these tests turn it on with
// UCrossguardSaveSubsystem::EnableForTesting and a temporary folder under
// Saved/Automation/SaveTests, deleted at the end. Steps run in standalone PIE (the server), where the
// local backend answers at once.

#include "Tests/CrossguardScenario.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Character/CrossguardCharacter.h"
#include "CrossguardRules/Json.h"
#include "CrossguardRules/Save.h"
#include "Game/CrossguardGuildSubsystem.h"
#include "Game/CrossguardLocalSaveBackend.h"
#include "Game/CrossguardPlayerState.h"
#include "Game/CrossguardSaveSubsystem.h"
#include "HAL/FileManager.h"
#include "Items/CrossguardEquipmentComponent.h"
#include "Items/CrossguardInventoryComponent.h"
#include "Items/CrossguardStashComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Skills/CrossguardLoadoutComponent.h"

#include <string>

namespace CrossguardScenarioSave
{
	constexpr float Row = 2500.0f;
	// Long enough that no debounced save fires while a step is between "wipe" and "load".
	constexpr float TestDebounceSeconds = 600.0f;

	// The folder a test writes into, shared by its steps.
	struct FSaveTestFolder
	{
		FString Directory;
	};

	FString NewTestDirectory()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("SaveTests"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
	}

	UCrossguardSaveSubsystem* SavesOf(FScenarioContext& C)
	{
		UCrossguardSaveSubsystem* Saves = UCrossguardSaveSubsystem::Get(C.World());
		C.Require(Saves != nullptr, TEXT("the game instance has a UCrossguardSaveSubsystem"));
		return Saves;
	}

	FString SaveFileOf(const FString& Directory, const ACrossguardPlayerState* PlayerState)
	{
		return FPaths::Combine(Directory, UCrossguardLocalSaveBackend::FileNameFor(UCrossguardGuildSubsystem::PlayerIdOf(PlayerState)));
	}

	// The record the subsystem would save for the player now, as its JSON document ("" if none).
	FString RecordOf(UCrossguardSaveSubsystem& Saves, ACrossguardPlayerState* PlayerState)
	{
		Crossguard::FCharacterSave Record;
		return Saves.BuildRecord(PlayerState, Record) ? FString(UTF8_TO_TCHAR(Crossguard::WriteCharacterSave(Record).c_str())) : FString();
	}

	// Turns saving on into a fresh folder and spawns a player whose save is ready (a new character).
	ACrossguardCharacter* StartSaving(FScenarioContext& C, FSaveTestFolder& Folder, FName Name)
	{
		UCrossguardSaveSubsystem* Saves = SavesOf(C);
		if (Saves == nullptr)
		{
			return nullptr;
		}
		Folder.Directory = NewTestDirectory();
		Saves->EnableForTesting(Folder.Directory);
		Saves->SaveDebounceSeconds = TestDebounceSeconds;
		C.Test.AddInfo(FString::Printf(TEXT("Saves go to %s"), *Folder.Directory));

		ACrossguardCharacter* Player = C.SpawnPlayer(Name, FVector(0.0f, Row, 0.0f));
		ACrossguardPlayerState* PlayerState = Player != nullptr ? C.PlayerState(Player) : nullptr;
		if (!C.Require(PlayerState != nullptr && PlayerState->GetStash() != nullptr && Player->GetInventoryComponent() != nullptr,
				TEXT("the player has a player state, a stash and an inventory")))
		{
			return nullptr;
		}
		// Login already did this (OnSetUniqueId, HandleStartingNewPlayer); both are no-ops when it did.
		Saves->PlayerJoined(PlayerState);
		Saves->PlayerReady(PlayerState);
		if (!C.Require(Saves->GetSaveState(PlayerState) == ECrossguardSaveState::Ready, TEXT("the new character's save is ready")))
		{
			return nullptr;
		}
		C.Test.TestTrue(TEXT("A new character is saved at once"), IFileManager::Get().FileSize(*SaveFileOf(Folder.Directory, PlayerState)) > 0);
		return Player;
	}

	void StopSaving(FScenarioContext& C, const FSaveTestFolder& Folder)
	{
		if (UCrossguardSaveSubsystem* Saves = UCrossguardSaveSubsystem::Get(C.World()))
		{
			Saves->DisableForTesting();
		}
		if (!Folder.Directory.IsEmpty())
		{
			IFileManager::Get().DeleteDirectory(*Folder.Directory, false, true);
		}
	}

	// Rare sword with two affixes, straight into the inventory. Its inventory id, or 0.
	int32 GiveAffixedSword(ACrossguardCharacter* Player)
	{
		UCrossguardInventoryComponent* Inventory = Player->GetInventoryComponent();
		FCrossguardInventoryItem Sword;
		Sword.Category = ECrossguardItemCategory::Weapon;
		Sword.Rarity = ECrossguardRarity::Rare;
		Sword.Weapon.WeaponId = TEXT("sword_one_handed");
		Sword.Weapon.Rarity = ECrossguardRarity::Rare;
		FCrossguardAffix& Strength = Sword.Weapon.Affixes.AddDefaulted_GetRef();
		Strength.AffixId = TEXT("strength");
		Strength.Value = 2.0f;
		FCrossguardAffix& Speed = Sword.Weapon.Affixes.AddDefaulted_GetRef();
		Speed.AffixId = TEXT("action_speed");
		Speed.Value = 0.03f;
		if (Inventory->AddItem(Sword) != 0)
		{
			return 0;
		}
		for (const FCrossguardInventoryItem& Item : Inventory->GetItems())
		{
			if (Item.Category == ECrossguardItemCategory::Weapon && Item.Weapon.WeaponId == Sword.Weapon.WeaponId)
			{
				return Item.Id;
			}
		}
		return 0;
	}

	int32 StackId(const ACrossguardCharacter* Player, FName ItemId)
	{
		for (const FCrossguardInventoryItem& Item : Player->GetInventoryComponent()->GetItems())
		{
			if (Item.ItemId == ItemId)
			{
				return Item.Id;
			}
		}
		return 0;
	}

	// Rewrites the player's save file through the rules JSON model.
	bool EditSaveFile(const FString& Path, TFunctionRef<void(Crossguard::FJsonNode&)> Edit)
	{
		FString Text;
		Crossguard::FJsonNode Root;
		if (!FFileHelper::LoadFileToString(Text, *Path) || !Crossguard::ParseJson(std::string(TCHAR_TO_UTF8(*Text)), Root))
		{
			return false;
		}
		Edit(Root);
		return FFileHelper::SaveStringToFile(FString(UTF8_TO_TCHAR(Crossguard::WriteJson(Root).c_str())), *Path,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	int32 CountCorruptCopies(const FString& Directory)
	{
		TArray<FString> Found;
		IFileManager::Get().FindFiles(Found, *FPaths::Combine(Directory, TEXT("*.json")), true, false);
		return Found.FilterByPredicate([](const FString& File) { return File.Contains(TEXT(".corrupt-")); }).Num();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioSaveRoundTrip, "Crossguard.Scenario.Save.RoundTrip", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioSaveRoundTrip::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSave;

	const TSharedRef<FSaveTestFolder> Folder = MakeShared<FSaveTestFolder>();
	const TSharedRef<FString> Before = MakeShared<FString>();

	FScenario Scenario(*this);
	Scenario.Raid(false);

	Scenario.Step(TEXT("Progress between raids, then save"), [Folder, Before](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = StartSaving(C, *Folder, TEXT("Saver"));
		UCrossguardSaveSubsystem* Saves = UCrossguardSaveSubsystem::Get(C.World());
		if (Player == nullptr || Saves == nullptr)
		{
			return;
		}
		ACrossguardPlayerState* Progression = C.PlayerState(Player);
		UCrossguardStashComponent* Stash = Progression->GetStash();

		// 300 XP banked (level 5: 42 points), 42 assigned, 2 respec tokens, a skill learned, one equipped.
		for (int32 Quest = 0; Quest < 3; ++Quest)
		{
			Progression->AwardXp(ECrossguardXpSource::Quest, 100);
		}
		FCrossguardAttributeAllocation Allocation;
		Allocation.Strength = 15;
		Allocation.Agility = 15;
		Allocation.Intelligence = 12;
		Progression->RequestAttributeAllocation(Allocation);
		Progression->GrantRespecTokens(2);
		Progression->LearnSkillForTesting(TEXT("fireball"));
		C.GrantSkills(Player, {TEXT("heal")});

		// 7321 gold, one tab bought (3000): 4321 left, two tabs.
		Progression->AddGold(7321);
		Stash->RequestBuyStashTab();
		C.Test.TestEqual(TEXT("Gold after buying a tab"), Progression->GetGold(), 4321);
		if (!C.Require(Stash->GetTabCount() == 2, TEXT("the stash has two tabs")))
		{
			return;
		}

		// Stash: a Rare sword with affixes, rotated, in tab 2 at (3, 4); 4 Uncommon potions anywhere.
		const int32 SwordId = GiveAffixedSword(Player);
		if (!C.Require(SwordId != 0, TEXT("the affixed sword is in the inventory (character Weapon Table set)")))
		{
			return;
		}
		Stash->RequestStore(SwordId, 1, FIntPoint(3, 4), true);
		C.Require(C.GiveItem(Player, TEXT("healing_potion"), 4, ECrossguardRarity::Uncommon) == 0, TEXT("the potions fit"));
		Stash->RequestStoreAnywhere(StackId(Player, TEXT("healing_potion")));

		// Carried between raids: an axe in hand, 3 Common potions and a Rare ruby in the inventory.
		C.Equip(Player, TEXT("axe_one_handed"));
		C.Require(C.GiveItem(Player, TEXT("healing_potion"), 3) == 0 && C.GiveItem(Player, TEXT("ruby"), 1, ECrossguardRarity::Rare) == 0,
			TEXT("the carried items fit"));

		Saves->SaveNow(Progression);
		*Before = RecordOf(*Saves, Progression);
		C.Test.TestFalse(TEXT("The player has a record"), Before->IsEmpty());
		C.Test.AddInfo(*Before);

		const FString Path = SaveFileOf(Folder->Directory, Progression);
		FString OnDisk;
		C.Test.TestTrue(TEXT("The save file is there"), FFileHelper::LoadFileToString(OnDisk, *Path));
		C.Test.TestTrue(TEXT("The file names the player"), OnDisk.Contains(UCrossguardGuildSubsystem::PlayerIdOf(Progression)));
		C.Test.TestTrue(TEXT("Affix values are written tidily"), OnDisk.Contains(TEXT("0.03")));
	});

	Scenario.Step(TEXT("Wipe the in-memory state"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Saver"));
		ACrossguardPlayerState* Progression = Player != nullptr ? C.PlayerState(Player) : nullptr;
		if (Progression == nullptr)
		{
			C.Fail(TEXT("player gone"));
			return;
		}
		Progression->RestoreSavedProgression(0, FCrossguardAttributeAllocation(), 0, 0, TArray<FName>());
		Progression->GetStash()->RestoreTabs(TArray<FCrossguardStashTab>());
		Player->GetInventoryComponent()->TakeEverything();
		if (UCrossguardLoadoutComponent* Loadout = Player->GetLoadoutComponent())
		{
			Loadout->RequestLoadout(TArray<FName>());
		}
		C.Test.TestEqual(TEXT("Wiped: XP"), Progression->GetTotalXp(), 0);
		C.Test.TestEqual(TEXT("Wiped: gold"), Progression->GetGold(), 0);
		C.Test.TestEqual(TEXT("Wiped: one empty tab"), Progression->GetStash()->GetTabCount(), 1);
		C.Test.TestEqual(TEXT("Wiped: inventory"), Player->GetInventoryComponent()->GetItems().Num(), 0);
		C.Test.TestTrue(TEXT("Wiped: no axe"), Player->GetEquipmentComponent()->GetItem(ECrossguardEquipmentSlot::MainHand).IsEmpty());
	});

	Scenario.Step(TEXT("Load: the same record comes back"), [Before](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Saver"));
		ACrossguardPlayerState* Progression = Player != nullptr ? C.PlayerState(Player) : nullptr;
		UCrossguardSaveSubsystem* Saves = SavesOf(C);
		if (Progression == nullptr || Saves == nullptr)
		{
			C.Fail(TEXT("player or subsystem gone"));
			return;
		}
		C.ClearLog();
		if (!C.Require(Saves->ReloadPlayer(Progression), TEXT("the save loads")))
		{
			return;
		}
		const FString After = RecordOf(*Saves, Progression);
		C.Test.TestEqual(TEXT("Loaded record equals the saved one"), After, *Before);
		if (After != *Before)
		{
			C.Test.AddInfo(After);
		}

		C.Test.TestEqual(TEXT("XP"), Progression->GetTotalXp(), 300);
		C.Test.TestEqual(TEXT("Strength"), Progression->GetAttributeAllocation().Strength, 15);
		C.Test.TestEqual(TEXT("Intelligence"), Progression->GetAttributeAllocation().Intelligence, 12);
		C.Test.TestEqual(TEXT("Respec tokens"), Progression->GetRespecTokens(), 2);
		C.Test.TestEqual(TEXT("Gold"), Progression->GetGold(), 4321);
		C.Test.TestTrue(TEXT("Fireball learned"), Progression->HasLearnedSkill(TEXT("fireball")));
		C.Test.TestTrue(TEXT("Heal learned"), Progression->HasLearnedSkill(TEXT("heal")));
		UCrossguardStashComponent* Stash = Progression->GetStash();
		C.Test.TestEqual(TEXT("Stash tabs"), Stash->GetTabCount(), 2);
		bool bSwordFound = false;
		if (Stash->GetTabCount() == 2)
		{
			for (const FCrossguardInventoryItem& Item : Stash->GetTabs()[1].Items)
			{
				if (Item.Category == ECrossguardItemCategory::Weapon)
				{
					bSwordFound = true;
					C.Test.TestEqual(TEXT("Sword position"), Item.Position, FIntPoint(3, 4));
					C.Test.TestTrue(TEXT("Sword rotated"), Item.bRotated);
					C.Test.TestEqual(TEXT("Sword rarity"), Item.Weapon.Rarity, ECrossguardRarity::Rare);
					C.Test.TestEqual(TEXT("Sword affixes"), Item.Weapon.Affixes.Num(), 2);
					if (Item.Weapon.Affixes.Num() == 2)
					{
						C.Test.TestEqual(TEXT("Action speed affix exactly"), Item.Weapon.Affixes[1].Value, 0.03f);
					}
				}
			}
		}
		C.Test.TestTrue(TEXT("The sword is in tab 2"), bSwordFound);
		C.Test.TestEqual(TEXT("Axe back in hand"), Player->GetEquipmentComponent()->GetItem(ECrossguardEquipmentSlot::MainHand).WeaponId,
			FName(TEXT("axe_one_handed")));
		C.Test.TestEqual(TEXT("Carried potions"), Player->GetInventoryComponent()->CountItem(TEXT("healing_potion")), 3);
		C.Test.TestEqual(TEXT("Carried ruby"), Player->GetInventoryComponent()->CountItem(TEXT("ruby")), 1);
		C.Test.TestTrue(TEXT("Log: loaded"), C.LogContains(TEXT("Loaded the save")));
	});

	Scenario.Step(TEXT("A save made in a raid lists no carried gear"), [Folder](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Saver"));
		ACrossguardPlayerState* Progression = Player != nullptr ? C.PlayerState(Player) : nullptr;
		if (Progression == nullptr)
		{
			C.Fail(TEXT("player gone"));
			return;
		}
		// BeginRaid saves.
		C.SetRaid(true);
		FString Text;
		C.Test.TestTrue(TEXT("Read the raid save"), FFileHelper::LoadFileToString(Text, *SaveFileOf(Folder->Directory, Progression)));
		const Crossguard::FSaveParseResult Parsed = Crossguard::ParseCharacterSave(std::string(TCHAR_TO_UTF8(*Text)));
		if (!C.Require(Parsed.IsValid(), TEXT("the raid save parses")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("Marked as in a raid"), Parsed.Save.bInRaid);
		C.Test.TestEqual(TEXT("No carried gear in a raid save"), static_cast<int32>(Parsed.Save.Carried.size()), 0);
		C.Test.TestEqual(TEXT("Stash kept"), static_cast<int32>(Parsed.Save.StashTabs.size()), 2);
		C.Test.TestEqual(TEXT("Gold kept"), static_cast<int32>(Parsed.Save.Gold), 4321);
		// The gear itself is still on the player until the raid decides.
		C.Test.TestEqual(TEXT("Still carrying the potions"), Player->GetInventoryComponent()->CountItem(TEXT("healing_potion")), 3);
	});

	Scenario.Step(TEXT("Clean up"), [Folder](FScenarioContext& C)
	{
		StopSaving(C, *Folder);
	});

	Scenario.Run();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioSaveCorrupt, "Crossguard.Scenario.Save.CorruptFileRefused", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioSaveCorrupt::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSave;

	const TSharedRef<FSaveTestFolder> Folder = MakeShared<FSaveTestFolder>();

	FScenario Scenario(*this);
	Scenario.Raid(false);

	Scenario.Step(TEXT("A cut-off file is refused and set aside; the live character is untouched"), [Folder](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = StartSaving(C, *Folder, TEXT("Victim"));
		UCrossguardSaveSubsystem* Saves = UCrossguardSaveSubsystem::Get(C.World());
		if (Player == nullptr || Saves == nullptr)
		{
			return;
		}
		ACrossguardPlayerState* Progression = C.PlayerState(Player);
		Progression->AddGold(500);
		Progression->AwardXp(ECrossguardXpSource::Quest, 60);
		C.Require(C.GiveItem(Player, TEXT("healing_potion"), 2) == 0, TEXT("the potions fit"));
		Saves->SaveNow(Progression);

		// A crash mid-write would leave something like this (the backend writes a temp file and
		// renames it, so this needs a disk fault or a hand edit).
		const FString Path = SaveFileOf(Folder->Directory, Progression);
		FString Text;
		C.Require(FFileHelper::LoadFileToString(Text, *Path), TEXT("the save file is there"));
		FFileHelper::SaveStringToFile(Text.Left(Text.Len() / 2), *Path);

		C.ClearLog();
		C.Test.TestFalse(TEXT("The cut-off save is refused"), Saves->ReloadPlayer(Progression));
		C.Test.TestTrue(TEXT("Log: refused"), C.LogContains(TEXT("refused (InvalidJson")));
		C.Test.TestEqual(TEXT("Gold untouched"), Progression->GetGold(), 500);
		C.Test.TestEqual(TEXT("XP untouched"), Progression->GetTotalXp(), 60);
		C.Test.TestEqual(TEXT("Potions untouched"), Player->GetInventoryComponent()->CountItem(TEXT("healing_potion")), 2);
		C.Test.TestEqual(TEXT("The refused file is kept aside"), CountCorruptCopies(Folder->Directory), 1);
		C.Test.TestEqual(TEXT("Still saving this player"), Saves->GetSaveState(Progression), ECrossguardSaveState::Ready);

		// The next save writes the live state again, and it loads.
		Saves->SaveNow(Progression);
		C.Test.TestTrue(TEXT("Saved again"), IFileManager::Get().FileSize(*Path) > 0);
		C.Test.TestTrue(TEXT("The new save loads"), Saves->ReloadPlayer(Progression));
		C.Test.TestEqual(TEXT("Gold after the reload"), Progression->GetGold(), 500);
	});

	Scenario.Step(TEXT("Tampered values are repaired, another player's file is refused"), [Folder](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Victim"));
		ACrossguardPlayerState* Progression = Player != nullptr ? C.PlayerState(Player) : nullptr;
		UCrossguardSaveSubsystem* Saves = SavesOf(C);
		if (Progression == nullptr || Saves == nullptr)
		{
			C.Fail(TEXT("player or subsystem gone"));
			return;
		}
		const FString Path = SaveFileOf(Folder->Directory, Progression);

		// Negative gold, a stack beyond its size, an item the game does not know.
		C.Require(EditSaveFile(Path, [](Crossguard::FJsonNode& Root)
		{
			Root.Set("gold", Crossguard::FJsonNode::MakeInt(-50));
			Crossguard::FJsonNode Unknown = Crossguard::FJsonNode::MakeObject();
			Unknown.Set("category", Crossguard::FJsonNode::MakeString("trinket"));
			Unknown.Set("id", Crossguard::FJsonNode::MakeString("removed_trinket"));
			Unknown.Set("rarity", Crossguard::FJsonNode::MakeString("rare"));
			Unknown.Set("count", Crossguard::FJsonNode::MakeInt(1));
			Crossguard::FJsonNode* Tabs = Root.Find("stash") != nullptr ? Root.Find("stash")->Find("tabs") : nullptr;
			if (Tabs != nullptr && !Tabs->Items().empty())
			{
				const_cast<Crossguard::FJsonNode&>(Tabs->Items()[0]).Find("items")->Push(Unknown);
			}
		}), TEXT("the save file can be edited"));
		C.ClearLog();
		C.Test.TestTrue(TEXT("The tampered save loads"), Saves->ReloadPlayer(Progression));
		C.Test.TestEqual(TEXT("Negative gold clamped to 0"), Progression->GetGold(), 0);
		C.Test.TestTrue(TEXT("Log: gold repaired"), C.LogContains(TEXT("GoldOutOfRange")));
		C.Test.TestTrue(TEXT("Log: unknown item quarantined"), C.LogContains(TEXT("UnknownItem")));
		C.Test.TestEqual(TEXT("Potions still carried"), Player->GetInventoryComponent()->CountItem(TEXT("healing_potion")), 2);
		FString Text;
		FFileHelper::LoadFileToString(Text, *Path);
		C.Test.TestTrue(TEXT("The unknown item is kept in the quarantine"), Text.Contains(TEXT("removed_trinket")));

		// A file copied from another player.
		C.Require(EditSaveFile(Path, [](Crossguard::FJsonNode& Root)
		{
			Root.Set("playerId", Crossguard::FJsonNode::MakeString("someone-else"));
			Root.Set("gold", Crossguard::FJsonNode::MakeInt(999999));
		}), TEXT("the save file can be edited"));
		C.ClearLog();
		C.Test.TestFalse(TEXT("Another player's save is refused"), Saves->ReloadPlayer(Progression));
		C.Test.TestTrue(TEXT("Log: wrong player"), C.LogContains(TEXT("WrongPlayer")));
		C.Test.TestEqual(TEXT("No gold from it"), Progression->GetGold(), 0);
	});

	Scenario.Step(TEXT("Clean up"), [Folder](FScenarioContext& C)
	{
		StopSaving(C, *Folder);
	});

	Scenario.Run();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
