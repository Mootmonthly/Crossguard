// Scenario tests: raid world rows of docs/next-build-checklist.md section 4 (first table): throwables,
// weapon sets, ammo, consumables, looting, death and revive, extraction (zone and portal), chests,
// boss loot, doors and locks, dropping items. Framework: CrossguardScenario.h, docs/editor-tests.md.
//
// "Kill the dummy" is CrossguardScenarioRaidTests.cpp (Crossguard.Scenario.Raid.KillDummyLeavesBody).
//
// Keys the checklist presses are private input handlers on the character; these tests call what they
// call: 1 / 2 -> UCrossguardEquipmentComponent::RequestSwitchWeaponSet(0 / 1); 3-7 ->
// UCrossguardInventoryComponent::RequestUseHotbarSlot(0-4); left click with an item readied ->
// RequestStartUse (pressed) / RequestStopUse (released); G -> RequestDropItem(GetReadiedItemId());
// F -> ACrossguardCharacter::TryInteract; X -> TrySummonPortal.

#include "Tests/CrossguardScenario.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AbilitySystem/CrossguardAttributeSet.h"
#include "AbilitySystem/CrossguardEffects.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Character/CrossguardCharacter.h"
#include "Combat/CrossguardThrownProjectile.h"
#include "Components/CapsuleComponent.h"
#include "CrossguardRules/Config.h"
#include "CrossguardRules/Economy.h"
#include "CrossguardRules/Items.h"
#include "CrossguardRules/Portal.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Game/CrossguardExtractPortal.h"
#include "Game/CrossguardExtractionZone.h"
#include "Game/CrossguardGameMode.h"
#include "Game/CrossguardPlayerState.h"
#include "Game/CrossguardPointOfInterest.h"
#include "Interaction/CrossguardDoor.h"
#include "Interaction/CrossguardLockComponent.h"
#include "Items/CrossguardEquipmentComponent.h"
#include "Items/CrossguardInventoryComponent.h"
#include "Items/CrossguardItemTypes.h"
#include "Items/CrossguardLoot.h"
#include "Items/CrossguardLootContainer.h"
#include "Items/CrossguardWorldItem.h"

namespace CrossguardScenarioRaidWorld
{
	using CrossguardScenario::FScenarioContext;

	const FName ThrowingKnife(TEXT("throwing_knife"));
	const FName ExplosiveBottle(TEXT("explosive_bottle"));
	const FName HealingPotion(TEXT("healing_potion"));
	const FName Bandage(TEXT("bandage"));
	const FName Arrow(TEXT("arrow"));
	const FName GoldCoin(TEXT("gold_coin"));
	const FName Ruby(TEXT("ruby"));
	const FName Lockpick(TEXT("lockpick"));
	const FName Sword(TEXT("sword_one_handed"));
	const FName Dagger(TEXT("dagger_one_handed"));
	const FName Bow(TEXT("bow"));
	const FName PlateHelm(TEXT("plate_helm"));
	const FName ChestCommon(TEXT("chest_common"));
	const FName BossMonster(TEXT("boss_monster"));

	// Let spawned characters land on the floor.
	constexpr float LandSeconds = 0.5f;
	// A player's view (eyes) is read through its camera manager, updated once a frame: wait this long
	// after aiming before something that looks from the eyes (throws, F).
	constexpr float ViewSettleSeconds = 0.2f;
	// ACrossguardThrownProjectile's constructor: 2000 cm/s with full gravity.
	constexpr float ThrowSpeed = 2000.0f;
	// Seconds after death until DebugReviveDelay revives (set by the tests).
	constexpr float ReviveDelay = 5.0f;
	// ACrossguardExtractionZone's default ExtractSeconds (the tests spawn the native class).
	constexpr float ExtractSeconds = 5.0f;
	// A body or a dropped item is spawned within this of where it came from (cm).
	constexpr float SearchRadius = 300.0f;
	// BowRunsOutOfArrows: the checklist's DebugInventory arrows.
	constexpr int32 ArrowCount = 45;
	// PotionAndBandage: Health taken off first, so every heal lands in full (15 + 10 + 10 at most); the
	// second bandage is released this long into its channel.
	constexpr float HealthDeficit = 40.0f;
	constexpr float BandageEarlyRelease = 1.25f;
	// ExtractionZone: gold coins carried out.
	constexpr int32 ExtractGold = 250;
	// ChestCommonLoot.
	constexpr int32 ChestCount = 3;
	constexpr float ChestSpacing = 500.0f;
	// DropAndPickUp.
	constexpr int32 DropPotionCount = 3;

	// The character by name; fails the scenario if it is gone.
	ACrossguardCharacter* RequireCharacter(FScenarioContext& C, const TCHAR* Name)
	{
		ACrossguardCharacter* Character = C.Get<ACrossguardCharacter>(FName(Name));
		if (Character == nullptr)
		{
			C.Fail(FString::Printf(TEXT("'%s' is gone"), Name));
		}
		return Character;
	}

	// The first stack of a table item (or Skill Gem) in the character's inventory; 0 if none.
	int32 StackIdOf(const ACrossguardCharacter* Character, FName ItemId)
	{
		const UCrossguardInventoryComponent* Inventory = Character != nullptr ? Character->GetInventoryComponent() : nullptr;
		if (Inventory != nullptr)
		{
			for (const FCrossguardInventoryItem& Item : Inventory->GetItems())
			{
				if (Item.Category != ECrossguardItemCategory::Weapon && Item.Category != ECrossguardItemCategory::Armor && Item.ItemId == ItemId)
				{
					return Item.Id;
				}
			}
		}
		return 0;
	}

	// The first stack holding that weapon in the character's inventory; 0 if none.
	int32 WeaponStackIdOf(const ACrossguardCharacter* Character, FName WeaponId)
	{
		const UCrossguardInventoryComponent* Inventory = Character != nullptr ? Character->GetInventoryComponent() : nullptr;
		if (Inventory != nullptr)
		{
			for (const FCrossguardInventoryItem& Item : Inventory->GetItems())
			{
				if (Item.Category == ECrossguardItemCategory::Weapon && Item.Weapon.WeaponId == WeaponId)
				{
					return Item.Id;
				}
			}
		}
		return 0;
	}

	// How many stacks of a table item the character's inventory holds.
	int32 StackCountOf(const ACrossguardCharacter* Character, FName ItemId)
	{
		int32 Stacks = 0;
		const UCrossguardInventoryComponent* Inventory = Character != nullptr ? Character->GetInventoryComponent() : nullptr;
		if (Inventory != nullptr)
		{
			for (const FCrossguardInventoryItem& Item : Inventory->GetItems())
			{
				if (Item.Category != ECrossguardItemCategory::Weapon && Item.Category != ECrossguardItemCategory::Armor && Item.ItemId == ItemId)
				{
					++Stacks;
				}
			}
		}
		return Stacks;
	}

	// Items in all the stacks (a stack of 3 potions counts 3).
	int32 SumCounts(const TArray<FCrossguardInventoryItem>& Items)
	{
		int32 Total = 0;
		for (const FCrossguardInventoryItem& Item : Items)
		{
			Total += Item.Count;
		}
		return Total;
	}

	// The item table row (the character's inventory Item Table, DT_Items); fails the scenario if missing.
	const FCrossguardItemRow* FindItemRowOf(FScenarioContext& C, const ACrossguardCharacter* Character, FName ItemId)
	{
		const UCrossguardInventoryComponent* Inventory = Character != nullptr ? Character->GetInventoryComponent() : nullptr;
		const FCrossguardItemRow* Row = Inventory != nullptr && Inventory->ItemTable != nullptr
			? Inventory->ItemTable->FindRow<FCrossguardItemRow>(ItemId, TEXT("Scenario"), false)
			: nullptr;
		if (Row == nullptr)
		{
			C.Fail(FString::Printf(TEXT("No %s row in the character's Item Table (set InventoryComponent Item Table to DT_Items, reimported from Data/Items/ItemTable.json)"),
				*ItemId.ToString()));
		}
		return Row;
	}

	// The game mode's loot table row, or null (with a Skip) when the loot data is not set up.
	const FCrossguardLootTableRow* LootRowOf(FScenarioContext& C, FName TableId)
	{
		const ACrossguardGameMode* Mode = C.GameMode();
		if (Mode == nullptr || Mode->Loot.LootTables == nullptr)
		{
			C.Skip(TEXT("the game mode's Loot Tables are unset (import Data/Loot/LootTable.json as DT_Loot and set BP_CrossguardGameMode, Crossguard | Loot)"));
			return nullptr;
		}
		const FCrossguardLootTableRow* Row = Mode->Loot.LootTables->FindRow<FCrossguardLootTableRow>(TableId, TEXT("Scenario"), false);
		if (Row == nullptr)
		{
			C.Skip(FString::Printf(TEXT("%s has no %s row (reimport it from Data/Loot/LootTable.json)"), *Mode->Loot.LootTables->GetName(),
				*TableId.ToString()));
		}
		return Row;
	}

	// Whether Items can be one roll of Row: each drop is one stack, except gold, whose drops (CountMin
	// to CountMax coins each) may fill several stacks of 100. "Nothing" weight: a roll may drop less
	// than RollsMin. OutDescription says what was found.
	bool IsOneRollOf(const FCrossguardLootTableRow& Row, const TArray<FCrossguardInventoryItem>& Items, FString& OutDescription)
	{
		int32 NonGoldDrops = 0;
		int32 GoldTotal = 0;
		for (const FCrossguardInventoryItem& Item : Items)
		{
			if (Item.Category == ECrossguardItemCategory::Currency)
			{
				GoldTotal += Item.Count;
			}
			else
			{
				++NonGoldDrops;
			}
		}
		int32 GoldMin = MAX_int32;
		int32 GoldMax = 0;
		for (const FCrossguardLootEntry& Entry : Row.Entries)
		{
			if (Entry.Kind == ECrossguardLootKind::Gold)
			{
				GoldMin = FMath::Min(GoldMin, FMath::Max(1, Entry.CountMin));
				GoldMax = FMath::Max(GoldMax, Entry.CountMax);
			}
		}
		int32 GoldDropsMin = 0;
		int32 GoldDropsMax = 0;
		if (GoldTotal > 0)
		{
			if (GoldMax <= 0)
			{
				OutDescription = FString::Printf(TEXT("%d gold, but the table has no Gold entry"), GoldTotal);
				return false;
			}
			GoldDropsMin = FMath::DivideAndRoundUp(GoldTotal, GoldMax);
			GoldDropsMax = GoldTotal / GoldMin;
		}
		const int32 Lowest = Row.NothingWeight > 0 ? 0 : Row.RollsMin;
		OutDescription = FString::Printf(TEXT("%d stacks: %d non-gold drops and %d gold (%d-%d gold drops); the table drops %d-%d"), Items.Num(),
			NonGoldDrops, GoldTotal, GoldDropsMin, GoldDropsMax, Lowest, Row.RollsMax);
		return GoldDropsMin <= GoldDropsMax && NonGoldDrops + GoldDropsMin <= Row.RollsMax && NonGoldDrops + GoldDropsMax >= Lowest;
	}

	// Aims the view at Target, raised to make up for the throw's drop on the way.
	void AimForThrow(FScenarioContext& C, ACrossguardCharacter* Thrower, const FVector& Target)
	{
		if (Thrower == nullptr)
		{
			return;
		}
		const double Seconds = FVector::Dist2D(Thrower->GetPawnViewLocation(), Target) / ThrowSpeed;
		const UWorld* World = C.World();
		const double Gravity = World != nullptr ? FMath::Abs(World->GetGravityZ()) : 980.0;
		C.AimAt(Thrower, Target + FVector(0.0, 0.0, 0.5 * Gravity * Seconds * Seconds));
	}

	// Left click pressed and released with an item in hand.
	void TapReadiedItem(ACrossguardCharacter* Character)
	{
		if (UCrossguardInventoryComponent* Inventory = Character != nullptr ? Character->GetInventoryComponent() : nullptr)
		{
			Inventory->RequestStartUse();
			Inventory->RequestStopUse();
		}
	}

	// What one throw (or one second of its damage over time) of Amount should take off Target.
	float ThrowHitDamage(const FScenarioContext& C, const AActor* Source, const AActor* Target, const FCrossguardItemRow& Row, float Amount)
	{
		return Row.ThrowDamageType == ECrossguardDamageType::Magic ? C.ExpectedDamage(Source, Target, 0.0f, Amount)
			: C.ExpectedDamage(Source, Target, Amount);
	}

	// Sets Health directly (the base value), e.g. to make room for healing.
	void SetHealthOf(const AActor* Actor, float Health)
	{
		if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor))
		{
			ASC->SetNumericAttributeBase(UCrossguardAttributeSet::GetHealthAttribute(), Health);
		}
	}

	// F at Target: aim at it now; press F in a later step (ViewSettleSeconds).
	void FaceInteractable(FScenarioContext& C, ACrossguardCharacter* Character, const AActor* Target)
	{
		if (Character != nullptr && Target != nullptr)
		{
			C.AimAt(Character, Target->GetActorLocation());
		}
	}

	FString DescribeItems(const TArray<FCrossguardInventoryItem>& Items)
	{
		TArray<FString> Parts;
		for (const FCrossguardInventoryItem& Item : Items)
		{
			const FName Id = Item.Category == ECrossguardItemCategory::Weapon ? Item.Weapon.WeaponId
				: Item.Category == ECrossguardItemCategory::Armor ? Item.Armor.ArmorId : Item.ItemId;
			Parts.Add(FString::Printf(TEXT("%s x%d (%s)"), *Id.ToString(), Item.Count,
				*StaticEnum<ECrossguardRarity>()->GetNameStringByValue(static_cast<int64>(
					Item.Category == ECrossguardItemCategory::Weapon ? Item.Weapon.Rarity
					: Item.Category == ECrossguardItemCategory::Armor ? Item.Armor.Rarity : Item.Rarity))));
		}
		return Parts.IsEmpty() ? FString(TEXT("nothing")) : FString::Join(Parts, TEXT(", "));
	}
}

