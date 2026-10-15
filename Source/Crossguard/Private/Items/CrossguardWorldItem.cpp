#include "Items/CrossguardWorldItem.h"

#include "Character/CrossguardCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Crossguard.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

ACrossguardWorldItem::ACrossguardWorldItem()
{
	bReplicates = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	// Found by the interact search, not by collision.
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		Mesh->SetStaticMesh(Cube.Object);
		Mesh->SetRelativeScale3D(FVector(0.25f));
	}
}

void ACrossguardWorldItem::Interact(ACrossguardCharacter* User)
{
	UCrossguardInventoryComponent* Inventory = User != nullptr ? User->GetInventoryComponent() : nullptr;
	if (!HasAuthority() || Inventory == nullptr || Item.Count <= 0)
	{
		return;
	}
	const int32 Left = Inventory->AddItem(Item);
	if (Left <= 0)
	{
		Destroy();
		return;
	}
	if (Left == Item.Count)
	{
		UE_LOG(LogCrossguard, Log, TEXT("%s cannot pick up %s (no room, or its item data is missing)"), *User->GetName(), *Item.ItemId.ToString());
	}
	Item.Count = Left;
}

void ACrossguardWorldItem::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACrossguardWorldItem, Item);
}
