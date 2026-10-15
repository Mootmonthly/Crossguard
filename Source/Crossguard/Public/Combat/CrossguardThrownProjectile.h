#pragma once

#include "Combat/CrossguardProjectile.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "CrossguardThrownProjectile.generated.h"

class UStaticMeshComponent;

// Damage over time left on the ground by an explosive bottle: applies DamageSpec to everyone within
// Radius once per second for Seconds, friendly fire and the thrower included.
UCLASS()
class CROSSGUARD_API ACrossguardDamageArea : public AActor
{
	GENERATED_BODY()

public:
	ACrossguardDamageArea();

	// Server, right after spawning. DamageSpec is one second's damage.
	void Initialize(const FGameplayEffectSpecHandle& InDamageSpec, float InRadius, float InSeconds);

protected:
	// Placeholder visual: give it a mesh or effect in a Blueprint subclass.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Damage Area")
	TObjectPtr<UStaticMeshComponent> AreaMesh;

private:
	void Pulse();

	FGameplayEffectSpecHandle DamageSpec;
	float Radius = 0.0f;
	int32 PulsesLeft = 0;
	FTimerHandle PulseTimer;
};

// A thrown item (throwing knife, throwing axe, explosive bottle): an arcing projectile. With
// ExplosionRadius 0 it damages what it hits; otherwise it explodes where it lands, damaging
// everyone within the radius (friendly fire and the thrower included), and leaves an
// ACrossguardDamageArea when DotSeconds > 0.
UCLASS()
class CROSSGUARD_API ACrossguardThrownProjectile : public ACrossguardProjectile
{
	GENERATED_BODY()

public:
	ACrossguardThrownProjectile();

	// Set by the thrower before FinishSpawning. Server only. Radius in cm.
	float ExplosionRadius = 0.0f;
	// One second of damage over time, applied DotSeconds times.
	FGameplayEffectSpecHandle DotSpec;
	float DotSeconds = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile")
	TSubclassOf<ACrossguardDamageArea> DamageAreaClass;

	// Applies Spec to every actor with an ability system within Radius of Center.
	static void ApplyInRadius(UWorld* World, const FVector& Center, float Radius, const FGameplayEffectSpecHandle& Spec);

protected:
	virtual void HandleImpact(AActor* OtherActor, const FHitResult& Hit) override;
};