// ------------------------------------------------------------------------------------------------
// Checklist: "Add throwing_knife ... press their hotbar key, left click -> The knife arcs and hits
// for 15" and "Throw a knife, then left click again -> The next knife throws; after the last one the
// hands are empty until 1 or 2."
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioRaidWorldThrowingKnife, "Crossguard.Scenario.Raid.ThrowingKnife", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioRaidWorldThrowingKnife::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioRaidWorld;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A player (sword in set 1, 3 throwing knives) readies the knives with key 3, facing a dummy 4 m away"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(0.0f, 1000.0f, 0.0f), 0.0f);
		ACrossguardCharacter* Dummy = C.SpawnDummy(TEXT("Dummy"), FVector(400.0f, 1000.0f, 0.0f), 180.0f);
		if (!C.Require(Player != nullptr && Dummy != nullptr, TEXT("player and dummy spawned")) || !C.Equip(Player, Sword))
		{
			return;
		}
		if (!C.Require(C.GiveItem(Player, ThrowingKnife, 3) == 0, TEXT("3 throwing knives fit in the inventory")))
		{
			return;
		}
		UCrossguardInventoryComponent* Inventory = Player->GetInventoryComponent();
		const int32 StackId = StackIdOf(Player, ThrowingKnife);
		if (!C.Require(StackId != 0, TEXT("the knives are in the inventory")))
		{
			return;
		}
		// Key 3 is hotbar slot 0. The Debug inventory fills the hotbar in order; here it is assigned by hand.
		Inventory->RequestAssignHotbarSlot(0, StackId);
		Inventory->RequestUseHotbarSlot(0);
		C.Test.TestEqual(TEXT("Key 3 readies the knife stack"), Inventory->GetReadiedItemId(), StackId);
		C.Test.TestFalse(TEXT("The sword is put away while knives are in hand"), Player->GetEquipmentComponent()->AreWeaponsReady());
		C.Test.TestTrue(TEXT("A knife in hand counts as a dagger (Equipment.Wielding.Dagger)"),
			C.HasTag(Player, UCrossguardEquipmentComponent::GetWieldingTag(ECrossguardWeaponType::Dagger)));
		AimForThrow(C, Player, Dummy->GetActorLocation());
	});

	Scenario.Wait(LandSeconds);

	Scenario.Step(TEXT("Left click throws one knife; the next one stays in hand"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		ACrossguardCharacter* Dummy = RequireCharacter(C, TEXT("Dummy"));
		if (Player == nullptr || Dummy == nullptr)
		{
			return;
		}
		AimForThrow(C, Player, Dummy->GetActorLocation());
		C.SetNumber(TEXT("HealthBefore"), C.Health(Dummy));
		TapReadiedItem(Player);
		const UCrossguardInventoryComponent* Inventory = Player->GetInventoryComponent();
		C.Test.TestEqual(TEXT("One knife is used up per throw"), Inventory->CountItem(ThrowingKnife), 2);
		C.Test.TestTrue(TEXT("The hands stay on the knives"), Inventory->GetReadiedItemId() != 0);
		C.Test.TestTrue(TEXT("A thrown knife is in the air"), C.FindAll<ACrossguardThrownProjectile>().Num() > 0);
	});

	Scenario.WaitUntil(TEXT("the knife hits the dummy"), 3.0f, [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Dummy = RequireCharacter(C, TEXT("Dummy"));
		return Dummy == nullptr || C.Health(Dummy) < static_cast<float>(C.Number(TEXT("HealthBefore")));
	});

	Scenario.Step(TEXT("The knife hit for the item data's throw damage"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		ACrossguardCharacter* Dummy = RequireCharacter(C, TEXT("Dummy"));
		const FCrossguardItemRow* Row = FindItemRowOf(C, Player, ThrowingKnife);
		if (Player == nullptr || Dummy == nullptr || Row == nullptr)
		{
			return;
		}
		const float Before = static_cast<float>(C.Number(TEXT("HealthBefore")));
		// Common: rarity level 0.
		const float Base = Row->ThrowDamage + Row->ThrowDamagePerRarity * UCrossguardItemLibrary::GetRarityLevel(ECrossguardRarity::Common);
		const float Expected = FMath::Min(ThrowHitDamage(C, Player, Dummy, *Row, Base), Before);
		C.Test.AddInfo(FString::Printf(TEXT("Knife hit: %.2f -> %.2f Health (expected -%.2f; base %.1f)"), Before, C.Health(Dummy), Expected, Base));
		C.Test.TestEqual(TEXT("Damage of one throwing knife"), Before - C.Health(Dummy), Expected, 0.05f);
		C.SetNumber(TEXT("KnifeDamage"), Expected);
		C.SetNumber(TEXT("HealthBefore"), C.Health(Dummy));
		AimForThrow(C, Player, Dummy->GetActorLocation());
	});

	Scenario.Wait(ViewSettleSeconds);

	// One at a time: two knives spawned in the same spot would hit each other.
	Scenario.Step(TEXT("Left click again: the second knife"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		ACrossguardCharacter* Dummy = RequireCharacter(C, TEXT("Dummy"));
		if (Player == nullptr || Dummy == nullptr)
		{
			return;
		}
		AimForThrow(C, Player, Dummy->GetActorLocation());
		TapReadiedItem(Player);
		const UCrossguardInventoryComponent* Inventory = Player->GetInventoryComponent();
		C.Test.TestEqual(TEXT("Second throw: 1 knife left"), Inventory->CountItem(ThrowingKnife), 1);
		C.Test.TestTrue(TEXT("Still a knife in hand"), Inventory->GetReadiedItemId() != 0);
	});

	Scenario.Wait(0.4f);

	Scenario.Step(TEXT("Left click again: the last knife; the hands are empty"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		ACrossguardCharacter* Dummy = RequireCharacter(C, TEXT("Dummy"));
		if (Player == nullptr || Dummy == nullptr)
		{
			return;
		}
		AimForThrow(C, Player, Dummy->GetActorLocation());
		TapReadiedItem(Player);
		const UCrossguardInventoryComponent* Inventory = Player->GetInventoryComponent();
		C.Test.TestEqual(TEXT("No knives left"), Inventory->CountItem(ThrowingKnife), 0);
		C.Test.TestEqual(TEXT("The hands hold nothing"), Inventory->GetReadiedItemId(), 0);
		C.Test.TestFalse(TEXT("The weapons are not drawn by themselves"), Player->GetEquipmentComponent()->AreWeaponsReady());
		C.Test.TestFalse(TEXT("Left click does not attack with empty hands"), C.BasicAttack(Player));
		C.Test.TestFalse(TEXT("No longer counts as wielding a dagger"),
			C.HasTag(Player, UCrossguardEquipmentComponent::GetWieldingTag(ECrossguardWeaponType::Dagger)));

		// Another left click with nothing readied throws nothing.
		const int32 InAir = C.FindAll<ACrossguardThrownProjectile>().Num();
		TapReadiedItem(Player);
		C.Test.TestEqual(TEXT("Nothing more is thrown"), C.FindAll<ACrossguardThrownProjectile>().Num(), InAir);
	});

	Scenario.WaitUntil(TEXT("both knives hit the dummy"), 3.0f, [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Dummy = RequireCharacter(C, TEXT("Dummy"));
		if (Dummy == nullptr)
		{
			return true;
		}
		const float Before = static_cast<float>(C.Number(TEXT("HealthBefore")));
		const float Expected = FMath::Min(2.0f * static_cast<float>(C.Number(TEXT("KnifeDamage"))), Before);
		return C.Health(Dummy) <= Before - Expected + 0.1f;
	});

	Scenario.Step(TEXT("Key 1 draws the sword set again"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		if (Player == nullptr)
		{
			return;
		}
		Player->GetEquipmentComponent()->RequestSwitchWeaponSet(0);
		C.Test.TestTrue(TEXT("Key 1 starts drawing set 1"), Player->GetEquipmentComponent()->IsDrawingWeapons());
	});

	Scenario.WaitUntil(TEXT("the sword is drawn"), 3.0f, [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		return Player == nullptr || Player->GetEquipmentComponent()->AreWeaponsReady();
	});

	Scenario.Step(TEXT("The sword attacks again"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		if (Player != nullptr)
		{
			C.Test.TestTrue(TEXT("Left click attacks with the drawn sword"), C.BasicAttack(Player));
		}
	});

	Scenario.Run();
	return true;
}

