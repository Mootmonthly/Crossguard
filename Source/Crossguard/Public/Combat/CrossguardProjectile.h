#pragma once

#include "Combat/CrossguardCosmetics.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayEffectTypes.h"

#include "CrossguardProjectile.generated.h"

class UProjectileMovementComponent;
class USphereComponent;
class UStaticMeshComponent;

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

	// What it hits with, for guards (block and parry): the weapon's Impact Power. 0: no guard stops it.
	int32 ImpactPower = 0;

	// Applied to an actor it hits after DamageSpec (next-attack skills such as [Purge Shot]).
	TArray<FGameplayEffectSpecHandle> OnHitSpecs;
	// The hit stops the target's running skills (a Mute in OnHitSpecs).
	bool bCancelSkillsOnHit = false;
	// Thrown weapons' next-attack skills (FCrossguardAttackRiders): [Neckbite]'s Silence stops running
	// skills, [Chop Down] ends Flight, a kill gives [Mortal Strike] back.
	bool bSilenceOnHit = false;
	bool bGroundFlyingOnHit = false;
	bool bMortalStrikeOnHit = false;

	// Art: mesh, trail, impact effect and sound (empty: none). Set on a Blueprint subclass's defaults;
	// spawners fill it from data before FinishSpawning (ammo and throwables: Items.json
	// projectileMesh, trailVfx, impactVfx, impactSound). Replicated with the spawn, so every machine
	// shows the same.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Replicated, Category = "Projectile|Art")
	FCrossguardProjectileVisual Visual;

	// It hit or exploded at Location: on every machine (Visual's impact effect and sound play on their
	// own). Blueprint: extra impact visuals.
	UFUNCTION(BlueprintImplementableEvent, Category = "Projectile|Art")
	void OnProjectileImpact(FVector Location);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Impacts played on this machine (tests).
	int32 GetImpactCount() const { return ImpactCount; }

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnProjectileHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		FVector NormalImpulse, const FHitResult& Hit);

	// Server. Applies DamageSpec to OtherActor (if it has an ability system) and destroys the
	// projectile. Override for other impacts (explosions).
	virtual void HandleImpact(AActor* OtherActor, const FHitResult& Hit);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
	TObjectPtr<UProjectileMovementComponent> Movement;

	// Shows Visual.Mesh (no collision). Empty until a mesh is set.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile|Art")
	TObjectPtr<UStaticMeshComponent> VisualMesh;

	// Server, once, right before it is destroyed by a hit or explosion: every machine plays the impact.
	void NotifyImpact(const FVector& Location);

	// Reliable: it is sent right before the actor goes away, and an impact is rare.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastImpact(FVector_NetQuantize Location);

private:
	// Every rendering machine: Visual's mesh and trail.
	void ApplyVisual();
	bool bImpactNotified = false;
	int32 ImpactCount = 0;
};
