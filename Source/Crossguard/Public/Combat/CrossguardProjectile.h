#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayEffectTypes.h"

#include "CrossguardProjectile.generated.h"

class UProjectileMovementComponent;
class USphereComponent;

// Server-spawned, replicated projectile (arrows, staff bolts). Applies DamageSpec to the first actor
// with an ability system it hits, then is destroyed. Speed and gravity are set per Blueprint subclass
// (fast arrow, slower bolt).
UCLASS()
class CROSSGUARD_API ACrossguardProjectile : public AActor
{
	GENERATED_BODY()

public:
	ACrossguardProjectile();

	// Set by the spawner before FinishSpawning. Server only.
	FGameplayEffectSpecHandle DamageSpec;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnProjectileHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		FVector NormalImpulse, const FHitResult& Hit);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
	TObjectPtr<UProjectileMovementComponent> Movement;
};
