#include "Combat/CrossguardThrownProjectile.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "TimerManager.h"

ACrossguardDamageArea::ACrossguardDamageArea()
{
	bReplicates = true;
	AreaMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AreaMesh"));
	AreaMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetRootComponent(AreaMesh);
}

void ACrossguardDamageArea::Initialize(const FGameplayEffectSpecHandle& InDamageSpec, float InRadius, float InSeconds)
{
	DamageSpec = InDamageSpec;
	Radius = InRadius;
	PulsesLeft = FMath::Max(0, FMath::RoundToInt(InSeconds));
	if (PulsesLeft > 0 && DamageSpec.IsValid())
	{
		// First pulse one second after the explosion.
		GetWorldTimerManager().SetTimer(PulseTimer, this, &ACrossguardDamageArea::Pulse, 1.0f, true);
	}
	else
	{
		Destroy();
	}
}

void ACrossguardDamageArea::Pulse()
{
	ACrossguardThrownProjectile::ApplyInRadius(GetWorld(), GetActorLocation(), Radius, DamageSpec);
	if (--PulsesLeft <= 0)
	{
		GetWorldTimerManager().ClearTimer(PulseTimer);
		Destroy();
	}
}

ACrossguardThrownProjectile::ACrossguardThrownProjectile()
{
	// Thrown by hand: slower than an arrow, and it arcs.
	Movement->InitialSpeed = 2000.0f;
	Movement->MaxSpeed = 2000.0f;
	Movement->ProjectileGravityScale = 1.0f;
	DamageAreaClass = ACrossguardDamageArea::StaticClass();
}

void ACrossguardThrownProjectile::ApplyInRadius(UWorld* World, const FVector& Center, float Radius, const FGameplayEffectSpecHandle& Spec)
{
	if (World == nullptr || !Spec.IsValid() || Radius <= 0.0f)
	{
		return;
	}
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(Radius));

	// One application per actor, however many of its components overlap.
	TSet<AActor*> Hit;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Target = Overlap.GetActor();
		if (Target == nullptr || Hit.Contains(Target))
		{
			continue;
		}
		Hit.Add(Target);
		if (UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target))
		{
			TargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
		}
	}
}

void ACrossguardThrownProjectile::HandleImpact(AActor* OtherActor, const FHitResult& Hit)
{
	if (ExplosionRadius <= 0.0f)
	{
		Super::HandleImpact(OtherActor, Hit);
		return;
	}

	Explode(Hit.ImpactPoint);
	Destroy();
}

void ACrossguardThrownProjectile::LifeSpanExpired()
{
	if (HasAuthority() && ExplosionRadius > 0.0f)
	{
		Explode(GetActorLocation());
	}
	Super::LifeSpanExpired();
}

void ACrossguardThrownProjectile::Explode(const FVector& Center)
{
	ApplyInRadius(GetWorld(), Center, ExplosionRadius, DamageSpec);
	if (DotSeconds > 0.0f && DotSpec.IsValid() && DamageAreaClass != nullptr)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.Instigator = GetInstigator();
		if (ACrossguardDamageArea* Area = GetWorld()->SpawnActor<ACrossguardDamageArea>(DamageAreaClass, FTransform(Center), Params))
		{
			Area->Initialize(DotSpec, ExplosionRadius, DotSeconds);
		}
	}
}
