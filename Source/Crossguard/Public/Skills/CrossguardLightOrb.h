#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "CrossguardLightOrb.generated.h"

class UPointLightComponent;

// [Light]'s ball of light: attached above its caster's head, gone after its life span (120 s).
// Server-spawned, replicated (the attachment replicates, so everyone sees it).
UCLASS()
class CROSSGUARD_API ACrossguardLightOrb : public AActor
{
	GENERATED_BODY()

public:
	ACrossguardLightOrb();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Light")
	TObjectPtr<UPointLightComponent> Light;
};
