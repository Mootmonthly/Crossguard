// Scenario tests: trading and guilds between two players (docs/next-build-checklist.md section 4,
// "Trading" and "Guilds" rows; docs/trading.md, docs/guild-rules.md), the between-raid hub actions
// (section 5) and two players looting one body at once (section 6). Framework: CrossguardScenario.h,
// docs/editor-tests.md.
//
// Requests go through the public Request* functions. In standalone PIE the scenario world is the
// server and a Server RPC called there runs its _Implementation at once (no connection involved),
// so each request has taken effect when the call returns.

#include "Tests/CrossguardScenario.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Character/CrossguardCharacter.h"
#include "CrossguardRules/Trade.h"
#include "Engine/GameInstance.h"
#include "Game/CrossguardGuildComponent.h"
#include "Game/CrossguardGuildSubsystem.h"
#include "Game/CrossguardPlayerState.h"
#include "Items/CrossguardInventoryComponent.h"
#include "Items/CrossguardLootContainer.h"
#include "Items/CrossguardStashComponent.h"
#include "Items/CrossguardTradeComponent.h"
#include "Items/CrossguardVendorComponent.h"
#include "Skills/CrossguardLoadoutComponent.h"

namespace CrossguardScenarioSocial
{
	// The scene row (Y) and the gap between the two players: well inside the 5 m trade distance.
	constexpr float Row = 1500.0f;
	constexpr float Gap = 200.0f;
	// The trade checks distance, death and changed stacks twice a second.
	constexpr float TradeCheckTimeout = 3.0f;
	// Data/Items/ItemTable.json: ruby SellValue 50 + SellPerRarity 50 x Rare (2).
	constexpr int32 RareRubyPrice = 150;
	// CrossguardRules FInventoryConfig: StashTabPrice, StartingStashTabs, MaxPurchasedStashTabs.
	constexpr int32 StashTabPrice = 3000;
	constexpr int32 StartingStashTabs = 1;
	constexpr int32 MaxPurchasedStashTabs = 3;

	// Data/Items/ItemTable.json rows (functions: no FName construction during static init).
	FName Potion() { return FName(TEXT("healing_potion")); }
	FName Bandage() { return FName(TEXT("bandage")); }
	FName Ruby() { return FName(TEXT("ruby")); }

	template <typename EnumType>
	FString EnumName(EnumType Value)
	{
		return StaticEnum<EnumType>()->GetNameStringByValue(static_cast<int64>(Value));
	}

	UCrossguardInventoryComponent* InventoryOf(const ACrossguardCharacter* Character)
	{
		return Character != nullptr ? Character->GetInventoryComponent() : nullptr;
	}

	UCrossguardTradeComponent* TradeOf(const ACrossguardCharacter* Character)
	{
		return Character != nullptr ? Character->GetTradeComponent() : nullptr;
	}

	// The first inventory stack of a table item (0: none).
	int32 StackId(const ACrossguardCharacter* Character, FName ItemId)
	{
		if (const UCrossguardInventoryComponent* Inventory = InventoryOf(Character))
		{
			for (const FCrossguardInventoryItem& Item : Inventory->GetItems())
			{
				if (Item.ItemId == ItemId)
				{
					return Item.Id;
				}
			}
		}
		return 0;
	}

	int32 CountOf(const ACrossguardCharacter* Character, FName ItemId)
	{
		const UCrossguardInventoryComponent* Inventory = InventoryOf(Character);
		return Inventory != nullptr ? Inventory->CountItem(ItemId) : 0;
	}

	ECrossguardRarity RarityOf(const ACrossguardCharacter* Character, FName ItemId)
	{
		FCrossguardInventoryItem Item;
		const UCrossguardInventoryComponent* Inventory = InventoryOf(Character);
		return Inventory != nullptr && Inventory->FindItem(StackId(Character, ItemId), Item) ? Item.Rarity : ECrossguardRarity::Common;
	}

	// UCrossguardTradeComponent::EndTrade logs "Trade of <owner> ended: <reason>" on the side that
	// ended it.
	bool TradeEndedWith(const CrossguardScenario::FScenarioContext& C, ECrossguardTradeEnd Reason)
	{
		const FString Ending = FString::Printf(TEXT(" ended: %d"), static_cast<int32>(Reason));
		for (const FString& Line : C.LogLines())
		{
			if (Line.Contains(TEXT("Trade of ")) && Line.Contains(Ending))
			{
				return true;
			}
		}
		return false;
	}

	// "Trade offer from <owner> refused: <ETradeError>".
	bool OfferRefusedWith(const CrossguardScenario::FScenarioContext& C, Crossguard::ETradeError Error)
	{
		const FString Refusal = FString::Printf(TEXT("refused: %d"), static_cast<int32>(Error));
		for (const FString& Line : C.LogLines())
		{
			if (Line.Contains(TEXT("Trade offer from ")) && Line.TrimEnd().EndsWith(Refusal))
			{
				return true;
			}
		}
		return false;
	}

	// Spawns players A and B Gap apart on Row, facing each other, both with a player state.
	bool SpawnPair(CrossguardScenario::FScenarioContext& C)
	{
		ACrossguardCharacter* A = C.SpawnPlayer(TEXT("A"), FVector(0.0f, Row, 0.0f), 0.0f);
		ACrossguardCharacter* B = C.SpawnPlayer(TEXT("B"), FVector(Gap, Row, 0.0f), 180.0f);
		return C.Require(A != nullptr && B != nullptr, TEXT("A and B spawned"))
			&& C.Require(C.PlayerState(A) != nullptr && C.PlayerState(B) != nullptr, TEXT("A and B have an ACrossguardPlayerState"))
			&& C.Require(TradeOf(A) != nullptr && TradeOf(B) != nullptr, TEXT("A and B have a trade component"))
			&& C.Require(InventoryOf(A) != nullptr && InventoryOf(B) != nullptr, TEXT("A and B have an inventory"));
	}

	bool GetPair(CrossguardScenario::FScenarioContext& C, ACrossguardCharacter*& OutA, ACrossguardCharacter*& OutB)
	{
		OutA = C.Get<ACrossguardCharacter>(TEXT("A"));
		OutB = C.Get<ACrossguardCharacter>(TEXT("B"));
		return C.Require(OutA != nullptr && OutB != nullptr && TradeOf(OutA) != nullptr && TradeOf(OutB) != nullptr, TEXT("A and B still exist"));
	}

	// A asks B, B accepts: the window is open on both sides.
	bool OpenTrade(CrossguardScenario::FScenarioContext& C, ACrossguardCharacter* A, ACrossguardCharacter* B)
	{
		UCrossguardTradeComponent* TradeA = TradeOf(A);
		UCrossguardTradeComponent* TradeB = TradeOf(B);
		TradeA->RequestTrade(B);
		C.Test.TestEqual(TEXT("A's trade state after asking"), EnumName(TradeA->GetTradeView().State), EnumName(ECrossguardTradeState::RequestSent));
		C.Test.TestEqual(TEXT("B's trade state after being asked"), EnumName(TradeB->GetTradeView().State),
			EnumName(ECrossguardTradeState::RequestReceived));
		C.Test.TestTrue(TEXT("B's partner is A"), TradeB->GetTradeView().Partner.Get() == A);
		TradeB->RespondToTrade(true);
		return C.Require(TradeA->GetTradeView().State == ECrossguardTradeState::Open && TradeB->GetTradeView().State == ECrossguardTradeState::Open,
			TEXT("the trade window is open on both sides"));
	}

	void LockBoth(ACrossguardCharacter* A, ACrossguardCharacter* B)
	{
		TradeOf(A)->RequestLock(true);
		TradeOf(B)->RequestLock(true);
	}

	bool BothIdle(const ACrossguardCharacter* A, const ACrossguardCharacter* B)
	{
		return TradeOf(A)->GetTradeView().State == ECrossguardTradeState::None && TradeOf(B)->GetTradeView().State == ECrossguardTradeState::None;
	}

	UCrossguardGuildSubsystem* Guilds(CrossguardScenario::FScenarioContext& C)
	{
		UGameInstance* GameInstance = C.World() != nullptr ? C.World()->GetGameInstance() : nullptr;
		UCrossguardGuildSubsystem* Subsystem = GameInstance != nullptr ? GameInstance->GetSubsystem<UCrossguardGuildSubsystem>() : nullptr;
		C.Require(Subsystem != nullptr, TEXT("the PIE game instance has a UCrossguardGuildSubsystem"));
		return Subsystem;
	}

	UCrossguardGuildComponent* GuildOf(const ACrossguardPlayerState* PlayerState)
	{
		return PlayerState != nullptr ? PlayerState->GetGuild() : nullptr;
	}

	// For the guild tests: A and B's player states with guild components and distinct guild ids.
	bool GetGuildPair(CrossguardScenario::FScenarioContext& C, ACrossguardPlayerState*& OutA, ACrossguardPlayerState*& OutB)
	{
		ACrossguardCharacter* A = nullptr;
		ACrossguardCharacter* B = nullptr;
		if (!GetPair(C, A, B))
		{
			return false;
		}
		OutA = C.PlayerState(A);
		OutB = C.PlayerState(B);
		return C.Require(OutA != nullptr && OutB != nullptr && GuildOf(OutA) != nullptr && GuildOf(OutB) != nullptr,
				   TEXT("A and B have player states with a guild component"))
			&& C.Require(UCrossguardGuildSubsystem::PlayerIdOf(OutA) != UCrossguardGuildSubsystem::PlayerIdOf(OutB),
				TEXT("A and B have different guild player ids (PlayerIdOf: session-<PlayerId> without an online id)"));
	}

	void ExpectGuild(CrossguardScenario::FScenarioContext& C, const TCHAR* What, ECrossguardGuildResult Actual, ECrossguardGuildResult Expected)
	{
		C.Test.TestEqual(What, EnumName(Actual), EnumName(Expected));
	}

	// "Guild request from <owner> refused: <ECrossguardGuildResult>".
	bool GuildRefusedWith(const CrossguardScenario::FScenarioContext& C, ECrossguardGuildResult Result)
	{
		const FString Refusal = FString::Printf(TEXT("refused: %s"), *EnumName(Result));
		for (const FString& Line : C.LogLines())
		{
			if (Line.Contains(TEXT("Guild request from ")) && Line.TrimEnd().EndsWith(Refusal))
			{
				return true;
			}
		}
		return false;
	}

	bool FindInStash(const UCrossguardStashComponent* Stash, FName ItemId, FCrossguardInventoryItem& OutItem)
	{
		for (const FCrossguardStashTab& Tab : Stash->GetTabs())
		{
			for (const FCrossguardInventoryItem& Item : Tab.Items)
			{
				if (Item.ItemId == ItemId)
				{
					OutItem = Item;
					return true;
				}
			}
		}
		return false;
	}

