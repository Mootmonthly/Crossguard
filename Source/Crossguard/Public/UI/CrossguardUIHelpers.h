#pragma once

#include "CoreMinimal.h"
#include "Items/CrossguardInventoryComponent.h"

class ACrossguardCharacter;
class APawn;
class UAbilitySystemComponent;
class UCrossguardEquipmentComponent;
class UCrossguardLoadoutComponent;
class UCrossguardUIButton;

// Text and lookups shared by the placeholder UI (docs/ui.md). Display only: nothing here decides a
// game outcome.
namespace CrossguardUI
{
	// The item's display name from the data tables (weapon, armor or item table), else its id.
	CROSSGUARD_API FString ItemName(const FCrossguardInventoryItem& Item, const UCrossguardInventoryComponent* Inventory,
		const UCrossguardEquipmentComponent* Equipment);
	CROSSGUARD_API FString WeaponName(const FCrossguardWeaponItem& Weapon, const UCrossguardEquipmentComponent* Equipment);
	CROSSGUARD_API FString ArmorName(const FCrossguardArmorItem& Armor, const UCrossguardEquipmentComponent* Equipment);

	// A label of at most 4 characters for a one-square item cell: the initials of a name of several
	// words ("Healing Potion" -> "HP", "healing_potion" -> "HP", small words like "of" skipped),
	// else the first 3 letters of a single word ("Arrow" -> "Arr").
	CROSSGUARD_API FString ShortItemName(const FString& Name);

	// "Healing Potion x3 (Rare)".
	CROSSGUARD_API FString ItemLabel(const FCrossguardInventoryItem& Item, const UCrossguardInventoryComponent* Inventory,
		const UCrossguardEquipmentComponent* Equipment);

	// The item's rarity (weapons and armor keep theirs on the weapon / armor).
	CROSSGUARD_API ECrossguardRarity ItemRarity(const FCrossguardInventoryItem& Item);
	CROSSGUARD_API FLinearColor RarityColor(ECrossguardRarity Rarity);

	// Readable rarity colours (docs/ui.md, "Contrast"). RarityColor itself is too dark or too bright
	// for text on the UI's grey (Rare blue on grey is about 1.3:1). Text: the rarity colour lightened
	// toward white until its relative luminance is at least MinRarityTextLuminance. Background: a dark
	// tint of it. Every rarity's text then reads at 8:1 or more on its background, 5:1 hovered, and
	// 8:1 on a window's panel (WCAG asks 4.5:1).
	constexpr float MinRarityTextLuminance = 0.6f;
	CROSSGUARD_API FLinearColor RarityTextColor(ECrossguardRarity Rarity);
	CROSSGUARD_API FLinearColor RarityBackgroundColor(ECrossguardRarity Rarity);
	// An item button or cell: the dark rarity background and the light rarity label.
	CROSSGUARD_API void StyleRarityButton(UCrossguardUIButton* Button, ECrossguardRarity Rarity);

	// WCAG relative luminance of a linear colour (alpha ignored), and the contrast ratio of two colours
	// (1 to 21; WCAG AA asks 4.5 for text).
	CROSSGUARD_API float RelativeLuminance(const FLinearColor& Color);
	CROSSGUARD_API float ContrastRatio(const FLinearColor& First, const FLinearColor& Second);

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
