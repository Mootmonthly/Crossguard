#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/CrossguardInteractable.h"

#include "CrossguardVendorNPC.generated.h"

class UStaticMeshComponent;

// A vendor in the hub: F opens the vendor screen, where the user sells to
// UCrossguardVendorComponent (on their own player state; the NPC only says where). The server
// refuses it in a raid (between raids only). Vendors only buy for now: selling to players is not
// ruled. Placeholder look: a 1.8 m cylinder (set VendorMesh in a Blueprint subclass).
UCLASS()
class CROSSGUARD_API ACrossguardVendorNPC : public AActor, public ICrossguardInteractable
{
	GENERATED_BODY()

public:
	ACrossguardVendorNPC();

	// Server. Asks the user's vendor component to open its screen on their client.
	virtual void Interact(ACrossguardCharacter* User) override;
	virtual float GetInteractRange() const override { return InteractRange; }
	virtual FString GetInteractPrompt() const override { return FString::Printf(TEXT("F: Talk to %s"), *VendorName); }

	// Shown in the prompt and the vendor screen's title.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vendor")
	FString VendorName = TEXT("Vendor");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vendor", meta = (ClampMin = "0"))
	float InteractRange = 250.0f;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vendor")
	TObjectPtr<UStaticMeshComponent> VendorMesh;
};
