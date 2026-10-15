#include "Game/CrossguardPointOfInterest.h"

#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

ACrossguardPointOfInterest::ACrossguardPointOfInterest()
{
	Area = CreateDefaultSubobject<UBoxComponent>(TEXT("Area"));
	Area->SetBoxExtent(FVector(2000.0f, 2000.0f, 500.0f));
	// Only a marked region: nothing collides with or overlaps it.
	Area->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetRootComponent(Area);
}

bool ACrossguardPointOfInterest::Contains(const FVector& Location) const
{
	// In the box's own space the unscaled extent bounds it, whatever its rotation and scale.
	const FVector Local = Area->GetComponentTransform().InverseTransformPosition(Location);
	const FVector Extent = Area->GetUnscaledBoxExtent();
	return FMath::Abs(Local.X) <= Extent.X && FMath::Abs(Local.Y) <= Extent.Y && FMath::Abs(Local.Z) <= Extent.Z;
}

bool ACrossguardPointOfInterest::IsInsideAny(const UWorld* World, const FVector& Location)
{
	if (World == nullptr)
	{
		return false;
	}
	for (TActorIterator<ACrossguardPointOfInterest> It(World); It; ++It)
	{
		if (It->Contains(Location))
		{
			return true;
		}
	}
	return false;
}
