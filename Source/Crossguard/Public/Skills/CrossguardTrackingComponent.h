#pragma once

#include "Components/InstancedStaticMeshComponent.h"
#include "CoreMinimal.h"

#include "CrossguardTrackingComponent.generated.h"

// [Hunter's Tracking]'s footprints, on the tracker's own screen only. While the locally controlled
// owner has Passive.HuntersTracking, it records where enemy players walk (a print every StepLength,
// alternating feet) and shows the last TrailSeconds of them as small flat markers. Only enemies this
// client knows about leave prints (network relevancy, roughly 150 m); stealthed enemies leave none.
// Placeholder look: flattened engine cylinders.
UCLASS(ClassGroup = (Crossguard), meta = (BlueprintSpawnableComponent))
class CROSSGUARD_API UCrossguardTrackingComponent : public UInstancedStaticMeshComponent
{
	GENERATED_BODY()

public:
	UCrossguardTrackingComponent();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tracking", meta = (ClampMin = "1"))
	float TrailSeconds = 30.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tracking", meta = (ClampMin = "10"))
	float StepLength = 80.0f;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	struct FFootprint
	{
		FVector Location;
		float Yaw = 0.0f;
		double Time = 0.0;
	};

	struct FTrail
	{
		FVector LastStep = FVector::ZeroVector;
		bool bLeftFoot = false;
	};

	bool IsTracking() const;
	void RecordSteps(double Now);
	void Rebuild();

	TArray<FFootprint> Footprints;
	TMap<TWeakObjectPtr<AActor>, FTrail> Trails;
};
