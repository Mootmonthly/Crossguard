#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CrossguardTeamLibrary.generated.h"

// Team lookups for any actor: a pawn, controller or player state. Teams decide who counts as an ally
// (party skills such as Multiscale). They never block damage: friendly fire is always on.
UCLASS()
class CROSSGUARD_API UCrossguardTeamLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// -1 if the actor has no team.
	UFUNCTION(BlueprintPure, Category = "Crossguard|Team")
	static int32 GetTeamId(const AActor* Actor);

	// True if both actors have the same, assigned team. An actor is its own ally.
	UFUNCTION(BlueprintPure, Category = "Crossguard|Team")
	static bool AreAllies(const AActor* A, const AActor* B);
};
