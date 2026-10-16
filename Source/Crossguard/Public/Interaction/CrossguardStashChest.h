#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/CrossguardInteractable.h"

#include "CrossguardStashChest.generated.h"

class UStaticMeshComponent;

// The stash in the hub: F opens the user's own stash screen (UCrossguardStashComponent on their
// player state; each character sees their own). The server refuses it in a raid (between raids
// only), so placing one on a raid map does nothing but say so. Placeholder look: a box (set
// ChestMesh in a Blueprint subclass).
UCLASS()
class CROSSGUARD_API ACrossguardStashChest : public AActor, public ICrossguardInteractable
{
	GENERATED_BODY()

public:
	ACrossguardStashChest();

	// Server. Asks the user's stash to open its screen on their client.
	virtual void Interact(ACrossguardCharacter* User) override;
	virtual float GetInteractRange() const override { return InteractRange; }
	virtual FString GetInteractPrompt() const override { return TEXT("F: Open stash"); }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stash", meta = (ClampMin = "0"))
	float InteractRange = 250.0f;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stash")
	TObjectPtr<UStaticMeshComponent> ChestMesh;
};
