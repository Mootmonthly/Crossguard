#include "Interaction/CrossguardVendorNPC.h"

#include "Character/CrossguardCharacter.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Game/CrossguardPlayerState.h"
#include "Items/CrossguardVendorComponent.h"
#include "UObject/ConstructorHelpers.h"

ACrossguardVendorNPC::ACrossguardVendorNPC()
{
	// Replicated so the client can resolve it (the vendor screen closes when its player walks away).
	bReplicates = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

	VendorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VendorMesh"));
	VendorMesh->SetupAttachment(GetRootComponent());
	// Placeholder: a person-sized cylinder (the engine's is 1 m tall and wide, centred on its origin).
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cylinder.Succeeded())
	{
		VendorMesh->SetStaticMesh(Cylinder.Object);
		VendorMesh->SetRelativeScale3D(FVector(0.6f, 0.6f, 1.8f));
		VendorMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 90.0f));
	}
}

void ACrossguardVendorNPC::Interact(ACrossguardCharacter* User)
{
	ACrossguardPlayerState* Progression = User != nullptr ? User->GetPlayerState<ACrossguardPlayerState>() : nullptr;
	if (UCrossguardVendorComponent* Vendor = Progression != nullptr ? Progression->GetVendor() : nullptr)
	{
		Vendor->OpenVendorScreen(this);
	}
}
