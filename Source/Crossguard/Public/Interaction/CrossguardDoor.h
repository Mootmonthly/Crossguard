#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/CrossguardInteractable.h"

#include "CrossguardDoor.generated.h"

class UCrossguardLockComponent;
class UStaticMeshComponent;

// A door that the interact key opens and closes, swinging on its root (put the hinge there).
// Tick its lock component's bLocked for a door that must be lockpicked first.
UCLASS()
class CROSSGUARD_API ACrossguardDoor : public AActor, public ICrossguardInteractable
{
	GENERATED_BODY()

public:
	ACrossguardDoor();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual void Interact(ACrossguardCharacter* User) override;
	virtual float GetInteractRange() const override { return InteractRange; }

	UFUNCTION(BlueprintPure, Category = "Door")
	bool IsOpen() const { return bOpen; }

	// Degrees the door swings around its root when open.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door")
	float OpenYaw = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door", meta = (ClampMin = "0"))
	float InteractRange = 250.0f;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRep_Open();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<UStaticMeshComponent> DoorMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<UCrossguardLockComponent> Lock;

private:
	void ApplyOpen();

	UPROPERTY(ReplicatedUsing = OnRep_Open)
	bool bOpen = false;

	FRotator ClosedRotation;
};