// ------------------------------------------------------------------------------------------------
// Checklist: "... explosive_bottle ... -> the bottle explodes for 10 within 3 m, then 3 per second for
// 5 s (you too, if you stand in it)."
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioRaidWorldExplosiveBottle, "Crossguard.Scenario.Raid.ExplosiveBottle", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioRaidWorldExplosiveBottle::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioRaidWorld;

	FScenario Scenario(*this);

	// The thrower stands 6 m from the target, beyond the 3 m blast; a bystander 1.8 m beside the
	// target is inside it, a far dummy 6 m beside it is not.
	Scenario.Step(TEXT("A player readies an explosive bottle; a target 6 m ahead, a bystander beside it, one far away"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(0.0f, 2000.0f, 0.0f), 0.0f);
		ACrossguardCharacter* Target = C.SpawnDummy(TEXT("Target"), FVector(600.0f, 2000.0f, 0.0f), 180.0f);
		ACrossguardCharacter* Bystander = C.SpawnDummy(TEXT("Bystander"), FVector(600.0f, 2180.0f, 0.0f), 180.0f);
		ACrossguardCharacter* Far = C.SpawnDummy(TEXT("Far"), FVector(600.0f, 2600.0f, 0.0f), 180.0f);
		if (!C.Require(Player != nullptr && Target != nullptr && Bystander != nullptr && Far != nullptr, TEXT("player and dummies spawned")))
		{
			return;
		}
		const FCrossguardItemRow* Row = FindItemRowOf(C, Player, ExplosiveBottle);
		if (Row == nullptr)
		{
			return;
		}
		if (!C.Require(Row->ThrowRadiusMeters > 2.0f && Row->ThrowRadiusMeters < 5.5f,
			FString::Printf(TEXT("the scene assumes a blast radius of 2-5.5 m (explosive_bottle has %.1f m)"), Row->ThrowRadiusMeters)))
		{
			return;
		}
		if (!C.Require(C.GiveItem(Player, ExplosiveBottle, 1) == 0, TEXT("the bottle fits in the inventory")))
		{
			return;
		}
		const int32 StackId = StackIdOf(Player, ExplosiveBottle);
		Player->GetInventoryComponent()->RequestReadyItem(StackId);
		C.Test.TestEqual(TEXT("The bottle is in hand"), Player->GetInventoryComponent()->GetReadiedItemId(), StackId);
		AimForThrow(C, Player, Target->GetActorLocation());
	});

	Scenario.Wait(LandSeconds);

	Scenario.Step(TEXT("Left click throws the bottle at the target"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		ACrossguardCharacter* Target = RequireCharacter(C, TEXT("Target"));
		ACrossguardCharacter* Bystander = RequireCharacter(C, TEXT("Bystander"));
		ACrossguardCharacter* Far = RequireCharacter(C, TEXT("Far"));
		if (Player == nullptr || Target == nullptr || Bystander == nullptr || Far == nullptr)
		{
			return;
		}
		AimForThrow(C, Player, Target->GetActorLocation());
		C.SetNumber(TEXT("Target0"), C.Health(Target));
		C.SetNumber(TEXT("Bystander0"), C.Health(Bystander));
		C.SetNumber(TEXT("Far0"), C.Health(Far));
		C.SetNumber(TEXT("Player0"), C.Health(Player));
		TapReadiedItem(Player);
		C.Test.TestEqual(TEXT("The bottle is used up"), Player->GetInventoryComponent()->CountItem(ExplosiveBottle), 0);
	});

	Scenario.WaitUntil(TEXT("the bottle explodes"), 3.0f, [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Target = RequireCharacter(C, TEXT("Target"));
		return Target == nullptr || C.Health(Target) < static_cast<float>(C.Number(TEXT("Target0")));
	});

	Scenario.Step(TEXT("The blast hits everyone within the radius once; a damage area is left; the thrower steps into it"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		ACrossguardCharacter* Target = RequireCharacter(C, TEXT("Target"));
		ACrossguardCharacter* Bystander = RequireCharacter(C, TEXT("Bystander"));
		ACrossguardCharacter* Far = RequireCharacter(C, TEXT("Far"));
		const FCrossguardItemRow* Row = FindItemRowOf(C, Player, ExplosiveBottle);
		if (Player == nullptr || Target == nullptr || Bystander == nullptr || Far == nullptr || Row == nullptr)
		{
			return;
		}
		const float Blast = Row->ThrowDamage + Row->ThrowDamagePerRarity * UCrossguardItemLibrary::GetRarityLevel(ECrossguardRarity::Common);
		const float TargetExpected = ThrowHitDamage(C, Player, Target, *Row, Blast);
		const float BystanderExpected = ThrowHitDamage(C, Player, Bystander, *Row, Blast);
		C.Test.AddInfo(FString::Printf(TEXT("Blast: target -%.2f (expected -%.2f), bystander -%.2f (expected -%.2f)"),
			static_cast<float>(C.Number(TEXT("Target0"))) - C.Health(Target), TargetExpected,
			static_cast<float>(C.Number(TEXT("Bystander0"))) - C.Health(Bystander), BystanderExpected));
		C.Test.TestEqual(TEXT("The target takes the blast"), static_cast<float>(C.Number(TEXT("Target0"))) - C.Health(Target), TargetExpected, 0.05f);
		C.Test.TestEqual(TEXT("A bystander within the radius takes the blast too"), static_cast<float>(C.Number(TEXT("Bystander0"))) - C.Health(Bystander),
			BystanderExpected, 0.05f);
		C.Test.TestEqual(TEXT("A dummy outside the radius takes nothing"), C.Health(Far), static_cast<float>(C.Number(TEXT("Far0"))), 0.001f);
		C.Test.TestEqual(TEXT("The thrower, 6 m away, takes nothing"), C.Health(Player), static_cast<float>(C.Number(TEXT("Player0"))), 0.001f);

		ACrossguardDamageArea* Area = C.FindNearest<ACrossguardDamageArea>(Target->GetActorLocation(), Row->ThrowRadiusMeters * 100.0f + 100.0f);
		if (!C.Require(Area != nullptr, TEXT("the explosion leaves a damage area (ACrossguardDamageArea) where it hit")))
		{
			return;
		}
		C.Remember(TEXT("Area"), Area);
		C.SetNumber(TEXT("Target1"), C.Health(Target));
		C.SetNumber(TEXT("Far1"), C.Health(Far));

		// "You too, if you stand in it": the thrower moves 1.5 m from the area's centre (on the
		// thrower's side), well inside the radius, before its first pulse (1 s after the blast).
		const FVector Center = Area->GetActorLocation();
		C.Place(Player, FVector(Center.X - 150.0f, Center.Y, 0.0f), 0.0f);
		C.SetNumber(TEXT("Player1"), C.Health(Player));
	});

	// Pulses at 1 s and 2 s after the blast.
	Scenario.Wait(2.3f);

	Scenario.Step(TEXT("Two pulses of damage over time: target and thrower"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		ACrossguardCharacter* Target = RequireCharacter(C, TEXT("Target"));
		const FCrossguardItemRow* Row = FindItemRowOf(C, Player, ExplosiveBottle);
		if (Player == nullptr || Target == nullptr || Row == nullptr)
		{
			return;
		}
		const float TargetPulse = ThrowHitDamage(C, Player, Target, *Row, Row->ThrowDotPerSecond);
		const float PlayerPulse = ThrowHitDamage(C, Player, Player, *Row, Row->ThrowDotPerSecond);
		C.Test.TestEqual(TEXT("The target took 2 pulses after 2 s"), static_cast<float>(C.Number(TEXT("Target1"))) - C.Health(Target), 2.0f * TargetPulse, 0.1f);
		C.Test.TestEqual(TEXT("The thrower standing in it took 2 pulses"), static_cast<float>(C.Number(TEXT("Player1"))) - C.Health(Player), 2.0f * PlayerPulse,
			0.1f);
		C.Test.TestNotNull(TEXT("The damage area is still there"), C.FindActor(TEXT("Area")));
	});

	// To 5.5 s after the blast: all 5 pulses, and the area is gone.
	Scenario.Wait(3.0f);

	Scenario.Step(TEXT("Five pulses in all, then the area is gone"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		ACrossguardCharacter* Target = RequireCharacter(C, TEXT("Target"));
		ACrossguardCharacter* Far = RequireCharacter(C, TEXT("Far"));
		const FCrossguardItemRow* Row = FindItemRowOf(C, Player, ExplosiveBottle);
		if (Player == nullptr || Target == nullptr || Far == nullptr || Row == nullptr)
		{
			return;
		}
		// ACrossguardDamageArea: one pulse per whole second.
		const float Pulses = static_cast<float>(FMath::Max(0, FMath::RoundToInt(Row->ThrowDotSeconds)));
		const float TargetPulse = ThrowHitDamage(C, Player, Target, *Row, Row->ThrowDotPerSecond);
		const float PlayerPulse = ThrowHitDamage(C, Player, Player, *Row, Row->ThrowDotPerSecond);
		C.Test.AddInfo(FString::Printf(TEXT("Damage over time: target -%.2f, thrower -%.2f (%.0f pulses of %.2f / %.2f)"),
			static_cast<float>(C.Number(TEXT("Target1"))) - C.Health(Target), static_cast<float>(C.Number(TEXT("Player1"))) - C.Health(Player), Pulses,
			TargetPulse, PlayerPulse));
		C.Test.TestEqual(TEXT("The target took every pulse"), static_cast<float>(C.Number(TEXT("Target1"))) - C.Health(Target), Pulses * TargetPulse, 0.1f);
		C.Test.TestEqual(TEXT("The thrower took every pulse"), static_cast<float>(C.Number(TEXT("Player1"))) - C.Health(Player), Pulses * PlayerPulse, 0.1f);
		C.Test.TestEqual(TEXT("The far dummy never took damage"), C.Health(Far), static_cast<float>(C.Number(TEXT("Far1"))), 0.001f);
		C.Test.TestNull(TEXT("The damage area is gone after its last pulse"), C.FindActor(TEXT("Area")));
	});

	Scenario.Run();
	return true;
}

// ------------------------------------------------------------------------------------------------
// Checklist: "Press 1 and 2 with weapons in both sets (equip with RequestEquipItem(..., WeaponSet))
// -> After about 1 s of drawing the new set attacks; nothing attacks while drawing."
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioRaidWorldWeaponSetSwitch, "Crossguard.Scenario.Raid.WeaponSetSwitch", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioRaidWorldWeaponSetSwitch::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioRaidWorld;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("Set 1: sword; set 2: a dagger equipped from the inventory with RequestEquipItem"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(0.0f, 1000.0f, 0.0f), 0.0f);
		ACrossguardCharacter* Dummy = C.SpawnDummy(TEXT("Dummy"), FVector(150.0f, 1000.0f, 0.0f), 180.0f);
		if (!C.Require(Player != nullptr && Dummy != nullptr, TEXT("player and dummy spawned")) || !C.Equip(Player, Sword))
		{
			return;
		}
		UCrossguardInventoryComponent* Inventory = Player->GetInventoryComponent();
		UCrossguardEquipmentComponent* Equipment = Player->GetEquipmentComponent();
		FCrossguardInventoryItem DaggerItem;
		DaggerItem.Category = ECrossguardItemCategory::Weapon;
		DaggerItem.Weapon.WeaponId = Dagger;
		if (!C.Require(Inventory->AddItem(DaggerItem) == 0, TEXT("a dagger fits in the inventory")))
		{
			return;
		}
		const int32 DaggerId = WeaponStackIdOf(Player, Dagger);
		Inventory->RequestEquipItem(DaggerId, ECrossguardEquipmentSlot::MainHand, 1);
		C.Test.TestEqual(TEXT("Set 1 is in hand"), Equipment->GetActiveWeaponSet(), 0);
		C.Test.TestTrue(TEXT("Set 2's main hand holds the dagger"), Equipment->GetWeaponInSet(1, ECrossguardEquipmentSlot::MainHand).WeaponId == Dagger);
		C.Test.TestTrue(TEXT("The sword is still in set 1"), Equipment->GetItem(ECrossguardEquipmentSlot::MainHand).WeaponId == Sword);
		C.Test.TestEqual(TEXT("The dagger left the inventory"), WeaponStackIdOf(Player, Dagger), 0);
		C.Aim(Player, Dummy);
	});

	Scenario.Wait(LandSeconds);

	Scenario.Step(TEXT("Key 2: set 2 starts drawing; nothing attacks"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		ACrossguardCharacter* Dummy = RequireCharacter(C, TEXT("Dummy"));
		if (Player == nullptr || Dummy == nullptr)
		{
			return;
		}
		C.Aim(Player, Dummy);
		UCrossguardEquipmentComponent* Equipment = Player->GetEquipmentComponent();
		C.Test.TestTrue(TEXT("Set 1 is ready before the switch"), Equipment->AreWeaponsReady());
		Equipment->RequestSwitchWeaponSet(1);
		C.Test.TestEqual(TEXT("Set 2 is active"), Equipment->GetActiveWeaponSet(), 1);
		C.Test.TestTrue(TEXT("Set 2 is being drawn"), Equipment->IsDrawingWeapons());
		C.Test.TestTrue(TEXT("The dagger is in the main hand"), Equipment->GetItem(ECrossguardEquipmentSlot::MainHand).WeaponId == Dagger);
		C.Test.TestFalse(TEXT("No attack while drawing"), C.BasicAttack(Player));
		const float Dexterity = C.Stat(Player, UCrossguardAttributeSet::GetDexterityAttribute());
		const float DrawSeconds = static_cast<float>(Crossguard::WeaponDrawSeconds(Crossguard::FItemConfig(), Dexterity));
		C.Test.AddInfo(FString::Printf(TEXT("Draw time at Dexterity %.2f: %.2f s"), Dexterity, DrawSeconds));
		C.SetNumber(TEXT("DrawSeconds"), DrawSeconds);
		C.SetNumber(TEXT("DrawStart"), C.Now());
		C.SetNumber(TEXT("AttacksWhileDrawing"), 0.0);
	});

	Scenario.WaitUntil(TEXT("set 2 is drawn (left click tried every frame meanwhile)"), 5.0f, [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		if (Player == nullptr)
		{
			return true;
		}
		const UCrossguardEquipmentComponent* Equipment = Player->GetEquipmentComponent();
		if (Equipment->IsDrawingWeapons())
		{
			if (C.BasicAttack(Player))
			{
				C.SetNumber(TEXT("AttacksWhileDrawing"), C.Number(TEXT("AttacksWhileDrawing")) + 1.0);
			}
			return false;
		}
		C.SetNumber(TEXT("DrawEnd"), C.Now());
		return true;
	});

	Scenario.Step(TEXT("About the draw time later the dagger attacks"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		ACrossguardCharacter* Dummy = RequireCharacter(C, TEXT("Dummy"));
		if (Player == nullptr || Dummy == nullptr)
		{
			return;
		}
		C.Test.TestEqual(TEXT("Nothing attacked while drawing"), static_cast<int32>(C.Number(TEXT("AttacksWhileDrawing"))), 0);
		C.Test.TestEqual(TEXT("Drawing took the draw time"), static_cast<float>(C.Number(TEXT("DrawEnd")) - C.Number(TEXT("DrawStart"))),
			static_cast<float>(C.Number(TEXT("DrawSeconds"))), 0.15f);
		C.Test.TestTrue(TEXT("Set 2 is ready"), Player->GetEquipmentComponent()->AreWeaponsReady());

		C.Aim(Player, Dummy);
		const float Before = C.Health(Dummy);
		const float Expected = FMath::Min(C.ExpectedDamage(Player, Dummy, C.WeaponValue(Player)), Before);
		if (!C.Require(C.BasicAttack(Player), TEXT("the dagger attack activates")))
		{
			return;
		}
		C.Test.TestEqual(TEXT("A dagger hit from set 2"), Before - C.Health(Dummy), Expected, 0.05f);

		// Key 1: back to the sword.
		UCrossguardEquipmentComponent* Equipment = Player->GetEquipmentComponent();
		Equipment->RequestSwitchWeaponSet(0);
		C.Test.TestEqual(TEXT("Set 1 is active again"), Equipment->GetActiveWeaponSet(), 0);
		C.Test.TestTrue(TEXT("Set 1 is being drawn"), Equipment->IsDrawingWeapons());
		C.Test.TestTrue(TEXT("The sword is back in the main hand"), Equipment->GetItem(ECrossguardEquipmentSlot::MainHand).WeaponId == Sword);
		C.Test.TestTrue(TEXT("The dagger is stowed in set 2"), Equipment->GetWeaponInSet(1, ECrossguardEquipmentSlot::MainHand).WeaponId == Dagger);
		C.Test.TestFalse(TEXT("No attack while drawing set 1"), C.BasicAttack(Player));
		C.SetNumber(TEXT("DrawStart"), C.Now());
	});

	Scenario.WaitUntil(TEXT("set 1 is drawn"), 5.0f, [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		if (Player == nullptr)
		{
			return true;
		}
		if (Player->GetEquipmentComponent()->IsDrawingWeapons())
		{
			return false;
		}
		C.SetNumber(TEXT("DrawEnd"), C.Now());
		return true;
	});

	Scenario.Step(TEXT("The sword attacks"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		ACrossguardCharacter* Dummy = RequireCharacter(C, TEXT("Dummy"));
		if (Player == nullptr || Dummy == nullptr)
		{
			return;
		}
		C.Test.TestEqual(TEXT("Drawing set 1 took the draw time"), static_cast<float>(C.Number(TEXT("DrawEnd")) - C.Number(TEXT("DrawStart"))),
			static_cast<float>(C.Number(TEXT("DrawSeconds"))), 0.15f);
		C.Aim(Player, Dummy);
		const float Before = C.Health(Dummy);
		const float Expected = FMath::Min(C.ExpectedDamage(Player, Dummy, C.WeaponValue(Player)), Before);
		if (!C.Require(C.BasicAttack(Player), TEXT("the sword attack activates")))
		{
			return;
		}
		C.Test.TestEqual(TEXT("A sword hit from set 1"), Before - C.Health(Dummy), Expected, 0.05f);
	});

	Scenario.Run();
	return true;
}

