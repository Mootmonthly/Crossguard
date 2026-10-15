#include "Items/CrossguardLoot.h"

#include "Crossguard.h"
#include "CrossguardRules/Loot.h"
#include "Items/CrossguardItemTypes.h"
#include "Math/RandomStream.h"
#include "Skills/CrossguardSkillCatalog.h"
#include "Skills/CrossguardSkillTypes.h"

static_assert(static_cast<int>(ECrossguardLootKind::Gold) == static_cast<int>(Crossguard::ELootKind::Gold), "Loot kind enums out of sync");
static_assert(static_cast<int>(ECrossguardLootKind::SkillGem) == static_cast<int>(Crossguard::ELootKind::SkillGem), "Loot kind enums out of sync");

namespace
{
	// The gold coin row in Data/Items/Items.json.
	const FName GoldItemId(TEXT("gold_coin"));

	Crossguard::FLootTable ToRulesTable(const FCrossguardLootTableRow& Row)
	{
		Crossguard::FLootTable Table;
		Table.RollsMin = Row.RollsMin;
		Table.RollsMax = Row.RollsMax;
		Table.NothingWeight = Row.NothingWeight;
		for (int32 Level = 0; Level < 6; ++Level)
		{
			Table.RarityWeights[static_cast<size_t>(Level)] = Row.RarityWeights.IsValidIndex(Level) ? Row.RarityWeights[Level] : 0;
		}
		for (const FCrossguardLootEntry& Entry : Row.Entries)
		{
			Crossguard::FLootEntry RulesEntry;
			RulesEntry.Kind = static_cast<Crossguard::ELootKind>(static_cast<uint8>(Entry.Kind));
			RulesEntry.Id = Entry.Id.IsNone() ? std::string() : std::string(TCHAR_TO_UTF8(*Entry.Id.ToString()));
			RulesEntry.Weight = Entry.Weight;
			RulesEntry.CountMin = Entry.CountMin;
			RulesEntry.CountMax = Entry.CountMax;
			Table.Entries.push_back(RulesEntry);
		}
		return Table;
	}

	// Every skill with an assigned Skill Gem rarity.
	Crossguard::FSkillGemRarities GemRarities(const UCrossguardSkillCatalog* Catalog)
	{
		Crossguard::FSkillGemRarities Gems;
		if (Catalog == nullptr || Catalog->SkillTable == nullptr)
		{
			return Gems;
		}
		Catalog->SkillTable->ForeachRow<FCrossguardSkillRow>(TEXT("GemRarities"), [&Gems](const FName& RowName, const FCrossguardSkillRow& Row)
		{
			if (Row.bHasRarity)
			{
				Gems.emplace(TCHAR_TO_UTF8(*RowName.ToString()), static_cast<Crossguard::ERarity>(static_cast<uint8>(Row.Rarity)));
			}
		});
		return Gems;
	}

	// Id, or for "any" a random row of Table.
	FName PickRow(const UDataTable* Table, const std::string& Id, FRandomStream& Stream)
	{
		if (!Id.empty())
		{
			return FName(UTF8_TO_TCHAR(Id.c_str()));
		}
		if (Table == nullptr)
		{
			return NAME_None;
		}
		const TArray<FName> Names = Table->GetRowNames();
		return Names.IsEmpty() ? NAME_None : Names[Stream.RandHelper(Names.Num())];
	}
}

bool UCrossguardLootLibrary::ResolveItemSize(const FCrossguardLootSources& Sources, FCrossguardInventoryItem& Item)
{
	FIntPoint Size = FIntPoint::ZeroValue;
	int32 MaxStack = 1;
	switch (Item.Category)
	{
	case ECrossguardItemCategory::Weapon:
		if (const FCrossguardWeaponRow* Row = Sources.WeaponTable != nullptr
				? Sources.WeaponTable->FindRow<FCrossguardWeaponRow>(Item.Weapon.WeaponId, TEXT("Loot"), false) : nullptr)
		{
			Size = {Row->GridWidth, Row->GridHeight};
		}
		break;
	case ECrossguardItemCategory::Armor:
		if (const FCrossguardArmorRow* Row = Sources.ArmorTable != nullptr
				? Sources.ArmorTable->FindRow<FCrossguardArmorRow>(Item.Armor.ArmorId, TEXT("Loot"), false) : nullptr)
		{
			Size = {Row->GridWidth, Row->GridHeight};
		}
		break;
	case ECrossguardItemCategory::SkillGem:
		Size = {1, 1};
		break;
	case ECrossguardItemCategory::Container:
		return false;
	default:
		if (const FCrossguardItemRow* Row = Sources.ItemTable != nullptr
				? Sources.ItemTable->FindRow<FCrossguardItemRow>(Item.ItemId, TEXT("Loot"), false) : nullptr)
		{
			Size = {Row->GridWidth, Row->GridHeight};
			MaxStack = FMath::Max(1, Row->MaxStack);
		}
		break;
	}
	if (Size.X <= 0 || Size.Y <= 0)
	{
		return false;
	}
	Item.Size = Size;
	Item.MaxStack = MaxStack;
	return true;
}

