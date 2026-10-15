#include "Skills/CrossguardTrackingComponent.h"

#include "AbilitySystem/CrossguardGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Character/CrossguardCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/CrossguardTeamLibrary.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// A print beside the walking line, and its size (the engine cylinder is 100 cm).
	constexpr float FootOffset = 12.0f;
	const FVector FootprintScale(0.25f, 0.12f, 0.01f);
}

UCrossguardTrackingComponent::UCrossguardTrackingComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.2f;
	// Instances are placed in world space.
	SetUsingAbsoluteLocation(true);
	SetUsingAbsoluteRotation(true);
	SetUsingAbsoluteScale(true);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetCanEverAffectNavigation(false);
	SetCastShadow(false);
	// Only the tracker fills it; this keeps it off other screens even so.
	SetOnlyOwnerSee(true);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cylinder.Succeeded())
	{
		SetStaticMesh(Cylinder.Object);
	}
}

bool UCrossguardTrackingComponent::IsTracking() const
{
	// Cheap test first: every character has this component, only a local player can track.
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (Pawn == nullptr || !Pawn->IsLocallyControlled() || !Pawn->IsPlayerControlled())
	{
		return false;
	}
	const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn);
	return Pawn != nullptr && Pawn->IsLocallyControlled() && ASC != nullptr && ASC->HasMatchingGameplayTag(CrossguardTags::Passive_HuntersTracking);
}

void UCrossguardTrackingComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!IsTracking())
	{
		if (!Footprints.IsEmpty() || !Trails.IsEmpty())
		{
			Footprints.Reset();
			Trails.Reset();
			Rebuild();
		}
		return;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	const int32 Before = Footprints.Num();
	Footprints.RemoveAll([this, Now](const FFootprint& Print) { return Now - Print.Time > TrailSeconds; });
	const bool bExpired = Footprints.Num() != Before;
	const int32 Recorded = Footprints.Num();
	RecordSteps(Now);
	if (bExpired || Footprints.Num() != Recorded)
	{
		Rebuild();
	}
}

void UCrossguardTrackingComponent::RecordSteps(double Now)
{
	const AActor* Tracker = GetOwner();
	for (TActorIterator<ACrossguardCharacter> It(GetWorld()); It; ++It)
	{
		ACrossguardCharacter* Enemy = *It;
		const UAbilitySystemComponent* ASC = Enemy->GetAbilitySystemComponent();
		// Enemy players only, alive, visible and on the ground.
		if (Enemy == Tracker || !Enemy->IsPlayerControlled() || UCrossguardTeamLibrary::AreAllies(Tracker, Enemy) || ASC == nullptr
			|| ASC->HasMatchingGameplayTag(CrossguardTags::State_Dead) || ASC->HasMatchingGameplayTag(CrossguardTags::State_Stealthed)
			|| !Enemy->GetCharacterMovement()->IsMovingOnGround())
		{
			continue;
		}
		const FVector Feet = Enemy->GetActorLocation() - FVector(0.0f, 0.0f, Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 1.0f);
		FTrail* Trail = Trails.Find(Enemy);
		if (Trail == nullptr)
		{
			Trails.Add(Enemy, FTrail{Feet, false});
			continue;
		}
		const FVector Step = Feet - Trail->LastStep;
		if (Step.Size2D() < StepLength)
		{
			continue;
		}
		const float Yaw = Step.Rotation().Yaw;
		const FVector Side = FRotator(0.0f, Yaw + 90.0f, 0.0f).Vector() * (Trail->bLeftFoot ? -FootOffset : FootOffset);
		Footprints.Add(FFootprint{Feet + Side, Yaw, Now});
		Trail->LastStep = Feet;
		Trail->bLeftFoot = !Trail->bLeftFoot;
	}
	// Forget characters that are gone.
	for (auto It = Trails.CreateIterator(); It; ++It)
	{
		if (!It->Key.IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

void UCrossguardTrackingComponent::Rebuild()
{
	ClearInstances();
	TArray<FTransform> Transforms;
	Transforms.Reserve(Footprints.Num());
	for (const FFootprint& Print : Footprints)
	{
		Transforms.Emplace(FRotator(0.0f, Print.Yaw, 0.0f), Print.Location, FootprintScale);
	}
	AddInstances(Transforms, false, true);
}
