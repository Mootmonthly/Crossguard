#pragma once

#include "Character/CrossguardAICharacter.h"
#include "CoreMinimal.h"
#include "Engine/DataTable.h"

#include "CrossguardMobRow.generated.h"

UENUM(BlueprintType)
enum class ECrossguardMobKind : uint8
{
	// Kill XP: BasicMonster.
	Monster,
	// Kill XP: BossMonster.
	Boss,
};

// One mob type (docs/mob-data-schema.md). Row name = mob id. Generated from Data/Mobs/Mobs.json by
// Tools/export_mob_table.py (distances in cm here, meters in the source); do not edit by hand.
// An ACrossguardMonster with this MobId takes these stats.
USTRUCT(BlueprintType)
struct CROSSGUARD_API FCrossguardMobRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mob")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mob")
	ECrossguardMobKind Kind = ECrossguardMobKind::Monster;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mob")
	ECrossguardCombatRole CombatRole = ECrossguardCombatRole::Warrior;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mob")
	float Health = 80.0f;

	// Warrior: Attack Damage per hit.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mob")
	float AttackDamage = 12.0f;

	// Seconds between hits.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mob")
	float AttackInterval = 1.5f;

	// cm.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mob")
	float AttackRange = 200.0f;

	// Mage: seconds between fireballs.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mob")
	float FireballInterval = 8.0f;

	// Mage: cm it stays away from its target.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mob")
	float KeepDistance = 800.0f;

	// cm.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mob")
	float AggroRange = 1500.0f;

	// cm from home.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mob")
	float LeashRange = 3000.0f;

	// cm/s.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mob")
	float MoveSpeed = 450.0f;

	// Size multiplier.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mob")
	float Scale = 1.0f;

	// Skill ids the mob has: AI-usable actives it uses on its target, passives always on.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mob")
	TArray<FName> Skills;

	// Its drops: the loot table "mob_<id>" (exported from the mob's own loot).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mob")
	FName LootTableId;
};
