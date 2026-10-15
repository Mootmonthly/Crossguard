#include "Items/CrossguardItemTypes.h"

#include "CrossguardRules/Items.h"

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

float UCrossguardItemLibrary::GetWeaponValue(const FCrossguardWeaponRow& Weapon, ECrossguardRarity Rarity)
{
	return static_cast<float>(Crossguard::ValueAtRarity(Weapon.BaseValue, Weapon.PerRarity, ToRulesRarity(Rarity)));
}