	// What an item is, for counting across containers: category, definition id, rarity.
	FString ItemKey(const FCrossguardInventoryItem& Item)
	{
		const FName Id = Item.Category == ECrossguardItemCategory::Weapon ? Item.Weapon.WeaponId
			: Item.Category == ECrossguardItemCategory::Armor ? Item.Armor.ArmorId : Item.ItemId;
		return FString::Printf(TEXT("%s/%s/%s"), *EnumName(Item.Category), *Id.ToString(), *EnumName(Item.Rarity));
	}

	void AddCounts(TMap<FString, int32>& Totals, const TArray<FCrossguardInventoryItem>& Items)
	{
		for (const FCrossguardInventoryItem& Item : Items)
		{
			Totals.FindOrAdd(ItemKey(Item)) += FMath::Max(1, Item.Count);
		}
	}
}

// ================================================================================================
// Trading
// ================================================================================================

// Checklist 4: "Trading (2 clients): TradeNearest; TradeRespond 1; each TradeOffer, TradeLock 1,
// TradeAccept -> after both accept, the items swap inventories."
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioTradeSwap, "Crossguard.Scenario.Social.TradeSwap", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioTradeSwap::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSocial;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A carries 3 healing potions, B a Rare ruby, 2 m apart"), [](FScenarioContext& C)
	{
		if (!SpawnPair(C))
		{
			return;
		}
		ACrossguardCharacter* A = C.Get<ACrossguardCharacter>(TEXT("A"));
		ACrossguardCharacter* B = C.Get<ACrossguardCharacter>(TEXT("B"));
		C.Require(C.GiveItem(A, Potion(), 3) == 0 && C.GiveItem(B, Ruby(), 1, ECrossguardRarity::Rare) == 0, TEXT("the items fit"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("A asks, B accepts, both offer"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = nullptr;
		ACrossguardCharacter* B = nullptr;
		if (!GetPair(C, A, B) || !OpenTrade(C, A, B))
		{
			return;
		}
		const int32 PotionId = StackId(A, Potion());
		const int32 RubyId = StackId(B, Ruby());
		TradeOf(A)->RequestSetOffer({PotionId}, 0);
		TradeOf(B)->RequestSetOffer({RubyId}, 0);

		const FCrossguardTradeView& ViewA = TradeOf(A)->GetTradeView();
		const FCrossguardTradeView& ViewB = TradeOf(B)->GetTradeView();
		C.Test.TestTrue(TEXT("A's offer is the potion stack"), ViewA.MyItemIds.Num() == 1 && ViewA.MyItemIds[0] == PotionId);
		C.Test.TestTrue(TEXT("A sees B's Rare ruby"),
			ViewA.TheirItems.Num() == 1 && ViewA.TheirItems[0].ItemId == Ruby() && ViewA.TheirItems[0].Rarity == ECrossguardRarity::Rare);
		C.Test.TestTrue(TEXT("B sees A's 3 potions"), ViewB.TheirItems.Num() == 1 && ViewB.TheirItems[0].ItemId == Potion()
			&& ViewB.TheirItems[0].Count == 3);

		// Accepting needs both offers locked.
		TradeOf(A)->RequestAccept();
		C.Test.TestFalse(TEXT("A cannot accept before both lock"), ViewA.bMyAccepted);
	});

	Scenario.Step(TEXT("Both lock; A accepts"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = nullptr;
		ACrossguardCharacter* B = nullptr;
		if (!GetPair(C, A, B))
		{
			return;
		}
		LockBoth(A, B);
		const FCrossguardTradeView& ViewA = TradeOf(A)->GetTradeView();
		const FCrossguardTradeView& ViewB = TradeOf(B)->GetTradeView();
		C.Test.TestTrue(TEXT("A sees both locked"), ViewA.bMyLocked && ViewA.bTheirLocked);
		C.Test.TestTrue(TEXT("B sees both locked"), ViewB.bMyLocked && ViewB.bTheirLocked);
		TradeOf(A)->RequestAccept();
		C.Test.TestTrue(TEXT("A has accepted"), ViewA.bMyAccepted);
		C.Test.TestTrue(TEXT("B sees A's acceptance"), ViewB.bTheirAccepted);
		C.Test.TestEqual(TEXT("Still open until B accepts"), EnumName(ViewA.State), EnumName(ECrossguardTradeState::Open));
		C.Test.TestEqual(TEXT("A still has the potions"), CountOf(A, Potion()), 3);
	});

	Scenario.Step(TEXT("B accepts: the items swap"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = nullptr;
		ACrossguardCharacter* B = nullptr;
		if (!GetPair(C, A, B))
		{
			return;
		}
		C.ClearLog();
		TradeOf(B)->RequestAccept();
		C.Test.TestTrue(TEXT("The trade is over on both sides"), BothIdle(A, B));
		C.Test.TestTrue(TEXT("Log: the trade ended Completed"), TradeEndedWith(C, ECrossguardTradeEnd::Completed));
		C.Test.TestEqual(TEXT("A's potions"), CountOf(A, Potion()), 0);
		C.Test.TestEqual(TEXT("A's rubies"), CountOf(A, Ruby()), 1);
		C.Test.TestEqual(TEXT("The ruby A got is Rare"), EnumName(RarityOf(A, Ruby())), EnumName(ECrossguardRarity::Rare));
		C.Test.TestEqual(TEXT("B's potions"), CountOf(B, Potion()), 3);
		C.Test.TestEqual(TEXT("B's rubies"), CountOf(B, Ruby()), 0);
	});

	Scenario.Run();
	return true;
}

// docs/trading.md step 4: changing an offer is only possible unlocked, and it unlocks both sides;
// unlocking takes back both acceptances.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioTradeChangeUnlocks, "Crossguard.Scenario.Social.TradeChangingAnOfferUnlocksBoth",
	CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioTradeChangeUnlocks::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSocial;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A carries potions and bandages, B a ruby"), [](FScenarioContext& C)
	{
		if (!SpawnPair(C))
		{
			return;
		}
		ACrossguardCharacter* A = C.Get<ACrossguardCharacter>(TEXT("A"));
		ACrossguardCharacter* B = C.Get<ACrossguardCharacter>(TEXT("B"));
		C.Require(C.GiveItem(A, Potion(), 3) == 0 && C.GiveItem(A, Bandage(), 2) == 0 && C.GiveItem(B, Ruby(), 1, ECrossguardRarity::Rare) == 0,
			TEXT("the items fit"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Offers made, both lock, B accepts"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = nullptr;
		ACrossguardCharacter* B = nullptr;
		if (!GetPair(C, A, B) || !OpenTrade(C, A, B))
		{
			return;
		}
		TradeOf(A)->RequestSetOffer({StackId(A, Potion())}, 0);
		TradeOf(B)->RequestSetOffer({StackId(B, Ruby())}, 0);
		LockBoth(A, B);
		TradeOf(B)->RequestAccept();
		C.Require(TradeOf(B)->GetTradeView().bMyAccepted, TEXT("B has accepted"));
	});

	Scenario.Step(TEXT("A cannot change a locked offer"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = nullptr;
		ACrossguardCharacter* B = nullptr;
		if (!GetPair(C, A, B))
		{
			return;
		}
		C.ClearLog();
		TradeOf(A)->RequestSetOffer({StackId(A, Potion()), StackId(A, Bandage())}, 0);
		const FCrossguardTradeView& ViewA = TradeOf(A)->GetTradeView();
		const FCrossguardTradeView& ViewB = TradeOf(B)->GetTradeView();
		C.Test.TestEqual(TEXT("A's offer is still one stack"), ViewA.MyItemIds.Num(), 1);
		C.Test.TestTrue(TEXT("Log: the offer was refused as Locked"), OfferRefusedWith(C, Crossguard::ETradeError::Locked));
		C.Test.TestTrue(TEXT("Both still locked"), ViewA.bMyLocked && ViewB.bMyLocked);
		C.Test.TestTrue(TEXT("B's acceptance stands"), ViewB.bMyAccepted);
	});

	Scenario.Step(TEXT("A unlocks: B's acceptance is taken back, B stays locked"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = nullptr;
		ACrossguardCharacter* B = nullptr;
		if (!GetPair(C, A, B))
		{
			return;
		}
		TradeOf(A)->RequestLock(false);
		const FCrossguardTradeView& ViewB = TradeOf(B)->GetTradeView();
		C.Test.TestFalse(TEXT("A is unlocked"), TradeOf(A)->GetTradeView().bMyLocked);
		C.Test.TestTrue(TEXT("B is still locked"), ViewB.bMyLocked);
		C.Test.TestFalse(TEXT("B's acceptance was taken back"), ViewB.bMyAccepted);
	});

	Scenario.Step(TEXT("A adds the bandages: both sides unlock"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = nullptr;
		ACrossguardCharacter* B = nullptr;
		if (!GetPair(C, A, B))
		{
			return;
		}
		TradeOf(A)->RequestSetOffer({StackId(A, Potion()), StackId(A, Bandage())}, 0);
		const FCrossguardTradeView& ViewA = TradeOf(A)->GetTradeView();
		const FCrossguardTradeView& ViewB = TradeOf(B)->GetTradeView();
		C.Test.TestEqual(TEXT("A's offer is two stacks"), ViewA.MyItemIds.Num(), 2);
		C.Test.TestEqual(TEXT("B sees two stacks"), ViewB.TheirItems.Num(), 2);
		C.Test.TestFalse(TEXT("B was unlocked by the change"), ViewB.bMyLocked);
		C.Test.TestFalse(TEXT("A sees B unlocked"), ViewA.bTheirLocked);
	});

	Scenario.Step(TEXT("Lock and accept again: the new offer swaps"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = nullptr;
		ACrossguardCharacter* B = nullptr;
		if (!GetPair(C, A, B))
		{
			return;
		}
		LockBoth(A, B);
		TradeOf(A)->RequestAccept();
		TradeOf(B)->RequestAccept();
		C.Test.TestTrue(TEXT("The trade is over"), BothIdle(A, B));
		C.Test.TestEqual(TEXT("B's potions"), CountOf(B, Potion()), 3);
		C.Test.TestEqual(TEXT("B's bandages"), CountOf(B, Bandage()), 2);
		C.Test.TestEqual(TEXT("A's rubies"), CountOf(A, Ruby()), 1);
		C.Test.TestEqual(TEXT("A's potions"), CountOf(A, Potion()), 0);
		C.Test.TestEqual(TEXT("A's bandages"), CountOf(A, Bandage()), 0);
	});

	Scenario.Run();
	return true;
}

// docs/trading.md step 4: an offered stack that changes on its own (used up) unlocks both sides
// and the offer follows the stack.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioTradeStackUsedUp, "Crossguard.Scenario.Social.TradeOfferedStackUsedMidTrade",
	CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioTradeStackUsedUp::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSocial;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A carries 3 potions, B a ruby"), [](FScenarioContext& C)
	{
		if (!SpawnPair(C))
		{
			return;
		}
		ACrossguardCharacter* A = C.Get<ACrossguardCharacter>(TEXT("A"));
		ACrossguardCharacter* B = C.Get<ACrossguardCharacter>(TEXT("B"));
		C.Require(C.GiveItem(A, Potion(), 3) == 0 && C.GiveItem(B, Ruby(), 1, ECrossguardRarity::Rare) == 0, TEXT("the items fit"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Offers locked on both sides, then A uses one offered potion"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = nullptr;
		ACrossguardCharacter* B = nullptr;
		if (!GetPair(C, A, B) || !OpenTrade(C, A, B))
		{
			return;
		}
		const int32 PotionId = StackId(A, Potion());
		C.SetNumber(TEXT("PotionId"), PotionId);
		TradeOf(A)->RequestSetOffer({PotionId}, 0);
		TradeOf(B)->RequestSetOffer({StackId(B, Ruby())}, 0);
		LockBoth(A, B);
		if (!C.Require(TradeOf(B)->GetTradeView().bMyLocked && TradeOf(B)->GetTradeView().bTheirLocked, TEXT("both locked")))
		{
			return;
		}
		C.ClearLog();
		C.Require(InventoryOf(A)->ConsumeItem(Potion(), 1), TEXT("one potion of the offered stack is used up"));
	});

	Scenario.WaitUntil(TEXT("the trade notices: both unlocked, the offer shows 2 potions"), TradeCheckTimeout, [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = nullptr;
		ACrossguardCharacter* B = nullptr;
		if (!GetPair(C, A, B))
		{
			return false;
		}
		const FCrossguardTradeView& ViewB = TradeOf(B)->GetTradeView();
		return !ViewB.bMyLocked && !ViewB.bTheirLocked && ViewB.TheirItems.Num() == 1 && ViewB.TheirItems[0].Count == 2;
	});

	Scenario.Step(TEXT("Re-offered at the new count; lock and accept swaps 2 potions"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = nullptr;
		ACrossguardCharacter* B = nullptr;
		if (!GetPair(C, A, B))
		{
			return;
		}
		const FCrossguardTradeView& ViewA = TradeOf(A)->GetTradeView();
		C.Test.TestEqual(TEXT("Still trading"), EnumName(ViewA.State), EnumName(ECrossguardTradeState::Open));
		C.Test.TestTrue(TEXT("A's offer is still the same stack"),
			ViewA.MyItemIds.Num() == 1 && ViewA.MyItemIds[0] == static_cast<int32>(C.Number(TEXT("PotionId"))));
		C.Test.TestTrue(TEXT("Log: an offered stack changed; both unlocked"), C.LogContains(TEXT("an offered stack changed; both unlocked")));

		LockBoth(A, B);
		TradeOf(A)->RequestAccept();
		TradeOf(B)->RequestAccept();
		C.Test.TestTrue(TEXT("The trade is over"), BothIdle(A, B));
		C.Test.TestEqual(TEXT("B received 2 potions"), CountOf(B, Potion()), 2);
		C.Test.TestEqual(TEXT("A has no potions left"), CountOf(A, Potion()), 0);
		C.Test.TestEqual(TEXT("A received the ruby"), CountOf(A, Ruby()), 1);
	});

	Scenario.Run();
	return true;
}

// Checklist 4, Trading: "Walk 6 m apart mid-trade: it ends (TooFar)."
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioTradeTooFar, "Crossguard.Scenario.Social.TradeEndsTooFar", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioTradeTooFar::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSocial;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A and B with something to offer"), [](FScenarioContext& C)
	{
		if (!SpawnPair(C))
		{
			return;
		}
		ACrossguardCharacter* A = C.Get<ACrossguardCharacter>(TEXT("A"));
		ACrossguardCharacter* B = C.Get<ACrossguardCharacter>(TEXT("B"));
		C.Require(C.GiveItem(A, Potion(), 3) == 0 && C.GiveItem(B, Ruby(), 1, ECrossguardRarity::Rare) == 0, TEXT("the items fit"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Trade open with offers; B steps to 4 m"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = nullptr;
		ACrossguardCharacter* B = nullptr;
		if (!GetPair(C, A, B) || !OpenTrade(C, A, B))
		{
			return;
		}
		TradeOf(A)->RequestSetOffer({StackId(A, Potion())}, 0);
		TradeOf(B)->RequestSetOffer({StackId(B, Ruby())}, 0);
		C.Place(B, FVector(400.0f, Row, 0.0f), 180.0f);
	});

	// Two distance checks at 4 m.
	Scenario.Wait(1.2f);

	Scenario.Step(TEXT("Still open at 4 m; B walks to 6 m"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = nullptr;
		ACrossguardCharacter* B = nullptr;
		if (!GetPair(C, A, B))
		{
			return;
		}
		C.Test.TestEqual(TEXT("Open at 4 m"), EnumName(TradeOf(A)->GetTradeView().State), EnumName(ECrossguardTradeState::Open));
		C.ClearLog();
		C.Place(B, FVector(600.0f, Row, 0.0f), 180.0f);
	});

	Scenario.WaitUntil(TEXT("the trade ends on both sides"), TradeCheckTimeout, [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = nullptr;
		ACrossguardCharacter* B = nullptr;
		return GetPair(C, A, B) && BothIdle(A, B);
	});

	Scenario.Step(TEXT("Ended TooFar; nothing moved"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = nullptr;
		ACrossguardCharacter* B = nullptr;
		if (!GetPair(C, A, B))
		{
			return;
		}
		C.Test.TestTrue(TEXT("Log: the trade ended TooFar"), TradeEndedWith(C, ECrossguardTradeEnd::TooFar));
		C.Test.TestEqual(TEXT("A keeps the potions"), CountOf(A, Potion()), 3);
		C.Test.TestEqual(TEXT("B keeps the ruby"), CountOf(B, Ruby()), 1);
	});

	Scenario.Run();
	return true;
}

// docs/trading.md step 3: banked gold can be offered between raids only (in a raid carried gold is
// coins, offered as items).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioTradeBankedGold, "Crossguard.Scenario.Social.TradeBankedGoldBetweenRaidsOnly",
	CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioTradeBankedGold::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSocial;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("In a raid: A has 500 banked gold, B a potion"), [](FScenarioContext& C)
	{
		if (!SpawnPair(C))
		{
			return;
		}
		ACrossguardCharacter* A = C.Get<ACrossguardCharacter>(TEXT("A"));
		ACrossguardCharacter* B = C.Get<ACrossguardCharacter>(TEXT("B"));
		C.PlayerState(A)->AddGold(500);
		C.Require(C.PlayerState(A)->GetGold() == 500, TEXT("A has 500 banked gold"));
		C.Require(C.GiveItem(B, Potion(), 1) == 0, TEXT("the potion fits"));
		C.SetNumber(TEXT("GoldB"), C.PlayerState(B)->GetGold());
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("In a raid, offering banked gold is refused"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = nullptr;
		ACrossguardCharacter* B = nullptr;
		if (!GetPair(C, A, B) || !C.Require(C.PlayerState(A)->IsInRaid(), TEXT("A is in a raid")) || !OpenTrade(C, A, B))
		{
			return;
		}
		C.ClearLog();
		TradeOf(A)->RequestSetOffer({}, 100);
		C.Test.TestEqual(TEXT("No gold on the table in a raid"), TradeOf(A)->GetTradeView().MyGold, 0);
		C.Test.TestTrue(TEXT("Log: refused NotEnoughGold"), OfferRefusedWith(C, Crossguard::ETradeError::NotEnoughGold));
		TradeOf(A)->RequestCancel();
		C.Test.TestTrue(TEXT("Cancelled on both sides"), BothIdle(A, B));
		C.Test.TestTrue(TEXT("Log: the trade ended Cancelled"), TradeEndedWith(C, ECrossguardTradeEnd::Cancelled));
	});

	Scenario.Step(TEXT("Between raids"), [](FScenarioContext& C)
	{
		C.SetRaid(false);
	});

	Scenario.Step(TEXT("Between raids: more than banked is refused, 200 of 500 is offered"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = nullptr;
		ACrossguardCharacter* B = nullptr;
		if (!GetPair(C, A, B) || !C.Require(!C.PlayerState(A)->IsInRaid(), TEXT("A is between raids")) || !OpenTrade(C, A, B))
		{
			return;
		}
		TradeOf(A)->RequestSetOffer({}, 600);
		C.Test.TestEqual(TEXT("600 (more than banked) refused"), TradeOf(A)->GetTradeView().MyGold, 0);
		TradeOf(A)->RequestSetOffer({}, 200);
		TradeOf(B)->RequestSetOffer({StackId(B, Potion())}, 0);
		C.Test.TestEqual(TEXT("A offers 200 gold"), TradeOf(A)->GetTradeView().MyGold, 200);
		C.Test.TestEqual(TEXT("B sees 200 gold"), TradeOf(B)->GetTradeView().TheirGold, 200);
		LockBoth(A, B);
		TradeOf(A)->RequestAccept();
		TradeOf(B)->RequestAccept();
		C.Test.TestTrue(TEXT("The trade is over"), BothIdle(A, B));
		C.Test.TestEqual(TEXT("A's banked gold"), C.PlayerState(A)->GetGold(), 300);
		C.Test.TestEqual(TEXT("B's banked gold"), C.PlayerState(B)->GetGold(), static_cast<int32>(C.Number(TEXT("GoldB"))) + 200);
		C.Test.TestEqual(TEXT("A received the potion"), CountOf(A, Potion()), 1);
		C.Test.TestEqual(TEXT("B gave the potion"), CountOf(B, Potion()), 0);
	});

	Scenario.Run();
	return true;
}

