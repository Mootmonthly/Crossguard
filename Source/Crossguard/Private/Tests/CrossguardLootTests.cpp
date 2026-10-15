// UCrossguardLootLibrary::RollLoot with small in-memory data tables built here: determinism, stack
// splitting, and the "any armor of one type" filter. No world, no assets.

#include "Tests/CrossguardTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Items/CrossguardItemTypes.h"
#include "Items/CrossguardLoot.h"

namespace CrossguardLootTestData
{
	const FName MixedTable(TEXT("test_mixed"));
	const FName PlateTable(TEXT("test_plate_only"));
	const FName GoldId(TEXT("gold_coin")); // UCrossguardLootLibrary drops gold as this item.

	// The tables RollLoot reads, kept alive for the test.
	struct FLootFixture
	{
		TStrongObjectPtr<UDataTable> Loot = CrossguardTests::MakeTable<FCrossguardLootTableRow>();
		TStrongObjectPtr<UDataTable> Weapons = CrossguardTests::MakeTable<FCrossguardWeaponRow>();
		TStrongObjectPtr<UDataTable> Armor = CrossguardTests::MakeTable<FCrossguardArmorRow>();
		TStrongObjectPtr<UDataTable> Affixes = CrossguardTests::MakeTable<FCrossguardAffixRow>();
		TStrongObjectPtr<UDataTable> Items = CrossguardTests::MakeTable<FCrossguardItemRow>();

		FLootFixture()
		{
			{
				FCrossguardWeaponRow Row;
				Row.WeaponType = ECrossguardWeaponType::Sword;
				Row.Stat = ECrossguardWeaponStat::AttackDamage;
				Row.BaseValue = 10.0f;
				Row.PerRarity = 2.0f;
				Row.GridWidth = 1;
				Row.GridHeight = 3;
				Weapons->AddRow(TEXT("test_sword"), Row);
				Row.WeaponType = ECrossguardWeaponType::Wand;
				Row.Stat = ECrossguardWeaponStat::MagicDamage;
				Row.Range = ECrossguardWeaponRange::Ranged;
				Row.GridHeight = 2;
				Weapons->AddRow(TEXT("test_wand"), Row);
			}
			const auto AddArmor = [this](const TCHAR* Id, ECrossguardArmorType Type, ECrossguardArmorSlot Slot, FName Pool)
			{
				FCrossguardArmorRow Row;
				Row.ArmorType = Type;
				Row.Slot = Slot;
				Row.GridWidth = 2;
				Row.GridHeight = 2;
				Row.AffixPool = Pool;
				Armor->AddRow(Id, Row);
			};
			AddArmor(TEXT("test_cloth_hood"), ECrossguardArmorType::Cloth, ECrossguardArmorSlot::Head, TEXT("ClothArmor"));
			AddArmor(TEXT("test_leather_cap"), ECrossguardArmorType::Leather, ECrossguardArmorSlot::Head, TEXT("LeatherArmor"));
			AddArmor(TEXT("test_plate_helm"), ECrossguardArmorType::Plate, ECrossguardArmorSlot::Head, TEXT("PlateArmor"));
			AddArmor(TEXT("test_leather_boots"), ECrossguardArmorType::Leather, ECrossguardArmorSlot::Feet, TEXT("LeatherArmor"));
			AddArmor(TEXT("test_plate_chest"), ECrossguardArmorType::Plate, ECrossguardArmorSlot::Chest, TEXT("PlateArmor"));

			const auto AddAffix = [this](const TCHAR* Id, FName Stat, TArray<FName> Pools)
			{
				FCrossguardAffixRow Row;
				Row.Stat = Stat;
				Row.Min = 1.0f;
				Row.Max = 5.0f;
				Row.Step = 1.0f;
				Row.Pools = MoveTemp(Pools);
				Affixes->AddRow(Id, Row);
			};
			AddAffix(TEXT("test_flat_attack"), TEXT("AdditionalAttackDamage"), {TEXT("AttackWeapon")});
			AddAffix(TEXT("test_true_damage"), TEXT("TrueDamage"), {TEXT("AttackWeapon"), TEXT("MagicWeapon")});
			AddAffix(TEXT("test_flat_magic"), TEXT("AdditionalMagicDamage"), {TEXT("MagicWeapon")});
			AddAffix(TEXT("test_armor_strength"), TEXT("Strength"), {TEXT("PlateArmor"), TEXT("LeatherArmor"), TEXT("ClothArmor")});
			AddAffix(TEXT("test_armor_agility"), TEXT("Agility"), {TEXT("PlateArmor"), TEXT("LeatherArmor")});

			{
				FCrossguardItemRow Row;
				Row.Category = ECrossguardItemCategory::Currency;
				Row.MaxStack = 100;
				Items->AddRow(GoldId, Row);
				Row.Category = ECrossguardItemCategory::Consumable;
				Row.MaxStack = 5;
				Row.GridHeight = 2;
				Items->AddRow(TEXT("test_potion"), Row);
			}

			{
				FCrossguardLootTableRow Row;
				Row.RollsMin = 3;
				Row.RollsMax = 6;
				Row.NothingWeight = 0;
				Row.RarityWeights = {40, 30, 15, 10, 4, 1};
				const auto AddEntry = [&Row](ECrossguardLootKind Kind, FName Id, int32 Weight, int32 CountMin, int32 CountMax)
				{
					FCrossguardLootEntry Entry;
					Entry.Kind = Kind;
					Entry.Id = Id;
					Entry.Weight = Weight;
					Entry.CountMin = CountMin;
					Entry.CountMax = CountMax;
					Row.Entries.Add(Entry);
				};
				AddEntry(ECrossguardLootKind::Weapon, NAME_None, 3, 1, 1);
				AddEntry(ECrossguardLootKind::Armor, NAME_None, 3, 1, 1);
				AddEntry(ECrossguardLootKind::Item, TEXT("test_potion"), 2, 1, 7);
				// Up to 250 coins: more than one stack of 100.
				AddEntry(ECrossguardLootKind::Gold, NAME_None, 2, 50, 250);
				AddEntry(ECrossguardLootKind::SkillGem, TEXT("fireball"), 1, 1, 1);
				Loot->AddRow(MixedTable, Row);
			}
			{
				FCrossguardLootTableRow Row;
				Row.RollsMin = 4;
				Row.RollsMax = 4;
				Row.NothingWeight = 0;
				Row.RarityWeights = {1, 1, 1, 1, 1, 1};
				FCrossguardLootEntry Entry;
				Entry.Kind = ECrossguardLootKind::Armor;
				Entry.Id = NAME_None;
				Entry.bHasArmorType = true;
				Entry.ArmorType = ECrossguardArmorType::Plate;
				Row.Entries.Add(Entry);
				Loot->AddRow(PlateTable, Row);
			}
		}

