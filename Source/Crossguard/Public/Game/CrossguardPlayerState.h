#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "GenericTeamAgentInterface.h"

#include "CrossguardPlayerState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCrossguardTeamChanged, int32, NewTeamId);

// Per-player match state. Holds the player's team, assigned by ACrossguardGameMode on login.
UCLASS()
class CROSSGUARD_API ACrossguardPlayerState : public APlayerState, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// -1 until assigned.
	UFUNCTION(BlueprintPure, Category = "Crossguard|Team")
	int32 GetTeamId() const { return TeamId; }

	// Server only.
	void SetTeamId(int32 NewTeamId);

	// IGenericTeamAgentInterface, for AI perception. Team ids above 254 report NoTeam.
	virtual FGenericTeamId GetGenericTeamId() const override;

	UPROPERTY(BlueprintAssignable, Category = "Crossguard|Team")
	FCrossguardTeamChanged OnTeamChanged;

protected:
	UFUNCTION()
	void OnRep_TeamId();

private:
	UPROPERTY(ReplicatedUsing = OnRep_TeamId)
	int32 TeamId = -1;
};