// docs/trading.md step 5: no room for what you get once your own offer is gone -> nothing
// changes and the trade ends (NoRoom).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioTradeNoRoom, "Crossguard.Scenario.Social.TradeNoRoomChangesNothing", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioTradeNoRoom::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSocial;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A has 3 potions; B's inventory is full of rubies"), [](FScenarioContext& C)
	{
		if (!SpawnPair(C))
		{
			return;
		}
		ACrossguardCharacter* A = C.Get<ACrossguardCharacter>(TEXT("A"));
		ACrossguardCharacter* B = C.Get<ACrossguardCharacter>(TEXT("B"));
		C.Require(C.GiveItem(A, Potion(), 3) == 0, TEXT("the potions fit"));
		// Rubies are 1 x 1 and do not stack: more than the grid holds, straight into the inventory
		// (GiveItem would warn about what does not fit).
		FCrossguardInventoryItem Rubies;
		Rubies.Category = ECrossguardItemCategory::Trinket;
		Rubies.ItemId = Ruby();
		Rubies.Count = 200;
		const int32 Left = InventoryOf(B)->AddItem(Rubies);
		C.Require(Left > 0 && Left < 200, TEXT("B's inventory is now full (some rubies did not fit)"));
		C.SetNumber(TEXT("RubiesB"), CountOf(B, Ruby()));
		C.SetNumber(TEXT("StacksB"), InventoryOf(B)->GetItems().Num());
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("A offers the potions, B nothing; both lock and accept"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = nullptr;
		ACrossguardCharacter* B = nullptr;
		if (!GetPair(C, A, B) || !OpenTrade(C, A, B))
		{
			return;
		}
		TradeOf(A)->RequestSetOffer({StackId(A, Potion())}, 0);
		LockBoth(A, B);
		C.ClearLog();
		TradeOf(A)->RequestAccept();
		TradeOf(B)->RequestAccept();
		C.Test.TestTrue(TEXT("The trade is over"), BothIdle(A, B));
		C.Test.TestTrue(TEXT("Log: the trade ended NoRoom"), TradeEndedWith(C, ECrossguardTradeEnd::NoRoom));
		C.Test.TestEqual(TEXT("A keeps the potions"), CountOf(A, Potion()), 3);
		C.Test.TestEqual(TEXT("B got no potion"), CountOf(B, Potion()), 0);
		C.Test.TestEqual(TEXT("B's rubies unchanged"), CountOf(B, Ruby()), static_cast<int32>(C.Number(TEXT("RubiesB"))));
		C.Test.TestEqual(TEXT("B's stacks unchanged"), InventoryOf(B)->GetItems().Num(), static_cast<int32>(C.Number(TEXT("StacksB"))));
	});

	Scenario.Run();
	return true;
}

