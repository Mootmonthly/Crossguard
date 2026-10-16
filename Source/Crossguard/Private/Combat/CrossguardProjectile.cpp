#include "Combat/CrossguardProjectile.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Combat/CrossguardGuard.h"
#include "Combat/CrossguardWeaponAttackAbility.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Skills/CrossguardSkillAbility.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"

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

	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
	VisualMesh->SetupAttachment(Collision);
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VisualMesh->SetGenerateOverlapEvents(false);
}

void ACrossguardProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// Set before FinishSpawning: it arrives with the spawn.
	DOREPLIFETIME_CONDITION(ACrossguardProjectile, Visual, COND_InitialOnly);
}

void ACrossguardProjectile::ApplyVisual()
{
	if (!CrossguardCosmetics::CanPlay(this))
	{
		return;
	}
	if (!Visual.Mesh.IsNull() && VisualMesh->GetStaticMesh() == nullptr)
	{
		if (UStaticMesh* Mesh = Visual.Mesh.LoadSynchronous())
		{
			VisualMesh->SetStaticMesh(Mesh);
			VisualMesh->SetRelativeRotation(Visual.MeshRotation);
			VisualMesh->SetRelativeScale3D(Visual.MeshScale);
		}
	}
	CrossguardCosmetics::SpawnFxAttached(Visual.TrailVfx, Collision);
}

void ACrossguardProjectile::NotifyImpact(const FVector& Location)
{
	if (HasAuthority() && !bImpactNotified)
	{
		bImpactNotified = true;
		MulticastImpact(Location);
	}
}

void ACrossguardProjectile::MulticastImpact_Implementation(FVector_NetQuantize Location)
{
	++ImpactCount;
	CrossguardCosmetics::SpawnFxAt(this, Visual.ImpactVfx, Location);
	CrossguardCosmetics::PlaySoundAt(this, Visual.ImpactSound, Location);
	OnProjectileImpact(Location);
}

void ACrossguardProjectile::BeginPlay()
{
	Super::BeginPlay();
	ApplyVisual();

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
				NotifyImpact(Hit.bBlockingHit ? FVector(Hit.ImpactPoint) : GetActorLocation());
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
	NotifyImpact(Hit.bBlockingHit ? FVector(Hit.ImpactPoint) : GetActorLocation());
	Destroy();
}
