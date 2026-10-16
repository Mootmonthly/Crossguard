#pragma once

#include "CoreMinimal.h"
#include "Game/CrossguardPartyComponent.h"
#include "Game/CrossguardPartySubsystem.h"
#include "GameFramework/Actor.h"
#include "Interaction/CrossguardInteractable.h"

#include "CrossguardRaidPortal.generated.h"

class UStaticMeshComponent;

// The hub's way into a raid (docs/hub-and-menu.md): F at the portal launches the raid for the
// user's party, exactly as the party window's "Start raid" (UCrossguardPartySubsystem::StartRaid:
// the leader, or a player in no party who goes alone; hub only; a match server from matchmaking, or
// in development the whole server to RaidMapName). Anyone else is refused with a HUD message ("Only your party's leader can start
// the raid."). Server-authoritative: the portal decides nothing itself. Placeholder look: a flat
// glowing disc (set PortalMesh in a Blueprint subclass).
UCLASS()
class CROSSGUARD_API ACrossguardRaidPortal : public AActor, public ICrossguardInteractable
{
	GENERATED_BODY()

public:
	ACrossguardRaidPortal();

	// Server. Launches the user's raid (Launch); a refusal goes back to the user.
	virtual void Interact(ACrossguardCharacter* User) override;
	virtual float GetInteractRange() const override { return InteractRange; }
	virtual FString GetInteractPrompt() const override { return TEXT("F: Enter the raid"); }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Raid Portal", meta = (ClampMin = "0"))
	float InteractRange = 300.0f;

	// Server. What Interact does: StartRaid for the user's player state (or, with bPlanOnly, only
	// PlanRaidTravel). Returns the result; the last one is kept for GetLastResult / GetLastUrl.
	ECrossguardPartyResult Launch(ACrossguardCharacter* User);

	// Tests: Interact plans the travel (PlanRaidTravel) instead of travelling. Never set in a game.
	bool bPlanOnly = false;

	ECrossguardPartyResult GetLastResult() const { return LastResult; }
	ECrossguardRaidTravel GetLastMode() const { return LastMode; }
	// With bPlanOnly: the URL the launch would travel to.
	const FString& GetLastUrl() const { return LastUrl; }

	// The HUD line for a refusal.
	static FString RefusalMessage(ECrossguardPartyResult Result);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Raid Portal")
	TObjectPtr<UStaticMeshComponent> PortalMesh;

private:
	ECrossguardPartyResult LastResult = ECrossguardPartyResult::None;
	ECrossguardRaidTravel LastMode = ECrossguardRaidTravel::NoDestination;
	FString LastUrl;
};
