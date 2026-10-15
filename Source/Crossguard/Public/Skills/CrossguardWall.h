#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "CrossguardWall.generated.h"

class UStaticMeshComponent;

// An earthen wall ([Wall], [Parting the Land]): blocks everything, cannot be destroyed, gone after
// its life span. Placeholder look: a box. Server-spawned, replicated.
UCLASS()
class CROSSGUARD_API ACrossguardWall : public AActor
{
	GENERATED_BODY()

public:
	ACrossguardWall();

	// Server, before or after spawning: width (X, along the wall), thickness (Y), height (Z) in cm.
	// The wall stands on its origin.
	void SetWallSize(const FVector& InSize);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wall")
	TObjectPtr<UStaticMeshComponent> Mesh;

private:
	UPROPERTY(ReplicatedUsing = OnRep_WallSize)
	FVector WallSize = FVector(400.0f, 50.0f, 300.0f);

	UFUNCTION()
	void OnRep_WallSize();
	void ApplyWallSize();
};
