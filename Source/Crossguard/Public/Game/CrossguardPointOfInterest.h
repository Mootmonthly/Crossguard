#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "CrossguardPointOfInterest.generated.h"

class UBoxComponent;

// A point of interest in a raid map: a box players can summon extract portals inside. Place one
// per POI and scale its Area to cover it.
UCLASS()
class CROSSGUARD_API ACrossguardPointOfInterest : public AActor
{
	GENERATED_BODY()

public:
	ACrossguardPointOfInterest();

	bool Contains(const FVector& Location) const;

	// True if Location is inside any point of interest in the world.
	static bool IsInsideAny(const UWorld* World, const FVector& Location);

	// Shown to players later (map, notifications).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Point of Interest")
	FText DisplayName;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Point of Interest")
	TObjectPtr<UBoxComponent> Area;
};