// docs/trading.md: the trade ends when either side dies (PartnerGone); a dead player cannot be asked.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioTradePartnerGone, "Crossguard.Scenario.Social.TradeEndsWhenPartnerDies", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioTradePartnerGone::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSocial;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A and B with something to offer"), [](FScenarioContext& C)
	{
		if (!SpawnPair(C))
		{
			return;
		}
		ACrossguardCharacter* A = C.Get<ACrossguardCharacter>(TEXT("A"));
		ACrossguardCharacter* B = C.Get<ACrossguardCharacter>(TEXT("B"));
		C.Require(C.GiveItem(A, Potion(), 3) == 0 && C.GiveItem(B, Ruby(), 1, ECrossguardRarity::Rare) == 0, TEXT("the items fit"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Trade open with offers; B dies"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = nullptr;
		ACrossguardCharacter* B = nullptr;
		if (!GetPair(C, A, B) || !OpenTrade(C, A, B))
		{
			return;
		}
		TradeOf(A)->RequestSetOffer({StackId(A, Potion())}, 0);
		TradeOf(B)->RequestSetOffer({StackId(B, Ruby())}, 0);
		C.ClearLog();
		C.Kill(B);
	});

	Scenario.WaitUntil(TEXT("A's trade ends"), TradeCheckTimeout, [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = C.Get<ACrossguardCharacter>(TEXT("A"));
		return A != nullptr && TradeOf(A) != nullptr && TradeOf(A)->GetTradeView().State == ECrossguardTradeState::None;
	});

	Scenario.Step(TEXT("Ended PartnerGone; A keeps its items and cannot ask the dead B"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = nullptr;
		ACrossguardCharacter* B = nullptr;
		if (!GetPair(C, A, B))
		{
			return;
		}
		C.Test.TestTrue(TEXT("B is dead"), C.IsDead(B));
		C.Test.TestTrue(TEXT("Log: the trade ended PartnerGone"), TradeEndedWith(C, ECrossguardTradeEnd::PartnerGone));
		C.Test.TestEqual(TEXT("A keeps the potions"), CountOf(A, Potion()), 3);
		C.Test.TestEqual(TEXT("A got no ruby"), CountOf(A, Ruby()), 0);
		TradeOf(A)->RequestTrade(B);
		C.Test.TestEqual(TEXT("Asking a dead player is refused"), EnumName(TradeOf(A)->GetTradeView().State), EnumName(ECrossguardTradeState::None));
	});

	Scenario.Run();
	return true;
}

// ================================================================================================
// Guilds
// ================================================================================================

// Checklist 4, Guilds: "GuildCreate "Night Watch", GuildInviteNearest; client 2 GuildAccept;
// GuildInfo -> the log shows the guild, both members." Through the component requests (the RPC
// path the console commands use).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioGuildCreateInviteAccept, "Crossguard.Scenario.Social.GuildCreateInviteAccept",
	CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioGuildCreateInviteAccept::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSocial;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A, B and an outsider"), [](FScenarioContext& C)
	{
		if (SpawnPair(C))
		{
			C.Require(C.SpawnPlayer(TEXT("Outsider"), FVector(0.0f, Row + 300.0f, 0.0f)) != nullptr, TEXT("the outsider spawned"));
		}
	});

	Scenario.Step(TEXT("A founds Night Watch; B cannot take the name in other case; a bad name is refused"), [](FScenarioContext& C)
	{
		ACrossguardPlayerState* A = nullptr;
		ACrossguardPlayerState* B = nullptr;
		if (!GetGuildPair(C, A, B))
		{
			return;
		}
		C.ClearLog();
		GuildOf(A)->RequestCreate(TEXT("Night Watch"));
		UCrossguardGuildComponent* GuildA = GuildOf(A);
		if (!C.Require(GuildA->IsInGuild(), TEXT("A is in a guild after RequestCreate")))
		{
			return;
		}
		C.Test.TestEqual(TEXT("Guild name"), GuildA->GuildName, FString(TEXT("Night Watch")));
		C.Test.TestEqual(TEXT("A's rank"), EnumName(GuildA->Rank), EnumName(ECrossguardGuildRank::Leader));
		C.Test.TestEqual(TEXT("Roster size"), GuildA->Members.Num(), 1);

		GuildOf(B)->RequestCreate(TEXT("night watch"));
		C.Test.TestFalse(TEXT("B founded nothing"), GuildOf(B)->IsInGuild());
		C.Test.TestTrue(TEXT("Log: refused NameTaken"), GuildRefusedWith(C, ECrossguardGuildResult::NameTaken));
		GuildOf(B)->RequestCreate(TEXT("ab"));
		C.Test.TestTrue(TEXT("Log: refused InvalidName"), GuildRefusedWith(C, ECrossguardGuildResult::InvalidName));
	});

	Scenario.Step(TEXT("A invites B; B accepts and joins as Member"), [](FScenarioContext& C)
	{
		ACrossguardPlayerState* A = nullptr;
		ACrossguardPlayerState* B = nullptr;
		if (!GetGuildPair(C, A, B))
		{
			return;
		}
		UCrossguardGuildComponent* GuildA = GuildOf(A);
		UCrossguardGuildComponent* GuildB = GuildOf(B);
		GuildA->RequestInvite(B);
		if (!C.Require(GuildB->Invites.Num() == 1, TEXT("B holds one invite")))
		{
			return;
		}
		C.Test.TestEqual(TEXT("The invite names the guild"), GuildB->Invites[0].GuildName, FString(TEXT("Night Watch")));
		C.Test.TestEqual(TEXT("The invite is for A's guild"), GuildB->Invites[0].GuildId, GuildA->GuildId);
		GuildB->RequestAcceptInvite(GuildB->Invites[0].GuildId);

		C.Test.TestEqual(TEXT("B's guild"), GuildB->GuildId, GuildA->GuildId);
		C.Test.TestEqual(TEXT("B's rank"), EnumName(GuildB->Rank), EnumName(ECrossguardGuildRank::Member));
		C.Test.TestEqual(TEXT("B's invites are used up"), GuildB->Invites.Num(), 0);
		C.Test.TestEqual(TEXT("A's roster"), GuildA->Members.Num(), 2);
		C.Test.TestEqual(TEXT("B's roster"), GuildB->Members.Num(), 2);
		const FString IdB = UCrossguardGuildSubsystem::PlayerIdOf(B);
		const FCrossguardGuildMemberView* Entry = GuildA->Members.FindByPredicate([&IdB](const FCrossguardGuildMemberView& Member)
		{
			return Member.PlayerId == IdB;
		});
		if (C.Require(Entry != nullptr, TEXT("B is on A's roster")))
		{
			C.Test.TestEqual(TEXT("B's roster rank"), EnumName(Entry->Rank), EnumName(ECrossguardGuildRank::Member));
			C.Test.TestTrue(TEXT("B shows online"), Entry->bOnline);
			C.Test.TestEqual(TEXT("B's roster name"), Entry->DisplayName, B->GetPlayerName());
		}
	});

	Scenario.Step(TEXT("A Member cannot invite"), [](FScenarioContext& C)
	{
		ACrossguardPlayerState* A = nullptr;
		ACrossguardPlayerState* B = nullptr;
		ACrossguardCharacter* Outsider = C.Get<ACrossguardCharacter>(TEXT("Outsider"));
		if (!GetGuildPair(C, A, B) || !C.Require(Outsider != nullptr && C.PlayerState(Outsider) != nullptr, TEXT("the outsider exists")))
		{
			return;
		}
		C.ClearLog();
		GuildOf(B)->RequestInvite(C.PlayerState(Outsider));
		C.Test.TestEqual(TEXT("The outsider got no invite"), GuildOf(C.PlayerState(Outsider))->Invites.Num(), 0);
		C.Test.TestTrue(TEXT("Log: refused NotPermitted"), GuildRefusedWith(C, ECrossguardGuildResult::NotPermitted));
	});

	Scenario.Run();
	return true;
}

