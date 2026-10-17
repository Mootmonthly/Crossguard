#pragma once

#include "Combat/CrossguardProjectile.h"
#include "CoreMinimal.h"

#include "CrossguardSpellProjectile.generated.h"

// A spell projectile that explodes ([Fireball]): flies straight, applies DamageSpec to what it hits
// and SplashSpec to everyone else within SplashRadius, the caster included. Explodes where it is if
// it hits nothing before its lifespan ends.
UCLASS(Config = Game)
class CROSSGUARD_API ACrossguardSpellProjectile : public ACrossguardProjectile
{
	GENERATED_BODY()

public:
	ACrossguardSpellProjectile();

	// Set by the caster before FinishSpawning. Server only. Radius in cm.
	FGameplayEffectSpecHandle SplashSpec;
	float SplashRadius = 0.0f;

	// Speed and lifespan for a projectile spawned at runtime. Server only, before FinishSpawning.
	void SetFlight(float Speed, float LifeSeconds);

	// The look of a spell projectile whose Visual leaves a field unset (placeholder art): from
	// Config/DefaultGame.ini, [/Script/Crossguard.CrossguardSpellProjectile] (Fireball: fire, an explosion).
	UPROPERTY(Config, EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile|Art")
	FCrossguardProjectileVisual DefaultSpellVisual;

protected:
	virtual void BeginPlay() override;
	virtual void HandleImpact(AActor* OtherActor, const FHitResult& Hit) override;
	virtual void LifeSpanExpired() override;

private:
	void Explode(const FVector& Center, AActor* DirectTarget);
	bool bExploded = false;
};
