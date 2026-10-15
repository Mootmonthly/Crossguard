#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Items/CrossguardInventoryComponent.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CrossguardLoot.generated.h"

class UCrossguardSkillCatalog;

// Loot tables (docs/loot-data-schema.md). Rows are generated from Data/Loot/LootTables.json by
// Tools/export_loot_table.py; do not edit the data table by hand. Rolled by Crossguard::RollLoot.

// Mirrors Crossguard::ELootKind.
UENUM(BlueprintType)
enum class ECrossguardLootKind : uint8
{
	Weapon,
	Armor,
	Item,
	SkillGem,
	Gold,
};

USTRUCT(BlueprintType)
struct CROSSGUARD_API FCrossguardLootEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	ECrossguardLootKind Kind = ECrossguardLootKind::Item;

	// Weapon / armor / item / skill id. None: any weapon, any armor piece, or any Skill Gem.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	FName Id;

	// Any armor piece of ArmorType only (Id None, Kind Armor, bHasArmorType).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	bool bHasArmorType = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	ECrossguardArmorType ArmorType = ECrossguardArmorType::Cloth;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "0"))
	int32 Weight = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "1"))
	int32 CountMin = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "1"))
	int32 CountMax = 1;
};

// One loot table. Row name = table id (e.g. "basic_monster").
USTRUCT(BlueprintType)
struct CROSSGUARD_API FCrossguardLootTableRow : public FTableRowBase
{
	GENERATED_BODY()

	// Drops per roll of the table, inclusive.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	int32 RollsMin = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	int32 RollsMax = 1;

	// Weight of "nothing" against the entries, per drop.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	int32 NothingWeight = 0;

	// Common .. Mystic: rarity of weapons, armor, items and "any Skill Gem" drops.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	TArray<int32> RarityWeights;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	TArray<FCrossguardLootEntry> Entries;
};

// The data a loot roll needs. The game mode holds one (ACrossguardGameMode::Loot).
USTRUCT(BlueprintType)
struct CROSSGUARD_API FCrossguardLootSources
{
	GENERATED_BODY()

	// Imported from Data/Loot/LootTable.json. Row struct: CrossguardLootTableRow.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	TObjectPtr<UDataTable> LootTables;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	TObjectPtr<UDataTable> WeaponTable;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	TObjectPtr<UDataTable> ArmorTable;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	TObjectPtr<UDataTable> AffixTable;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	TObjectPtr<UDataTable> ItemTable;

	// For Skill Gem rarities.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	TObjectPtr<UCrossguardSkillCatalog> SkillCatalog;
};

UCLASS()
class CROSSGUARD_API UCrossguardLootLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Server. Rolls the table TableId into inventory items: weapons and armor with rolled affixes,
	// stacks sized from the item data, gold as gold coins. Items whose data is missing are left
	// out (and logged). Same Seed, same loot.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Crossguard|Loot")
	static TArray<FCrossguardInventoryItem> RollLoot(const FCrossguardLootSources& Sources, FName TableId, int64 Seed);

	// Grid size and stack limit of a weapon, armor, table item or Skill Gem; false if its data is
	// missing. (Backpacks and Quivers are sized by the inventory.)
	static bool ResolveItemSize(const FCrossguardLootSources& Sources, FCrossguardInventoryItem& Item);
};