// Checklist 4, Guilds: "client 1 GuildDeposit; client 2 (Member) GuildWithdraw is refused
// (NotPermitted) until client 1 runs GuildRank Officer." guild-rules.md: every member deposits;
// only officers withdraw. Gold and items, between raids.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioGuildRanks, "Crossguard.Scenario.Social.GuildBankRankPermissions", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioGuildRanks::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSocial;

	FScenario Scenario(*this);
	Scenario.Raid(false);

	Scenario.Step(TEXT("Between raids: A leads Night Watch, B joins with 500 gold and 3 potions"), [](FScenarioContext& C)
	{
		if (!SpawnPair(C))
		{
			return;
		}
		ACrossguardPlayerState* A = nullptr;
		ACrossguardPlayerState* B = nullptr;
		UCrossguardGuildSubsystem* Subsystem = Guilds(C);
		if (Subsystem == nullptr || !GetGuildPair(C, A, B))
		{
			return;
		}
		B->AddGold(500);
		C.Require(C.GiveItem(C.Get<ACrossguardCharacter>(TEXT("B")), Potion(), 3) == 0, TEXT("the potions fit"));
		C.Require(Subsystem->Create(A, TEXT("Night Watch")) == ECrossguardGuildResult::None, TEXT("A founds Night Watch"));
		C.Require(Subsystem->Invite(A, B) == ECrossguardGuildResult::None, TEXT("A invites B"));
		C.Require(Subsystem->Accept(B, GuildOf(A)->GuildId) == ECrossguardGuildResult::None, TEXT("B accepts"));
		C.Require(GuildOf(B)->Rank == ECrossguardGuildRank::Member, TEXT("B is a Member"));
	});

	Scenario.Step(TEXT("Member B deposits 100 gold and the potions"), [](FScenarioContext& C)
	{
		ACrossguardPlayerState* A = nullptr;
		ACrossguardPlayerState* B = nullptr;
		UCrossguardGuildSubsystem* Subsystem = Guilds(C);
		ACrossguardCharacter* CharacterB = C.Get<ACrossguardCharacter>(TEXT("B"));
		if (Subsystem == nullptr || !GetGuildPair(C, A, B) || CharacterB == nullptr)
		{
			return;
		}
		ExpectGuild(C, TEXT("Member deposits gold"), Subsystem->MoveGold(B, 100), ECrossguardGuildResult::None);
		C.Test.TestEqual(TEXT("B's banked gold"), B->GetGold(), 400);
		C.Test.TestEqual(TEXT("Bank gold (A's view)"), GuildOf(A)->BankGold, static_cast<int64>(100));
		ExpectGuild(C, TEXT("Member deposits an item"), Subsystem->DepositItem(B, StackId(CharacterB, Potion())), ECrossguardGuildResult::None);
		C.Test.TestEqual(TEXT("B's potions"), CountOf(CharacterB, Potion()), 0);
		if (C.Require(GuildOf(B)->BankItems.Num() == 1, TEXT("the bank holds one stack")))
		{
			C.Test.TestTrue(TEXT("Banked stack is the potions"), GuildOf(B)->BankItems[0].Item.ItemId == Potion());
			C.Test.TestEqual(TEXT("Banked count"), GuildOf(B)->BankItems[0].Item.Count, 3);
		}
	});

	Scenario.Step(TEXT("Member B cannot withdraw"), [](FScenarioContext& C)
	{
		ACrossguardPlayerState* A = nullptr;
		ACrossguardPlayerState* B = nullptr;
		UCrossguardGuildSubsystem* Subsystem = Guilds(C);
		ACrossguardCharacter* CharacterB = C.Get<ACrossguardCharacter>(TEXT("B"));
		if (Subsystem == nullptr || !GetGuildPair(C, A, B) || CharacterB == nullptr || GuildOf(B)->BankItems.Num() != 1)
		{
			C.Fail(TEXT("set-up missing"));
			return;
		}
		const int32 Slot = GuildOf(B)->BankItems[0].SlotId;
		ExpectGuild(C, TEXT("Member withdraws gold"), Subsystem->MoveGold(B, -50), ECrossguardGuildResult::NotPermitted);
		ExpectGuild(C, TEXT("Member withdraws the item"), Subsystem->WithdrawItem(B, Slot), ECrossguardGuildResult::NotPermitted);
		C.Test.TestEqual(TEXT("B's banked gold unchanged"), B->GetGold(), 400);
		C.Test.TestEqual(TEXT("Bank gold unchanged"), GuildOf(B)->BankGold, static_cast<int64>(100));
		C.Test.TestEqual(TEXT("The stack stays in the bank"), GuildOf(B)->BankItems.Num(), 1);
		C.Test.TestEqual(TEXT("B has no potions"), CountOf(CharacterB, Potion()), 0);

		// Through the component too (the GuildWithdraw console path).
		C.ClearLog();
		GuildOf(B)->RequestWithdrawItem(Slot);
		C.Test.TestTrue(TEXT("Log: refused NotPermitted"), GuildRefusedWith(C, ECrossguardGuildResult::NotPermitted));
	});

	Scenario.Step(TEXT("A makes B an Officer; now B withdraws"), [](FScenarioContext& C)
	{
		ACrossguardPlayerState* A = nullptr;
		ACrossguardPlayerState* B = nullptr;
		UCrossguardGuildSubsystem* Subsystem = Guilds(C);
		ACrossguardCharacter* CharacterB = C.Get<ACrossguardCharacter>(TEXT("B"));
		if (Subsystem == nullptr || !GetGuildPair(C, A, B) || CharacterB == nullptr || GuildOf(B)->BankItems.Num() != 1)
		{
			C.Fail(TEXT("set-up missing"));
			return;
		}
		ExpectGuild(C, TEXT("A promotes B"), Subsystem->SetRank(A, UCrossguardGuildSubsystem::PlayerIdOf(B), ECrossguardGuildRank::Officer),
			ECrossguardGuildResult::None);
		C.Test.TestEqual(TEXT("B's rank"), EnumName(GuildOf(B)->Rank), EnumName(ECrossguardGuildRank::Officer));
		const int32 Slot = GuildOf(B)->BankItems[0].SlotId;
		ExpectGuild(C, TEXT("Officer withdraws the item"), Subsystem->WithdrawItem(B, Slot), ECrossguardGuildResult::None);
		C.Test.TestEqual(TEXT("B has the potions back"), CountOf(CharacterB, Potion()), 3);
		C.Test.TestEqual(TEXT("The bank holds no stacks"), GuildOf(A)->BankItems.Num(), 0);
		ExpectGuild(C, TEXT("Officer withdraws gold"), Subsystem->MoveGold(B, -50), ECrossguardGuildResult::None);
		C.Test.TestEqual(TEXT("B's banked gold"), B->GetGold(), 450);
		C.Test.TestEqual(TEXT("Bank gold"), GuildOf(A)->BankGold, static_cast<int64>(50));
	});

	Scenario.Run();
	return true;
}

// guild-rules.md, "In the game": the bank is used between raids only; membership works anywhere.
// The last member leaving disbands the guild and takes the bank (between raids only).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioGuildBankRaids, "Crossguard.Scenario.Social.GuildBankBetweenRaidsAndDisband",
	CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioGuildBankRaids::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSocial;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("In a raid: A founds Iron Wolves, B joins (membership works in raids)"), [](FScenarioContext& C)
	{
		if (!SpawnPair(C))
		{
			return;
		}
		ACrossguardPlayerState* A = nullptr;
		ACrossguardPlayerState* B = nullptr;
		UCrossguardGuildSubsystem* Subsystem = Guilds(C);
		if (Subsystem == nullptr || !GetGuildPair(C, A, B) || !C.Require(A->IsInRaid(), TEXT("A is in a raid")))
		{
			return;
		}
		A->AddGold(1000);
		C.Require(C.GiveItem(C.Get<ACrossguardCharacter>(TEXT("A")), Ruby(), 1, ECrossguardRarity::Rare) == 0, TEXT("the ruby fits"));
		ExpectGuild(C, TEXT("Create in a raid"), Subsystem->Create(A, TEXT("Iron Wolves")), ECrossguardGuildResult::None);
		ExpectGuild(C, TEXT("Invite in a raid"), Subsystem->Invite(A, B), ECrossguardGuildResult::None);
		ExpectGuild(C, TEXT("Accept in a raid"), Subsystem->Accept(B, GuildOf(A)->GuildId), ECrossguardGuildResult::None);
	});

	Scenario.Step(TEXT("In a raid the bank is refused"), [](FScenarioContext& C)
	{
		ACrossguardPlayerState* A = nullptr;
		ACrossguardPlayerState* B = nullptr;
		UCrossguardGuildSubsystem* Subsystem = Guilds(C);
		ACrossguardCharacter* CharacterA = C.Get<ACrossguardCharacter>(TEXT("A"));
		if (Subsystem == nullptr || !GetGuildPair(C, A, B) || CharacterA == nullptr)
		{
			return;
		}
		ExpectGuild(C, TEXT("Deposit gold in a raid"), Subsystem->MoveGold(A, 300), ECrossguardGuildResult::OnlyBetweenRaids);
		ExpectGuild(C, TEXT("Deposit an item in a raid"), Subsystem->DepositItem(A, StackId(CharacterA, Ruby())),
			ECrossguardGuildResult::OnlyBetweenRaids);
		C.Test.TestEqual(TEXT("A's banked gold unchanged"), A->GetGold(), 1000);
		C.Test.TestEqual(TEXT("A keeps the ruby"), CountOf(CharacterA, Ruby()), 1);
		C.Test.TestEqual(TEXT("Bank gold"), GuildOf(A)->BankGold, static_cast<int64>(0));
		C.SetRaid(false);
	});

	Scenario.Step(TEXT("Between raids A deposits 300 gold and the ruby"), [](FScenarioContext& C)
	{
		ACrossguardPlayerState* A = nullptr;
		ACrossguardPlayerState* B = nullptr;
		UCrossguardGuildSubsystem* Subsystem = Guilds(C);
		ACrossguardCharacter* CharacterA = C.Get<ACrossguardCharacter>(TEXT("A"));
		if (Subsystem == nullptr || !GetGuildPair(C, A, B) || CharacterA == nullptr || !C.Require(!A->IsInRaid(), TEXT("A is between raids")))
		{
			return;
		}
		ExpectGuild(C, TEXT("Deposit gold"), Subsystem->MoveGold(A, 300), ECrossguardGuildResult::None);
		ExpectGuild(C, TEXT("Deposit the ruby"), Subsystem->DepositItem(A, StackId(CharacterA, Ruby())), ECrossguardGuildResult::None);
		C.Test.TestEqual(TEXT("A's banked gold"), A->GetGold(), 700);
		C.Test.TestEqual(TEXT("A's rubies"), CountOf(CharacterA, Ruby()), 0);
		C.Test.TestEqual(TEXT("Bank gold (B's view)"), GuildOf(B)->BankGold, static_cast<int64>(300));
		C.Test.TestEqual(TEXT("Bank stacks (B's view)"), GuildOf(B)->BankItems.Num(), 1);
		C.SetRaid(true);
	});

	Scenario.Step(TEXT("In a raid: withdrawing is refused; the leader cannot leave while B is in"), [](FScenarioContext& C)
	{
		ACrossguardPlayerState* A = nullptr;
		ACrossguardPlayerState* B = nullptr;
		UCrossguardGuildSubsystem* Subsystem = Guilds(C);
		if (Subsystem == nullptr || !GetGuildPair(C, A, B) || !C.Require(GuildOf(A)->BankItems.Num() == 1, TEXT("the ruby is in the bank")))
		{
			return;
		}
		ExpectGuild(C, TEXT("Withdraw the item in a raid"), Subsystem->WithdrawItem(A, GuildOf(A)->BankItems[0].SlotId),
			ECrossguardGuildResult::OnlyBetweenRaids);
		ExpectGuild(C, TEXT("Withdraw gold in a raid"), Subsystem->MoveGold(A, -100), ECrossguardGuildResult::OnlyBetweenRaids);
		ExpectGuild(C, TEXT("Leader leaves with members left"), Subsystem->Leave(A), ECrossguardGuildResult::LeaderMustTransfer);
		ExpectGuild(C, TEXT("B leaves (in a raid)"), Subsystem->Leave(B), ECrossguardGuildResult::None);
		C.Test.TestFalse(TEXT("B is out"), GuildOf(B)->IsInGuild());
		C.Test.TestEqual(TEXT("A's roster"), GuildOf(A)->Members.Num(), 1);
		// The last member takes the bank: not into a raid inventory.
		ExpectGuild(C, TEXT("Last member leaves in a raid"), Subsystem->Leave(A), ECrossguardGuildResult::OnlyBetweenRaids);
		C.Test.TestTrue(TEXT("A is still in the guild"), GuildOf(A)->IsInGuild());
		C.SetRaid(false);
	});

	Scenario.Step(TEXT("Between raids the last member leaves: disbanded, A takes the bank, the name is free"), [](FScenarioContext& C)
	{
		ACrossguardPlayerState* A = nullptr;
		ACrossguardPlayerState* B = nullptr;
		UCrossguardGuildSubsystem* Subsystem = Guilds(C);
		ACrossguardCharacter* CharacterA = C.Get<ACrossguardCharacter>(TEXT("A"));
		if (Subsystem == nullptr || !GetGuildPair(C, A, B) || CharacterA == nullptr)
		{
			return;
		}
		C.ClearLog();
		ExpectGuild(C, TEXT("Last member leaves"), Subsystem->Leave(A), ECrossguardGuildResult::None);
		C.Test.TestFalse(TEXT("A is out"), GuildOf(A)->IsInGuild());
		C.Test.TestTrue(TEXT("Log: guild disbanded"), C.LogContains(TEXT("disbanded")));
		C.Test.TestEqual(TEXT("A's banked gold back"), A->GetGold(), 1000);
		C.Test.TestEqual(TEXT("A's ruby back"), CountOf(CharacterA, Ruby()), 1);
		C.Test.TestEqual(TEXT("The ruby is still Rare"), EnumName(RarityOf(CharacterA, Ruby())), EnumName(ECrossguardRarity::Rare));
		ExpectGuild(C, TEXT("B founds a guild with the freed name"), Subsystem->Create(B, TEXT("Iron Wolves")), ECrossguardGuildResult::None);
	});

	Scenario.Run();
	return true;
}