TArray<FCrossguardInventoryItem> UCrossguardLootLibrary::RollLoot(const FCrossguardLootSources& Sources, FName TableId, int64 Seed)
{
	TArray<FCrossguardInventoryItem> Loot;
	const FCrossguardLootTableRow* Row = Sources.LootTables != nullptr && !TableId.IsNone()
		? Sources.LootTables->FindRow<FCrossguardLootTableRow>(TableId, TEXT("RollLoot"), false)
		: nullptr;
	if (Row == nullptr)
	{
		UE_LOG(LogCrossguard, Warning, TEXT("No loot table '%s' (is the game mode's Loot set?)"), *TableId.ToString());
		return Loot;
	}

	for (const Crossguard::FLootDrop& Drop : Crossguard::RollLoot(ToRulesTable(*Row), GemRarities(Sources.SkillCatalog), static_cast<uint64>(Seed)))
	{
		FRandomStream Stream(static_cast<int32>(Drop.Seed & 0x7fffffff));
		const ECrossguardRarity Rarity = static_cast<ECrossguardRarity>(static_cast<uint8>(Drop.Rarity));
		FCrossguardInventoryItem Item;
		Item.Rarity = Rarity;
		Item.Count = Drop.Count;
		switch (Drop.Kind)
		{
		case Crossguard::ELootKind::Weapon:
		{
			const FName WeaponId = PickRow(Sources.WeaponTable, Drop.Id, Stream);
			const FCrossguardWeaponRow* Weapon = Sources.WeaponTable != nullptr && !WeaponId.IsNone()
				? Sources.WeaponTable->FindRow<FCrossguardWeaponRow>(WeaponId, TEXT("RollLoot"), false) : nullptr;
			if (Weapon == nullptr)
			{
				continue;
			}
			Item.Category = ECrossguardItemCategory::Weapon;
			Item.Weapon = UCrossguardItemLibrary::RollWeaponItem(Sources.AffixTable, WeaponId, *Weapon, Rarity, static_cast<int64>(Drop.Seed));
			break;
		}
		case Crossguard::ELootKind::Armor:
		{
			const FName ArmorId = PickRow(Sources.ArmorTable, Drop.Id, Stream);
			const FCrossguardArmorRow* Armor = Sources.ArmorTable != nullptr && !ArmorId.IsNone()
				? Sources.ArmorTable->FindRow<FCrossguardArmorRow>(ArmorId, TEXT("RollLoot"), false) : nullptr;
			if (Armor == nullptr)
			{
				continue;
			}
			Item.Category = ECrossguardItemCategory::Armor;
			Item.Armor = UCrossguardItemLibrary::RollArmorItem(Sources.AffixTable, ArmorId, *Armor, Rarity, static_cast<int64>(Drop.Seed));
			break;
		}
		case Crossguard::ELootKind::SkillGem:
			Item.Category = ECrossguardItemCategory::SkillGem;
			Item.ItemId = FName(UTF8_TO_TCHAR(Drop.Id.c_str()));
			break;
		case Crossguard::ELootKind::Gold:
		case Crossguard::ELootKind::Item:
		{
			Item.ItemId = Drop.Kind == Crossguard::ELootKind::Gold ? GoldItemId : FName(UTF8_TO_TCHAR(Drop.Id.c_str()));
			const FCrossguardItemRow* ItemRow = Sources.ItemTable != nullptr
				? Sources.ItemTable->FindRow<FCrossguardItemRow>(Item.ItemId, TEXT("RollLoot"), false) : nullptr;
			if (ItemRow == nullptr)
			{
				UE_LOG(LogCrossguard, Warning, TEXT("Loot table '%s': no item '%s'"), *TableId.ToString(), *Item.ItemId.ToString());
				continue;
			}
			Item.Category = ItemRow->Category;
			break;
		}
		}

		if (!ResolveItemSize(Sources, Item))
		{
			UE_LOG(LogCrossguard, Warning, TEXT("Loot table '%s': no size for a dropped item; skipped"), *TableId.ToString());
			continue;
		}
		// Big counts become several stacks.
		int32 Left = Item.Count;
		while (Left > 0)
		{
			FCrossguardInventoryItem Stack = Item;
			Stack.Count = FMath::Min(Left, Item.MaxStack);
			Left -= Stack.Count;
			Loot.Add(Stack);
		}
	}
	return Loot;
}
