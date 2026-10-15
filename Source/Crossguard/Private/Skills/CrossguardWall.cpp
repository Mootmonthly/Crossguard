#include "Skills/CrossguardWall.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

ACrossguardWall::ACrossguardWall()
{
	bReplicates = true;
	InitialLifeSpan = 15.0f;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		Mesh->SetStaticMesh(Cube.Object);
	}
	ApplyWallSize();
}

void ACrossguardWall::SetWallSize(const FVector& InSize)
{
	WallSize = InSize;
	ApplyWallSize();
}

void ACrossguardWall::OnRep_WallSize()
{
	ApplyWallSize();
}

void ACrossguardWall::ApplyWallSize()
{
	// The engine cube is 100 cm, centered: scale it and lift it so the wall stands on the origin.
	Mesh->SetRelativeScale3D(WallSize / 100.0f);
	Mesh->SetRelativeLocation(FVector(0.0f, 0.0f, WallSize.Z * 0.5f));
}

void ACrossguardWall::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACrossguardWall, WallSize);
}