		FCrossguardLootSources Sources() const
		{
			FCrossguardLootSources Result;
			Result.LootTables = Loot.Get();
			Result.WeaponTable = Weapons.Get();
			Result.ArmorTable = Armor.Get();
			Result.AffixTable = Affixes.Get();
			Result.ItemTable = Items.Get();
			// No skill catalog: named Skill Gems still drop (as Common).
			return Result;
		}
	};

	FString DescribeAffixes(const TArray<FCrossguardAffix>& Affixes)
	{
		FString Out;
		for (const FCrossguardAffix& Affix : Affixes)
		{
			Out += FString::Printf(TEXT("%s=%g;"), *Affix.AffixId.ToString(), Affix.Value);
		}
		return Out;
	}

	// Everything a roll decides about one item, for comparing two rolls.
	FString Describe(const FCrossguardInventoryItem& Item)
	{
		return FString::Printf(TEXT("[cat %d item %s weapon %s (r%d %s) armor %s (r%d %s) rarity %d x%d size %dx%d max %d]"),
			static_cast<int32>(Item.Category), *Item.ItemId.ToString(),
			*Item.Weapon.WeaponId.ToString(), static_cast<int32>(Item.Weapon.Rarity), *DescribeAffixes(Item.Weapon.Affixes),
			*Item.Armor.ArmorId.ToString(), static_cast<int32>(Item.Armor.Rarity), *DescribeAffixes(Item.Armor.Affixes),
			static_cast<int32>(Item.Rarity), Item.Count, Item.Size.X, Item.Size.Y, Item.MaxStack);
	}

