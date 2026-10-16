#include "Game/CrossguardHubMarker.h"

#include "Engine/Level.h"
#include "Engine/World.h"

ACrossguardHubMarker::ACrossguardHubMarker()
{
	// Server-side information only: the game mode reads it.
	bReplicates = false;
	PrimaryActorTick.bCanEverTick = false;
}

bool ACrossguardHubMarker::IsHubWorld(const UWorld* World)
{
	if (World == nullptr)
	{
		return false;
	}
	// The level's own actor lists, not an actor iterator: InitGame runs before the levels are
	// initialized for play (an iterator may skip a level not yet marked visible).
	for (const ULevel* Level : World->GetLevels())
	{
		if (Level == nullptr)
		{
			continue;
		}
		for (const AActor* Actor : Level->Actors)
		{
			if (IsValid(Actor) && Actor->IsA<ACrossguardHubMarker>())
			{
				return true;
			}
		}
	}
	return false;
}
