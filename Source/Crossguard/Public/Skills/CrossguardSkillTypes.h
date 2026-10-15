#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"

#include "CrossguardSkillTypes.generated.h"

// Skill data types. Mirrors docs/skill-data-schema.md. Rows are generated from
// Data/Skills/Skills.json by Tools/export_skill_table.py; do not edit the data table by hand.

UENUM(BlueprintType)
enum class ECrossguardSkillCategory : uint8
{
	Active,
	Spell,
	Art,
	Passive,
};

UENUM(BlueprintType)
enum class ECrossguardWeaponTag : uint8
{
	None,
	Melee,
	Ranged,
};

UENUM(BlueprintType)
enum class ECrossguardWeaponType : uint8
{
	Sword,
	Axe,
	Dagger,
	Spear,
	Mace,
	Staff,
	Hammer,
	Shield,
	Bow,
	Crossbow,
	Sling,
	Throwing,
	Wand,
	AnyMelee,
	TwoHandedMelee,
};

UENUM(BlueprintType)
enum class ECrossguardArmorRequirement : uint8
{
	None,
	Plate,
	PlateAllSlots,
};

// Stats a skill can modify. Names match UCrossguardAttributeSet; the exporter maps the schema's
// abbreviations (STR, ADB, ...) to these.
UENUM(BlueprintType)
enum class ECrossguardStat : uint8
{
	Strength,
	Agility,
	Intelligence,
	AllAttributes,
	MaxHealth,
	MaxHealthBonus,
	AttackDamage,
	AdditionalAttackDamage,
	AttackDamageBonus,
	AttackResistance,
	AttackPenetration,
	Dexterity,
	ActionSpeed,
	MoveSpeed,
	PhysicalHealing,
	AdditionalPhysicalHealing,
	PhysicalHealingBonus,
	MagicDamage,
	AdditionalMagicDamage,
	MagicDamageBonus,
	MagicResistance,
	MagicalHealing,
	AdditionalMagicalHealing,
	MagicalHealingBonus,
	CooldownReduction,
	CastTimeReduction,
};

UENUM(BlueprintType)
enum class ECrossguardModifierOp : uint8
{
	// Add points.
	Flat,
	// Add a fraction (0.15 = +15%).
	Percent,
	// Multiply (2.0 = doubled).
	Multiply,
};

UENUM(BlueprintType)
enum class ECrossguardModifierTarget : uint8
{
	Self,
	Party,
	AlliesInRadius,
	Target,
};

UENUM(BlueprintType)
enum class ECrossguardSkillFilter : uint8
{
	None,
	Melee,
	Ranged,
	Minion,
	Active,
	Spell,
	Art,
};

UENUM(BlueprintType)
enum class ECrossguardConditionType : uint8
{
	// Always, while equipped or active.
	None,
	Wielding,
	Armor,
	PerEmptyArmorSlot,
	OnActivation,
	NextAttack,
	Other,
};

UENUM(BlueprintType)
enum class ECrossguardDamageType : uint8
{
	Attack,
	Magic,
};

UENUM(BlueprintType)
enum class ECrossguardDamageKind : uint8
{
	Impact,
	Splash,
	PerSecond,
	OverTime,
	Additional,
	// Amount is a fraction of weapon AD (0.85 = 85%).
	WeaponPercent,
};

UENUM(BlueprintType)
enum class ECrossguardHealingType : uint8
{
	Physical,
	Magical,
};

USTRUCT(BlueprintType)
struct CROSSGUARD_API FCrossguardSkillRequirements
{
	GENERATED_BODY()

	// Any one of these must be wielded. Empty: no weapon requirement.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	TArray<ECrossguardWeaponType> Weapons;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	ECrossguardArmorRequirement Armor = ECrossguardArmorRequirement::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	TArray<FString> Items;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	TArray<FString> Forms;
};

USTRUCT(BlueprintType)
struct CROSSGUARD_API FCrossguardSkillCondition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	ECrossguardConditionType Type = ECrossguardConditionType::None;

	// For Wielding.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	TArray<ECrossguardWeaponType> Weapons;

	// For Armor.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	ECrossguardArmorRequirement Armor = ECrossguardArmorRequirement::None;

	// For Other.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	FString Description;
};

USTRUCT(BlueprintType)
struct CROSSGUARD_API FCrossguardStatModifier
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	ECrossguardStat Stat = ECrossguardStat::Strength;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	ECrossguardModifierOp Op = ECrossguardModifierOp::Flat;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	float Value = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	ECrossguardModifierTarget Target = ECrossguardModifierTarget::Self;

	// For CooldownReduction / CastTimeReduction: which skills it applies to.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	ECrossguardSkillFilter SkillFilter = ECrossguardSkillFilter::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	FCrossguardSkillCondition Condition;

	// 0: permanent.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	float DurationSec = 0.0f;
};

USTRUCT(BlueprintType)
struct CROSSGUARD_API FCrossguardSkillDamage
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	ECrossguardDamageType Type = ECrossguardDamageType::Attack;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	float Amount = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	float Coefficient = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	ECrossguardDamageKind Kind = ECrossguardDamageKind::Impact;

	// For PerSecond / OverTime. 0 otherwise.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	float DurationSec = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	int32 Hits = 1;
};

USTRUCT(BlueprintType)
struct CROSSGUARD_API FCrossguardSkillHealing
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	ECrossguardHealingType Type = ECrossguardHealingType::Magical;

	// Ignored when bFull.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	float Amount = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	float Coefficient = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	bool bFull = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	bool bCanRevive = false;
};

// One row per skill. Row name = skill id (e.g. "fireball").
USTRUCT(BlueprintType)
struct CROSSGUARD_API FCrossguardSkillRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill", meta = (ClampMin = "0"))
	int32 CapacityCost = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	ECrossguardSkillCategory Category = ECrossguardSkillCategory::Active;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	ECrossguardWeaponTag WeaponTag = ECrossguardWeaponTag::None;

	// Extra tags: Minion, Buff, Debuff, Summon, Heal, Raid.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	TArray<FName> Tags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	FCrossguardSkillRequirements Requirements;

	// 0: no cooldown.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	float CooldownSec = 0.0f;

	// 0: no duration.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	float DurationSec = 0.0f;

	// Bundle components: full effects included permanently. Cannot be equipped alongside.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	TArray<FName> Includes;

	// Usable only while this skill is active. Cannot be equipped alongside.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	TArray<FName> GrantsWhileActive;

	// Skills a summoned minion uses. Not granted to the player.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	TArray<FName> MinionSkills;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	TArray<FCrossguardStatModifier> StatModifiers;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	TArray<FCrossguardSkillDamage> Damage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	TArray<FCrossguardSkillHealing> Healing;

	// Behaviours the structured fields cannot express.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	TArray<FString> Mechanics;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill", meta = (MultiLine = "true"))
	FText Description;
};