// guild-rules.md: amounts must be positive; banked gold is an int32, so withdrawals, gifts and the
// last member's take that would overflow it are refused and change nothing.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioGuildOverflow, "Crossguard.Scenario.Social.GuildGoldGuards", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioGuildOverflow::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSocial;

	FScenario Scenario(*this);
	Scenario.Raid(false);

	Scenario.Step(TEXT("A leads a guild with 1000 in the bank and B in it; both near the int32 cap"), [](FScenarioContext& C)
	{
		if (!SpawnPair(C))
		{
			return;
		}
		ACrossguardPlayerState* A = nullptr;
		ACrossguardPlayerState* B = nullptr;
		UCrossguardGuildSubsystem* Subsystem = Guilds(C);
		if (Subsystem == nullptr || !GetGuildPair(C, A, B))
		{
			return;
		}
		A->AddGold(1000);
		C.Require(Subsystem->Create(A, TEXT("Gold Guards")) == ECrossguardGuildResult::None, TEXT("A founds a guild"));
		C.Require(Subsystem->Invite(A, B) == ECrossguardGuildResult::None && Subsystem->Accept(B, GuildOf(A)->GuildId) == ECrossguardGuildResult::None,
			TEXT("B joins"));

		ExpectGuild(C, TEXT("Deposit 0"), Subsystem->MoveGold(A, 0), ECrossguardGuildResult::InvalidAmount);
		ExpectGuild(C, TEXT("Deposit/withdraw MIN_int32"), Subsystem->MoveGold(A, MIN_int32), ECrossguardGuildResult::InvalidAmount);
		ExpectGuild(C, TEXT("Deposit more than banked"), Subsystem->MoveGold(A, 1001), ECrossguardGuildResult::NotEnoughBankedGold);
		ExpectGuild(C, TEXT("Deposit 1000"), Subsystem->MoveGold(A, 1000), ECrossguardGuildResult::None);
		ExpectGuild(C, TEXT("Withdraw more than the bank holds"), Subsystem->MoveGold(A, -1001), ECrossguardGuildResult::NotEnoughGold);

		// Now 0 banked each: raise both close to MAX_int32.
		C.Require(A->GetGold() == 0, TEXT("A deposited everything"));
		A->AddGold(MAX_int32 - 500);
		B->AddGold(MAX_int32 - 10);
		C.Require(A->GetGold() == MAX_int32 - 500 && B->GetGold() == MAX_int32 - 10, TEXT("both near the cap"));
	});

	Scenario.Step(TEXT("Overflowing withdrawals and gifts are refused"), [](FScenarioContext& C)
	{
		ACrossguardPlayerState* A = nullptr;
		ACrossguardPlayerState* B = nullptr;
		UCrossguardGuildSubsystem* Subsystem = Guilds(C);
		if (Subsystem == nullptr || !GetGuildPair(C, A, B))
		{
			return;
		}
		ExpectGuild(C, TEXT("Withdraw 1000 onto MAX-500"), Subsystem->MoveGold(A, -1000), ECrossguardGuildResult::GoldOverflow);
		ExpectGuild(C, TEXT("Give B 100 onto MAX-10"), Subsystem->GiveGold(A, UCrossguardGuildSubsystem::PlayerIdOf(B), 100),
			ECrossguardGuildResult::GoldOverflow);
		C.Test.TestEqual(TEXT("A's gold unchanged"), A->GetGold(), MAX_int32 - 500);
		C.Test.TestEqual(TEXT("B's gold unchanged"), B->GetGold(), MAX_int32 - 10);
		C.Test.TestEqual(TEXT("Bank unchanged"), GuildOf(A)->BankGold, static_cast<int64>(1000));
		// What fits still works.
		ExpectGuild(C, TEXT("Give B 10 (exactly fits)"), Subsystem->GiveGold(A, UCrossguardGuildSubsystem::PlayerIdOf(B), 10),
			ECrossguardGuildResult::None);
		C.Test.TestEqual(TEXT("B at the cap"), B->GetGold(), MAX_int32);
		C.Test.TestEqual(TEXT("Bank after the gift"), GuildOf(A)->BankGold, static_cast<int64>(990));
	});

	Scenario.Step(TEXT("The last member cannot take a bank that would overflow"), [](FScenarioContext& C)
	{
		ACrossguardPlayerState* A = nullptr;
		ACrossguardPlayerState* B = nullptr;
		UCrossguardGuildSubsystem* Subsystem = Guilds(C);
		if (Subsystem == nullptr || !GetGuildPair(C, A, B))
		{
			return;
		}
		ExpectGuild(C, TEXT("B leaves"), Subsystem->Leave(B), ECrossguardGuildResult::None);
		ExpectGuild(C, TEXT("Last member A leaves with 990 bank gold onto MAX-500"), Subsystem->Leave(A), ECrossguardGuildResult::GoldOverflow);
		C.Test.TestTrue(TEXT("A is still in the guild"), GuildOf(A)->IsInGuild());
		C.Test.TestEqual(TEXT("Bank kept"), GuildOf(A)->BankGold, static_cast<int64>(990));
		C.Test.TestEqual(TEXT("A's gold unchanged"), A->GetGold(), MAX_int32 - 500);
	});

	Scenario.Run();
	return true;
}

// ================================================================================================
// Two players: looting one body
// ================================================================================================

// Checklist 6: "Looting the same body at the same time never duplicates an item." Both open the
// body and take every stack in the same frame (alternating who goes first).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioConcurrentLoot, "Crossguard.Scenario.Social.ConcurrentLootNeverDuplicates",
	CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioConcurrentLoot::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSocial;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A dummy carrying potions, bandages and a ruby between A and B"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = C.SpawnPlayer(TEXT("A"), FVector(-100.0f, Row, 0.0f), 0.0f);
		ACrossguardCharacter* B = C.SpawnPlayer(TEXT("B"), FVector(100.0f, Row, 0.0f), 180.0f);
		ACrossguardCharacter* Dummy = C.SpawnDummy(TEXT("Dummy"), FVector(0.0f, Row, 0.0f));
		if (!C.Require(A != nullptr && B != nullptr && Dummy != nullptr, TEXT("A, B and the dummy spawned")))
		{
			return;
		}
		C.Require(C.GiveItem(Dummy, Potion(), 3) == 0 && C.GiveItem(Dummy, Bandage(), 2) == 0 && C.GiveItem(Dummy, Ruby(), 1, ECrossguardRarity::Rare) == 0,
			TEXT("the dummy's items fit"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("A kills the dummy"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = C.Get<ACrossguardCharacter>(TEXT("A"));
		ACrossguardCharacter* Dummy = C.Get<ACrossguardCharacter>(TEXT("Dummy"));
		if (A == nullptr || Dummy == nullptr)
		{
			C.Fail(TEXT("A or the dummy gone"));
			return;
		}
		C.Kill(Dummy, A);
	});

	Scenario.WaitUntil(TEXT("a body holding the dummy's items lies there"), TradeCheckTimeout, [](FScenarioContext& C)
	{
		ACrossguardLootContainer* Body = C.FindNearest<ACrossguardLootContainer>(FVector(0.0f, Row, 0.0f), 400.0f);
		if (Body != nullptr && Body->GetItems().Num() > 0)
		{
			C.Remember(TEXT("Body"), Body);
			return true;
		}
		return false;
	});

	Scenario.Step(TEXT("Both open the body and take every stack in the same frame"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = nullptr;
		ACrossguardCharacter* B = nullptr;
		ACrossguardLootContainer* Body = C.Get<ACrossguardLootContainer>(TEXT("Body"));
		if (!GetPair(C, A, B) || Body == nullptr)
		{
			return;
		}
		UCrossguardInventoryComponent* InventoryA = InventoryOf(A);
		UCrossguardInventoryComponent* InventoryB = InventoryOf(B);
		if (!C.Require(InventoryA->OpenLootContainer(Body) && InventoryB->OpenLootContainer(Body), TEXT("both reach and open the body")))
		{
			return;
		}

		TMap<FString, int32> Before;
		AddCounts(Before, Body->GetItems());
		AddCounts(Before, InventoryA->GetItems());
		AddCounts(Before, InventoryB->GetItems());
		C.Test.TestTrue(TEXT("The body holds the potions"), Before.Contains(TEXT("Consumable/healing_potion/Common")));

		const TArray<FCrossguardInventoryItem> Stacks = Body->GetItems();
		for (int32 Index = 0; Index < Stacks.Num(); ++Index)
		{
			UCrossguardInventoryComponent* First = Index % 2 == 0 ? InventoryA : InventoryB;
			UCrossguardInventoryComponent* Second = Index % 2 == 0 ? InventoryB : InventoryA;
			First->RequestTakeFromContainer(Stacks[Index].Id);
			Second->RequestTakeFromContainer(Stacks[Index].Id);
		}

		TMap<FString, int32> After;
		AddCounts(After, Body->GetItems());
		AddCounts(After, InventoryA->GetItems());
		AddCounts(After, InventoryB->GetItems());
		C.Test.TestEqual(TEXT("Same kinds of items"), After.Num(), Before.Num());
		for (const TPair<FString, int32>& Entry : Before)
		{
			const int32* Now = After.Find(Entry.Key);
			C.Test.TestEqual(*FString::Printf(TEXT("%s: total across the body and both inventories"), *Entry.Key), Now != nullptr ? *Now : 0,
				Entry.Value);
		}
		C.Test.TestEqual(TEXT("The body is empty (everything fit)"), Body->GetItems().Num(), 0);
		C.Test.TestEqual(TEXT("A and B together hold the 3 potions"), CountOf(A, Potion()) + CountOf(B, Potion()), 3);
		C.Test.TestEqual(TEXT("A and B together hold the 2 bandages"), CountOf(A, Bandage()) + CountOf(B, Bandage()), 2);
		C.Test.TestEqual(TEXT("A and B together hold one ruby"), CountOf(A, Ruby()) + CountOf(B, Ruby()), 1);
	});

	Scenario.Run();
	return true;
}

