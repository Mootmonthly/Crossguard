// Data checks.
//
// Crossguard.Data.RepoTables: imports the generated Data/**/…Table.json files into transient data
// tables with the real row structs (what the editor's import does), so it needs no editor assets:
// import problems, cross references (loot ids, mob skills and loot tables), native skills against
// their data rows, and a loot roll of every table.
//
// Crossguard.Data.EditorAssets: the imported assets the docs ask for (Content/Data/DT_*, the skill
// catalog). Missing assets are skipped with a warning; present ones must use the right row struct
// and have the same rows as the JSON in the repository (else: reimport).

#include "Tests/CrossguardTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AbilitySystem/CrossguardGameplayAbility.h"
#include "Character/CrossguardMobRow.h"
#include "Items/CrossguardItemTypes.h"
#include "Items/CrossguardLoot.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Skills/CrossguardNativeSkills.h"
#include "Skills/CrossguardSkillCatalog.h"
#include "Skills/CrossguardSkillTypes.h"
#include "UObject/UObjectGlobals.h"

namespace CrossguardDataTestLocal
{
	struct FTableInfo
	{
		// Relative to the project folder.
		const TCHAR* JsonPath;
		// Package name under /Game, as docs/next-build-checklist.md names it.
		const TCHAR* AssetPackage;
		UScriptStruct* (*RowStruct)();
	};

	// Indices into Tables().
	enum ETable : int32
	{
		Skills,
		Weapons,
		Affixes,
		Armor,
		Items,
		Loot,
		Mobs,
		Count
	};

	const FTableInfo* Tables()
	{
		static const FTableInfo Infos[ETable::Count] = {
			{TEXT("Data/Skills/SkillTable.json"), TEXT("/Game/Data/DT_Skills"), &FCrossguardSkillRow::StaticStruct},
			{TEXT("Data/Items/WeaponTable.json"), TEXT("/Game/Data/DT_Weapons"), &FCrossguardWeaponRow::StaticStruct},
			{TEXT("Data/Items/AffixTable.json"), TEXT("/Game/Data/DT_Affixes"), &FCrossguardAffixRow::StaticStruct},
			{TEXT("Data/Items/ArmorTable.json"), TEXT("/Game/Data/DT_Armor"), &FCrossguardArmorRow::StaticStruct},
			{TEXT("Data/Items/ItemTable.json"), TEXT("/Game/Data/DT_Items"), &FCrossguardItemRow::StaticStruct},
			{TEXT("Data/Loot/LootTable.json"), TEXT("/Game/Data/DT_Loot"), &FCrossguardLootTableRow::StaticStruct},
			{TEXT("Data/Mobs/MobTable.json"), TEXT("/Game/Data/DT_Mobs"), &FCrossguardMobRow::StaticStruct},
		};
		return Infos;
	}

	// Where the docs leave the catalog's folder open, the places it is looked for.
	const TCHAR* const CatalogPackages[] = {
		TEXT("/Game/Data/DA_SkillCatalog"),
		TEXT("/Game/DA_SkillCatalog"),
		TEXT("/Game/Data/Skills/DA_SkillCatalog"),
		TEXT("/Game/Skills/DA_SkillCatalog"),
	};

	// The table JsonPath imports into, or null (with errors added to Test).
	TStrongObjectPtr<UDataTable> ImportRepoTable(FAutomationTestBase& Test, const FTableInfo& Info)
	{
		const FString Path = FPaths::Combine(FPaths::ProjectDir(), Info.JsonPath);
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *Path))
		{
			Test.AddError(FString::Printf(TEXT("Cannot read %s (%s)"), Info.JsonPath, *FPaths::ConvertRelativePathToFull(Path)));
			return TStrongObjectPtr<UDataTable>();
		}
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
		Table->RowStruct = Info.RowStruct();
		const TArray<FString> Problems = Table->CreateTableFromJSONString(Json);
		for (const FString& Problem : Problems)
		{
			Test.AddError(FString::Printf(TEXT("%s import: %s"), Info.JsonPath, *Problem));
		}
		if (Table->GetRowNames().Num() == 0)
		{
			Test.AddError(FString::Printf(TEXT("%s imported no rows"), Info.JsonPath));
		}
		return TStrongObjectPtr<UDataTable>(Table);
	}

	bool HasRow(const UDataTable* Table, FName Row)
	{
		return Table != nullptr && !Row.IsNone() && Table->GetRowMap().Contains(Row);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardRepoTablesTest, "Crossguard.Data.RepoTables", CROSSGUARD_TEST_FLAGS)

