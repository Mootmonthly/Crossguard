#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Skills/CrossguardSkillTypes.h"

#include "CrossguardItemTypes.generated.h"

// Item data types. Mirrors docs/item-data-schema.md. Weapon rows are generated from
// Data/Items/Weapons.json by Tools/export_weapon_table.py; do not edit the data table by hand.

UENUM(BlueprintType)
enum class ECrossguardRarity : uint8
{
	Common,
	Uncommon,
	Rare,
	Epic,
	Legendary,
	Mystic,
};

UENUM(BlueprintType)
enum class ECrossguardHandedness : uint8
{
	OneHanded,
	TwoHanded,
};

UENUM(BlueprintType)
enum class ECrossguardWeaponRange : uint8
{
	Melee,
	Ranged,
};

// What a weapon provides.
UENUM(BlueprintType)
enum class ECrossguardWeaponStat : uint8
{
	// Base Attack Damage per attack.
	AttackDamage,
	// Base Magic Damage per attack.
	MagicDamage,
	// Fraction added to AR while equipped (shields).
	AttackResistance,
};

UENUM(BlueprintType)
enum class ECrossguardAttackDelivery : uint8
{
	None,
	MeleeSwing,
	Hitscan,
	Projectile,
};

UENUM(BlueprintType)
enum class ECrossguardEquipmentSlot : uint8
{
	MainHand,
	OffHand,
};

// One row per weapon class. Row name = weapon id (e.g. "sword_one_handed").
USTRUCT(BlueprintType)
struct CROSSGUARD_API FCrossguardWeaponRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	ECrossguardWeaponType WeaponType = ECrossguardWeaponType::Sword;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	ECrossguardHandedness Handedness = ECrossguardHandedness::OneHanded;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	ECrossguardWeaponRange Range = ECrossguardWeaponRange::Melee;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	ECrossguardWeaponStat Stat = ECrossguardWeaponStat::AttackDamage;

	// Value at Common. Damage in points; AR as a fraction.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	float BaseValue = 0.0f;

	// Added per rarity level.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	float PerRarity = 0.0f;

	// Attacks per PerSeconds seconds. 0: does not attack.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	float Attacks = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	float PerSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	ECrossguardAttackDelivery Delivery = ECrossguardAttackDelivery::None;

	bool IsMelee() const { return Range == ECrossguardWeaponRange::Melee; }
};

// A rolled affix on an item instance.
USTRUCT(BlueprintType)
struct CROSSGUARD_API FCrossguardAffix
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FName AffixId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	float Value = 0.0f;
};

// A specific weapon: class, rarity and rolled affixes.
USTRUCT(BlueprintType)
struct CROSSGUARD_API FCrossguardWeaponItem
{
	GENERATED_BODY()

	// Row in the weapon table. None: empty slot.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FName WeaponId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	ECrossguardRarity Rarity = ECrossguardRarity::Common;

	// Not applied to stats yet; the affix pool is not designed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	TArray<FCrossguardAffix> Affixes;

	bool IsEmpty() const { return WeaponId.IsNone(); }
};

UCLASS()
class CROSSGUARD_API UCrossguardItemLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Common = 0 ... Mystic = 5.
	UFUNCTION(BlueprintPure, Category = "Crossguard|Items")
	static int32 GetRarityLevel(ECrossguardRarity Rarity);

	UFUNCTION(BlueprintPure, Category = "Crossguard|Items")
	static FLinearColor GetRarityColor(ECrossguardRarity Rarity);

	UFUNCTION(BlueprintPure, Category = "Crossguard|Items")
	static int32 GetAffixSlots(ECrossguardRarity Rarity);

	// The weapon's AD, MD or AR at the item's rarity.
	UFUNCTION(BlueprintPure, Category = "Crossguard|Items")
	static float GetWeaponValue(const FCrossguardWeaponRow& Weapon, ECrossguardRarity Rarity);
};
