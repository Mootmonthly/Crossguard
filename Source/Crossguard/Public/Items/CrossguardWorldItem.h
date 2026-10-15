#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/CrossguardInteractable.h"
#include "Items/CrossguardInventoryComponent.h"

#include "CrossguardWorldItem.generated.h"

class UStaticMeshComponent;

// One stack lying in the world, dropped from an inventory (UCrossguardInventoryComponent::
// RequestDropItem). The interact key picks it up: whatever fits goes into the inventory, the rest
// stays on the ground. Stays until picked up or the raid ends. Placeholder look: a small box.
// Server-spawned, replicated.
UCLASS()
class CROSSGUARD_API ACrossguardWorldItem : public AActor, public ICrossguardInteractable
{
	GENERATED_BODY()

public:
	ACrossguardWorldItem();

	// Server, before FinishSpawning or right after spawning.
	void SetItem(const FCrossguardInventoryItem& InItem) { Item = InItem; }

	// What lies here (for a name plate or tooltip).
	UFUNCTION(BlueprintPure, Category = "Item")
	const FCrossguardInventoryItem& GetItem() const { return Item; }

	virtual void Interact(ACrossguardCharacter* User) override;
	virtual float GetInteractRange() const override { return 200.0f; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Item")
	TObjectPtr<UStaticMeshComponent> Mesh;

private:
	UPROPERTY(Replicated)
	FCrossguardInventoryItem Item;
};
