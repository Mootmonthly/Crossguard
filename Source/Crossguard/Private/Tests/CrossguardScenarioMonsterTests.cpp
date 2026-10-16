// Scenario tests: monsters and spawners (docs/next-build-checklist.md section 4). Framework:
// CrossguardScenario.h, docs/editor-tests.md.

#include "Tests/CrossguardScenario.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Character/CrossguardMobRow.h"
#include "Character/CrossguardMonster.h"
#include "Engine/DataTable.h"
#include "Game/CrossguardGameMode.h"
#include "Game/CrossguardMobSpawner.h"

namespace CrossguardScenarioMonsters
{
	const FName SkeletonSoldier(TEXT("skeleton_soldier"));
	constexpr int32 SpawnCount = 3;
	// 40 m from the origin: more than 15 m (aggro range) from every other scenario and the PIE player.
	const FVector SpawnerLocation(4000.0f, 0.0f, 50.0f);
	constexpr float SpawnerRadius = 600.0f;

	TArray<ACrossguardMonster*> SpawnedBy(CrossguardScenario::FScenarioContext& C, const AActor* Spawner)
	{
		// Not by owner: possession makes the AI controller the owner. By the spot each guards (where it
		// spawned), which stays inside the spawner's circle after it chases or dies.
		TArray<ACrossguardMonster*> Result;
		const ACrossguardMobSpawner* MobSpawner = Cast<ACrossguardMobSpawner>(Spawner);
		if (MobSpawner == nullptr)
		{
			return Result;
		}
		for (ACrossguardMonster* Monster : C.FindAll<ACrossguardMonster>())
		{
			if (FVector::Dist2D(Monster->GetHomeLocation(), MobSpawner->GetActorLocation()) <= MobSpawner->Radius + 1.0f)
			{
				Result.Add(Monster);
			}
		}
		return Result;
	}
}

// Checklist: "Place a CrossguardMobSpawner ... Mobs skeleton_soldier, Count 3 -> on Play, skeletons
// appear around it" with the mob's stats from DT_Mobs.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioMobSpawner, "Crossguard.Scenario.Monsters.MobSpawner", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioMobSpawner::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioMonsters;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A spawner of 3 skeleton soldiers begins play"), [](FScenarioContext& C)
	{
		const ACrossguardGameMode* Mode = C.GameMode();
		if (!C.Require(Mode != nullptr, TEXT("a Crossguard game mode")))
		{
			return;
		}
		if (Mode->MobTable == nullptr)
		{
			C.Skip(TEXT("the game mode's Mob Table is unset (import Data/Mobs/MobTable.json as DT_Mobs and set it on BP_CrossguardGameMode, Crossguard | Mobs)"));
			return;
		}
		const FCrossguardMobRow* Row = Mode->MobTable->FindRow<FCrossguardMobRow>(SkeletonSoldier, TEXT("Scenario"), false);
		if (!C.Require(Row != nullptr, FString::Printf(TEXT("%s has a skeleton_soldier row (reimport it from Data/Mobs/MobTable.json)"),
				*Mode->MobTable->GetName())))
		{
			return;
		}
		C.SetNumber(TEXT("MobHealth"), Row->Health);

		// The spawner only spawns in raids.
		C.SetRaid(true);
		ACrossguardMobSpawner* Spawner = C.SpawnActor<ACrossguardMobSpawner>(TEXT("Spawner"), SpawnerLocation, 0.0f, [](ACrossguardMobSpawner& Spawned)
		{
			FCrossguardMobSpawnOption Option;
			Option.MobId = SkeletonSoldier;
			Option.Weight = 1;
			Spawned.Mobs = {Option};
			Spawned.CountMin = SpawnCount;
			Spawned.CountMax = SpawnCount;
			Spawned.Radius = SpawnerRadius;
			Spawned.RespawnSeconds = 0.0f;
		});
		C.Require(Spawner != nullptr, TEXT("the spawner spawned"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("3 skeleton soldiers with the mob's Health"), [](FScenarioContext& C)
	{
		const AActor* Spawner = C.FindActor(TEXT("Spawner"));
		if (!C.Require(Spawner != nullptr, TEXT("the spawner still exists")))
		{
			return;
		}
		const TArray<ACrossguardMonster*> Monsters = SpawnedBy(C, Spawner);
		C.Test.TestEqual(TEXT("Monsters spawned by the spawner"), Monsters.Num(), SpawnCount);

		const float MobHealth = static_cast<float>(C.Number(TEXT("MobHealth")));
		for (const ACrossguardMonster* Monster : Monsters)
		{
			const FString MonsterName = Monster->GetName();
			C.Test.TestTrue(*FString::Printf(TEXT("%s MobId is skeleton_soldier (is %s)"), *MonsterName, *Monster->MobId.ToString()),
				Monster->MobId == SkeletonSoldier);
			C.Test.TestEqual(*FString::Printf(TEXT("%s Max Health is the mob's Health"), *MonsterName), C.MaxHealth(Monster), MobHealth, 0.01f);
			C.Test.TestEqual(*FString::Printf(TEXT("%s is at full Health"), *MonsterName), C.Health(Monster), MobHealth, 0.01f);
			C.Test.TestTrue(*FString::Printf(TEXT("%s has a controller"), *MonsterName), Monster->GetController() != nullptr);
			C.Test.TestTrue(*FString::Printf(TEXT("%s is within the spawner's radius"), *MonsterName),
				FVector::Dist2D(Monster->GetActorLocation(), Spawner->GetActorLocation()) <= SpawnerRadius + 1.0f);
			C.Test.TestTrue(*FString::Printf(TEXT("%s stands on the floor, not under it"), *MonsterName), Monster->GetActorLocation().Z > 0.0f);
		}
	});

	Scenario.Run();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