bool FCrossguardRepoTablesTest::RunTest(const FString& Parameters)
{
	using namespace CrossguardDataTestLocal;

	TStrongObjectPtr<UDataTable> Imported[ETable::Count];
	for (int32 Index = 0; Index < ETable::Count; ++Index)
	{
		Imported[Index] = ImportRepoTable(*this, Tables()[Index]);
		if (!Imported[Index].IsValid())
		{
			return false;
		}
	}
	const UDataTable* SkillTable = Imported[ETable::Skills].Get();
	const UDataTable* WeaponTable = Imported[ETable::Weapons].Get();
	const UDataTable* ArmorTable = Imported[ETable::Armor].Get();
	const UDataTable* ItemTable = Imported[ETable::Items].Get();
	const UDataTable* LootTable = Imported[ETable::Loot].Get();
	const UDataTable* MobTable = Imported[ETable::Mobs].Get();

	// A catalog over the imported skill table, as DA_SkillCatalog is set up (no AbilityClasses).
	TStrongObjectPtr<UCrossguardSkillCatalog> Catalog(NewObject<UCrossguardSkillCatalog>(GetTransientPackage(), NAME_None, RF_Transient));
	Catalog->SkillTable = Imported[ETable::Skills].Get();

	// Native skills match their data rows (what the catalog's data validation checks for AbilityClasses).
	for (const TPair<FName, TSubclassOf<UCrossguardGameplayAbility>>& Pair : CrossguardNativeSkills::All())
	{
		const FCrossguardSkillRow* Row = Catalog->FindSkill(Pair.Key);
		const UCrossguardGameplayAbility* Ability = Pair.Value.Get() != nullptr ? Pair.Value->GetDefaultObject<UCrossguardGameplayAbility>() : nullptr;
		if (!TestNotNull(*FString::Printf(TEXT("Native skill '%s' has a row in SkillTable.json"), *Pair.Key.ToString()), Row) || Ability == nullptr)
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%s: SkillCategory %d matches the '%s' row's Category %d"), *Pair.Value->GetName(),
			static_cast<int32>(Ability->SkillCategory), *Pair.Key.ToString(), static_cast<int32>(Row->Category)), Ability->SkillCategory == Row->Category);
		TestTrue(*FString::Printf(TEXT("%s: BaseCooldownSeconds %g matches the '%s' row's CooldownSec %g"), *Pair.Value->GetName(),
			Ability->BaseCooldownSeconds, *Pair.Key.ToString(), Row->CooldownSec), FMath::IsNearlyEqual(Ability->BaseCooldownSeconds, Row->CooldownSec));
	}

	// Loot entries point at rows that exist.
	LootTable->ForeachRow<FCrossguardLootTableRow>(TEXT("CrossguardTests"), [&](const FName& TableId, const FCrossguardLootTableRow& Row)
	{
		TestTrue(*FString::Printf(TEXT("Loot table '%s': RollsMin <= RollsMax"), *TableId.ToString()), Row.RollsMin <= Row.RollsMax);
		for (int32 EntryIndex = 0; EntryIndex < Row.Entries.Num(); ++EntryIndex)
		{
			const FCrossguardLootEntry& Entry = Row.Entries[EntryIndex];
			const FString What = FString::Printf(TEXT("Loot table '%s' entry %d ('%s')"), *TableId.ToString(), EntryIndex, *Entry.Id.ToString());
			TestTrue(*FString::Printf(TEXT("%s: CountMin <= CountMax"), *What), Entry.CountMin <= Entry.CountMax);
			switch (Entry.Kind)
			{
			case ECrossguardLootKind::Weapon:
				TestTrue(*FString::Printf(TEXT("%s is in WeaponTable.json"), *What), Entry.Id.IsNone() || HasRow(WeaponTable, Entry.Id));
				break;
			case ECrossguardLootKind::Armor:
				TestTrue(*FString::Printf(TEXT("%s is in ArmorTable.json"), *What), Entry.Id.IsNone() || HasRow(ArmorTable, Entry.Id));
				if (Entry.Id.IsNone() && Entry.bHasArmorType)
				{
					bool bAnyOfType = false;
					ArmorTable->ForeachRow<FCrossguardArmorRow>(TEXT("CrossguardTests"), [&](const FName&, const FCrossguardArmorRow& Armor)
					{
						bAnyOfType |= Armor.ArmorType == Entry.ArmorType;
					});
					TestTrue(*FString::Printf(TEXT("%s: some armor has its ArmorType %d"), *What, static_cast<int32>(Entry.ArmorType)), bAnyOfType);
				}
				break;
			case ECrossguardLootKind::Item:
				TestTrue(*FString::Printf(TEXT("%s is in ItemTable.json"), *What), HasRow(ItemTable, Entry.Id));
				break;
			case ECrossguardLootKind::SkillGem:
				TestTrue(*FString::Printf(TEXT("%s is in SkillTable.json"), *What), Entry.Id.IsNone() || HasRow(SkillTable, Entry.Id));
				break;
			case ECrossguardLootKind::Gold:
				TestTrue(*FString::Printf(TEXT("%s: ItemTable.json has gold_coin"), *What), HasRow(ItemTable, TEXT("gold_coin")));
				break;
			}
		}
	});

	// Every table rolls, with every drop resolvable (RollLoot logs a warning for each one it drops).
	FCrossguardLootSources Sources;
	Sources.LootTables = Imported[ETable::Loot].Get();
	Sources.WeaponTable = Imported[ETable::Weapons].Get();
	Sources.ArmorTable = Imported[ETable::Armor].Get();
	Sources.AffixTable = Imported[ETable::Affixes].Get();
	Sources.ItemTable = Imported[ETable::Items].Get();
	Sources.SkillCatalog = Catalog.Get();
	int32 Dropped = 0;
	for (const FName& TableId : LootTable->GetRowNames())
	{
		for (int64 Seed = 1; Seed <= 16; ++Seed)
		{
			for (const FCrossguardInventoryItem& Item : UCrossguardLootLibrary::RollLoot(Sources, TableId, Seed * 104729))
			{
				++Dropped;
				TestTrue(*FString::Printf(TEXT("Loot table '%s' seed %lld: every drop has a size and a count"), *TableId.ToString(), Seed),
					Item.Size.X > 0 && Item.Size.Y > 0 && Item.Count >= 1 && Item.Count <= Item.MaxStack);
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d loot tables rolled 16 times each: %d stacks"), LootTable->GetRowNames().Num(), Dropped));

	// Mobs: skills are known (native or a data row), loot tables exist, numbers are sane.
	MobTable->ForeachRow<FCrossguardMobRow>(TEXT("CrossguardTests"), [&](const FName& MobId, const FCrossguardMobRow& Mob)
	{
		const FString What = FString::Printf(TEXT("Mob '%s'"), *MobId.ToString());
		TestTrue(*FString::Printf(TEXT("%s: Health > 0"), *What), Mob.Health > 0.0f);
		TestTrue(*FString::Printf(TEXT("%s: Scale > 0"), *What), Mob.Scale > 0.0f);
		TestTrue(*FString::Printf(TEXT("%s: LootTableId '%s' is in LootTable.json"), *What, *Mob.LootTableId.ToString()),
			Mob.LootTableId.IsNone() || HasRow(LootTable, Mob.LootTableId));
		for (const FName& SkillId : Mob.Skills)
		{
			const bool bKnown = Catalog->FindAbilityClass(SkillId).Get() != nullptr || Catalog->FindSkill(SkillId) != nullptr
				|| Catalog->FindSkill(FName(*SkillId.ToString().ToLower())) != nullptr;
			TestTrue(*FString::Printf(TEXT("%s: skill '%s' is a native skill or a row in SkillTable.json"), *What, *SkillId.ToString()), bKnown);
		}
	});
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardEditorAssetsTest, "Crossguard.Data.EditorAssets", CROSSGUARD_TEST_FLAGS)

bool FCrossguardEditorAssetsTest::RunTest(const FString& Parameters)
{
	using namespace CrossguardDataTestLocal;

	int32 Found = 0;
	const UDataTable* SkillsAsset = nullptr;
	for (int32 Index = 0; Index < ETable::Count; ++Index)
	{
		const FTableInfo& Info = Tables()[Index];
		const FString PackageName = Info.AssetPackage;
		if (!FPackageName::DoesPackageExist(PackageName))
		{
			AddWarning(FString::Printf(TEXT("%s not found: skipped (import %s as described in docs/next-build-checklist.md)"), *PackageName, Info.JsonPath));
			continue;
		}
		const FString ObjectPath = PackageName + TEXT(".") + FPackageName::GetShortName(PackageName);
		const UDataTable* Asset = LoadObject<UDataTable>(nullptr, *ObjectPath);
		if (!TestNotNull(*FString::Printf(TEXT("%s loads as a Data Table"), *ObjectPath), Asset))
		{
			continue;
		}
		++Found;
		if (Index == ETable::Skills)
		{
			SkillsAsset = Asset;
		}
		const UScriptStruct* Expected = Info.RowStruct();
		if (!TestTrue(*FString::Printf(TEXT("%s uses row struct %s (has %s)"), *PackageName, *Expected->GetName(), *GetNameSafe(Asset->GetRowStruct())),
				Asset->GetRowStruct() == Expected))
		{
			continue;
		}

		// Same rows as the JSON in the repository; a difference means the asset needs a reimport.
		TStrongObjectPtr<UDataTable> Repo = ImportRepoTable(*this, Info);
		if (!Repo.IsValid())
		{
			continue;
		}
		const TSet<FName> AssetRows(Asset->GetRowNames());
		const TSet<FName> RepoRows(Repo->GetRowNames());
		for (const FName& Row : RepoRows.Difference(AssetRows))
		{
			AddError(FString::Printf(TEXT("%s has no row '%s' that %s has: reimport it"), *PackageName, *Row.ToString(), Info.JsonPath));
		}
		for (const FName& Row : AssetRows.Difference(RepoRows))
		{
			AddError(FString::Printf(TEXT("%s has row '%s' that %s no longer has: reimport it"), *PackageName, *Row.ToString(), Info.JsonPath));
		}
	}

	const UCrossguardSkillCatalog* CatalogAsset = nullptr;
	for (const TCHAR* Candidate : CatalogPackages)
	{
		const FString PackageName = Candidate;
		if (FPackageName::DoesPackageExist(PackageName))
		{
			const FString ObjectPath = PackageName + TEXT(".") + FPackageName::GetShortName(PackageName);
			CatalogAsset = LoadObject<UCrossguardSkillCatalog>(nullptr, *ObjectPath);
			TestNotNull(*FString::Printf(TEXT("%s loads as a CrossguardSkillCatalog"), *ObjectPath), CatalogAsset);
			break;
		}
	}
	if (CatalogAsset == nullptr)
	{
		AddWarning(TEXT("DA_SkillCatalog not found under /Game/Data, /Game, /Game/Data/Skills or /Game/Skills: catalog checks skipped"));
	}
	else
	{
		++Found;
		if (TestNotNull(TEXT("DA_SkillCatalog has its SkillTable set"), CatalogAsset->SkillTable.Get()) && SkillsAsset != nullptr)
		{
			TestTrue(TEXT("DA_SkillCatalog's SkillTable is /Game/Data/DT_Skills"), CatalogAsset->SkillTable.Get() == SkillsAsset);
		}
		for (const TPair<FName, TSubclassOf<UCrossguardGameplayAbility>>& Pair : CatalogAsset->AbilityClasses)
		{
			const UClass* Class = Pair.Value.Get();
			if (!TestNotNull(*FString::Printf(TEXT("DA_SkillCatalog AbilityClasses['%s'] has a class"), *Pair.Key.ToString()), Class))
			{
				continue;
			}
			const UCrossguardGameplayAbility* Ability = Class->GetDefaultObject<UCrossguardGameplayAbility>();
			TestTrue(*FString::Printf(TEXT("DA_SkillCatalog AbilityClasses['%s'] -> %s has that SkillId (has '%s')"), *Pair.Key.ToString(),
				*Class->GetName(), Ability != nullptr ? *Ability->SkillId.ToString() : TEXT("?")), Ability != nullptr && Ability->SkillId == Pair.Key);
			TestNotNull(*FString::Printf(TEXT("DA_SkillCatalog AbilityClasses['%s'] is a row of its SkillTable"), *Pair.Key.ToString()),
				CatalogAsset->FindSkill(Pair.Key));
		}
	}

	AddInfo(FString::Printf(TEXT("%d of %d editor assets found"), Found, static_cast<int32>(ETable::Count) + 1));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
