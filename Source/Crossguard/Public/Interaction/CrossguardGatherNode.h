#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/CrossguardInteractable.h"

#include "CrossguardGatherNode.generated.h"

class ACrossguardCharacter;
class UAbilitySystemComponent;
class UGameplayAbility;
class UStaticMeshComponent;
struct FOnAttributeChangeData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCrossguardGatherNodeChanged);

// A gathering node in a raid map: an herb patch or an ore vein (docs/design-decisions.md,
// Gathering). The interact key (F) starts a channel of GatherSeconds; moving, taking damage,
// attacking or using a skill, dying or leaving the interact range breaks it (Crossguard::
// CheckGatherInterrupt), and pressing F at the node again stops it. When the channel completes,
// LootTableId (Data/Loot/LootTables.json, through the game mode's Loot) is rolled into the
// gatherer's inventory (whatever does not fit drops on the ground at the node), the gatherer earns
// GatherNode XP (10, held until extraction in a raid), and the node is depleted: its mesh hides
// until RespawnSeconds pass (0: once per raid).
//
// Server authoritative. One gatherer at a time. Visuals are placeholders: set NodeMesh's mesh in a
// Blueprint subclass (BP_HerbNode, BP_OreNode) or hook OnGatherNodeChanged to effects.
UCLASS()
class CROSSGUARD_API ACrossguardGatherNode : public AActor, public ICrossguardInteractable
{
	GENERATED_BODY()

public:
	ACrossguardGatherNode();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// Starts gathering; the gatherer pressing F again stops it.
	virtual void Interact(ACrossguardCharacter* User) override;
	virtual float GetInteractRange() const override { return InteractRange; }

	UFUNCTION(BlueprintPure, Category = "Gathering")
	bool IsDepleted() const { return bDepleted; }

	// The character channeling at it; null if nobody.
	UFUNCTION(BlueprintPure, Category = "Gathering")
	ACrossguardCharacter* GetGatherer() const { return Gatherer; }

	UFUNCTION(BlueprintPure, Category = "Gathering")
	bool IsBeingGathered() const { return Gatherer != nullptr; }

	// 0..1 for a progress bar while someone gathers; 0 otherwise. Uses the server's start time
	// (replicated) against this machine's game time, so it is approximate on clients.
	UFUNCTION(BlueprintPure, Category = "Gathering")
	float GetGatherProgress() const;

	// Server. Stops a channel in progress without gathering (logs Reason).
	void CancelGather(const TCHAR* Reason);

	// What gathering yields: a loot table id, e.g. gather_herb or gather_ore.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gathering")
	FName LootTableId;

	// Seconds the gatherer channels. Placeholder (Crossguard::FGatherConfig).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gathering", meta = (ClampMin = "0"))
	float GatherSeconds = 2.0f;

	// Seconds after depletion before it can be gathered again. 0: once per raid. Placeholder.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gathering", meta = (ClampMin = "0"))
	float RespawnSeconds = 0.0f;

	// cm the gatherer may drift from where the channel started before it breaks. Placeholder.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gathering", meta = (ClampMin = "0"))
	float MoveTolerance = 30.0f;

	// How close a player must be to start gathering and stay within while channeling, in cm.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gathering", meta = (ClampMin = "0"))
	float InteractRange = 250.0f;

	// Server and clients: depleted, respawned, a channel started or stopped.
	UPROPERTY(BlueprintAssignable, Category = "Gathering")
	FCrossguardGatherNodeChanged OnGatherNodeChanged;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRep_Depleted();

	UFUNCTION()
	void OnRep_Gatherer();

	// Hidden while depleted. Placeholder look: a small sphere.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gathering")
	TObjectPtr<UStaticMeshComponent> NodeMesh;

private:
	void StartGather(ACrossguardCharacter* User);
	// Checks the channel every PollSeconds; completes it once GatherSeconds have passed.
	void PollGather();
	void CompleteGather();
	// Clears the channel and unbinds from the gatherer.
	void StopGather();
	void Deplete();
	void Respawn();
	void ApplyDepleted();

	void OnGathererAbilityActivated(UGameplayAbility* Ability);
	void OnGathererHealthChanged(const FOnAttributeChangeData& Data);

	UPROPERTY(ReplicatedUsing = OnRep_Depleted)
	bool bDepleted = false;

	UPROPERTY(ReplicatedUsing = OnRep_Gatherer)
	TObjectPtr<ACrossguardCharacter> Gatherer;

	// Server game time the channel started.
	UPROPERTY(Replicated)
	float GatherStartTime = 0.0f;

	// Server only: the channel in progress.
	FVector GatherStartLocation = FVector::ZeroVector;
	double PendingHealthLost = 0.0;
	bool bGathererAttacked = false;
	TWeakObjectPtr<UAbilitySystemComponent> BoundAbilitySystem;
	FDelegateHandle ActivatedHandle;
	FDelegateHandle HealthHandle;
	FTimerHandle PollTimer;
	FTimerHandle RespawnTimer;
	float DepletedTime = 0.0f;
	// NodeMesh's collision as set up (Blueprint), restored when the node respawns.
	TEnumAsByte<ECollisionEnabled::Type> MeshCollision = ECollisionEnabled::QueryAndPhysics;
};
