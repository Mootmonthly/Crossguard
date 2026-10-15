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

UENUM(BlueprintType)
enum class ECrossguardArmorSlot : uint8
{
	Head,
	Chest,
	Hands,
	Legs,
	Feet,
	// Capes; later backpacks and quivers.
	Back,
};

UENUM(BlueprintType)
enum class ECrossguardArmorType : uint8
{
	Cloth,
	Leather,
	Plate,
};

// Mirrors Crossguard::EItemCategory.
UENUM(BlueprintType)
enum class ECrossguardItemCategory : uint8
{
	Weapon,
	Armor,
	Ammo,
	SkillGem,
	Container,
	Material,
	Consumable,
	Throwable,
	Other,
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

	// Squares the item takes in the inventory grid, unrotated.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	int32 GridWidth = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	int32 GridHeight = 1;

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

	// Parries with right click when the off hand is empty.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	bool bCanParry = false;

	bool IsMelee() const { return Range == ECrossguardWeaponRange::Melee; }
};

// One row per affix. Row name = affix id (e.g. "true_damage"). Generated from
// Data/Items/Affixes.json by Tools/export_affix_table.py.
USTRUCT(BlueprintType)
struct CROSSGUARD_API FCrossguardAffixRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	FText DisplayName;

	// Attribute name on UCrossguardAttributeSet that the value adds to.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	FName Stat;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	float Min = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	float Max = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	float Step = 0.0f;

	// AttackWeapon, MagicWeapon, Shield.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	TArray<FName> Pools;
};

USTRUCT(BlueprintType)
struct CROSSGUARD_API FCrossguardArmorStat
{
	GENERATED_BODY()

	// Attribute name on UCrossguardAttributeSet.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Armor")
	FName Stat;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Armor")
	float BaseValue = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Armor")
	float PerRarity = 0.0f;
};

// One row per armor piece. Row name = armor id (e.g. "plate_helm"). Generated from
// Data/Items/Armor.json by Tools/export_armor_table.py.
USTRUCT(BlueprintType)
struct CROSSGUARD_API FCrossguardArmorRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Armor")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Armor")
	ECrossguardArmorSlot Slot = ECrossguardArmorSlot::Head;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Armor")
	ECrossguardArmorType ArmorType = ECrossguardArmorType::Cloth;

	// Squares the item takes in the inventory grid, unrotated.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Armor")
	int32 GridWidth = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Armor")
	int32 GridHeight = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Armor")
	TArray<FCrossguardArmorStat> Stats;

	// PlateArmor, LeatherArmor, ClothArmor.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Armor")
	FName AffixPool;
};

// One row per general item (ammo, consumables, throwables, materials). Row name = item id
// (e.g. "arrow"). Generated from Data/Items/Items.json by Tools/export_item_table.py.
USTRUCT(BlueprintType)
struct CROSSGUARD_API FCrossguardItemRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	ECrossguardItemCategory Category = ECrossguardItemCategory::Other;

	// Squares the item takes in the inventory grid, unrotated.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	int32 GridWidth = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	int32 GridHeight = 1;

	// Most items one inventory stack holds.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	int32 MaxStack = 1;
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

	// Added to the wearer's stats while equipped.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	TArray<FCrossguardAffix> Affixes;

	bool IsEmpty() const { return WeaponId.IsNone(); }
};

// A specific armor piece: which piece, rarity and rolled affixes.
USTRUCT(BlueprintType)
struct CROSSGUARD_API FCrossguardArmorItem
{
	GENERATED_BODY()

	// Row in the armor table. None: empty slot.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FName ArmorId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	ECrossguardRarity Rarity = ECrossguardRarity::Common;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	TArray<FCrossguardAffix> Affixes;

	bool IsEmpty() const { return ArmorId.IsNone(); }
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

	// The affix pool a weapon rolls from: AttackWeapon (AD), MagicWeapon (MD) or Shield (AR).
	UFUNCTION(BlueprintPure, Category = "Crossguard|Items")
	static FName GetAffixPool(const FCrossguardWeaponRow& Weapon);

	// An armor stat's value at the item's rarity.
	UFUNCTION(BlueprintPure, Category = "Crossguard|Items")
	static float GetArmorStatValue(const FCrossguardArmorStat& Stat, ECrossguardRarity Rarity);

	// Server only. A new armor item with affixes from the piece's pool. Same Seed, same item.
	UFUNCTION(BlueprintCallable, Category = "Crossguard|Items")
	static FCrossguardArmorItem RollArmorItem(const UDataTable* AffixTable, FName ArmorId, const FCrossguardArmorRow& Armor,
		ECrossguardRarity Rarity, int64 Seed);

	// Server only. A new weapon item with as many distinct affixes from its pool as its rarity
	// allows. The same Seed always gives the same item.
	UFUNCTION(BlueprintCallable, Category = "Crossguard|Items")
	static FCrossguardWeaponItem RollWeaponItem(const UDataTable* AffixTable, FName WeaponId, const FCrossguardWeaponRow& Weapon,
		ECrossguardRarity Rarity, int64 Seed);
};