// ------------------------------------------------------------------------------------------------
// Checklist: "Shoot the bow 45 times -> Arrows run out and the bow stops firing." (DebugInventory
// arrow 45: "expect stacks of 20 + 20 + 5".)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioRaidWorldBowRunsOutOfArrows, "Crossguard.Scenario.Raid.BowRunsOutOfArrows", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioRaidWorldBowRunsOutOfArrows::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioRaidWorld;

	FScenario Scenario(*this);
	// 45 shots at 1 per second: 4.5 real seconds at x10.
	Scenario.TimeDilation(10.0f);

	Scenario.Step(TEXT("A player with a bow and 45 arrows (20 + 20 + 5)"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(0.0f, 1000.0f, 0.0f), 0.0f);
		if (!C.Require(Player != nullptr, TEXT("player spawned")) || !C.Equip(Player, Bow))
		{
			return;
		}
		if (!C.Require(C.GiveItem(Player, Arrow, ArrowCount) == 0, TEXT("45 arrows fit in the inventory")))
		{
			return;
		}
		C.Test.TestEqual(TEXT("45 arrows"), Player->GetInventoryComponent()->CountItem(Arrow), ArrowCount);
		C.Test.TestEqual(TEXT("In 3 stacks (20 + 20 + 5)"), StackCountOf(Player, Arrow), 3);
		C.SetNumber(TEXT("Shots"), 0.0);
		// At the floor 20 m ahead: the arrows land there.
		C.AimAt(Player, FVector(2000.0f, 1000.0f, 0.0f));
	});

	Scenario.Wait(LandSeconds);

	Scenario.WaitUntil(TEXT("every arrow is shot"), 200.0f, [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		if (Player == nullptr)
		{
			return true;
		}
		if (Player->GetInventoryComponent()->CountItem(Arrow) <= 0)
		{
			return true;
		}
		C.AimAt(Player, FVector(2000.0f, 1000.0f, 0.0f));
		if (C.BasicAttack(Player))
		{
			C.SetNumber(TEXT("Shots"), C.Number(TEXT("Shots")) + 1.0);
		}
		return false;
	});

	Scenario.Step(TEXT("45 shots, no arrows left; the bow does not fire"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		if (Player == nullptr)
		{
			return;
		}
		C.Test.TestEqual(TEXT("One arrow per shot: 45 shots"), static_cast<int32>(C.Number(TEXT("Shots"))), ArrowCount);
		C.Test.TestEqual(TEXT("No arrows left"), Player->GetInventoryComponent()->CountItem(Arrow), 0);
		C.Test.TestEqual(TEXT("No arrow stacks left"), StackCountOf(Player, Arrow), 0);
	});

	// Past the last shot's recovery.
	Scenario.Wait(3.0f);

	Scenario.Step(TEXT("Left click does nothing without arrows"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		if (Player == nullptr)
		{
			return;
		}
		C.Test.TestTrue(TEXT("The bow is drawn and in hand"), Player->GetEquipmentComponent()->AreWeaponsReady());
		C.Test.TestFalse(TEXT("The bow does not fire without arrows"), C.BasicAttack(Player));
	});

	Scenario.Run();
	return true;
}

// ------------------------------------------------------------------------------------------------
// Checklist: "Press 3 (debug consumables fill the hotbar in order), then left click -> A potion heals
// 15 HP over 10 s; a bandage heals 10 HP while left click is held for 2 s." Also: releasing a bandage
// early stops its healing (the bandage is used up).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioRaidWorldPotionAndBandage, "Crossguard.Scenario.Raid.PotionAndBandage", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioRaidWorldPotionAndBandage::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioRaidWorld;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A hurt player with a potion (key 3) and 2 bandages (key 4)"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(0.0f, 1000.0f, 0.0f), 0.0f);
		if (!C.Require(Player != nullptr, TEXT("player spawned")))
		{
			return;
		}
		if (!C.Require(C.GiveItem(Player, HealingPotion, 1) == 0 && C.GiveItem(Player, Bandage, 2) == 0, TEXT("a potion and 2 bandages fit")))
		{
			return;
		}
		UCrossguardInventoryComponent* Inventory = Player->GetInventoryComponent();
		// As the Debug inventory does: consumables fill the hotbar in order.
		Inventory->RequestAssignHotbarSlot(0, StackIdOf(Player, HealingPotion));
		Inventory->RequestAssignHotbarSlot(1, StackIdOf(Player, Bandage));
		if (!C.Require(C.MaxHealth(Player) > HealthDeficit + 1.0f, TEXT("Max Health above 41")))
		{
			return;
		}
		SetHealthOf(Player, C.MaxHealth(Player) - HealthDeficit);
		C.Test.TestEqual(TEXT("The player is hurt"), C.Health(Player), C.MaxHealth(Player) - HealthDeficit, 0.01f);
	});

	Scenario.Wait(LandSeconds);

	Scenario.Step(TEXT("Key 3, left click: the potion is used at once and starts healing"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		if (Player == nullptr)
		{
			return;
		}
		UCrossguardInventoryComponent* Inventory = Player->GetInventoryComponent();
		Inventory->RequestUseHotbarSlot(0);
		if (!C.Require(Inventory->GetReadiedItemId() == StackIdOf(Player, HealingPotion), TEXT("key 3 readies the potion")))
		{
			return;
		}
		C.SetNumber(TEXT("Health0"), C.Health(Player));
		TapReadiedItem(Player);
		C.Test.TestEqual(TEXT("The potion is used up at once"), Inventory->CountItem(HealingPotion), 0);
		C.Test.TestEqual(TEXT("No potion left: the hands are empty"), Inventory->GetReadiedItemId(), 0);
	});

	Scenario.Wait(5.25f);

	Scenario.Step(TEXT("Half-way: about half the potion's healing"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		const FCrossguardItemRow* Row = FindItemRowOf(C, Player, HealingPotion);
		if (Player == nullptr || Row == nullptr)
		{
			return;
		}
		const float Total = Row->HealAmount + Row->HealPerRarity * UCrossguardItemLibrary::GetRarityLevel(ECrossguardRarity::Common);
		const float Healed = C.Health(Player) - static_cast<float>(C.Number(TEXT("Health0")));
		const float Fraction = FMath::Min(5.25f / FMath::Max(Row->HealDuration, 0.01f), 1.0f);
		C.Test.AddInfo(FString::Printf(TEXT("Potion after 5.25 s: +%.2f of %.2f over %.1f s"), Healed, Total, Row->HealDuration));
		// One heal tick (0.5 s) of slack.
		const float Tick = Total * UCrossguardHealOverTimeEffect::TickSeconds / FMath::Max(Row->HealDuration, 0.01f);
		C.Test.TestEqual(TEXT("The potion heals over time, not at once"), Healed, Total * Fraction, Tick + 0.01f);
	});

	Scenario.Wait(5.25f);

	Scenario.Step(TEXT("After 10.5 s: the potion's full healing; key 4 readies a bandage and left click is held"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		const FCrossguardItemRow* Row = FindItemRowOf(C, Player, HealingPotion);
		if (Player == nullptr || Row == nullptr)
		{
			return;
		}
		const float Total = Row->HealAmount + Row->HealPerRarity * UCrossguardItemLibrary::GetRarityLevel(ECrossguardRarity::Common);
		C.Test.TestEqual(TEXT("The potion healed its full amount"), C.Health(Player) - static_cast<float>(C.Number(TEXT("Health0"))), Total, 0.01f);

		UCrossguardInventoryComponent* Inventory = Player->GetInventoryComponent();
		Inventory->RequestUseHotbarSlot(1);
		if (!C.Require(Inventory->GetReadiedItemId() == StackIdOf(Player, Bandage), TEXT("key 4 readies the bandages")))
		{
			return;
		}
		C.SetNumber(TEXT("Health1"), C.Health(Player));
		// Pressed and held.
		Inventory->RequestStartUse();
		C.Test.TestEqual(TEXT("The bandage is used up when the channel starts"), Inventory->CountItem(Bandage), 1);
		// The hands are busy: another item cannot be readied mid-channel.
		Inventory->RequestReadyItem(0);
		C.Test.TestTrue(TEXT("The hands stay on the bandage while channelling"), Inventory->GetReadiedItemId() != 0);
	});

	Scenario.Wait(2.5f);

	Scenario.Step(TEXT("Held for the whole channel: the bandage's full healing; the second bandage starts"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		const FCrossguardItemRow* Row = FindItemRowOf(C, Player, Bandage);
		if (Player == nullptr || Row == nullptr)
		{
			return;
		}
		UCrossguardInventoryComponent* Inventory = Player->GetInventoryComponent();
		// Released after the channel finished: nothing to stop.
		Inventory->RequestStopUse();
		const float Total = Row->HealAmount + Row->HealPerRarity * UCrossguardItemLibrary::GetRarityLevel(ECrossguardRarity::Common);
		C.Test.AddInfo(FString::Printf(TEXT("Bandage held %.1f s: +%.2f (expected %.2f)"), Row->UseTime,
			C.Health(Player) - static_cast<float>(C.Number(TEXT("Health1"))), Total));
		C.Test.TestEqual(TEXT("A bandage held to the end heals its full amount"), C.Health(Player) - static_cast<float>(C.Number(TEXT("Health1"))), Total, 0.01f);
		C.Test.TestTrue(TEXT("The hands move on to the next bandage"), Inventory->GetReadiedItemId() != 0);

		C.SetNumber(TEXT("Health2"), C.Health(Player));
		Inventory->RequestStartUse();
		C.Test.TestEqual(TEXT("The second bandage is used up"), Inventory->CountItem(Bandage), 0);
	});

	Scenario.Wait(BandageEarlyRelease);

	Scenario.Step(TEXT("Released early: the healing stops"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		if (Player == nullptr)
		{
			return;
		}
		Player->GetInventoryComponent()->RequestStopUse();
		C.SetNumber(TEXT("Health3"), C.Health(Player));
	});

	Scenario.Wait(2.0f);

	Scenario.Step(TEXT("Only the ticks before the release healed"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		const FCrossguardItemRow* Row = FindItemRowOf(C, Player, Bandage);
		if (Player == nullptr || Row == nullptr)
		{
			return;
		}
		const float Total = Row->HealAmount + Row->HealPerRarity * UCrossguardItemLibrary::GetRarityLevel(ECrossguardRarity::Common);
		// UCrossguardHealOverTimeEffect::SetHealing: equal ticks every TickSeconds, the first one tick in.
		const float TickSeconds = UCrossguardHealOverTimeEffect::TickSeconds;
		const int32 Ticks = FMath::Max(1, FMath::RoundToInt(Row->HealDuration / TickSeconds));
		const int32 TicksBeforeRelease = FMath::Min(Ticks, FMath::FloorToInt(BandageEarlyRelease / TickSeconds));
		const float Expected = Total * static_cast<float>(TicksBeforeRelease) / static_cast<float>(Ticks);
		const float Healed = C.Health(Player) - static_cast<float>(C.Number(TEXT("Health2")));
		C.Test.AddInfo(FString::Printf(TEXT("Bandage released after %.2f s: +%.2f (expected %.2f of %.2f)"), BandageEarlyRelease, Healed, Expected, Total));
		C.Test.TestEqual(TEXT("A bandage released early heals only until the release"), Healed, Expected, 0.01f);
		C.Test.TestEqual(TEXT("No healing after the release"), C.Health(Player), static_cast<float>(C.Number(TEXT("Health3"))), 0.01f);
		C.Test.TestEqual(TEXT("No bandages left: the hands are empty"), Player->GetInventoryComponent()->GetReadiedItemId(), 0);
	});

	Scenario.Run();
	return true;
}

