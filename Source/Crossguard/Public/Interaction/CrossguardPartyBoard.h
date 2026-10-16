#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/CrossguardInteractable.h"

#include "CrossguardPartyBoard.generated.h"

class UStaticMeshComponent;

// The hub's party board (placeholder, docs/hub-and-menu.md): F opens the user's party window
// (ACrossguardPlayerController::ClientOpenPartyPanel; the same window as the P key). It only opens
// UI: every party action is still a request the server checks. Placeholder look: a 2 m x 1.5 m
// board on a post (set BoardMesh in a Blueprint subclass).
UCLASS()
class CROSSGUARD_API ACrossguardPartyBoard : public AActor, public ICrossguardInteractable
{
	GENERATED_BODY()

public:
	ACrossguardPartyBoard();

	// Server. Asks the user's client to open the party window.
	virtual void Interact(ACrossguardCharacter* User) override;
	virtual float GetInteractRange() const override { return InteractRange; }
	virtual FString GetInteractPrompt() const override { return TEXT("F: Party board"); }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Party Board", meta = (ClampMin = "0"))
	float InteractRange = 250.0f;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Party Board")
	TObjectPtr<UStaticMeshComponent> BoardMesh;
};
