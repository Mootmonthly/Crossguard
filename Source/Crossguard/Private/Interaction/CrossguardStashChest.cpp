#include "Interaction/CrossguardStashChest.h"

#include "Character/CrossguardCharacter.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Game/CrossguardPlayerState.h"
#include "Items/CrossguardStashComponent.h"
#include "UObject/ConstructorHelpers.h"

ACrossguardStashChest::ACrossguardStashChest()
{
	// Replicated so the client can resolve it (the stash screen closes when its player walks away).
	bReplicates = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

	ChestMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ChestMesh"));
	ChestMesh->SetupAttachment(GetRootComponent());
	// Placeholder: a 1 m x 0.6 m x 0.6 m box on the ground.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		ChestMesh->SetStaticMesh(Cube.Object);
		ChestMesh->SetRelativeScale3D(FVector(1.0f, 0.6f, 0.6f));
		ChestMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 30.0f));
	}
}

void ACrossguardStashChest::Interact(ACrossguardCharacter* User)
{
	ACrossguardPlayerState* Progression = User != nullptr ? User->GetPlayerState<ACrossguardPlayerState>() : nullptr;
	if (UCrossguardStashComponent* Stash = Progression != nullptr ? Progression->GetStash() : nullptr)
	{
		Stash->OpenStashScreen(this);
	}
}