	FString Describe(const TArray<FCrossguardInventoryItem>& Items)
	{
		FString Out;
		for (const FCrossguardInventoryItem& Item : Items)
		{
			Out += Describe(Item);
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardLootDeterminismTest, "Crossguard.Loot.RollLootDeterminism", CROSSGUARD_TEST_FLAGS)

bool FCrossguardLootDeterminismTest::RunTest(const FString& Parameters)
{
	using namespace CrossguardLootTestData;
	const FLootFixture Fixture;
	const FCrossguardLootSources Sources = Fixture.Sources();

	TSet<FString> DistinctRolls;
	const int64 Seeds[] = {0, 1, 2, 3, 7, 42, 1000, 65535, 123456789, 9876543210LL, -1, -424242};
	for (const int64 Seed : Seeds)
	{
		const TArray<FCrossguardInventoryItem> First = UCrossguardLootLibrary::RollLoot(Sources, MixedTable, Seed);
		const TArray<FCrossguardInventoryItem> Second = UCrossguardLootLibrary::RollLoot(Sources, MixedTable, Seed);
		const FString FirstText = Describe(First);
		TestEqual(*FString::Printf(TEXT("Seed %lld: the same seed rolls the same loot"), Seed), Describe(Second), FirstText);
		DistinctRolls.Add(FirstText);

		// NothingWeight 0 and 3-6 rolls: at least 3 items (gold can split into more stacks).
		TestTrue(*FString::Printf(TEXT("Seed %lld: at least 3 items (got %d)"), Seed, First.Num()), First.Num() >= 3);
		for (const FCrossguardInventoryItem& Item : First)
		{
			const FString What = FString::Printf(TEXT("Seed %lld: %s"), Seed, *Describe(Item));
			TestTrue(*FString::Printf(TEXT("%s has a size"), *What), Item.Size.X > 0 && Item.Size.Y > 0);
			TestTrue(*FString::Printf(TEXT("%s count is within 1..MaxStack"), *What), Item.Count >= 1 && Item.Count <= Item.MaxStack);
			switch (Item.Category)
			{
			case ECrossguardItemCategory::Weapon:
				TestTrue(*FString::Printf(TEXT("%s is a weapon from the table"), *What), Fixture.Weapons->FindRow<FCrossguardWeaponRow>(Item.Weapon.WeaponId, TEXT("Test"), false) != nullptr);
				break;
			case ECrossguardItemCategory::Armor:
				TestTrue(*FString::Printf(TEXT("%s is armor from the table"), *What), Fixture.Armor->FindRow<FCrossguardArmorRow>(Item.Armor.ArmorId, TEXT("Test"), false) != nullptr);
				break;
			case ECrossguardItemCategory::Currency:
				TestTrue(*FString::Printf(TEXT("%s is gold coins"), *What), Item.ItemId == GoldId);
				TestTrue(*FString::Printf(TEXT("%s gold stacks hold at most 100"), *What), Item.Count <= 100);
				break;
			case ECrossguardItemCategory::Consumable:
				TestTrue(*FString::Printf(TEXT("%s is the potion"), *What), Item.ItemId == FName(TEXT("test_potion")));
				break;
			case ECrossguardItemCategory::SkillGem:
				TestTrue(*FString::Printf(TEXT("%s is the fireball gem"), *What), Item.ItemId == FName(TEXT("fireball")));
				break;
			default:
				AddError(FString::Printf(TEXT("%s: unexpected category"), *What));
				break;
			}
		}
	}
	TestTrue(*FString::Printf(TEXT("Different seeds roll different loot (%d distinct of %d)"), DistinctRolls.Num(), static_cast<int32>(UE_ARRAY_COUNT(Seeds))),
		DistinctRolls.Num() > 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardLootArmorTypeTest, "Crossguard.Loot.AnyArmorOfOneType", CROSSGUARD_TEST_FLAGS)

bool FCrossguardLootArmorTypeTest::RunTest(const FString& Parameters)
{
	using namespace CrossguardLootTestData;
	const FLootFixture Fixture;
	const FCrossguardLootSources Sources = Fixture.Sources();

	int32 Drops = 0;
	TSet<FName> PiecesSeen;
	for (int64 Seed = 0; Seed < 64; ++Seed)
	{
		const TArray<FCrossguardInventoryItem> Loot = UCrossguardLootLibrary::RollLoot(Sources, PlateTable, Seed * 7919 + 13);
		TestEqual(*FString::Printf(TEXT("Seed %lld: 4 rolls, no nothing weight: 4 pieces"), Seed), Loot.Num(), 4);
		for (const FCrossguardInventoryItem& Item : Loot)
		{
			++Drops;
			if (!TestTrue(*FString::Printf(TEXT("Seed %lld: %s is armor"), Seed, *Describe(Item)), Item.Category == ECrossguardItemCategory::Armor))
			{
				continue;
			}
			const FCrossguardArmorRow* Row = Fixture.Armor->FindRow<FCrossguardArmorRow>(Item.Armor.ArmorId, TEXT("Test"), false);
			if (TestNotNull(*FString::Printf(TEXT("Seed %lld: '%s' is in the armor table"), Seed, *Item.Armor.ArmorId.ToString()), Row))
			{
				TestTrue(*FString::Printf(TEXT("Seed %lld: '%s' is Plate (the entry's ArmorType)"), Seed, *Item.Armor.ArmorId.ToString()),
					Row->ArmorType == ECrossguardArmorType::Plate);
			}
			PiecesSeen.Add(Item.Armor.ArmorId);
		}
	}
	TestTrue(TEXT("Plate-only table dropped something"), Drops > 0);
	if (PiecesSeen.Num() < 2)
	{
		AddWarning(FString::Printf(TEXT("Only %d of the 2 plate pieces ever dropped in 64 rolls; the pick may not be random"), PiecesSeen.Num()));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