// ------------------------------------------------------------------------------------------------
// Checklist: "Put gear on the dummy, kill it, press F at the body -> With bDebugLootTakesAll, its items
// move into your inventory."
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioRaidWorldLootBody, "Crossguard.Scenario.Raid.LootBody", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioRaidWorldLootBody::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioRaidWorld;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A dummy with a sword and 2 bandages is killed in front of the player"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(0.0f, 1000.0f, 0.0f), 0.0f);
		ACrossguardCharacter* Dummy = C.SpawnDummy(TEXT("Dummy"), FVector(150.0f, 1000.0f, 0.0f), 180.0f);
		if (!C.Require(Player != nullptr && Dummy != nullptr, TEXT("player and dummy spawned")) || !C.Equip(Dummy, Sword))
		{
			return;
		}
		if (!C.Require(C.GiveItem(Dummy, Bandage, 2) == 0, TEXT("2 bandages fit in the dummy's inventory")))
		{
			return;
		}
		// Only what it carries (the Blueprint may give it a loot table).
		Dummy->LootTableId = NAME_None;
		Player->bDebugLootTakesAll = true;
	});

	Scenario.Wait(LandSeconds);

	Scenario.Step(TEXT("Killed: its body holds the sword and the bandages"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		ACrossguardCharacter* Dummy = RequireCharacter(C, TEXT("Dummy"));
		if (Player == nullptr || Dummy == nullptr)
		{
			return;
		}
		C.Kill(Dummy, Player);
		if (!C.Require(C.IsDead(Dummy), TEXT("the dummy is dead")))
		{
			return;
		}
		ACrossguardLootContainer* Body = C.FindNearest<ACrossguardLootContainer>(Dummy->GetActorLocation(), SearchRadius);
		if (!C.Require(Body != nullptr, TEXT("a body (loot container) where the dummy died")))
		{
			return;
		}
		C.Remember(TEXT("Body"), Body);
		C.Test.AddInfo(FString::Printf(TEXT("Body holds: %s"), *DescribeItems(Body->GetItems())));
		C.Test.TestEqual(TEXT("The body holds the sword and one bandage stack"), Body->GetItems().Num(), 2);
		C.Test.TestEqual(TEXT("The body holds 3 items in all"), SumCounts(Body->GetItems()), 3);
		C.Test.TestTrue(TEXT("The dummy's hands are empty"), Dummy->GetEquipmentComponent()->GetItem(ECrossguardEquipmentSlot::MainHand).IsEmpty());
		FaceInteractable(C, Player, Body);
	});

	Scenario.Wait(ViewSettleSeconds);

	Scenario.Step(TEXT("F at the body takes everything"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		ACrossguardLootContainer* Body = C.Get<ACrossguardLootContainer>(TEXT("Body"));
		if (Player == nullptr || Body == nullptr)
		{
			C.Fail(TEXT("the player or the body is gone"));
			return;
		}
		FaceInteractable(C, Player, Body);
		C.ClearLog();
		Player->TryInteract();
		C.Test.TestTrue(TEXT("The body is open in the player's inventory"), Player->GetInventoryComponent()->GetOpenContainer() == Body);
		C.Test.TestTrue(TEXT("Log: opened"), C.LogContains(TEXT("opened")));
		C.Test.TestEqual(TEXT("The body is empty"), Body->GetItems().Num(), 0);
		C.Test.TestTrue(TEXT("The sword is in the player's inventory"), WeaponStackIdOf(Player, Sword) != 0);
		C.Test.TestEqual(TEXT("Both bandages are in the player's inventory"), Player->GetInventoryComponent()->CountItem(Bandage), 2);
	});

	Scenario.Run();
	return true;
}

// ------------------------------------------------------------------------------------------------
// Checklist: "Die with DebugReviveDelay set -> You ragdoll, a body appears, then you stand up with
// your gear back."
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioRaidWorldDeathAndRevive, "Crossguard.Scenario.Raid.DeathAndRevive", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioRaidWorldDeathAndRevive::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioRaidWorld;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A player with a sword, a plate helm and 2 potions; DebugReviveDelay 5"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(0.0f, 1000.0f, 0.0f), 0.0f);
		if (!C.Require(Player != nullptr, TEXT("player spawned")) || !C.Equip(Player, Sword))
		{
			return;
		}
		if (!C.Require(C.GiveItem(Player, HealingPotion, 2) == 0, TEXT("2 potions fit")))
		{
			return;
		}
		UCrossguardEquipmentComponent* Equipment = Player->GetEquipmentComponent();
		if (Equipment->ArmorTable != nullptr)
		{
			FCrossguardArmorItem Helm;
			Helm.ArmorId = PlateHelm;
			C.Test.TestTrue(TEXT("The plate helm goes on"), Equipment->EquipArmor(ECrossguardArmorSlot::Head, Helm) == ECrossguardEquipError::None);
		}
		else
		{
			C.Test.AddWarning(TEXT("The character's EquipmentComponent has no Armor Table (DT_Armor): testing without armor"));
		}
		C.SetNumber(TEXT("ArmorResistance"), C.Stat(Player, UCrossguardAttributeSet::GetAttackResistanceAttribute()));
		// Read at death (HandleDeath): set after spawning, since spawning clears the Debug settings.
		Player->DebugReviveDelay = ReviveDelay;
	});

	Scenario.Wait(LandSeconds);

	Scenario.Step(TEXT("Killed: dead, ragdolled, everything on a body"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		if (Player == nullptr)
		{
			return;
		}
		C.Kill(Player);
		if (!C.Require(C.IsDead(Player), TEXT("the player is dead")))
		{
			return;
		}
		C.SetNumber(TEXT("DiedAt"), C.Now());
		C.SetNumber(TEXT("MaxHealthDead"), C.MaxHealth(Player));
		C.Test.TestTrue(TEXT("The capsule stops colliding (walk-through ragdoll)"),
			Player->GetCapsuleComponent()->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
		ACrossguardLootContainer* Body = C.FindNearest<ACrossguardLootContainer>(Player->GetActorLocation(), SearchRadius);
		if (!C.Require(Body != nullptr, TEXT("a body where the player died")))
		{
			return;
		}
		C.Remember(TEXT("Body"), Body);
		const bool bHelm = Player->GetEquipmentComponent()->ArmorTable != nullptr;
		C.Test.AddInfo(FString::Printf(TEXT("Body holds: %s"), *DescribeItems(Body->GetItems())));
		C.Test.TestEqual(TEXT("The body holds the sword, the potions (and the helm)"), Body->GetItems().Num(), bHelm ? 3 : 2);
		C.Test.TestEqual(TEXT("The inventory is empty"), Player->GetInventoryComponent()->GetItems().Num(), 0);
		C.Test.TestTrue(TEXT("The main hand is empty"), Player->GetEquipmentComponent()->GetItem(ECrossguardEquipmentSlot::MainHand).IsEmpty());
		C.Test.TestFalse(TEXT("Not permanently dead"), Player->IsPermanentlyDead());
	});

	Scenario.Wait(ReviveDelay - 0.5f);

	Scenario.Step(TEXT("Half a second before the revive: still dead"), [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		if (Player != nullptr)
		{
			C.Test.TestTrue(TEXT("Still dead before DebugReviveDelay"), C.IsDead(Player));
		}
	});

	Scenario.WaitUntil(TEXT("the player revives"), 2.0f, [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		return Player == nullptr || !C.IsDead(Player);
	});

	Scenario.Step(TEXT("Revived at half health with the gear back on; the body is gone"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		if (Player == nullptr)
		{
			return;
		}
		C.Test.TestEqual(TEXT("Revived DebugReviveDelay after death"), C.Now() - static_cast<float>(C.Number(TEXT("DiedAt"))), ReviveDelay, 0.2f);
		// ACrossguardCharacter::Revive(0.5), before the gear goes back on.
		const float ExpectedHealth = FMath::Max(1.0f, 0.5f * static_cast<float>(C.Number(TEXT("MaxHealthDead"))));
		C.Test.TestEqual(TEXT("Half health"), C.Health(Player), ExpectedHealth, 0.5f);
		C.Test.TestTrue(TEXT("The capsule collides again"), Player->GetCapsuleComponent()->GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics);
		const UCrossguardEquipmentComponent* Equipment = Player->GetEquipmentComponent();
		C.Test.TestTrue(TEXT("The sword is back in the main hand"), Equipment->GetItem(ECrossguardEquipmentSlot::MainHand).WeaponId == Sword);
		if (Equipment->ArmorTable != nullptr)
		{
			C.Test.TestTrue(TEXT("The plate helm is back on"), Equipment->GetArmorItem(ECrossguardArmorSlot::Head).ArmorId == PlateHelm);
		}
		C.Test.TestEqual(TEXT("The helm's resistance counts again"), C.Stat(Player, UCrossguardAttributeSet::GetAttackResistanceAttribute()),
			static_cast<float>(C.Number(TEXT("ArmorResistance"))), 0.001f);
		C.Test.TestEqual(TEXT("The potions are back in the inventory"), Player->GetInventoryComponent()->CountItem(HealingPotion), 2);
		C.Test.TestNull(TEXT("The body is gone (everything went back)"), C.FindActor(TEXT("Body")));
		C.Test.TestTrue(TEXT("Log: revived"), C.LogContains(TEXT("revived")));
		// Death puts the weapons away; 1 / 2 draw them again.
		C.Test.TestFalse(TEXT("The weapons are put away after the revive"), Equipment->AreWeaponsReady());
		Player->GetEquipmentComponent()->RequestSwitchWeaponSet(0);
	});

	Scenario.WaitUntil(TEXT("key 1 draws the sword after the revive"), 3.0f, [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		return Player == nullptr || Player->GetEquipmentComponent()->AreWeaponsReady();
	});

	Scenario.Step(TEXT("The revived player attacks"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		if (Player != nullptr)
		{
			C.Test.TestTrue(TEXT("Left click attacks after the revive"), C.BasicAttack(Player));
		}
	});

	Scenario.Run();
	return true;
}

// ------------------------------------------------------------------------------------------------
// Checklist: "Have someone loot your body before the revive -> The looted items stay gone." Looting
// through the inventory loot API (F opens, RequestTakeFromContainer per item).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioRaidWorldLootedBeforeRevive, "Crossguard.Scenario.Raid.LootedBeforeRevive", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioRaidWorldLootedBeforeRevive::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioRaidWorld;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A victim (sword, ruby, 2 potions, DebugReviveDelay 5) and a looter 1.5 m away"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Looter = C.SpawnPlayer(TEXT("Looter"), FVector(0.0f, 1000.0f, 0.0f), 0.0f);
		ACrossguardCharacter* Victim = C.SpawnPlayer(TEXT("Victim"), FVector(150.0f, 1000.0f, 0.0f), 180.0f);
		if (!C.Require(Looter != nullptr && Victim != nullptr, TEXT("looter and victim spawned")) || !C.Equip(Victim, Sword))
		{
			return;
		}
		if (!C.Require(C.GiveItem(Victim, Ruby, 1) == 0 && C.GiveItem(Victim, HealingPotion, 2) == 0, TEXT("a ruby and 2 potions fit")))
		{
			return;
		}
		Victim->DebugReviveDelay = ReviveDelay;
	});

	Scenario.Wait(LandSeconds);

	Scenario.Step(TEXT("The looter kills the victim"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Looter = RequireCharacter(C, TEXT("Looter"));
		ACrossguardCharacter* Victim = RequireCharacter(C, TEXT("Victim"));
		if (Looter == nullptr || Victim == nullptr)
		{
			return;
		}
		C.Kill(Victim, Looter);
		if (!C.Require(C.IsDead(Victim), TEXT("the victim is dead")))
		{
			return;
		}
		ACrossguardLootContainer* Body = C.FindNearest<ACrossguardLootContainer>(Victim->GetActorLocation(), SearchRadius);
		if (!C.Require(Body != nullptr, TEXT("a body where the victim died")))
		{
			return;
		}
		C.Remember(TEXT("Body"), Body);
		C.Test.AddInfo(FString::Printf(TEXT("Body holds: %s"), *DescribeItems(Body->GetItems())));
		FaceInteractable(C, Looter, Body);
	});

	Scenario.Wait(ViewSettleSeconds);

	Scenario.Step(TEXT("F opens the body; the looter takes the sword and the ruby"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Looter = RequireCharacter(C, TEXT("Looter"));
		ACrossguardLootContainer* Body = C.Get<ACrossguardLootContainer>(TEXT("Body"));
		if (Looter == nullptr || Body == nullptr)
		{
			C.Fail(TEXT("the looter or the body is gone"));
			return;
		}
		FaceInteractable(C, Looter, Body);
		UCrossguardInventoryComponent* Inventory = Looter->GetInventoryComponent();
		Looter->TryInteract();
		if (!C.Require(Inventory->GetOpenContainer() == Body, TEXT("F opens the body (bDebugLootTakesAll off: nothing is taken yet)")))
		{
			return;
		}
		C.Test.TestEqual(TEXT("The looter sees the body's items"), Inventory->GetOpenContainerItems().Num(), Body->GetItems().Num());

		TArray<int32> Take;
		for (const FCrossguardInventoryItem& Item : Body->GetItems())
		{
			if ((Item.Category == ECrossguardItemCategory::Weapon && Item.Weapon.WeaponId == Sword) || Item.ItemId == Ruby)
			{
				Take.Add(Item.Id);
			}
		}
		C.Test.TestEqual(TEXT("The sword and the ruby are on the body"), Take.Num(), 2);
		for (const int32 Id : Take)
		{
			Inventory->RequestTakeFromContainer(Id);
		}
		C.Test.TestTrue(TEXT("The looter has the sword"), WeaponStackIdOf(Looter, Sword) != 0);
		C.Test.TestEqual(TEXT("The looter has the ruby"), Inventory->CountItem(Ruby), 1);
		C.Test.TestEqual(TEXT("Only the potions are left on the body"), Body->GetItems().Num(), 1);
	});

	Scenario.WaitUntil(TEXT("the victim revives"), ReviveDelay + 2.0f, [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Victim = RequireCharacter(C, TEXT("Victim"));
		return Victim == nullptr || !C.IsDead(Victim);
	});

	Scenario.Step(TEXT("Revived with what was left; the looted items stay with the looter"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Looter = RequireCharacter(C, TEXT("Looter"));
		ACrossguardCharacter* Victim = RequireCharacter(C, TEXT("Victim"));
		if (Looter == nullptr || Victim == nullptr)
		{
			return;
		}
		C.Test.TestTrue(TEXT("The victim's main hand is empty (the sword was looted)"),
			Victim->GetEquipmentComponent()->GetItem(ECrossguardEquipmentSlot::MainHand).IsEmpty());
		C.Test.TestEqual(TEXT("The victim has no ruby"), Victim->GetInventoryComponent()->CountItem(Ruby), 0);
		C.Test.TestEqual(TEXT("The victim got the potions back"), Victim->GetInventoryComponent()->CountItem(HealingPotion), 2);
		C.Test.TestTrue(TEXT("The looter keeps the sword"), WeaponStackIdOf(Looter, Sword) != 0);
		C.Test.TestEqual(TEXT("The looter keeps the ruby"), Looter->GetInventoryComponent()->CountItem(Ruby), 1);
		C.Test.TestNull(TEXT("The body is gone"), C.FindActor(TEXT("Body")));
		C.Test.TestFalse(TEXT("The looter no longer has a body open"), IsValid(Looter->GetInventoryComponent()->GetOpenContainer()));
	});

	Scenario.Run();
	return true;
}

