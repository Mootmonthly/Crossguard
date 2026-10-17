#include "Combat/CrossguardSpellProjectile.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/ProjectileMovementComponent.h"

ACrossguardSpellProjectile::ACrossguardSpellProjectile()
{
	Movement->ProjectileGravityScale = 0.0f;
}

void ACrossguardSpellProjectile::BeginPlay()
{
	// The configured look under this projectile's own (on every machine: the same config).
	FCrossguardProjectileVisual Merged = DefaultSpellVisual;
	Merged.MergeFrom(Visual);
	Visual = Merged;
	Super::BeginPlay();
}

void ACrossguardSpellProjectile::SetFlight(float Speed, float LifeSeconds)
{
	Movement->InitialSpeed = Speed;
	Movement->MaxSpeed = Speed;
	InitialLifeSpan = LifeSeconds;
}

void ACrossguardSpellProjectile::HandleImpact(AActor* OtherActor, const FHitResult& Hit)
{
	Explode(Hit.ImpactPoint, OtherActor);
	Destroy();
}

void ACrossguardSpellProjectile::LifeSpanExpired()
{
	if (HasAuthority())
	{
		Explode(GetActorLocation(), nullptr);
	}
	Super::LifeSpanExpired();
}

void ACrossguardSpellProjectile::Explode(const FVector& Center, AActor* DirectTarget)
{
	if (bExploded)
	{
		return;
	}
	bExploded = true;
	NotifyImpact(Center);

	UAbilitySystemComponent* DirectASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(DirectTarget);
	if (DirectASC != nullptr && DamageSpec.IsValid())
	{
		DirectASC->ApplyGameplayEffectSpecToSelf(*DamageSpec.Data);
	}
	if (!SplashSpec.IsValid() || SplashRadius <= 0.0f || GetWorld() == nullptr)
	{
		return;
	}

	TArray<FOverlapResult> Overlaps;
	GetWorld()->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeSphere(SplashRadius));
	TSet<AActor*> Splashed;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Target = Overlap.GetActor();
		// The direct target took the impact damage instead.
		if (Target == nullptr || Target == DirectTarget || Splashed.Contains(Target))
		{
			continue;
		}
		Splashed.Add(Target);
		if (UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target))
		{
			TargetASC->ApplyGameplayEffectSpecToSelf(*SplashSpec.Data);
		}
	}
}
