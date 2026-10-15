#include "Items/CrossguardItemTypes.h"

#include "CrossguardRules/Items.h"
#include "Engine/DataTable.h"

namespace
{
	Crossguard::ERarity ToRulesRarity(ECrossguardRarity Rarity)
	{
		return static_cast<Crossguard::ERarity>(static_cast<uint8>(Rarity));
	}
}

// The two enums must stay in the same order.
static_assert(static_cast<int>(ECrossguardRarity::Common) == static_cast<int>(Crossguard::ERarity::Common), "Rarity enums out of sync");
static_assert(static_cast<int>(ECrossguardRarity::Uncommon) == static_cast<int>(Crossguard::ERarity::Uncommon), "Rarity enums out of sync");
static_assert(static_cast<int>(ECrossguardRarity::Rare) == static_cast<int>(Crossguard::ERarity::Rare), "Rarity enums out of sync");
static_assert(static_cast<int>(ECrossguardRarity::Epic) == static_cast<int>(Crossguard::ERarity::Epic), "Rarity enums out of sync");
static_assert(static_cast<int>(ECrossguardRarity::Legendary) == static_cast<int>(Crossguard::ERarity::Legendary), "Rarity enums out of sync");
static_assert(static_cast<int>(ECrossguardRarity::Mystic) == static_cast<int>(Crossguard::ERarity::Mystic), "Rarity enums out of sync");

int32 UCrossguardItemLibrary::GetRarityLevel(ECrossguardRarity Rarity)
{
	return Crossguard::RarityLevel(ToRulesRarity(Rarity));
}

FLinearColor UCrossguardItemLibrary::GetRarityColor(ECrossguardRarity Rarity)
{
	// docs/item-data-schema.md, "Rarity".
	switch (Rarity)
	{
	case ECrossguardRarity::Common: return FLinearColor(FColor::FromHex(TEXT("9D9D9D")));
	case ECrossguardRarity::Uncommon: return FLinearColor(FColor::FromHex(TEXT("1EFF00")));
	case ECrossguardRarity::Rare: return FLinearColor(FColor::FromHex(TEXT("0070DD")));
	case ECrossguardRarity::Epic: return FLinearColor(FColor::FromHex(TEXT("A335EE")));
	case ECrossguardRarity::Legendary: return FLinearColor(FColor::FromHex(TEXT("FF8000")));
	case ECrossguardRarity::Mystic: return FLinearColor(FColor::FromHex(TEXT("FFD700")));
	}
	return FLinearColor::White;
}

int32 UCrossguardItemLibrary::GetAffixSlots(ECrossguardRarity Rarity)
{
	return Crossguard::AffixSlots(Crossguard::FItemConfig(), ToRulesRarity(Rarity));
}

FName UCrossguardItemLibrary::GetAffixPool(const FCrossguardWeaponRow& Weapon)
{
	switch (Weapon.Stat)
	{
	case ECrossguardWeaponStat::AttackDamage: return FName(TEXT("AttackWeapon"));
	case ECrossguardWeaponStat::MagicDamage: return FName(TEXT("MagicWeapon"));
	case ECrossguardWeaponStat::AttackResistance: return NAME_None;
	}
	return NAME_None;
}

FCrossguardWeaponItem UCrossguardItemLibrary::RollWeaponItem(const UDataTable* AffixTable, FName WeaponId, const FCrossguardWeaponRow& Weapon,
	ECrossguardRarity Rarity, int64 Seed)
{
	FCrossguardWeaponItem Item;
	Item.WeaponId = WeaponId;
	Item.Rarity = Rarity;

	const FName Pool = GetAffixPool(Weapon);
	if (AffixTable == nullptr || Pool.IsNone())
	{
		return Item;
	}

	// Sorted by row name so the same seed rolls the same affixes regardless of table order.
	TArray<FName> RowNames = AffixTable->GetRowNames();
	RowNames.Sort(FNameLexicalLess());
	std::vector<Crossguard::FAffixDefinition> Definitions;
	for (const FName& RowName : RowNames)
	{
		const FCrossguardAffixRow* Row = AffixTable->FindRow<FCrossguardAffixRow>(RowName, TEXT("RollWeaponItem"), false);
		if (Row != nullptr && Row->Pools.Contains(Pool))
		{
			Definitions.push_back({std::string(TCHAR_TO_UTF8(*RowName.ToString())), Row->Min, Row->Max, Row->Step});
		}
	}

	const int Slots = Crossguard::AffixSlots(Crossguard::FItemConfig(), ToRulesRarity(Rarity));
	for (const Crossguard::FRolledAffix& Rolled : Crossguard::RollAffixes(Definitions, Slots, static_cast<uint64>(Seed)))
	{
		FCrossguardAffix Affix;
		Affix.AffixId = FName(UTF8_TO_TCHAR(Rolled.Id.c_str()));
		Affix.Value = static_cast<float>(Rolled.Value);
		Item.Affixes.Add(Affix);
	}
	return Item;
}

float UCrossguardItemLibrary::GetWeaponValue(const FCrossguardWeaponRow& Weapon, ECrossguardRarity Rarity)
{
	return static_cast<float>(Crossguard::ValueAtRarity(Weapon.BaseValue, Weapon.PerRarity, ToRulesRarity(Rarity)));
}
