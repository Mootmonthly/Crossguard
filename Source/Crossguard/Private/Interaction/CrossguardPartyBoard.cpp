#include "Interaction/CrossguardPartyBoard.h"

#include "Character/CrossguardCharacter.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Game/CrossguardPlayerController.h"
#include "UObject/ConstructorHelpers.h"

ACrossguardPartyBoard::ACrossguardPartyBoard()
{
	// Replicated so clients see it and the HUD can show its prompt.
	bReplicates = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

	BoardMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BoardMesh"));
	BoardMesh->SetupAttachment(GetRootComponent());
	// Placeholder: a thin upright board, 2 m wide and 1.5 m tall, its bottom 0.5 m above the ground.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		BoardMesh->SetStaticMesh(Cube.Object);
		BoardMesh->SetRelativeScale3D(FVector(0.1f, 2.0f, 1.5f));
		BoardMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 125.0f));
	}
}

void ACrossguardPartyBoard::Interact(ACrossguardCharacter* User)
{
	if (ACrossguardPlayerController* Controller = User != nullptr ? Cast<ACrossguardPlayerController>(User->GetController()) : nullptr)
	{
		Controller->ClientOpenPartyPanel();
	}
}
