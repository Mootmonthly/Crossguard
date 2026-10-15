#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "CrossguardMobSpawner.generated.h"

class ACrossguardMonster;
class UBillboardComponent;

// One mob a spawner can pick, by weight.
USTRUCT(BlueprintType)
struct CROSSGUARD_API FCrossguardMobSpawnOption
{
	GENERATED_BODY()

	// A row of the game mode's Mob Table (Data/Mobs/Mobs.json, the Mob Forge).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawner")
	FName MobId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawner", meta = (ClampMin = "0"))
	int32 Weight = 1;
};

// Spawns monsters when a raid starts: between CountMin and CountMax of them, each a weighted pick
// of Mobs, scattered within Radius on the ground (each guards the spot it spawned at). With
// RespawnSeconds above 0, a monster that died is replaced that long after (its body stays). Outside
// raids (game mode bIsRaid off) it spawns nothing, like loot chests.
UCLASS()
class CROSSGUARD_API ACrossguardMobSpawner : public AActor
{
	GENERATED_BODY()

public:
	ACrossguardMobSpawner();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawner")
	TArray<FCrossguardMobSpawnOption> Mobs;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawner", meta = (ClampMin = "0"))
	int32 CountMin = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawner", meta = (ClampMin = "0"))
	int32 CountMax = 1;

	// cm around the spawner.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawner", meta = (ClampMin = "0"))
	float Radius = 500.0f;

	// Seconds after a death before a replacement spawns. 0: never (the default: a raid's monsters
	// are a one-time supply).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawner", meta = (ClampMin = "0"))
	float RespawnSeconds = 0.0f;

	// Set a Blueprint subclass for a mesh.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawner")
	TSubclassOf<ACrossguardMonster> MonsterClass;

	virtual void BeginPlay() override;

private:
	// Server. One weighted pick at a random spot. False if there was no room.
	bool SpawnOne();
	FName PickMobId() const;
	// Server, every few seconds while respawning is on: schedules replacements for the dead.
	void CheckDead();

	UPROPERTY(VisibleAnywhere, Category = "Spawner")
	TObjectPtr<UBillboardComponent> Icon;

	TArray<TWeakObjectPtr<ACrossguardMonster>> Spawned;
	FTimerHandle CheckTimer;
};
