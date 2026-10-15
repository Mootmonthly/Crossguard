#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "CrossguardExtractionZone.generated.h"

class ACrossguardCharacter;
class UBoxComponent;

// Place in a raid map. A living player who stays inside for ExtractSeconds extracts: the game
// mode ends their raid (raid XP plus 35 is banked, carried gold coins become banked gold).
//
// Not built yet: leaving the raid map for the hub afterwards (needs the hub and saved
// progression). Until then the extracted character stays in the map, between raids.
UCLASS()
class CROSSGUARD_API ACrossguardExtractionZone : public AActor
{
	GENERATED_BODY()

public:
	ACrossguardExtractionZone();

	// Placeholder.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Extraction", meta = (ClampMin = "0"))
	float ExtractSeconds = 5.0f;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Extraction")
	TObjectPtr<UBoxComponent> Area;

	UFUNCTION()
	void OnAreaBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnAreaEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

private:
	void Extract(TWeakObjectPtr<ACrossguardCharacter> Character);

	// Server only: one countdown per character inside.
	TMap<TWeakObjectPtr<ACrossguardCharacter>, FTimerHandle> Countdowns;
};