// ------------------------------------------------------------------------------------------------
// Checklist: "Place an ACrossguardExtractionZone, stand in it 5 s -> Output Log: extracted, XP banked,
// gold banked (250)."
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioRaidWorldExtractionZone, "Crossguard.Scenario.Raid.ExtractionZone", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioRaidWorldExtractionZone::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioRaidWorld;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A player in a raid with 250 gold coins and 5 raid XP; an extraction zone 10 m away"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(0.0f, 1000.0f, 0.0f), 0.0f);
		// Its box is 4 x 4 x 3 m around its centre: 1.5 m up, so it stands on the floor.
		ACrossguardExtractionZone* Zone = C.SpawnActor<ACrossguardExtractionZone>(TEXT("Zone"), FVector(1000.0f, 1000.0f, 150.0f));
		if (!C.Require(Player != nullptr && Zone != nullptr, TEXT("player and zone spawned")))
		{
			return;
		}
		C.Test.TestEqual(TEXT("The zone's countdown is the default 5 s"), Zone->ExtractSeconds, ExtractSeconds);
		if (!C.Require(C.GiveItem(Player, GoldCoin, ExtractGold) == 0, TEXT("250 gold coins fit")))
		{
			return;
		}
		ACrossguardPlayerState* Progression = C.PlayerState(Player);
		if (!C.Require(Progression != nullptr && Progression->IsInRaid(), TEXT("the player is in a raid")))
		{
			return;
		}
		Progression->AwardXp(ECrossguardXpSource::BasicMonster);
		C.SetNumber(TEXT("RaidXp"), Progression->GetRaidXp());
		C.SetNumber(TEXT("TotalXp"), Progression->GetTotalXp());
		C.SetNumber(TEXT("Gold"), Progression->GetGold());
		C.Test.TestEqual(TEXT("5 raid XP held"), Progression->GetRaidXp(), Crossguard::FXpConfig().BasicMonster);
	});

	Scenario.Wait(LandSeconds);

	Scenario.Step(TEXT("The player steps into the zone"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		if (Player == nullptr)
		{
			return;
		}
		C.ClearLog();
		C.Place(Player, FVector(1000.0f, 1000.0f, 0.0f), 0.0f);
		C.SetNumber(TEXT("EnteredAt"), C.Now());
	});

	Scenario.Wait(ExtractSeconds - 0.5f);

	Scenario.Step(TEXT("Half a second before the countdown ends: still in the raid"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		const ACrossguardPlayerState* Progression = Player != nullptr ? C.PlayerState(Player) : nullptr;
		if (C.Require(Progression != nullptr, TEXT("the player state")))
		{
			C.Test.TestTrue(TEXT("Still in the raid after 4.5 s"), Progression->IsInRaid());
		}
	});

	Scenario.WaitUntil(TEXT("the player extracts"), 2.0f, [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		const ACrossguardPlayerState* Progression = Player != nullptr ? C.PlayerState(Player) : nullptr;
		return Progression == nullptr || !Progression->IsInRaid();
	});

	Scenario.Step(TEXT("Extracted after 5 s: raid XP + 35 and the 250 gold banked"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		const ACrossguardPlayerState* Progression = Player != nullptr ? C.PlayerState(Player) : nullptr;
		if (!C.Require(Progression != nullptr, TEXT("the player state")))
		{
			return;
		}
		C.Test.TestEqual(TEXT("Extracted 5 s after stepping in"), C.Now() - static_cast<float>(C.Number(TEXT("EnteredAt"))), ExtractSeconds, 0.2f);
		const int32 ExpectedXp = static_cast<int32>(C.Number(TEXT("RaidXp"))) + Crossguard::FXpConfig().Extract;
		C.Test.TestEqual(TEXT("Raid XP plus the extraction reward is banked"), Progression->GetTotalXp() - static_cast<int32>(C.Number(TEXT("TotalXp"))),
			ExpectedXp);
		C.Test.TestEqual(TEXT("No raid XP is held any more"), Progression->GetRaidXp(), 0);
		C.Test.TestEqual(TEXT("The 250 gold coins are banked"), Progression->GetGold() - static_cast<int32>(C.Number(TEXT("Gold"))), ExtractGold);
		C.Test.TestEqual(TEXT("No gold coins carried any more"), Player->GetInventoryComponent()->CountItem(GoldCoin), 0);
		C.Test.TestTrue(TEXT("Log: extracted"), C.LogContains(TEXT("extracted")));
		C.Test.TestTrue(TEXT("Log: gold banked"), C.LogContains(TEXT("gold banked")));
	});

	Scenario.Run();
	return true;
}

// ------------------------------------------------------------------------------------------------
// Checklist: "Set the game mode's PortalUnlockSeconds to 0, place an ACrossguardPointOfInterest around
// you, press X -> A portal appears; 60 s later it opens; step in to extract. Walk 10 m+ away while it
// channels and it collapses." (Two players: A's portal opens, B walks away from B's.)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioRaidWorldExtractPortal, "Crossguard.Scenario.Raid.ExtractPortal", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioRaidWorldExtractPortal::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioRaidWorld;

	FScenario Scenario(*this);
	// The 60 s channel in 12 real seconds.
	Scenario.TimeDilation(5.0f);

	Scenario.Step(TEXT("A point of interest (20 x 20 m) with players A and B (separate teams) inside; PortalUnlockSeconds 0"), [](FScenarioContext& C)
	{
		ACrossguardPointOfInterest* Poi = C.SpawnActor<ACrossguardPointOfInterest>(TEXT("Poi"), FVector(0.0f, 1000.0f, 0.0f), 0.0f,
			[](ACrossguardPointOfInterest& Spawned) { Spawned.SetActorScale3D(FVector(0.5f)); });
		ACrossguardCharacter* A = C.SpawnPlayer(TEXT("A"), FVector(0.0f, 1000.0f, 0.0f), 0.0f);
		ACrossguardCharacter* B = C.SpawnPlayer(TEXT("B"), FVector(500.0f, 1500.0f, 0.0f), 0.0f);
		ACrossguardGameMode* Mode = C.GameMode();
		if (!C.Require(Poi != nullptr && A != nullptr && B != nullptr && Mode != nullptr, TEXT("point of interest, A and B spawned")))
		{
			return;
		}
		Mode->PortalUnlockSeconds = 0.0f;
		C.Test.TestTrue(TEXT("A stands inside the point of interest"), ACrossguardPointOfInterest::IsInsideAny(C.World(), A->GetActorLocation()));
		C.Test.TestTrue(TEXT("B stands inside the point of interest"), ACrossguardPointOfInterest::IsInsideAny(C.World(), B->GetActorLocation()));
		const ACrossguardPlayerState* StateA = C.PlayerState(A);
		const ACrossguardPlayerState* StateB = C.PlayerState(B);
		if (!C.Require(StateA != nullptr && StateB != nullptr && StateA->IsInRaid() && StateB->IsInRaid(), TEXT("A and B are in the raid")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("A and B are on different teams"), StateA->GetTeamId() != StateB->GetTeamId() || StateA->GetTeamId() < 0);
	});

	Scenario.Wait(LandSeconds);

	Scenario.Step(TEXT("X: A and B each summon a portal; A waits 3 m from its portal, B walks out of keep-alive range"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = RequireCharacter(C, TEXT("A"));
		ACrossguardCharacter* B = RequireCharacter(C, TEXT("B"));
		if (A == nullptr || B == nullptr)
		{
			return;
		}
		C.ClearLog();
		A->TrySummonPortal();
		B->TrySummonPortal();
		ACrossguardExtractPortal* PortalA = C.FindNearest<ACrossguardExtractPortal>(A->GetActorLocation(), 200.0f);
		ACrossguardExtractPortal* PortalB = C.FindNearest<ACrossguardExtractPortal>(B->GetActorLocation(), 200.0f);
		if (!C.Require(PortalA != nullptr && PortalB != nullptr && PortalA != PortalB, TEXT("X summons a portal at each player's feet")))
		{
			return;
		}
		C.Remember(TEXT("PortalA"), PortalA);
		C.Remember(TEXT("PortalB"), PortalB);
		C.Test.TestTrue(TEXT("Log: summoned an extract portal"), C.LogContains(TEXT("summoned an extract portal")));
		C.Test.TestTrue(TEXT("A's portal channels"), PortalA->GetState() == ECrossguardPortalState::Channeling);
		C.Test.TestTrue(TEXT("B's portal channels"), PortalB->GetState() == ECrossguardPortalState::Channeling);
		C.SetNumber(TEXT("SummonedAt"), C.Now());
		C.Test.AddInfo(FString::Printf(TEXT("Portal: channel %.0f s, open %.0f s, keep-alive %.0f cm"), PortalA->ChannelSeconds, PortalA->OpenSeconds,
			PortalA->KeepAliveRadius));

		// A keeps its portal alive from 3 m away (outside its 1.2 m enter sphere).
		const FVector AtA = PortalA->GetActorLocation();
		C.Place(A, FVector(AtA.X - 300.0f, AtA.Y, 0.0f), 0.0f);
		// B walks off: nobody of B's team stays within the keep-alive radius.
		const FVector AtB = PortalB->GetActorLocation();
		C.Place(B, FVector(AtB.X + PortalB->KeepAliveRadius + 500.0f, AtB.Y, 0.0f), 0.0f);
	});

	Scenario.WaitUntil(TEXT("B's portal collapses"), 5.0f, [](FScenarioContext& C)
	{
		const ACrossguardExtractPortal* PortalB = Cast<ACrossguardExtractPortal>(C.FindActor(TEXT("PortalB")));
		if (PortalB == nullptr)
		{
			C.Fail(TEXT("B's portal disappeared before it was seen collapsing"));
			return true;
		}
		return PortalB->GetState() == ECrossguardPortalState::Collapsed;
	});

	Scenario.Step(TEXT("A's portal still channels"), [](FScenarioContext& C)
	{
		const ACrossguardExtractPortal* PortalA = C.Get<ACrossguardExtractPortal>(TEXT("PortalA"));
		if (PortalA != nullptr)
		{
			C.Test.TestTrue(TEXT("A's portal still channels while A stays near"), PortalA->GetState() == ECrossguardPortalState::Channeling);
		}
	});

	Scenario.WaitUntil(TEXT("A's portal opens"), 75.0f, [](FScenarioContext& C)
	{
		const ACrossguardExtractPortal* PortalA = Cast<ACrossguardExtractPortal>(C.FindActor(TEXT("PortalA")));
		if (PortalA == nullptr)
		{
			C.Fail(TEXT("A's portal disappeared while channelling"));
			return true;
		}
		return PortalA->GetState() == ECrossguardPortalState::Open;
	});

	Scenario.Step(TEXT("Open after the channel time; A steps in"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = RequireCharacter(C, TEXT("A"));
		const ACrossguardExtractPortal* PortalA = C.Get<ACrossguardExtractPortal>(TEXT("PortalA"));
		if (A == nullptr || PortalA == nullptr)
		{
			return;
		}
		// The portal updates every 0.25 s (its TickInterval).
		C.Test.TestEqual(TEXT("It opens the channel time after it was summoned"), C.Now() - static_cast<float>(C.Number(TEXT("SummonedAt"))),
			PortalA->ChannelSeconds, 1.5f);
		const ACrossguardPlayerState* StateA = C.PlayerState(A);
		C.Test.TestTrue(TEXT("A, standing next to it, is still in the raid"), StateA != nullptr && StateA->IsInRaid());
		C.ClearLog();
		const FVector At = PortalA->GetActorLocation();
		C.Place(A, FVector(At.X, At.Y, 0.0f), 0.0f);
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Stepping in extracts A"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = RequireCharacter(C, TEXT("A"));
		const ACrossguardPlayerState* StateA = A != nullptr ? C.PlayerState(A) : nullptr;
		if (!C.Require(StateA != nullptr, TEXT("A's player state")))
		{
			return;
		}
		C.Test.TestFalse(TEXT("A extracted through the open portal"), StateA->IsInRaid());
		C.Test.TestTrue(TEXT("Log: extracted"), C.LogContains(TEXT("extracted")));
	});

	Scenario.Run();
	return true;
}