// ================================================================================================
// Between raids (checklist section 5)
// ================================================================================================

// Checklist 5: "Right-click a Skill Gem (RequestUseItem) -> The skill joins the Skill Library; the
// gem is used up." In a raid it is refused and the gem stays.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioHubSkillGem, "Crossguard.Scenario.Hub.SkillGemLearnsSkill", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioHubSkillGem::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSocial;

	FScenario Scenario(*this);
	Scenario.Raid(false);

	Scenario.Step(TEXT("Between raids, use a heroic_might gem"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(0.0f, Row, 0.0f));
		if (!C.Require(Player != nullptr && C.PlayerState(Player) != nullptr && InventoryOf(Player) != nullptr, TEXT("the player spawned")))
		{
			return;
		}
		const UCrossguardLoadoutComponent* Loadout = Player->GetLoadoutComponent();
		if (Loadout == nullptr || Loadout->SkillCatalog == nullptr)
		{
			C.Skip(TEXT("The character Blueprint's LoadoutComponent has no Skill Catalog (DA_SkillCatalog): Skill Gems cannot be learned"));
			return;
		}
		ACrossguardPlayerState* Progression = C.PlayerState(Player);
		C.Require(!Progression->IsInRaid(), TEXT("the player is between raids"));
		C.Require(!Progression->HasLearnedSkill(TEXT("heroic_might")), TEXT("heroic_might is not learned yet"));
		C.Require(C.GiveItemOfCategory(Player, ECrossguardItemCategory::SkillGem, TEXT("heroic_might")) == 0, TEXT("the gem fits"));
		const int32 GemId = StackId(Player, TEXT("heroic_might"));
		if (!C.Require(GemId != 0, TEXT("the gem is in the inventory")))
		{
			return;
		}
		InventoryOf(Player)->RequestUseItem(GemId);
		C.Test.TestTrue(TEXT("heroic_might joined the Skill Library"), Progression->HasLearnedSkill(TEXT("heroic_might")));
		FCrossguardInventoryItem Gem;
		C.Test.TestFalse(TEXT("The gem is used up"), InventoryOf(Player)->FindItem(GemId, Gem));
	});

	Scenario.Step(TEXT("In a raid a gem cannot be learned and stays"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Player"));
		if (Player == nullptr)
		{
			return;
		}
		C.SetRaid(true);
		ACrossguardPlayerState* Progression = C.PlayerState(Player);
		C.Require(Progression->IsInRaid(), TEXT("the player is in a raid"));
		C.Require(C.GiveItemOfCategory(Player, ECrossguardItemCategory::SkillGem, TEXT("fireball")) == 0, TEXT("the gem fits"));
		const int32 GemId = StackId(Player, TEXT("fireball"));
		const bool bKnewFireball = Progression->HasLearnedSkill(TEXT("fireball"));
		C.ClearLog();
		InventoryOf(Player)->RequestUseItem(GemId);
		C.Test.TestTrue(TEXT("fireball not learned in a raid"), Progression->HasLearnedSkill(TEXT("fireball")) == bKnewFireball);
		FCrossguardInventoryItem Gem;
		C.Test.TestTrue(TEXT("The gem stays"), InventoryOf(Player)->FindItem(GemId, Gem));
		C.Test.TestTrue(TEXT("Log: inventory request rejected"), C.LogContains(TEXT("Inventory request rejected")));
	});

	Scenario.Run();
	return true;
}

// Checklist 5: "Stash: RequestStoreAnywhere, RequestRetrieve -> Items move between inventory and
// stash." In a raid the stash is refused.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioHubStash, "Crossguard.Scenario.Hub.StashStoreAndRetrieve", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioHubStash::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSocial;

	FScenario Scenario(*this);
	Scenario.Raid(false);

	Scenario.Step(TEXT("Store 3 potions anywhere, then retrieve them"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(0.0f, Row, 0.0f));
		ACrossguardPlayerState* Progression = Player != nullptr ? C.PlayerState(Player) : nullptr;
		UCrossguardStashComponent* Stash = Progression != nullptr ? Progression->GetStash() : nullptr;
		if (!C.Require(Stash != nullptr && InventoryOf(Player) != nullptr, TEXT("the player has a stash and an inventory")))
		{
			return;
		}
		C.Require(C.GiveItem(Player, Potion(), 3) == 0 && C.GiveItem(Player, Ruby(), 1, ECrossguardRarity::Rare) == 0, TEXT("the items fit"));
		C.Test.TestEqual(TEXT("Starting stash tabs"), Stash->GetTabCount(), StartingStashTabs);

		Stash->RequestStoreAnywhere(StackId(Player, Potion()));
		C.Test.TestEqual(TEXT("Potions left in the inventory"), CountOf(Player, Potion()), 0);
		FCrossguardInventoryItem Stored;
		if (!C.Require(FindInStash(Stash, Potion(), Stored), TEXT("the potions are in the stash")))
		{
			return;
		}
		C.Test.TestEqual(TEXT("Stashed count"), Stored.Count, 3);

		Stash->RequestRetrieve(Stored.Id);
		C.Test.TestEqual(TEXT("Potions back in the inventory"), CountOf(Player, Potion()), 3);
		FCrossguardInventoryItem Left;
		C.Test.TestFalse(TEXT("No potions left in the stash"), FindInStash(Stash, Potion(), Left));
	});

	Scenario.Step(TEXT("In a raid the stash refuses"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Player"));
		ACrossguardPlayerState* Progression = Player != nullptr ? C.PlayerState(Player) : nullptr;
		UCrossguardStashComponent* Stash = Progression != nullptr ? Progression->GetStash() : nullptr;
		if (Stash == nullptr)
		{
			C.Fail(TEXT("player or stash gone"));
			return;
		}
		C.SetRaid(true);
		C.ClearLog();
		Stash->RequestStoreAnywhere(StackId(Player, Ruby()));
		C.Test.TestEqual(TEXT("The ruby stays in the inventory"), CountOf(Player, Ruby()), 1);
		FCrossguardInventoryItem Stored;
		C.Test.TestFalse(TEXT("The ruby is not in the stash"), FindInStash(Stash, Ruby(), Stored));
		C.Test.TestTrue(TEXT("Log: stash request rejected"), C.LogContains(TEXT("Stash request rejected")));
	});

	Scenario.Run();
	return true;
}

// Checklist 5: "Vendor: RequestSell(Inventory, ruby id) -> +150 gold for a Rare ruby."
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioHubVendor, "Crossguard.Scenario.Hub.VendorSellsRareRuby", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioHubVendor::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSocial;

	FScenario Scenario(*this);
	Scenario.Raid(false);

	Scenario.Step(TEXT("Sell a Rare ruby; a potion (no sell value) is refused"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(0.0f, Row, 0.0f));
		ACrossguardPlayerState* Progression = Player != nullptr ? C.PlayerState(Player) : nullptr;
		UCrossguardVendorComponent* Vendor = Progression != nullptr ? Progression->GetVendor() : nullptr;
		if (!C.Require(Vendor != nullptr && InventoryOf(Player) != nullptr, TEXT("the player has a vendor component and an inventory")))
		{
			return;
		}
		C.Require(C.GiveItem(Player, Ruby(), 1, ECrossguardRarity::Rare) == 0 && C.GiveItem(Player, Potion(), 1) == 0, TEXT("the items fit"));
		const int32 RubyId = StackId(Player, Ruby());
		FCrossguardInventoryItem RubyItem;
		if (!C.Require(InventoryOf(Player)->FindItem(RubyId, RubyItem), TEXT("the ruby is in the inventory")))
		{
			return;
		}
		C.Test.TestEqual(TEXT("Sell price of a Rare ruby"), Vendor->GetSellPrice(RubyItem), RareRubyPrice);

		const int32 GoldBefore = Progression->GetGold();
		Vendor->RequestSell(ECrossguardItemSource::Inventory, RubyId);
		C.Test.TestEqual(TEXT("Banked gold after the sale"), Progression->GetGold(), GoldBefore + RareRubyPrice);
		C.Test.TestEqual(TEXT("The ruby is sold"), CountOf(Player, Ruby()), 0);

		C.ClearLog();
		Vendor->RequestSell(ECrossguardItemSource::Inventory, StackId(Player, Potion()));
		C.Test.TestEqual(TEXT("No gold for a potion"), Progression->GetGold(), GoldBefore + RareRubyPrice);
		C.Test.TestEqual(TEXT("The potion stays"), CountOf(Player, Potion()), 1);
		C.Test.TestTrue(TEXT("Log: sale rejected"), C.LogContains(TEXT("Sale rejected")));
	});

	Scenario.Run();
	return true;
}

