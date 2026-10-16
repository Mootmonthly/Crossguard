#pragma once

#include "CoreMinimal.h"
#include "Items/CrossguardInventoryComponent.h"

class ACrossguardCharacter;
class APawn;
class UAbilitySystemComponent;
class UCrossguardEquipmentComponent;
class UCrossguardLoadoutComponent;

// Text and lookups shared by the placeholder UI (docs/ui.md). Display only: nothing here decides a
// game outcome.
namespace CrossguardUI
{
	// The item's display name from the data tables (weapon, armor or item table), else its id.
	CROSSGUARD_API FString ItemName(const FCrossguardInventoryItem& Item, const UCrossguardInventoryComponent* Inventory,
		const UCrossguardEquipmentComponent* Equipment);
	CROSSGUARD_API FString WeaponName(const FCrossguardWeaponItem& Weapon, const UCrossguardEquipmentComponent* Equipment);
	CROSSGUARD_API FString ArmorName(const FCrossguardArmorItem& Armor, const UCrossguardEquipmentComponent* Equipment);

	// "Healing Potion x3 (Rare)".
	CROSSGUARD_API FString ItemLabel(const FCrossguardInventoryItem& Item, const UCrossguardInventoryComponent* Inventory,
		const UCrossguardEquipmentComponent* Equipment);

	// The item's rarity (weapons and armor keep theirs on the weapon / armor).
	CROSSGUARD_API ECrossguardRarity ItemRarity(const FCrossguardInventoryItem& Item);
	CROSSGUARD_API FLinearColor RarityColor(ECrossguardRarity Rarity);

	// The skill's display name from the loadout's skill catalog, else its id.
	CROSSGUARD_API FString SkillName(FName SkillId, const UCrossguardLoadoutComponent* Loadout);

	// Seconds left on the cooldown of a granted skill (its ability's cooldown tags); 0 when ready or
	// not granted.
	CROSSGUARD_API float SkillCooldownRemaining(const UAbilitySystemComponent* AbilitySystem, FName SkillId);

	// The nearest other player's character within MaxDistance cm; null if none. Only picks whom a
	// request is about: the server checks distance and everything else.
	CROSSGUARD_API ACrossguardCharacter* FindNearestPlayer(const ACrossguardCharacter* From, float MaxDistance);

	// The player name of a pawn's player state; "?" if none.
	CROSSGUARD_API FString PlayerNameOf(const APawn* Pawn);

	template <typename EnumType>
	FString UIEnumName(EnumType Value)
	{
		return StaticEnum<EnumType>()->GetNameStringByValue(static_cast<int64>(Value));
	}
}
