#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"

#include "CrossguardLockComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCrossguardLockChanged);

// A lock on a chest or door. While locked, the interact key starts lockpicking instead of opening:
// 5 s, faster with Dexterity, and it cannot fail. Needs a lockpick item or the
// Ability.Lockpicking tag (the lockpicking skill).
UCLASS(ClassGroup = (Crossguard), meta = (BlueprintSpawnableComponent))
class CROSSGUARD_API UCrossguardLockComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCrossguardLockComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Lock")
	bool IsLocked() const { return bLocked; }

	// Server.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Lock")
	void SetLocked(bool bNewLocked);

	UPROPERTY(BlueprintAssignable, Category = "Lock")
	FCrossguardLockChanged OnLockChanged;

protected:
	UFUNCTION()
	void OnRep_Locked();

	// Set per placed chest or door.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_Locked, Category = "Lock")
	bool bLocked = false;
};
