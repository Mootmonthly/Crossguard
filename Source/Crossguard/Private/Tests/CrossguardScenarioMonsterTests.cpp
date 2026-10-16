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

// Checklist: "Mob spawner: soldiers and archers, 3–5". Statistically, with a fixed random seed: 24
// spawners of skeleton_soldier (weight 3) and skeleton_archer (weight 1), Count 3–5, each spawn
// 3, 4 or 5 (all three counts seen), both mobs appear, archers about a quarter of the ~96.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioSpawnerMixAndCount, "Crossguard.Scenario.Monsters.SpawnerMixAndCount", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioSpawnerMixAndCount::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioMonsters;

	static constexpr int32 SpawnerCount = 24;
	static constexpr int32 Columns = 6;
	static constexpr float Spacing = 1500.0f;
	static constexpr int32 SoldierWeight = 3;
	static constexpr int32 ArcherWeight = 1;
	static const FName SkeletonArcher(TEXT("skeleton_archer"));
	FScenario Scenario(*this);

	Scenario.Step(TEXT("24 spawners of soldiers (3) and archers (1), 3-5 each, seeded"), [](FScenarioContext& C)
	{
		const ACrossguardGameMode* Mode = C.GameMode();
		if (!C.Require(Mode != nullptr, TEXT("a Crossguard game mode")))
		{
			return;
		}
		if (Mode->MobTable == nullptr || Mode->MobTable->FindRow<FCrossguardMobRow>(SkeletonSoldier, TEXT("Scenario"), false) == nullptr
			|| Mode->MobTable->FindRow<FCrossguardMobRow>(SkeletonArcher, TEXT("Scenario"), false) == nullptr)
		{
			C.Skip(TEXT("the game mode's Mob Table needs skeleton_soldier and skeleton_archer rows (DT_Mobs from Data/Mobs/MobTable.json)"));
			return;
		}
		C.SetRaid(true);
		// The spawners pick with FMath::RandRange: one seed, one outcome.
		FMath::RandInit(20261016);
		for (int32 Index = 0; Index < SpawnerCount; ++Index)
		{
			// A grid north of the origin, away from the PIE player at (-30 m, -30 m).
			const FVector Location(-4000.0f + Spacing * (Index % Columns), 1000.0f + Spacing * (Index / Columns), 50.0f);
			ACrossguardMobSpawner* Spawner = C.SpawnActor<ACrossguardMobSpawner>(FName(*FString::Printf(TEXT("Spawner%d"), Index)), Location, 0.0f,
				[](ACrossguardMobSpawner& Spawned)
				{
					FCrossguardMobSpawnOption Soldier;
					Soldier.MobId = SkeletonSoldier;
					Soldier.Weight = SoldierWeight;
					FCrossguardMobSpawnOption Archer;
					Archer.MobId = SkeletonArcher;
					Archer.Weight = ArcherWeight;
					Spawned.Mobs = {Soldier, Archer};
					Spawned.CountMin = 3;
					Spawned.CountMax = 5;
					Spawned.Radius = 300.0f;
					Spawned.RespawnSeconds = 0.0f;
				});
			if (!C.Require(Spawner != nullptr, TEXT("spawner spawned")))
			{
				return;
			}
		}
	});

	Scenario.Wait(1.0f);

	Scenario.Step(TEXT("Each spawned 3 to 5; soldiers and archers about 3:1"), [](FScenarioContext& C)
	{
		int32 Total = 0;
		int32 Archers = 0;
		int32 Soldiers = 0;
		TSet<int32> CountsSeen;
		for (int32 Index = 0; Index < SpawnerCount; ++Index)
		{
			const AActor* Spawner = C.FindActor(FName(*FString::Printf(TEXT("Spawner%d"), Index)));
			if (!C.Require(Spawner != nullptr, TEXT("the spawner still exists")))
			{
				return;
			}
			const TArray<ACrossguardMonster*> Monsters = SpawnedBy(C, Spawner);
			C.Test.TestTrue(*FString::Printf(TEXT("Spawner %d spawned 3 to 5 (spawned %d)"), Index, Monsters.Num()), Monsters.Num() >= 3 && Monsters.Num() <= 5);
			CountsSeen.Add(Monsters.Num());
			for (const ACrossguardMonster* Monster : Monsters)
			{
				++Total;
				Soldiers += Monster->MobId == SkeletonSoldier ? 1 : 0;
				Archers += Monster->MobId == SkeletonArcher ? 1 : 0;
			}
		}
		const float ArcherShare = Total > 0 ? static_cast<float>(Archers) / static_cast<float>(Total) : 0.0f;
		C.Test.AddInfo(FString::Printf(TEXT("%d monsters from %d spawners: %d soldiers, %d archers (%.0f%% archers; weights 3:1 expect 25%%)"),
			Total, SpawnerCount, Soldiers, Archers, ArcherShare * 100.0f));
		C.Test.TestEqual(TEXT("Every monster is a soldier or an archer"), Soldiers + Archers, Total);
		C.Test.TestTrue(TEXT("Counts of 3, 4 and 5 all occur over 24 spawners"), CountsSeen.Contains(3) && CountsSeen.Contains(4) && CountsSeen.Contains(5));
		C.Test.TestTrue(TEXT("Archers are between 12% and 40% of the spawns (3:1 weights)"), ArcherShare >= 0.12f && ArcherShare <= 0.40f);
	});

	Scenario.Run();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
