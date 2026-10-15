#pragma once

#include "CoreMinimal.h"
#include "CrossguardRules/Portal.h"
#include "GameFramework/Actor.h"

#include "CrossguardExtractPortal.generated.h"

class ACrossguardCharacter;
class UStaticMeshComponent;
class USphereComponent;

// Mirrors Crossguard::EPortalState.
UENUM(BlueprintType)
enum class ECrossguardPortalState : uint8
{
	// A beam of light shoots into the sky while it channels.
	Channeling,
	// Anyone who steps in extracts.
	Open,
	Closed,
	// The summoner's team left or died during the channel.
	Collapsed,
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCrossguardPortalStateChanged, ECrossguardPortalState, State);

// An extract portal summoned by a player (ACrossguardCharacter::TrySummonPortal). It channels for
// 60 s where it was summoned; the channel holds while a living member of the summoner's team stays
// within 10 m, and damage does not interrupt it. Then it opens for 30 s and anyone who steps in
// extracts, enemies included.
//
// Visuals are placeholders: set BeamMesh and PortalMesh (or hook OnStateChanged to effects) in a
// Blueprint subclass.
UCLASS()
class CROSSGUARD_API ACrossguardExtractPortal : public AActor
{
	GENERATED_BODY()

public:
	ACrossguardExtractPortal();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

	// Server, right after spawning. Team -1: only the summoner keeps it alive.
	void Initialize(ACrossguardCharacter* InSummoner, int32 InTeamId);

	UFUNCTION(BlueprintPure, Category = "Portal")
	ECrossguardPortalState GetState() const { return State; }

	// Seconds left in the current state, as of the last update (every TickInterval).
	UFUNCTION(BlueprintPure, Category = "Portal")
	float GetSecondsLeft() const { return SecondsLeft; }

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Portal", meta = (ClampMin = "0"))
	float ChannelSeconds = 60.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Portal", meta = (ClampMin = "0"))
	float OpenSeconds = 30.0f;

	// cm.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Portal", meta = (ClampMin = "0"))
	float KeepAliveRadius = 1000.0f;

	UPROPERTY(BlueprintAssignable, Category = "Portal")
	FCrossguardPortalStateChanged OnStateChanged;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRep_State();

	UFUNCTION()
	void OnEnterAreaBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	// Stepping in here extracts while the portal is open.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Portal")
	TObjectPtr<USphereComponent> EnterArea;

	// Shown while channeling: the beam of light that tells nearby players someone is leaving.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Portal")
	TObjectPtr<UStaticMeshComponent> BeamMesh;

	// Shown while open.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Portal")
	TObjectPtr<UStaticMeshComponent> PortalMesh;

private:
	bool IsKeeperPresent() const;
	void SetState(ECrossguardPortalState NewState);
	void ApplyVisuals();
	void TryExtract(AActor* Actor);

	UPROPERTY(ReplicatedUsing = OnRep_State)
	ECrossguardPortalState State = ECrossguardPortalState::Channeling;

	UPROPERTY(Replicated)
	float SecondsLeft = 0.0f;

	// Server only.
	TWeakObjectPtr<ACrossguardCharacter> Summoner;
	int32 TeamId = -1;
	Crossguard::FExtractPortal Timeline{Crossguard::FPortalConfig()};
};