// ------------------------------------------------------------------------------------------------
// Checklist: "Press X outside a point of interest, or a second time -> Refused (Output Log says why)."
// Also: before PortalUnlockSeconds into the raid.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioRaidWorldPortalRefusals, "Crossguard.Scenario.Raid.PortalRefusals", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioRaidWorldPortalRefusals::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioRaidWorld;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A inside a point of interest, C 25 m outside it"), [](FScenarioContext& C)
	{
		ACrossguardPointOfInterest* Poi = C.SpawnActor<ACrossguardPointOfInterest>(TEXT("Poi"), FVector(0.0f, 1000.0f, 0.0f), 0.0f,
			[](ACrossguardPointOfInterest& Spawned) { Spawned.SetActorScale3D(FVector(0.5f)); });
		ACrossguardCharacter* A = C.SpawnPlayer(TEXT("A"), FVector(0.0f, 1000.0f, 0.0f), 0.0f);
		ACrossguardCharacter* Outsider = C.SpawnPlayer(TEXT("Outsider"), FVector(0.0f, 3500.0f, 0.0f), 0.0f);
		if (!C.Require(Poi != nullptr && A != nullptr && Outsider != nullptr && C.GameMode() != nullptr, TEXT("point of interest and players spawned")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("A is inside"), ACrossguardPointOfInterest::IsInsideAny(C.World(), A->GetActorLocation()));
		C.Test.TestFalse(TEXT("The outsider is outside"), ACrossguardPointOfInterest::IsInsideAny(C.World(), Outsider->GetActorLocation()));
	});

	Scenario.Wait(LandSeconds);

	Scenario.Step(TEXT("X too early in the raid (the game mode's PortalUnlockSeconds): refused"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = RequireCharacter(C, TEXT("A"));
		ACrossguardGameMode* Mode = C.GameMode();
		if (A == nullptr || Mode == nullptr)
		{
			return;
		}
		if (Mode->PortalUnlockSeconds > Mode->GetRaidElapsedSeconds() + 1.0f)
		{
			C.ClearLog();
			A->TrySummonPortal();
			C.Test.TestTrue(TEXT("Log: too early (error TooEarly)"), C.LogContains(FString::Printf(TEXT("%s cannot summon a portal: error %d"), *A->GetName(),
				static_cast<int32>(Crossguard::EPortalSummonError::TooEarly))));
			C.Test.TestEqual(TEXT("No portal"), C.FindAll<ACrossguardExtractPortal>().Num(), 0);
		}
		else
		{
			C.Test.AddInfo(FString::Printf(TEXT("PortalUnlockSeconds is %.0f: the too-early refusal is not tested"), Mode->PortalUnlockSeconds));
		}
		Mode->PortalUnlockSeconds = 0.0f;
	});

	Scenario.Step(TEXT("X outside a point of interest: refused"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Outsider = RequireCharacter(C, TEXT("Outsider"));
		if (Outsider == nullptr)
		{
			return;
		}
		C.ClearLog();
		Outsider->TrySummonPortal();
		C.Test.TestTrue(TEXT("Log: outside a point of interest (error OutsidePointOfInterest)"),
			C.LogContains(FString::Printf(TEXT("%s cannot summon a portal: error %d"), *Outsider->GetName(),
				static_cast<int32>(Crossguard::EPortalSummonError::OutsidePointOfInterest))));
		C.Test.TestEqual(TEXT("No portal"), C.FindAll<ACrossguardExtractPortal>().Num(), 0);
	});

	Scenario.Step(TEXT("X inside: a portal; X again: refused"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = RequireCharacter(C, TEXT("A"));
		if (A == nullptr)
		{
			return;
		}
		C.ClearLog();
		A->TrySummonPortal();
		C.Test.TestEqual(TEXT("One portal"), C.FindAll<ACrossguardExtractPortal>().Num(), 1);
		C.Test.TestTrue(TEXT("Log: summoned"), C.LogContains(TEXT("summoned an extract portal")));
		A->TrySummonPortal();
		C.Test.TestTrue(TEXT("Log: already summoned (error AlreadySummoned)"), C.LogContains(FString::Printf(TEXT("%s cannot summon a portal: error %d"),
			*A->GetName(), static_cast<int32>(Crossguard::EPortalSummonError::AlreadySummoned))));
		C.Test.TestEqual(TEXT("Still one portal"), C.FindAll<ACrossguardExtractPortal>().Num(), 1);
	});

	Scenario.Run();
	return true;
}

// ------------------------------------------------------------------------------------------------
// Checklist: "Place a Blueprint of CrossguardLootContainer with Loot Table Id chest_common, press F
// (bDebugLootTakesAll) -> It holds 1-3 rolled items (gold, potions, gear with affixes, maybe a Skill
// Gem) and they move to you." The native class is spawned (a Blueprint only adds a mesh). chest_common
// has a "nothing" weight, so a chest can roll fewer than RollsMin: three chests, the first non-empty
// one is looted.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioRaidWorldChestCommonLoot, "Crossguard.Scenario.Raid.ChestCommonLoot", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioRaidWorldChestCommonLoot::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioRaidWorld;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("Three chest_common chests roll their loot as the raid map's chests do"), [](FScenarioContext& C)
	{
		const FCrossguardLootTableRow* Row = LootRowOf(C, ChestCommon);
		if (Row == nullptr)
		{
			return;
		}
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(0.0f, 1000.0f, 0.0f), 0.0f);
		if (!C.Require(Player != nullptr, TEXT("player spawned")))
		{
			return;
		}
		Player->bDebugLootTakesAll = true;
		int32 FirstFull = INDEX_NONE;
		for (int32 Index = 0; Index < ChestCount; ++Index)
		{
			const FName Name(*FString::Printf(TEXT("Chest%d"), Index));
			// Rolled in BeginPlay (raid maps only), so the id is set before it.
			ACrossguardLootContainer* Chest = C.SpawnActor<ACrossguardLootContainer>(Name, FVector(150.0f, 1000.0f + Index * ChestSpacing, 50.0f), 180.0f,
				[](ACrossguardLootContainer& Spawned) { Spawned.LootTableId = ChestCommon; });
			if (!C.Require(Chest != nullptr, TEXT("chest spawned")))
			{
				return;
			}
			C.Test.AddInfo(FString::Printf(TEXT("%s holds: %s"), *Name.ToString(), *DescribeItems(Chest->GetItems())));
			FString Description;
			const bool bOneRoll = IsOneRollOf(*Row, Chest->GetItems(), Description);
			C.Test.TestTrue(*FString::Printf(TEXT("%s holds one roll of chest_common (%s)"), *Name.ToString(), *Description), bOneRoll);
			if (FirstFull == INDEX_NONE && Chest->GetItems().Num() > 0)
			{
				FirstFull = Index;
			}
		}
		if (!C.Require(FirstFull != INDEX_NONE, TEXT("at least one of three chest_common chests holds something")))
		{
			return;
		}
		C.SetNumber(TEXT("Chest"), FirstFull);
		C.Place(Player, FVector(0.0f, 1000.0f + FirstFull * ChestSpacing, 0.0f), 0.0f);
	});

	Scenario.Wait(LandSeconds);

	Scenario.Step(TEXT("The player faces the chest"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		const AActor* Chest = C.FindActor(FName(*FString::Printf(TEXT("Chest%d"), static_cast<int32>(C.Number(TEXT("Chest"))))));
		if (Player != nullptr && C.Require(Chest != nullptr, TEXT("the chest is there")))
		{
			FaceInteractable(C, Player, Chest);
		}
	});

	Scenario.Wait(ViewSettleSeconds);

	Scenario.Step(TEXT("F takes everything in the chest"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		ACrossguardLootContainer* Chest = Cast<ACrossguardLootContainer>(C.FindActor(FName(*FString::Printf(TEXT("Chest%d"),
			static_cast<int32>(C.Number(TEXT("Chest")))))));
		if (Player == nullptr || !C.Require(Chest != nullptr, TEXT("the chest is there")))
		{
			return;
		}
		FaceInteractable(C, Player, Chest);
		const int32 InChest = SumCounts(Chest->GetItems());
		const int32 InInventory = SumCounts(Player->GetInventoryComponent()->GetItems());
		for (const FCrossguardInventoryItem& Item : Chest->GetItems())
		{
			if (Item.Category == ECrossguardItemCategory::Weapon)
			{
				C.Test.AddInfo(FString::Printf(TEXT("Rolled weapon %s with %d affixes"), *Item.Weapon.WeaponId.ToString(), Item.Weapon.Affixes.Num()));
			}
			else if (Item.Category == ECrossguardItemCategory::Armor)
			{
				C.Test.AddInfo(FString::Printf(TEXT("Rolled armor %s with %d affixes"), *Item.Armor.ArmorId.ToString(), Item.Armor.Affixes.Num()));
			}
		}
		Player->TryInteract();
		C.Test.TestTrue(TEXT("The chest is open in the player's inventory"), Player->GetInventoryComponent()->GetOpenContainer() == Chest);
		C.Test.TestEqual(TEXT("The chest is empty"), Chest->GetItems().Num(), 0);
		C.Test.TestEqual(TEXT("Every item moved to the player"), SumCounts(Player->GetInventoryComponent()->GetItems()) - InInventory, InChest);
	});

	Scenario.Run();
	return true;
}

// ------------------------------------------------------------------------------------------------
// Checklist: "Set the dummy's Loot Table Id to boss_monster, kill it, loot the body -> 3-5 extra items
// on top of what it carried."
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioRaidWorldBossLootOnDummy, "Crossguard.Scenario.Raid.BossLootOnDummy", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioRaidWorldBossLootOnDummy::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioRaidWorld;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A dummy with a sword, 2 potions and Loot Table Id boss_monster"), [](FScenarioContext& C)
	{
		if (LootRowOf(C, BossMonster) == nullptr)
		{
			return;
		}
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(0.0f, 1000.0f, 0.0f), 0.0f);
		ACrossguardCharacter* Dummy = C.SpawnDummy(TEXT("Dummy"), FVector(150.0f, 1000.0f, 0.0f), 180.0f);
		if (!C.Require(Player != nullptr && Dummy != nullptr, TEXT("player and dummy spawned")) || !C.Equip(Dummy, Sword))
		{
			return;
		}
		// Potions: boss_monster never drops them, so they tell what it carried.
		if (!C.Require(C.GiveItem(Dummy, HealingPotion, 2) == 0, TEXT("2 potions fit in the dummy's inventory")))
		{
			return;
		}
		Dummy->LootTableId = BossMonster;
		Player->bDebugLootTakesAll = true;
	});

	Scenario.Wait(LandSeconds);

	Scenario.Step(TEXT("Killed: the body holds what it carried plus one boss_monster roll"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		ACrossguardCharacter* Dummy = RequireCharacter(C, TEXT("Dummy"));
		const FCrossguardLootTableRow* Row = LootRowOf(C, BossMonster);
		if (Player == nullptr || Dummy == nullptr || Row == nullptr)
		{
			return;
		}
		C.Kill(Dummy, Player);
		ACrossguardLootContainer* Body = C.FindNearest<ACrossguardLootContainer>(Dummy->GetActorLocation(), SearchRadius);
		if (!C.Require(C.IsDead(Dummy) && Body != nullptr, TEXT("the dummy is dead and left a body")))
		{
			return;
		}
		C.Remember(TEXT("Body"), Body);
		TArray<FCrossguardInventoryItem> Carried;
		TArray<FCrossguardInventoryItem> Extra;
		for (const FCrossguardInventoryItem& Item : Body->GetItems())
		{
			(Item.bWasEquipped || Item.ItemId == HealingPotion ? Carried : Extra).Add(Item);
		}
		C.Test.AddInfo(FString::Printf(TEXT("Carried: %s. Rolled: %s"), *DescribeItems(Carried), *DescribeItems(Extra)));
		C.Test.TestEqual(TEXT("What it carried is on the body (sword and potions)"), Carried.Num(), 2);
		FString Description;
		const bool bOneRoll = IsOneRollOf(*Row, Extra, Description);
		C.Test.TestTrue(*FString::Printf(TEXT("The extra items are one boss_monster roll (%s)"), *Description), bOneRoll);
		C.Test.TestTrue(*FString::Printf(TEXT("At least %d extra items"), Row->RollsMin), Extra.Num() >= (Row->NothingWeight > 0 ? 0 : Row->RollsMin));
		FaceInteractable(C, Player, Body);
	});

	Scenario.Wait(ViewSettleSeconds);

	Scenario.Step(TEXT("F at the body takes everything"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		ACrossguardLootContainer* Body = C.Get<ACrossguardLootContainer>(TEXT("Body"));
		if (Player == nullptr || Body == nullptr)
		{
			C.Fail(TEXT("the player or the body is gone"));
			return;
		}
		FaceInteractable(C, Player, Body);
		const int32 OnBody = SumCounts(Body->GetItems());
		Player->TryInteract();
		C.Test.TestEqual(TEXT("The body is empty"), Body->GetItems().Num(), 0);
		C.Test.TestEqual(TEXT("Everything moved to the player"), SumCounts(Player->GetInventoryComponent()->GetItems()), OnBody);
	});

	Scenario.Run();
	return true;
}

