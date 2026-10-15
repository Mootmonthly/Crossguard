#include "Combat/CrossguardProjectile.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"

ACrossguardProjectile::ACrossguardProjectile()
{
	bReplicates = true;
	SetReplicateMovement(true);
	InitialLifeSpan = 5.0f;

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(8.0f);
	Collision->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	RootComponent = Collision;

	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->UpdatedComponent = Collision;
	Movement->InitialSpeed = 4000.0f;
	Movement->MaxSpeed = 4000.0f;
	Movement->bRotationFollowsVelocity = true;
	Movement->ProjectileGravityScale = 0.0f;
}

void ACrossguardProjectile::BeginPlay()
{
	Super::BeginPlay();

	if (AActor* Shooter = GetInstigator())
	{
		Collision->IgnoreActorWhenMoving(Shooter, true);
	}
	if (HasAuthority())
	{
		Collision->OnComponentHit.AddDynamic(this, &ACrossguardProjectile::OnProjectileHit);
	}
}

void ACrossguardProjectile::OnProjectileHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	FVector NormalImpulse, const FHitResult& Hit)
{
	HandleImpact(OtherActor, Hit);
}

void ACrossguardProjectile::HandleImpact(AActor* OtherActor, const FHitResult& Hit)
{
	if (OtherActor != nullptr && OtherActor != GetInstigator() && DamageSpec.IsValid())
	{
		if (UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OtherActor))
		{
			TargetASC->ApplyGameplayEffectSpecToSelf(*DamageSpec.Data);
		}
	}
	Destroy();
}
