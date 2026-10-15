#include "Game/CrossguardMobSpawner.h"

#include "Character/CrossguardMonster.h"
#include "Components/BillboardComponent.h"
#include "Crossguard.h"
#include "Engine/World.h"
#include "Game/CrossguardGameMode.h"
#include "TimerManager.h"

namespace
{
	constexpr float SpawnerCheckSeconds = 2.0f;
	// Tries per monster for a spot with room.
	constexpr int32 SpawnerTries = 5;
}

ACrossguardMobSpawner::ACrossguardMobSpawner()
{
	// Server-only bookkeeping; the monsters replicate themselves.
	bReplicates = false;
	MonsterClass = ACrossguardMonster::StaticClass();
	Icon = CreateDefaultSubobject<UBillboardComponent>(TEXT("Icon"));
	Icon->bIsEditorOnly = true;
	RootComponent = Icon;
}

void ACrossguardMobSpawner::BeginPlay()
{
	Super::BeginPlay();
	const ACrossguardGameMode* GameMode = GetWorld()->GetAuthGameMode<ACrossguardGameMode>();
	if (!HasAuthority() || GameMode == nullptr || !GameMode->bIsRaid || MonsterClass == nullptr)
	{
		return;
	}
	const int32 Count = FMath::RandRange(FMath::Min(CountMin, CountMax), FMath::Max(CountMin, CountMax));
	for (int32 Index = 0; Index < Count; ++Index)
	{
		SpawnOne();
	}
	if (RespawnSeconds > 0.0f)
	{
		GetWorldTimerManager().SetTimer(CheckTimer, FTimerDelegate::CreateUObject(this, &ACrossguardMobSpawner::CheckDead),
			SpawnerCheckSeconds, true);
	}
}

FName ACrossguardMobSpawner::PickMobId() const
{
	int32 Total = 0;
	for (const FCrossguardMobSpawnOption& Option : Mobs)
	{
		Total += FMath::Max(Option.Weight, 0);
	}
	if (Total <= 0)
	{
		return NAME_None;
	}
	int32 Roll = FMath::RandRange(0, Total - 1);
	for (const FCrossguardMobSpawnOption& Option : Mobs)
	{
		Roll -= FMath::Max(Option.Weight, 0);
		if (Roll < 0)
		{
			return Option.MobId;
		}
	}
	return NAME_None;
}

bool ACrossguardMobSpawner::SpawnOne()
{
	const FName MobId = PickMobId();
	if (MobId.IsNone())
	{
		UE_LOG(LogCrossguard, Warning, TEXT("%s: no mobs to spawn (add Mobs with a weight)"), *GetName());
		return false;
	}
	UWorld* World = GetWorld();
	for (int32 Try = 0; Try < SpawnerTries; ++Try)
	{
		// A random spot in the circle, dropped onto the ground below (world geometry only).
		const FVector2D Offset = FMath::RandPointInCircle(Radius);
		const FVector Point = GetActorLocation() + FVector(Offset.X, Offset.Y, 0.0f);
		FHitResult Ground;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CrossguardMobSpawner), false, this);
		if (!World->LineTraceSingleByObjectType(Ground, Point + FVector(0.0f, 0.0f, 300.0f), Point - FVector(0.0f, 0.0f, 1000.0f),
				FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			continue;
		}
		const FRotator Facing(0.0f, FMath::FRandRange(0.0f, 360.0f), 0.0f);
		// Capsule half height above the ground; nudged out of walls and others, or skipped.
		const float HalfHeight = MonsterClass->GetDefaultObject<ACrossguardMonster>()->GetDefaultHalfHeight();
		const FTransform Transform(Facing, Ground.ImpactPoint + FVector(0.0f, 0.0f, HalfHeight + 5.0f));
		ACrossguardMonster* Monster = World->SpawnActorDeferred<ACrossguardMonster>(MonsterClass, Transform, this, nullptr,
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding);
		if (Monster == nullptr)
		{
			continue;
		}
		// Its stats, skills and loot come from the Mob Table when it begins play.
		Monster->MobId = MobId;
		Monster->FinishSpawning(Transform);
		if (IsValid(Monster))
		{
			Spawned.Add(Monster);
			return true;
		}
	}
	UE_LOG(LogCrossguard, Warning, TEXT("%s: no room to spawn %s"), *GetName(), *MobId.ToString());
	return false;
}

void ACrossguardMobSpawner::CheckDead()
{
	for (int32 Index = Spawned.Num() - 1; Index >= 0; --Index)
	{
		const ACrossguardMonster* Monster = Spawned[Index].Get();
		if (Monster != nullptr && Monster->IsAlive())
		{
			continue;
		}
		// Dead (its body stays) or gone: a replacement after RespawnSeconds.
		Spawned.RemoveAt(Index);
		FTimerHandle Respawn;
		GetWorldTimerManager().SetTimer(Respawn, FTimerDelegate::CreateWeakLambda(this, [this]() { SpawnOne(); }), RespawnSeconds, false);
	}
}