// ------------------------------------------------------------------------------------------------
// Checklist: "Place an ACrossguardDoor (give it a mesh), press F -> It snaps open and closed." No mesh
// here: the interaction does not need one.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioRaidWorldDoorOpenClose, "Crossguard.Scenario.Raid.DoorOpenClose", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioRaidWorldDoorOpenClose::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioRaidWorld;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A player faces a door 1.5 m away"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(0.0f, 1000.0f, 0.0f), 0.0f);
		ACrossguardDoor* Door = C.SpawnActor<ACrossguardDoor>(TEXT("Door"), FVector(150.0f, 1000.0f, 100.0f));
		if (!C.Require(Player != nullptr && Door != nullptr, TEXT("player and door spawned")))
		{
			return;
		}
		C.Test.TestFalse(TEXT("The door starts closed"), Door->IsOpen());
		FaceInteractable(C, Player, Door);
	});

	Scenario.Wait(LandSeconds);

	Scenario.Step(TEXT("F opens it at once"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		ACrossguardDoor* Door = C.Get<ACrossguardDoor>(TEXT("Door"));
		if (Player == nullptr || Door == nullptr)
		{
			C.Fail(TEXT("the player or the door is gone"));
			return;
		}
		FaceInteractable(C, Player, Door);
		Player->TryInteract();
		C.Test.TestTrue(TEXT("F snaps the door open"), Door->IsOpen());
	});

	Scenario.Wait(0.2f);

	Scenario.Step(TEXT("F again closes it"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		ACrossguardDoor* Door = C.Get<ACrossguardDoor>(TEXT("Door"));
		if (Player == nullptr || Door == nullptr)
		{
			C.Fail(TEXT("the player or the door is gone"));
			return;
		}
		C.Test.TestTrue(TEXT("The door stays open"), Door->IsOpen());
		FaceInteractable(C, Player, Door);
		Player->TryInteract();
		C.Test.TestFalse(TEXT("F snaps the door closed"), Door->IsOpen());
	});

	Scenario.Run();
	return true;
}

// ------------------------------------------------------------------------------------------------
// Checklist: "Tick the door's Lock -> bLocked, press F -> Nothing without a lockpick; with the
// Ability.Lockpicking tag it opens after 5 s." Also with a lockpick item (used up).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioRaidWorldLockedDoor, "Crossguard.Scenario.Raid.LockedDoor", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioRaidWorldLockedDoor::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioRaidWorld;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A player faces a locked door; a second locked door 10 m to the side"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(0.0f, 1000.0f, 0.0f), 0.0f);
		ACrossguardDoor* Door = C.SpawnActor<ACrossguardDoor>(TEXT("Door"), FVector(150.0f, 1000.0f, 100.0f));
		ACrossguardDoor* Door2 = C.SpawnActor<ACrossguardDoor>(TEXT("Door2"), FVector(150.0f, 2000.0f, 100.0f));
		if (!C.Require(Player != nullptr && Door != nullptr && Door2 != nullptr, TEXT("player and doors spawned")))
		{
			return;
		}
		for (ACrossguardDoor* Each : {Door, Door2})
		{
			UCrossguardLockComponent* Lock = Each->FindComponentByClass<UCrossguardLockComponent>();
			if (!C.Require(Lock != nullptr, TEXT("the door has a lock component")))
			{
				return;
			}
			Lock->SetLocked(true);
		}
		FaceInteractable(C, Player, Door);
	});

	Scenario.Wait(LandSeconds);

	Scenario.Step(TEXT("F without a lockpick: nothing"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		ACrossguardDoor* Door = C.Get<ACrossguardDoor>(TEXT("Door"));
		if (Player == nullptr || Door == nullptr)
		{
			C.Fail(TEXT("the player or the door is gone"));
			return;
		}
		FaceInteractable(C, Player, Door);
		C.ClearLog();
		Player->TryInteract();
		C.Test.TestFalse(TEXT("The locked door does not open"), Door->IsOpen());
		C.Test.TestFalse(TEXT("No lockpicking without a lockpick"), Player->IsLockpicking());
		C.Test.TestTrue(TEXT("Log: no lockpick"), C.LogContains(TEXT("no lockpick")));
	});

	Scenario.Step(TEXT("With Ability.Lockpicking, F starts picking the lock"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		ACrossguardDoor* Door = C.Get<ACrossguardDoor>(TEXT("Door"));
		if (Player == nullptr || Door == nullptr)
		{
			C.Fail(TEXT("the player or the door is gone"));
			return;
		}
		Player->GetAbilitySystemComponent()->AddLooseGameplayTag(CrossguardTags::Ability_Lockpicking);
		Player->TryInteract();
		if (!C.Require(Player->IsLockpicking(), TEXT("F starts lockpicking with the Ability.Lockpicking tag")))
		{
			return;
		}
		const float Dexterity = C.Stat(Player, UCrossguardAttributeSet::GetDexterityAttribute());
		const float Seconds = static_cast<float>(Crossguard::LockpickSeconds(Crossguard::FEconomyConfig(), Dexterity));
		C.Test.AddInfo(FString::Printf(TEXT("Lockpicking at Dexterity %.2f: %.2f s"), Dexterity, Seconds));
		C.SetNumber(TEXT("PickStart"), C.Now());
		C.SetNumber(TEXT("PickSeconds"), Seconds);
	});

	Scenario.WaitUntil(TEXT("half a second before the lock should open"), 15.0f, [](FScenarioContext& C)
	{
		return C.Now() >= C.Number(TEXT("PickStart")) + C.Number(TEXT("PickSeconds")) - 0.5;
	});

	Scenario.Step(TEXT("Still picking, still locked"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		ACrossguardDoor* Door = C.Get<ACrossguardDoor>(TEXT("Door"));
		if (Player == nullptr || Door == nullptr)
		{
			C.Fail(TEXT("the player or the door is gone"));
			return;
		}
		C.Test.TestTrue(TEXT("Still picking"), Player->IsLockpicking());
		C.Test.TestFalse(TEXT("Still closed"), Door->IsOpen());
		C.Test.TestTrue(TEXT("Still locked"), Door->FindComponentByClass<UCrossguardLockComponent>()->IsLocked());
	});

	Scenario.WaitUntil(TEXT("the door opens"), 2.0f, [](FScenarioContext& C)
	{
		const ACrossguardDoor* Door = Cast<ACrossguardDoor>(C.FindActor(TEXT("Door")));
		if (Door == nullptr || Door->IsOpen())
		{
			C.SetNumber(TEXT("OpenedAt"), C.Now());
			return true;
		}
		return false;
	});

	Scenario.Step(TEXT("Picked after the lockpicking time: unlocked and open; then to the second door"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		ACrossguardDoor* Door = C.Get<ACrossguardDoor>(TEXT("Door"));
		ACrossguardDoor* Door2 = C.Get<ACrossguardDoor>(TEXT("Door2"));
		if (Player == nullptr || Door == nullptr || Door2 == nullptr)
		{
			C.Fail(TEXT("the player or a door is gone"));
			return;
		}
		C.Test.TestEqual(TEXT("Picking took the lockpicking time"), static_cast<float>(C.Number(TEXT("OpenedAt")) - C.Number(TEXT("PickStart"))),
			static_cast<float>(C.Number(TEXT("PickSeconds"))), 0.2f);
		C.Test.TestFalse(TEXT("The lock is open"), Door->FindComponentByClass<UCrossguardLockComponent>()->IsLocked());
		C.Test.TestFalse(TEXT("No longer picking"), Player->IsLockpicking());

		// Now without the skill, with one lockpick item.
		Player->GetAbilitySystemComponent()->RemoveLooseGameplayTag(CrossguardTags::Ability_Lockpicking);
		if (!C.Require(C.GiveItem(Player, Lockpick, 1) == 0, TEXT("a lockpick fits")))
		{
			return;
		}
		C.Place(Player, FVector(0.0f, 2000.0f, 0.0f), 0.0f);
		FaceInteractable(C, Player, Door2);
	});

	Scenario.Wait(LandSeconds);

	Scenario.Step(TEXT("F at the second door with a lockpick item: picking"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		ACrossguardDoor* Door2 = C.Get<ACrossguardDoor>(TEXT("Door2"));
		if (Player == nullptr || Door2 == nullptr)
		{
			C.Fail(TEXT("the player or the door is gone"));
			return;
		}
		FaceInteractable(C, Player, Door2);
		Player->TryInteract();
		C.Test.TestTrue(TEXT("A lockpick item lets the player pick"), Player->IsLockpicking());
		C.Test.TestEqual(TEXT("The lockpick is used up only when the lock opens"), Player->GetInventoryComponent()->CountItem(Lockpick), 1);
	});

	Scenario.WaitUntil(TEXT("the second door opens"), 15.0f, [](FScenarioContext& C)
	{
		const ACrossguardDoor* Door2 = Cast<ACrossguardDoor>(C.FindActor(TEXT("Door2")));
		return Door2 == nullptr || Door2->IsOpen();
	});

	Scenario.Step(TEXT("Opened; the lockpick is used up"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		ACrossguardDoor* Door2 = C.Get<ACrossguardDoor>(TEXT("Door2"));
		if (Player == nullptr || Door2 == nullptr)
		{
			return;
		}
		C.Test.TestTrue(TEXT("The second door is open"), Door2->IsOpen());
		C.Test.TestEqual(TEXT("One lockpick used up"), Player->GetInventoryComponent()->CountItem(Lockpick), 0);
	});

	Scenario.Run();
	return true;
}

// ------------------------------------------------------------------------------------------------
// Checklist: "Press 3 with a potion readied, then G; walk to the box and press F -> The stack drops as
// a small box and comes back on pickup."
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioRaidWorldDropAndPickUp, "Crossguard.Scenario.Raid.DropAndPickUp", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioRaidWorldDropAndPickUp::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioRaidWorld;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A player readies a stack of 3 potions with key 3"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(0.0f, 1000.0f, 0.0f), 0.0f);
		if (!C.Require(Player != nullptr, TEXT("player spawned")) || !C.Require(C.GiveItem(Player, HealingPotion, DropPotionCount) == 0, TEXT("3 potions fit")))
		{
			return;
		}
		UCrossguardInventoryComponent* Inventory = Player->GetInventoryComponent();
		Inventory->RequestAssignHotbarSlot(0, StackIdOf(Player, HealingPotion));
		Inventory->RequestUseHotbarSlot(0);
		C.Test.TestTrue(TEXT("Key 3 readies the potions"), Inventory->GetReadiedItemId() != 0);
	});

	Scenario.Wait(LandSeconds);

	Scenario.Step(TEXT("G drops the stack in hand as a box about 1.5 m ahead"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		if (Player == nullptr)
		{
			return;
		}
		UCrossguardInventoryComponent* Inventory = Player->GetInventoryComponent();
		// What G does (ACrossguardCharacter::OnDrop).
		Inventory->RequestDropItem(Inventory->GetReadiedItemId());
		C.Test.TestEqual(TEXT("The potions left the inventory"), Inventory->CountItem(HealingPotion), 0);
		C.Test.TestEqual(TEXT("The hands are empty"), Inventory->GetReadiedItemId(), 0);
		ACrossguardWorldItem* Dropped = C.FindNearest<ACrossguardWorldItem>(Player->GetActorLocation(), SearchRadius);
		if (!C.Require(Dropped != nullptr, TEXT("a dropped item (ACrossguardWorldItem) lies near the player")))
		{
			return;
		}
		C.Remember(TEXT("Dropped"), Dropped);
		C.Test.TestTrue(TEXT("It is the potion stack"), Dropped->GetItem().ItemId == HealingPotion);
		C.Test.TestEqual(TEXT("All 3 potions"), Dropped->GetItem().Count, DropPotionCount);
		C.Test.TestEqual(TEXT("About 1.5 m ahead"), static_cast<float>(FVector::Dist2D(Dropped->GetActorLocation(), Player->GetActorLocation())), 150.0f, 35.0f);
		C.Test.TestTrue(TEXT("On the floor"), Dropped->GetActorLocation().Z < 50.0);
	});

	Scenario.Step(TEXT("The player walks up to the box and looks at it"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		const AActor* Dropped = C.FindActor(TEXT("Dropped"));
		if (Player == nullptr || !C.Require(Dropped != nullptr, TEXT("the dropped box is there")))
		{
			return;
		}
		const FVector At = Dropped->GetActorLocation();
		C.Place(Player, FVector(At.X - 80.0f, At.Y, 0.0f), 0.0f);
		FaceInteractable(C, Player, Dropped);
	});

	Scenario.Wait(LandSeconds);

	Scenario.Step(TEXT("F picks the stack back up"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = RequireCharacter(C, TEXT("Player"));
		const AActor* Dropped = C.FindActor(TEXT("Dropped"));
		if (Player == nullptr || !C.Require(Dropped != nullptr, TEXT("the dropped box is there")))
		{
			return;
		}
		FaceInteractable(C, Player, Dropped);
		Player->TryInteract();
		C.Test.TestEqual(TEXT("All 3 potions are back"), Player->GetInventoryComponent()->CountItem(HealingPotion), DropPotionCount);
		C.Test.TestNull(TEXT("The box is gone"), C.FindActor(TEXT("Dropped")));
	});

	Scenario.Run();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
