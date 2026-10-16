#include "Combat/CrossguardProjectile.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Combat/CrossguardGuard.h"
#include "Combat/CrossguardWeaponAttackAbility.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Skills/CrossguardSkillAbility.h"
#include "Components/SphereComponent.h"

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
			// Through the target's guard: it comes from back along the flight.
			FCrossguardHit GuardHit;
			GuardHit.Attacker = GetInstigator();
			GuardHit.ImpactPower = ImpactPower;
			GuardHit.bRanged = true;
			GuardHit.Origin = GetActorLocation() - Movement->Velocity.GetSafeNormal() * 1000.0f;
			UAbilitySystemComponent* ShooterASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetInstigator());
			const float Through = CrossguardGuard::ApplyHit(ShooterASC, OtherActor, DamageSpec, GuardHit);
			if (Through <= 0.0f)
			{
				Destroy();
				return;
			}
			// The attack's riders, as on a swing.
			FCrossguardAttackRiders Landed;
			Landed.OnHitSpecs = OnHitSpecs;
			Landed.bCancelSkills = bCancelSkillsOnHit;
			Landed.bSilence = bSilenceOnHit;
			Landed.bGroundFlying = bGroundFlyingOnHit;
			Landed.bMortalStrike = bMortalStrikeOnHit;
			UCrossguardWeaponAttackAbility::ApplyRiders(Landed, TargetASC);
			UCrossguardWeaponAttackAbility::FinishMeleeHits(Landed, ShooterASC, {OtherActor});
		}
	}
	Destroy();
}
