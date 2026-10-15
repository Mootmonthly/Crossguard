#include "Interaction/CrossguardDoor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Interaction/CrossguardLockComponent.h"
#include "Net/UnrealNetwork.h"

ACrossguardDoor::ACrossguardDoor()
{
	bReplicates = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Hinge")));

	DoorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorMesh"));
	DoorMesh->SetupAttachment(GetRootComponent());

	Lock = CreateDefaultSubobject<UCrossguardLockComponent>(TEXT("Lock"));
}

void ACrossguardDoor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACrossguardDoor, bOpen);
}

void ACrossguardDoor::BeginPlay()
{
	Super::BeginPlay();
	ClosedRotation = DoorMesh->GetRelativeRotation();
	ApplyOpen();
}

void ACrossguardDoor::Interact(ACrossguardCharacter* User)
{
	if (HasAuthority())
	{
		bOpen = !bOpen;
		ApplyOpen();
	}
}

void ACrossguardDoor::OnRep_Open()
{
	ApplyOpen();
}

void ACrossguardDoor::ApplyOpen()
{
	// Placeholder until door animations exist: snaps open.
	DoorMesh->SetRelativeRotation(bOpen ? ClosedRotation + FRotator(0.0f, OpenYaw, 0.0f) : ClosedRotation);
}