// Ruling: gemstones stack up to 3 per kind per rarity. Four Common rubies make stacks of 3 and 1; a
// Rare ruby and a Common sapphire each start their own stack; a whole stack of 3 sells at once.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioHubGemstonesStack, "Crossguard.Scenario.Hub.GemstonesStackToThree", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioHubGemstonesStack::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSocial;

	FScenario Scenario(*this);
	Scenario.Raid(false);

	Scenario.Step(TEXT("Rubies stack to 3 per rarity; other gems and rarities keep apart"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(0.0f, Row, 0.0f));
		ACrossguardPlayerState* Progression = Player != nullptr ? C.PlayerState(Player) : nullptr;
		UCrossguardVendorComponent* Vendor = Progression != nullptr ? Progression->GetVendor() : nullptr;
		UCrossguardInventoryComponent* Inventory = InventoryOf(Player);
		if (!C.Require(Vendor != nullptr && Inventory != nullptr, TEXT("the player has a vendor component and an inventory"))
			|| !C.Require(CountOf(Player, Ruby()) == 0, TEXT("no rubies to start with")))
		{
			return;
		}
		const FName Sapphire(TEXT("sapphire"));
		C.Require(C.GiveItem(Player, Ruby(), 4) == 0, TEXT("four Common rubies fit"));
		C.Require(C.GiveItem(Player, Ruby(), 1, ECrossguardRarity::Rare) == 0, TEXT("a Rare ruby fits"));
		C.Require(C.GiveItem(Player, Sapphire, 1) == 0, TEXT("a Common sapphire fits"));

		TArray<int32> CommonRubies;
		int32 RareRubies = 0;
		int32 FullStackId = 0;
		for (const FCrossguardInventoryItem& Item : Inventory->GetItems())
		{
			if (Item.ItemId != Ruby())
			{
				continue;
			}
			C.Test.TestEqual(TEXT("A ruby stacks to 3"), Item.MaxStack, 3);
			if (Item.Rarity == ECrossguardRarity::Common)
			{
				CommonRubies.Add(Item.Count);
				FullStackId = Item.Count == 3 ? Item.Id : FullStackId;
			}
			else
			{
				C.Test.TestEqual(TEXT("The Rare ruby keeps its own stack of 1"), Item.Count, 1);
				++RareRubies;
			}
		}
		CommonRubies.Sort();
		C.Test.TestEqual(TEXT("Four Common rubies: two stacks"), CommonRubies.Num(), 2);
		C.Test.TestTrue(TEXT("Stacks of 1 and 3"), CommonRubies.Num() == 2 && CommonRubies[0] == 1 && CommonRubies[1] == 3);
		C.Test.TestEqual(TEXT("One Rare ruby stack"), RareRubies, 1);
		C.Test.TestEqual(TEXT("Rubies in all"), CountOf(Player, Ruby()), 5);
		C.Test.TestEqual(TEXT("The sapphire is apart"), CountOf(Player, Sapphire), 1);

		// The full stack sells whole: 3 x 50 gold (Common).
		FCrossguardInventoryItem FullStack;
		if (C.Require(FullStackId != 0 && Inventory->FindItem(FullStackId, FullStack), TEXT("the stack of 3 is in the inventory")))
		{
			const int32 GoldBefore = Progression->GetGold();
			C.Test.TestEqual(TEXT("A stack of 3 Common rubies sells for 150"), Vendor->GetSellPrice(FullStack), 150);
			Vendor->RequestSell(ECrossguardItemSource::Inventory, FullStackId);
			C.Test.TestEqual(TEXT("Banked gold after the sale"), Progression->GetGold(), GoldBefore + 150);
			C.Test.TestEqual(TEXT("Two rubies left (Common 1, Rare 1)"), CountOf(Player, Ruby()), 2);
		}
	});

	Scenario.Run();
	return true;
}

// Checklist 5: "RequestBuyStashTab with 3000+ gold -> A second tab appears; a fourth purchase is
// refused."
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioHubStashTabs, "Crossguard.Scenario.Hub.BuyStashTabs", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioHubStashTabs::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSocial;

	FScenario Scenario(*this);
	Scenario.Raid(false);

	Scenario.Step(TEXT("No gold: refused; with gold: three tabs, the fourth purchase refused"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(0.0f, Row, 0.0f));
		ACrossguardPlayerState* Progression = Player != nullptr ? C.PlayerState(Player) : nullptr;
		UCrossguardStashComponent* Stash = Progression != nullptr ? Progression->GetStash() : nullptr;
		if (!C.Require(Stash != nullptr, TEXT("the player has a stash")) || !C.Require(Progression->GetGold() == 0, TEXT("no banked gold yet")))
		{
			return;
		}
		Stash->RequestBuyStashTab();
		C.Test.TestEqual(TEXT("Tabs after buying with no gold"), Stash->GetTabCount(), StartingStashTabs);

		const int32 Gold = StashTabPrice * (MaxPurchasedStashTabs + 1) + 500;
		Progression->AddGold(Gold);
		Stash->RequestBuyStashTab();
		C.Test.TestEqual(TEXT("A second tab"), Stash->GetTabCount(), StartingStashTabs + 1);
		C.Test.TestEqual(TEXT("Gold after one tab"), Progression->GetGold(), Gold - StashTabPrice);
		for (int32 Purchase = 1; Purchase < MaxPurchasedStashTabs; ++Purchase)
		{
			Stash->RequestBuyStashTab();
		}
		C.Test.TestEqual(TEXT("Tabs after three purchases"), Stash->GetTabCount(), StartingStashTabs + MaxPurchasedStashTabs);
		C.Test.TestEqual(TEXT("Bought tabs"), Stash->GetPurchasedTabCount(), MaxPurchasedStashTabs);

		C.ClearLog();
		Stash->RequestBuyStashTab();
		C.Test.TestEqual(TEXT("The fourth purchase is refused"), Stash->GetTabCount(), StartingStashTabs + MaxPurchasedStashTabs);
		C.Test.TestEqual(TEXT("No gold taken for it"), Progression->GetGold(), Gold - StashTabPrice * MaxPurchasedStashTabs);
		C.Test.TestTrue(TEXT("Log: refused AtCap"), C.LogContains(FString::Printf(TEXT("error %d ("), static_cast<int32>(ECrossguardInventoryError::AtCap))));
	});

	Scenario.Run();
	return true;
}

// Checklist 5: "RequestAttributeAllocation lowering a stat -> Refused without a respec token."
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioHubRespec, "Crossguard.Scenario.Hub.LoweringAttributeNeedsRespecToken", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioHubRespec::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSocial;

	FScenario Scenario(*this);
	Scenario.Raid(false);

	Scenario.Step(TEXT("Lowering Strength: refused without a token, done with one"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(0.0f, Row, 0.0f));
		ACrossguardPlayerState* Progression = Player != nullptr ? C.PlayerState(Player) : nullptr;
		if (!C.Require(Progression != nullptr, TEXT("the player has a player state")))
		{
			return;
		}
		const FCrossguardAttributeAllocation Current = Progression->GetAttributeAllocation();
		if (!C.Require(Current.Strength > 0, TEXT("Strength above 0 to lower")) || !C.Require(!Progression->IsInRaid(), TEXT("between raids")))
		{
			return;
		}
		C.Test.TestEqual(TEXT("No respec tokens"), Progression->GetRespecTokens(), 0);

		FCrossguardAttributeAllocation Lowered = Current;
		Lowered.Strength -= 1;
		Progression->RequestAttributeAllocation(Lowered);
		C.Test.TestEqual(TEXT("Strength unchanged without a token"), Progression->GetAttributeAllocation().Strength, Current.Strength);
		C.Test.TestEqual(TEXT("Agility unchanged"), Progression->GetAttributeAllocation().Agility, Current.Agility);
		C.Test.TestEqual(TEXT("Intelligence unchanged"), Progression->GetAttributeAllocation().Intelligence, Current.Intelligence);

		Progression->GrantRespecTokens(1);
		Progression->RequestAttributeAllocation(Lowered);
		C.Test.TestEqual(TEXT("Strength lowered with a token"), Progression->GetAttributeAllocation().Strength, Current.Strength - 1);
		C.Test.TestEqual(TEXT("The token is used"), Progression->GetRespecTokens(), 0);
	});

	Scenario.Run();
	return true;
}

// Ruling: a respec costs 1000 banked gold or one respec token, the player's choice. Paying with gold:
// refused (nothing taken) at 999 gold and in a raid; at 1000 the stat drops and exactly 1000 gold
// goes, the token stays; paying with the token then leaves the gold alone.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioHubRespecGold, "Crossguard.Scenario.Hub.RespecPaidWithGold", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioHubRespecGold::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSocial;

	FScenario Scenario(*this);
	Scenario.Raid(false);

	Scenario.Step(TEXT("Lowering Strength paid with gold: 999 is short, 1000 pays"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(0.0f, Row, 0.0f));
		ACrossguardPlayerState* Progression = Player != nullptr ? C.PlayerState(Player) : nullptr;
		if (!C.Require(Progression != nullptr, TEXT("the player has a player state")))
		{
			return;
		}
		const int32 Price = Progression->GetRespecGoldPrice();
		C.Test.TestEqual(TEXT("Ruled price"), Price, 1000);
		const FCrossguardAttributeAllocation Current = Progression->GetAttributeAllocation();
		if (!C.Require(Current.Strength > 1, TEXT("Strength above 1 to lower twice")) || !C.Require(!Progression->IsInRaid(), TEXT("between raids")))
		{
			return;
		}
		// Start from exactly 999 banked gold and one token.
		Progression->SpendGold(Progression->GetGold());
		Progression->AddGold(Price - 1);
		Progression->GrantRespecTokens(1);
		const int32 Tokens = Progression->GetRespecTokens();

		FCrossguardAttributeAllocation Lowered = Current;
		Lowered.Strength -= 1;
		Progression->RequestAttributeAllocation(Lowered, true);
		C.Test.TestEqual(TEXT("999 gold: Strength unchanged"), Progression->GetAttributeAllocation().Strength, Current.Strength);
		C.Test.TestEqual(TEXT("999 gold: no gold taken"), Progression->GetGold(), Price - 1);
		C.Test.TestEqual(TEXT("999 gold: the token is not used instead"), Progression->GetRespecTokens(), Tokens);

		Progression->AddGold(1);
		Progression->BeginRaid();
		Progression->RequestAttributeAllocation(Lowered, true);
		C.Test.TestEqual(TEXT("In a raid: Strength unchanged"), Progression->GetAttributeAllocation().Strength, Current.Strength);
		C.Test.TestEqual(TEXT("In a raid: no gold taken"), Progression->GetGold(), Price);
		// Back to the hub (a failed extraction: no XP, so the level and the allocation's limits stay),
		// with exactly the price banked.
		Progression->EndRaid(false);
		C.Require(!Progression->IsInRaid(), TEXT("back between raids"));
		Progression->SpendGold(Progression->GetGold());
		Progression->AddGold(Price);

		Progression->RequestAttributeAllocation(Lowered, true);
		C.Test.TestEqual(TEXT("1000 gold: Strength lowered"), Progression->GetAttributeAllocation().Strength, Current.Strength - 1);
		C.Test.TestEqual(TEXT("1000 gold: all of it paid"), Progression->GetGold(), 0);
		C.Test.TestEqual(TEXT("1000 gold: the token kept"), Progression->GetRespecTokens(), Tokens);

		FCrossguardAttributeAllocation LoweredAgain = Progression->GetAttributeAllocation();
		LoweredAgain.Strength -= 1;
		Progression->RequestAttributeAllocation(LoweredAgain, true);
		C.Test.TestEqual(TEXT("No gold left: Strength unchanged"), Progression->GetAttributeAllocation().Strength, Current.Strength - 1);
		Progression->RequestAttributeAllocation(LoweredAgain);
		C.Test.TestEqual(TEXT("Paid with the token: Strength lowered"), Progression->GetAttributeAllocation().Strength, Current.Strength - 2);
		C.Test.TestEqual(TEXT("The token is used"), Progression->GetRespecTokens(), Tokens - 1);
		C.Test.TestEqual(TEXT("No gold taken for a token respec"), Progression->GetGold(), 0);
	});

	Scenario.Run();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
